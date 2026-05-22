#include "vr/text/text_renderer_3d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/text/text_runtime_contract.hpp"
#include "vr/text/generated/text_3d_frag_spv.hpp"
#include "vr/text/generated/text_3d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vr::text {

bool TextRenderer3D::AnyTextComponentDirty(const ecs::Text<ecs::Dim3>* components_,
                                           std::uint32_t component_count_) noexcept {
    if (components_ == nullptr || component_count_ == 0U) {
        return false;
    }

    for (std::uint32_t i = 0U; i < component_count_; ++i) {
        if (components_[i].runtime.dirty_flags != 0U) {
            return true;
        }
    }
    return false;
}

std::uint64_t TextRenderer3D::ComputeTransformRevisionSignature(
    const ecs::Transform<ecs::Dim3>* transforms_,
    std::uint32_t component_count_,
    const std::uint32_t* candidate_component_indices_,
    std::uint32_t candidate_component_count_,
    bool use_candidate_indices_) noexcept {
    if (transforms_ == nullptr || component_count_ == 0U) {
        return 0U;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    if (!use_candidate_indices_) {
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            hash ^= static_cast<std::uint64_t>(transforms_[i].runtime.world_revision);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    hash ^= static_cast<std::uint64_t>(candidate_component_count_);
    hash *= 1099511628211ULL;
    if (candidate_component_indices_ == nullptr) {
        return hash;
    }

    for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
        const std::uint32_t component_index = candidate_component_indices_[i];
        if (component_index >= component_count_) {
            continue;
        }
        hash ^= static_cast<std::uint64_t>(transforms_[component_index].runtime.world_revision);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void TextRenderer3D::SetSceneData(ecs::Text<ecs::Dim3>* text_components_,
                                  ecs::Transform<ecs::Dim3>* text_transforms_,
                                  std::uint32_t component_count_,
                                  ecs::Camera<ecs::Dim3>* camera_component_,
                                  ecs::Transform<ecs::Dim3>* camera_transform_,
                                  ecs::Bounds<ecs::Dim3>* bounds_components_) noexcept {
    text_components = text_components_;
    text_transforms = text_transforms_;
    component_count = component_count_;
    camera_component = camera_component_;
    camera_transform = camera_transform_;
    bounds_components = bounds_components_;
    if (component_count_ > 0U) {
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch, component_count_);
    }
}

void TextRenderer3D::PrepareFrame(const render::TextRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer3D::PrepareFrame called before Initialize");
    }
    ValidateTextRuntimePrepareView(prepare_view_, "TextRenderer3D::PrepareFrame");

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    bindless_resources = prepare_view_.bindless;
    gpu_memory_host = &prepare_view_.gpu_memory;
    freetype_host = &prepare_view_.freetype;
    glyph_atlas_host = &prepare_view_.glyph_atlas;
    glyph_upload_host = &prepare_view_.glyph_upload;
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (glyph_upload_host != nullptr &&
        glyph_upload_host->IsInitialized() &&
        (!glyph_upload_host->BindlessConfig().Enabled() ||
         glyph_upload_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureGlyphUploadHost(*glyph_upload_host);
    }
    active_frame_index = prepare_view_.frame.frame_index;
    last_submitted_value_seen = std::max(last_submitted_value_seen,
                                         prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen,
                                           prepare_view_.progress.completed_submit_value);
    CollectRetiredDepthResources(*context, completed_submit_value_seen);

    stats = {};
    stats.component_count = component_count;
    culling_stats = {};

    if (text_components == nullptr ||
        text_transforms == nullptr ||
        camera_component == nullptr ||
        camera_transform == nullptr ||
        component_count == 0U) {
        render_scratch.instances.clear();
        render_scratch.draw_batches.clear();
        cached_runtime_stats = {};
        cached_render_stats = {};
        frame_data_cache = {};
        cached_components_ptr = nullptr;
        cached_transforms_ptr = nullptr;
        cached_camera_component_ptr = nullptr;
        cached_camera_transform_ptr = nullptr;
        cached_component_count = 0U;
        cached_transform_signature = 0U;
        cached_camera_world_revision = 0U;
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        runtime_geometry_valid = false;
        instance_geometry_valid = false;
        contains_billboard_instances = false;
        active_camera_reverse_z = false;
        ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());
        return;
    }

    frame_data_cache = ecs::TextRender3DSystem::BuildFrameData(*camera_component, *camera_transform);
    active_camera_reverse_z = camera_component->style.reverse_z != 0U;

    ecs::TextRuntimeBuildHint runtime_build_hint{};
    const bool use_bounds_culling = bounds_components != nullptr && camera_component != nullptr;
    if (use_bounds_culling) {
        const ecs::CullingBuildOptions culling_options{
            .enable_culling_mask_filter = true,
            .enable_frustum_culling = true,
            .enable_aabb_refine = true,
            .write_visibility_bits = false
        };
        culling_stats = ecs::CullingSystem<ecs::Dim3>::BuildVisibleSet(bounds_components,
                                                                        component_count,
                                                                        camera_component,
                                                                        culling_scratch,
                                                                        culling_options);
        runtime_build_hint.visible_component_indices = culling_scratch.visible_indices.data();
        runtime_build_hint.visible_component_count = culling_stats.visible_count;
        runtime_build_hint.use_visible_component_indices = 1U;
        runtime_build_hint.external_visible_set_signature = culling_stats.visible_set_signature;
        runtime_build_hint.use_external_visible_set_signature = 1U;

        stats.used_bounds_culling = true;
        stats.culling_input_count = culling_stats.input_count;
        stats.culling_visible_count = culling_stats.visible_count;
        stats.culling_culled_count = culling_stats.culled_by_mask_count +
                                     culling_stats.culled_by_frustum_count +
                                     culling_stats.culled_by_invalid_bounds_count;
        stats.culling_mask_reject_count = culling_stats.culled_by_mask_count;
        stats.culling_frustum_reject_count = culling_stats.culled_by_frustum_count;
        stats.culling_invalid_bounds_count = culling_stats.culled_by_invalid_bounds_count;
        stats.culling_plane_test_count = culling_stats.plane_test_count;
    } else {
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
    }

    const bool visible_mode_changed =
        runtime_geometry_valid &&
        (cached_runtime_stats.used_visible_component_indices != use_bounds_culling);
    const bool visible_set_changed =
        runtime_geometry_valid &&
        use_bounds_culling &&
        (cached_runtime_stats.visible_set_signature != runtime_build_hint.external_visible_set_signature ||
         cached_runtime_stats.candidate_component_count != runtime_build_hint.visible_component_count);

    const bool runtime_rebuild_required =
        !runtime_geometry_valid ||
        cached_components_ptr != text_components ||
        cached_component_count != component_count ||
        AnyTextComponentDirty(text_components, component_count) ||
        visible_mode_changed ||
        visible_set_changed;

    if (runtime_rebuild_required) {
        cached_runtime_stats = ecs::TextRuntimeSystem<ecs::Dim3>::Build(text_components,
                                                                         component_count,
                                                                         *glyph_atlas_host,
                                                                         *freetype_host,
                                                                         render_scratch.runtime_scratch,
                                                                         create_info_cache.runtime_build,
                                                                         runtime_build_hint);
        runtime_geometry_valid = true;
        instance_geometry_valid = false;
    }

    const bool use_transform_candidates = cached_runtime_stats.used_visible_component_indices;
    const std::uint32_t* transform_candidate_indices = use_transform_candidates
        ? ecs::TextBatchSystem<ecs::Dim3>::OrderedIndices(render_scratch.runtime_scratch.batch_scratch)
        : nullptr;
    const std::uint32_t transform_candidate_count = use_transform_candidates
        ? ecs::TextBatchSystem<ecs::Dim3>::OrderedIndexCount(render_scratch.runtime_scratch.batch_scratch)
        : component_count;
    const std::uint64_t transform_signature =
        ComputeTransformRevisionSignature(text_transforms,
                                          component_count,
                                          transform_candidate_indices,
                                          transform_candidate_count,
                                          use_transform_candidates);
    const std::uint32_t camera_world_revision = camera_transform->runtime.world_revision;

    const bool instance_rebuild_required =
        !instance_geometry_valid ||
        runtime_rebuild_required ||
        cached_transforms_ptr != text_transforms ||
        cached_camera_component_ptr != camera_component ||
        cached_camera_transform_ptr != camera_transform ||
        transform_signature != cached_transform_signature ||
        (contains_billboard_instances && camera_world_revision != cached_camera_world_revision);

    if (instance_rebuild_required) {
        cached_render_stats = ecs::TextRender3DSystem::BuildFromRuntime(text_components,
                                                                         text_transforms,
                                                                         component_count,
                                                                         *camera_component,
                                                                         *camera_transform,
                                                                         render_scratch,
                                                                         cached_runtime_stats);
        cached_transforms_ptr = text_transforms;
        cached_camera_component_ptr = camera_component;
        cached_camera_transform_ptr = camera_transform;
        cached_transform_signature = transform_signature;
        cached_camera_world_revision = camera_world_revision;
        contains_billboard_instances = cached_render_stats.billboard_instance_count > 0U;
        instance_geometry_valid = true;

        if (runtime_geometry_revision == std::numeric_limits<std::uint64_t>::max()) {
            runtime_geometry_revision = 1U;
        } else {
            ++runtime_geometry_revision;
            if (runtime_geometry_revision == 0U) {
                runtime_geometry_revision = 1U;
            }
        }
    }

    cached_components_ptr = text_components;
    cached_component_count = component_count;

    stats.visible_component_count = cached_render_stats.runtime.visible_component_count;
    stats.built_component_count = cached_render_stats.runtime.built_component_count;
    stats.glyph_quad_count = static_cast<std::uint32_t>(render_scratch.runtime_scratch.glyph_quads.size());
    stats.instance_count = static_cast<std::uint32_t>(render_scratch.instances.size());
    stats.draw_batch_count = static_cast<std::uint32_t>(render_scratch.draw_batches.size());
    stats.billboard_instance_count = cached_render_stats.billboard_instance_count;
    stats.depth_test_batch_count = cached_render_stats.depth_test_batch_count;
    stats.depth_write_batch_count = cached_render_stats.depth_write_batch_count;

    const VkDeviceSize required_bytes =
        static_cast<VkDeviceSize>(render_scratch.instances.size()) * sizeof(ecs::Text3DGpuInstance);

    EnsureGpuResourcesForFrame(*context, prepare_view_, active_frame_index, required_bytes);
    if (prepare_view_.render_graph_upload_active) {
        EnsureGraphUploadStagingForFrame(*context,
                                         prepare_view_,
                                         active_frame_index,
                                         required_bytes);
    }
    ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());

    PerFrameState& frame_state = frame_states[active_frame_index];
    frame_state.instance_count = static_cast<std::uint32_t>(render_scratch.instances.size());

    if (required_bytes == 0U) {
        return;
    }

    const bool needs_upload = frame_state.uploaded_revision != runtime_geometry_revision;
    if (!needs_upload) {
        return;
    }

    if (prepare_view_.render_graph_upload_active) {
        return;
    }

    upload_host->StageAndRecordCopyBuffer(active_frame_index,
                                          frame_state.vertex_buffer.buffer,
                                          0U,
                                          render_scratch.instances.data(),
                                          required_bytes,
                                          16U);

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = frame_state.vertex_buffer.buffer;
    barrier.offset = 0U;
    barrier.size = required_bytes;
    upload_host->RecordBufferBarrier2(active_frame_index, barrier);

    frame_state.uploaded_revision = runtime_geometry_revision;
    stats.uploaded_bytes = required_bytes;
}

} // namespace vr::text
