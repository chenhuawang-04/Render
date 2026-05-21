#include "vr/text/text_renderer_3d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
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

namespace {

[[nodiscard]] render::BindlessTableId ResolveSampledImageTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SampledImageTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SampledImageTableContractId();
}

[[nodiscard]] render::BindlessTableId ResolveSamplerTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SamplerTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SamplerTableContractId();
}

} // namespace

VkDeviceSize TextRenderer3D::NextPow2(VkDeviceSize value_) noexcept {
    if (value_ <= 1U) {
        return 1U;
    }

    VkDeviceSize result = 1U;
    while (result < value_) {
        if (result > (std::numeric_limits<VkDeviceSize>::max() >> 1U)) {
            return std::numeric_limits<VkDeviceSize>::max();
        }
        result <<= 1U;
    }
    return result;
}

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

bool TextRenderer3D::IsDepthFormatSupported(VulkanContext& context_,
                                            VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }

    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool TextRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags TextRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags mask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return mask;
}

VkFormat TextRenderer3D::ResolveDepthFormat(VulkanContext& context_,
                                            VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (const VkFormat candidate : fallback_formats) {
        if (IsDepthFormatSupported(context_, candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error("TextRenderer3D failed to resolve depth format for depth attachment");
}

std::size_t TextRenderer3D::PipelineModeIndex(DepthPipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
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

TextRenderer3D::DepthPipelineMode TextRenderer3D::ResolveDepthPipelineMode(
    const ecs::Text3DDrawBatch& batch_,
    bool use_depth_,
    bool reverse_z_) noexcept {
    if (!use_depth_ || (batch_.depth_flags & 0x1U) == 0U) {
        return DepthPipelineMode::no_depth;
    }

    const bool depth_write = (batch_.depth_flags & 0x2U) != 0U;
    if (reverse_z_) {
        return depth_write
            ? DepthPipelineMode::depth_test_write_reverse_z
            : DepthPipelineMode::depth_test_reverse_z;
    }
    return depth_write
        ? DepthPipelineMode::depth_test_write
        : DepthPipelineMode::depth_test;
}

void TextRenderer3D::Initialize(const TextRenderer3DCreateInfo& create_info_) {
    if (initialized) {
        if (context != nullptr) {
            Shutdown(*context);
        } else {
            throw std::runtime_error(
                "TextRenderer3D::Initialize called while already initialized without valid VulkanContext");
        }
    }

    create_info_cache = create_info_;
    if (create_info_cache.initial_vertex_buffer_bytes == 0U) {
        create_info_cache.initial_vertex_buffer_bytes = 1U;
    }
    if (!(create_info_cache.sdf_smooth > 0.0F) || !std::isfinite(create_info_cache.sdf_smooth)) {
        create_info_cache.sdf_smooth = 1.0F;
    }
    if (!(create_info_cache.bitmap_gamma > 0.0F) || !std::isfinite(create_info_cache.bitmap_gamma)) {
        create_info_cache.bitmap_gamma = 1.0F;
    }
    if (!(create_info_cache.bitmap_edge_sharpness > 0.0F) ||
        !std::isfinite(create_info_cache.bitmap_edge_sharpness)) {
        create_info_cache.bitmap_edge_sharpness = 1.0F;
    }
    if (!std::isfinite(create_info_cache.clear_depth_value)) {
        create_info_cache.clear_depth_value = 1.0F;
    }
    create_info_cache.clear_depth_value = std::clamp(create_info_cache.clear_depth_value, 0.0F, 1.0F);

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_glyph_count > 0U) {
        ecs::TextRender3DSystem::Reserve(render_scratch,
                                         create_info_cache.reserve_component_count,
                                         create_info_cache.reserve_glyph_count);
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch,
                                               create_info_cache.reserve_component_count);
    }

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
    bounds_components = nullptr;
    culling_stats = {};
    runtime_geometry_revision = 1U;
    runtime_geometry_valid = false;
    instance_geometry_valid = false;
    contains_billboard_instances = false;
    active_camera_reverse_z = false;
    stats = {};

    for (auto& pipeline_id : graphics_pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = true;
}

void TextRenderer3D::Shutdown(VulkanContext& context_) {
    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    for (auto& frame_state : frame_states) {
        resource::BufferHost::DestroyBuffer(context_, frame_state.vertex_buffer);
        frame_state.vertex_buffer_capacity_bytes = 0U;
        resource::BufferHost::DestroyBuffer(context_, frame_state.graph_staging_buffer);
        frame_state.graph_staging_buffer_capacity_bytes = 0U;
        frame_state.instance_count = 0U;
        frame_state.uploaded_revision = 0U;
        frame_state.graph_vertex_buffer = render_graph::invalid_resource_handle;
        frame_state.graph_vertex_version = render_graph::invalid_resource_version;
        frame_state.graph_vertex_size_bytes = 0U;
    }
    frame_states.clear();

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);
    image_initialized.clear();

    render_scratch.instances.clear();
    render_scratch.draw_batches.clear();
    render_scratch.runtime_scratch.glyph_quads.clear();
    render_scratch.runtime_scratch.draw_batches.clear();
    render_scratch.runtime_scratch.batch_scratch.visible_items.clear();
    render_scratch.runtime_scratch.batch_scratch.radix_scratch.clear();
    render_scratch.runtime_scratch.batch_scratch.ordered_indices.clear();
    render_scratch.runtime_scratch.utf32_codepoints.clear();
    render_scratch.runtime_scratch.line_widths.clear();
    render_scratch.runtime_scratch.line_x_offsets.clear();
    render_scratch.runtime_scratch.run_glyphs.clear();
    render_scratch.runtime_scratch.face_variants.clear();
    render_scratch.runtime_scratch.glyph_resolve_cache.clear();
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    culling_stats = {};

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : graphics_pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    text_components = nullptr;
    text_transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    bindless_resources = nullptr;
    gpu_memory_host = nullptr;
    freetype_host = nullptr;
    glyph_atlas_host = nullptr;
    glyph_upload_host = nullptr;

    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;

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

    runtime_geometry_revision = 1U;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    runtime_geometry_valid = false;
    instance_geometry_valid = false;
    contains_billboard_instances = false;
    active_camera_reverse_z = false;
    stats = {};
    initialized = false;
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

void TextRenderer3D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "TextRenderer3D::BuildDirectRuntimeGraph called before Initialize");
    }

    render_graph::ResourceHandle depth_target = render_graph::invalid_resource_handle;
    if (create_info_cache.enable_depth) {
        const render_graph::Extent3D depth_extent{
            .width = graph_view_.reference_extent.width != 0U ? graph_view_.reference_extent.width : 1U,
            .height = graph_view_.reference_extent.height != 0U ? graph_view_.reference_extent.height : 1U,
            .depth = graph_view_.reference_extent.depth != 0U ? graph_view_.reference_extent.depth : 1U,
        };
        render_graph::TextureDesc depth_desc{
            .dimension = render_graph::TextureDimension::image_2d,
            .format = render_graph::TextureFormat::d32_sfloat,
            .extent = depth_extent,
            .usage = render_graph::texture_usage_depth_stencil_attachment_flag,
            .mip_level_count = 1U,
            .array_layer_count = 1U,
            .sample_count = render_graph::SampleCount::x1,
            .prefer_lazy_memory = create_info_cache.clear_depth,
        };
        if (!create_info_cache.clear_depth &&
            descriptor_host != nullptr &&
            descriptor_host->FramesInFlight() > 1U) {
            const std::uint32_t frames_in_flight =
                (std::max)(descriptor_host->FramesInFlight(), 1U);
            const std::uint32_t selected_frame_slot = active_frame_index % frames_in_flight;
            for (std::uint32_t frame_slot = 0U; frame_slot < frames_in_flight; ++frame_slot) {
                char debug_name[64]{};
                std::snprintf(debug_name,
                              sizeof(debug_name),
                              "text_renderer_3d_depth_slot_%u",
                              frame_slot);
                const auto candidate = graph_view_.builder.CreateTexture(
                    debug_name,
                    depth_desc,
                    render_graph::ResourceLifetime::persistent);
                if (frame_slot == selected_frame_slot) {
                    depth_target = candidate;
                }
            }
        } else {
            depth_target = graph_view_.builder.CreateTexture(
                "text_renderer_3d_depth",
                depth_desc,
                create_info_cache.clear_depth
                    ? render_graph::ResourceLifetime::transient
                    : render_graph::ResourceLifetime::persistent);
        }
    }

    render_graph::ResourceVersionHandle color_version =
        render_graph::invalid_resource_version;
    render_graph::ResourceVersionHandle depth_version =
        render_graph::invalid_resource_version;
    auto append_stage_pass = [&](const render::SceneRenderStage stage_,
                                 const char* debug_name_,
                                 const bool clear_color_,
                                 const bool clear_depth_) {
        const auto pass = graph_view_.builder.AddPass(debug_name_);
        if (render_graph::IsValidResourceVersionHandle(color_version)) {
            (void)graph_view_.builder.Read(
                pass,
                color_version,
                render_graph::AccessDesc{
                    .access = render_graph::AccessKind::color_attachment_read,
                });
        }
        color_version = graph_view_.builder.Write(
            pass,
            graph_view_.present_target,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::color_attachment_write,
            });

        render_graph::RasterPassDesc raster_pass_desc{
            .color_attachments = {
                render_graph::RasterColorAttachmentDesc{
                    .target = graph_view_.present_target,
                    .load_op = clear_color_
                        ? render_graph::AttachmentLoadOp::clear
                        : render_graph::AttachmentLoadOp::load,
                    .store_op = render_graph::AttachmentStoreOp::store,
                    .clear_value = {
                        .red = create_info_cache.clear_color.float32[0],
                        .green = create_info_cache.clear_color.float32[1],
                        .blue = create_info_cache.clear_color.float32[2],
                        .alpha = create_info_cache.clear_color.float32[3],
                    },
                },
            },
        };

        if (render_graph::IsValidResourceHandle(depth_target)) {
            if (render_graph::IsValidResourceVersionHandle(depth_version)) {
                (void)graph_view_.builder.Read(
                    pass,
                    depth_version,
                    render_graph::AccessDesc{
                        .access = render_graph::AccessKind::depth_stencil_read,
                    });
            }
            depth_version = graph_view_.builder.Write(
                pass,
                depth_target,
                render_graph::AccessDesc{
                    .access = render_graph::AccessKind::depth_stencil_write,
                });
            raster_pass_desc.has_depth_attachment = true;
            raster_pass_desc.depth_attachment = render_graph::RasterDepthAttachmentDesc{
                .target = depth_target,
                .load_op = clear_depth_
                    ? render_graph::AttachmentLoadOp::clear
                    : render_graph::AttachmentLoadOp::load,
                .store_op = render_graph::AttachmentStoreOp::store,
                .stencil_load_op = clear_depth_
                    ? render_graph::AttachmentLoadOp::clear
                    : render_graph::AttachmentLoadOp::load,
                .stencil_store_op = render_graph::AttachmentStoreOp::store,
                .clear_value = {
                    .depth = create_info_cache.clear_depth_value,
                    .stencil = create_info_cache.clear_stencil_value,
                },
            };
        }

        graph_view_.builder.SetRasterPassDesc(pass, raster_pass_desc);
        DescribeGraphDescriptorBindings(graph_view_.builder, pass);
        graph_view_.builder.SetExecuteCallback(
            pass,
            [this,
             stage_,
             color_target = graph_view_.present_target,
             depth_target](render_graph::GraphCommandContext& context_) {
                RecordGraphSceneStage(context_, stage_, color_target, depth_target);
            });
    };

    append_stage_pass(render::SceneRenderStage::opaque,
                      "text_renderer_3d_direct_opaque",
                      create_info_cache.clear_swapchain,
                      create_info_cache.clear_depth);
    append_stage_pass(render::SceneRenderStage::transparent,
                      "text_renderer_3d_direct_transparent",
                      false,
                      false);
    graph_view_.present_ready_version = color_version;
}

void TextRenderer3D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                     const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "TextRenderer3D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const_cast<TextRenderer3D*>(this)->ScheduleGraphInstanceUpload(builder_, pass_);

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("text_3d.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     0U,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     1U,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);
}

void TextRenderer3D::RecordGraphSceneStage(render_graph::GraphCommandContext& context_,
                                           render::SceneRenderStage stage_,
                                           render_graph::ResourceHandle color_target_,
                                           render_graph::ResourceHandle depth_target_) {
    RecordGraphInternal(context_,
                        render::SceneRenderStagePassHintValue(stage_),
                        true,
                        color_target_,
                        depth_target_);
}

void TextRenderer3D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                         std::uint32_t pass_bucket_,
                                         bool filter_by_pass_bucket_,
                                         render_graph::ResourceHandle color_target_,
                                         render_graph::ResourceHandle depth_target_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer3D::RecordGraphSceneStage called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        glyph_upload_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("TextRenderer3D::RecordGraphSceneStage called before PrepareFrame");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("TextRenderer3D::RecordGraphSceneStage requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("TextRenderer3D::RecordGraphSceneStage resolved zero-sized render extent");
    }

    const bool use_depth_attachment = render_graph::IsValidResourceHandle(depth_target_);
    const VkFormat active_depth_format = use_depth_attachment
        ? context_.ResolveTextureView(depth_target_).format
        : VK_FORMAT_UNDEFINED;

    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          resolved_color.format,
                          use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(context_.CommandBuffer(), 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);

    PushConstants push_constants{};
    push_constants.view_projection = frame_data_cache.view_projection;
    push_constants.shading_params = ecs::Float4{
        .x = create_info_cache.sdf_smooth,
        .y = create_info_cache.bitmap_gamma,
        .z = create_info_cache.bitmap_edge_sharpness,
        .w = 0.0F,
    };
    push_constants.texture_slot = 0U;
    push_constants.sampler_slot = 0U;
    push_constants.reserved0 = 0U;
    push_constants.reserved1 = 0U;
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    if (active_frame_index < frame_states.size()) {
        const PerFrameState& frame_state = frame_states[active_frame_index];
        if (frame_state.instance_count > 0U && frame_state.vertex_buffer.buffer != VK_NULL_HANDLE) {
            std::uint32_t stage_draw_call_count = 0U;
            std::uint32_t stage_filtered_batch_count = 0U;
            context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   pipeline_layout,
                                                   0U,
                                                   2U);
            stats.descriptor_set_bind_count += 2U;

            VkBuffer vertex_buffer = frame_state.vertex_buffer.buffer;
            if (frame_state.graph_vertex_size_bytes > 0U &&
                render_graph::IsValidResourceHandle(frame_state.graph_vertex_buffer)) {
                if (const auto* graph_vertex_record =
                        context_.FindBuffer(frame_state.graph_vertex_buffer);
                    graph_vertex_record != nullptr) {
                    if (graph_vertex_record->owned_resource.buffer != VK_NULL_HANDLE) {
                        vertex_buffer = graph_vertex_record->owned_resource.buffer;
                    } else if (graph_vertex_record->imported_buffer.buffer != VK_NULL_HANDLE) {
                        vertex_buffer = graph_vertex_record->imported_buffer.buffer;
                    }
                }
            }
            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(context_.CommandBuffer(), 0U, 1U, &vertex_buffer, &vertex_offset);

            render::GraphicsPipelineId bound_pipeline_id{};
            for (const auto& batch : render_scratch.draw_batches) {
                if (filter_by_pass_bucket_ &&
                    ecs::TextSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                    ++stage_filtered_batch_count;
                    continue;
                }
                if (batch.glyph_count == 0U) {
                    continue;
                }
                if (batch.atlas_page_id >= glyph_upload_host->PageCount()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const render::BindlessSlot texture_slot =
                    glyph_upload_host->ResolveBindlessImageSlot(batch.atlas_page_id);
                const render::BindlessSlot sampler_slot =
                    glyph_upload_host->BindlessConfig().sampler_slot;
                if (!texture_slot.IsValid() || !sampler_slot.IsValid()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const DepthPipelineMode mode = ResolveDepthPipelineMode(batch,
                                                                       use_depth_attachment,
                                                                       active_camera_reverse_z);
                const render::GraphicsPipelineId pipeline_id =
                    EnsureGraphicsPipelineForMode(*context,
                                                 *pipeline_host,
                                                 resolved_color.format,
                                                 use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED,
                                                 mode);
                if (!pipeline_id.IsValid()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }
                if (!bound_pipeline_id.IsValid() || bound_pipeline_id.value != pipeline_id.value) {
                    vkCmdBindPipeline(context_.CommandBuffer(),
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      pipeline_host->GetGraphicsPipeline(pipeline_id));
                    bound_pipeline_id = pipeline_id;
                    ++stats.depth_pipeline_bind_count;
                }

                push_constants.texture_slot = texture_slot.index;
                push_constants.sampler_slot = sampler_slot.index;
                vkCmdPushConstants(context_.CommandBuffer(),
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);

                vkCmdDraw(context_.CommandBuffer(),
                          4U,
                          batch.glyph_count,
                          0U,
                          batch.glyph_begin);
                ++stats.draw_call_count;
                ++stage_draw_call_count;
                if (mode == DepthPipelineMode::depth_test_reverse_z ||
                    mode == DepthPipelineMode::depth_test_write_reverse_z) {
                    ++stats.reverse_z_draw_call_count;
                }
            }

            if (filter_by_pass_bucket_) {
                stats.stage_filtered_batch_count += stage_filtered_batch_count;
                if (stage_draw_call_count == 0U) {
                    ++stats.empty_stage_pass_count;
                }
                if (pass_bucket_ == static_cast<std::uint32_t>(ecs::TextRenderPassHint::opaque)) {
                    stats.opaque_draw_call_count += stage_draw_call_count;
                } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::TextRenderPassHint::transparent)) {
                    stats.transparent_draw_call_count += stage_draw_call_count;
                }
            }
        }
    }
}

void TextRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                          VkExtent2D extent_,
                                          VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void TextRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                          VkExtent2D extent_,
                                          VkFormat format_,
                                          std::uint64_t last_submitted_value_,
                                          std::uint64_t completed_submit_value_) {
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);

    if (context != nullptr && create_info_cache.enable_depth) {
        RetireDepthResources(last_submitted_value_seen);
        CollectRetiredDepthResources(*context, completed_submit_value_seen);
    }

    image_initialized.resize(image_count_);
    for (auto& initialized_flag : image_initialized) {
        initialized_flag = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;

}

bool TextRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const TextRenderer3DStats& TextRenderer3D::Stats() const noexcept {
    return stats;
}

void TextRenderer3D::ResetPerFrameDrawState(std::uint32_t frame_index_,
                                            std::uint32_t atlas_page_count_) {
    (void)atlas_page_count_;
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    frame_state.instance_count = 0U;
    frame_state.graph_vertex_buffer = render_graph::invalid_resource_handle;
    frame_state.graph_vertex_version = render_graph::invalid_resource_version;
    frame_state.graph_vertex_size_bytes = 0U;
}

void TextRenderer3D::EnsureGpuResourcesForFrame(VulkanContext& context_,
                                                const render::TextRenderer3DPrepareView& prepare_view_,
                                                std::uint32_t frame_index_,
                                                VkDeviceSize required_bytes_) {
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    const VkDeviceSize required_capacity = (required_bytes_ > 0U)
        ? std::max(create_info_cache.initial_vertex_buffer_bytes,
                   NextPow2(required_bytes_))
        : frame_state.vertex_buffer_capacity_bytes;

    if (required_bytes_ == 0U &&
        frame_state.vertex_buffer.buffer == VK_NULL_HANDLE &&
        frame_state.vertex_buffer_capacity_bytes == 0U) {
        return;
    }
    if (frame_state.vertex_buffer.buffer != VK_NULL_HANDLE &&
        frame_state.vertex_buffer_capacity_bytes >= required_bytes_) {
        return;
    }

    resource::BufferHost::DestroyBuffer(context_, frame_state.vertex_buffer);
    frame_state.vertex_buffer_capacity_bytes = 0U;
    frame_state.uploaded_revision = 0U;

    if (required_capacity == 0U) {
        return;
    }

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = required_capacity;
    buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_create_info.persistently_mapped = false;

    const std::uint32_t upload_queue_family_index = prepare_view_.upload.QueueFamilyIndex();
    const std::uint32_t graphics_queue_family_index = context_.QueueFamilies().graphics.value();
    if (upload_queue_family_index != graphics_queue_family_index) {
        buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        buffer_create_info.queue_family_indices.push_back(upload_queue_family_index);
        buffer_create_info.queue_family_indices.push_back(graphics_queue_family_index);
    }

    frame_state.vertex_buffer = resource::BufferHost::CreateBuffer(context_,
                                                                   buffer_create_info,
                                                                   *gpu_memory_host);
    frame_state.vertex_buffer_capacity_bytes = required_capacity;
    frame_state.uploaded_revision = 0U;
}

void TextRenderer3D::EnsureGraphUploadStagingForFrame(
    VulkanContext& context_,
    const render::TextRenderer3DPrepareView& prepare_view_,
    std::uint32_t frame_index_,
    VkDeviceSize required_bytes_) {
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    const VkDeviceSize required_capacity = (required_bytes_ > 0U)
        ? std::max(create_info_cache.initial_vertex_buffer_bytes,
                   NextPow2(required_bytes_))
        : frame_state.graph_staging_buffer_capacity_bytes;
    if (required_capacity == 0U) {
        return;
    }
    if (frame_state.graph_staging_buffer.buffer != VK_NULL_HANDLE &&
        frame_state.graph_staging_buffer_capacity_bytes >= required_bytes_) {
        return;
    }

    resource::BufferHost::DestroyBuffer(context_, frame_state.graph_staging_buffer);
    frame_state.graph_staging_buffer_capacity_bytes = 0U;

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = required_capacity;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_create_info.memory_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    buffer_create_info.persistently_mapped = true;

    if (context_.QueueFamilies().graphics.has_value() &&
        context_.QueueFamilies().transfer.has_value() &&
        context_.QueueFamilies().graphics.value() != context_.QueueFamilies().transfer.value()) {
        buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        buffer_create_info.queue_family_indices.push_back(context_.QueueFamilies().graphics.value());
        buffer_create_info.queue_family_indices.push_back(context_.QueueFamilies().transfer.value());
    } else {
        const std::uint32_t upload_queue_family_index = prepare_view_.upload.QueueFamilyIndex();
        const std::uint32_t graphics_queue_family_index = context_.QueueFamilies().graphics.value();
        if (upload_queue_family_index != graphics_queue_family_index) {
            buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
            buffer_create_info.queue_family_indices.push_back(upload_queue_family_index);
            buffer_create_info.queue_family_indices.push_back(graphics_queue_family_index);
        }
    }

    frame_state.graph_staging_buffer = resource::BufferHost::CreateBuffer(context_,
                                                                          buffer_create_info,
                                                                          *gpu_memory_host);
    frame_state.graph_staging_buffer_capacity_bytes = required_capacity;
}

void TextRenderer3D::ScheduleGraphInstanceUpload(render_graph::RenderGraphBuilder& builder_,
                                                 const render_graph::PassHandle pass_) {
    if (context == nullptr || gpu_memory_host == nullptr) {
        return;
    }
    if (active_frame_index >= frame_states.size() || render_scratch.instances.empty()) {
        return;
    }

    PerFrameState& frame_state = frame_states[active_frame_index];
    const VkDeviceSize required_bytes =
        static_cast<VkDeviceSize>(frame_state.instance_count) * sizeof(ecs::Text3DGpuInstance);
    if (frame_state.instance_count == 0U || required_bytes == 0U) {
        return;
    }
    if (render_graph::IsValidResourceVersionHandle(frame_state.graph_vertex_version) &&
        render_graph::IsValidResourceHandle(frame_state.graph_vertex_buffer) &&
        frame_state.graph_vertex_size_bytes == required_bytes) {
        (void)builder_.Read(
            pass_,
            frame_state.graph_vertex_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::vertex_buffer_read,
                .buffer_range = {.offset_bytes = 0U, .size_bytes = required_bytes},
            });
        return;
    }
    if (frame_state.graph_staging_buffer.buffer == VK_NULL_HANDLE ||
        frame_state.graph_staging_buffer.mapped_ptr == nullptr ||
        frame_state.graph_staging_buffer_capacity_bytes < required_bytes) {
        return;
    }

    std::memcpy(frame_state.graph_staging_buffer.mapped_ptr,
                render_scratch.instances.data(),
                static_cast<std::size_t>(required_bytes));

    frame_state.graph_vertex_buffer = builder_.CreateBuffer(
        "text_3d_instances",
        render_graph::BufferDesc{
            .size_bytes = required_bytes,
            .usage = render_graph::buffer_usage_vertex_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::transient);
    const auto upload_pass = builder_.AddPass("text_3d_upload_instances",
                                              false,
                                              render_graph::QueueClass::transfer);
    frame_state.graph_vertex_version = builder_.Write(
        upload_pass,
        frame_state.graph_vertex_buffer,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = required_bytes},
        });
    frame_state.graph_vertex_size_bytes = required_bytes;
    stats.uploaded_bytes = required_bytes;

    builder_.SetExecuteCallback(
        upload_pass,
        [this,
         frame_index = active_frame_index,
         target = frame_state.graph_vertex_buffer,
         size = required_bytes](render_graph::GraphCommandContext& context_) {
            if (frame_index >= frame_states.size()) {
                return;
            }

            const PerFrameState& frame_state_ref = frame_states[frame_index];
            if (frame_state_ref.graph_staging_buffer.buffer == VK_NULL_HANDLE) {
                return;
            }

            const auto* target_record = context_.FindBuffer(target);
            if (target_record == nullptr || target_record->owned_resource.buffer == VK_NULL_HANDLE) {
                throw std::runtime_error(
                    "TextRenderer3D graph upload pass could not resolve target vertex buffer");
            }

            VkBufferCopy copy_region{};
            copy_region.srcOffset = 0U;
            copy_region.dstOffset = 0U;
            copy_region.size = size;
            vkCmdCopyBuffer(context_.CommandBuffer(),
                            frame_state_ref.graph_staging_buffer.buffer,
                            target_record->owned_resource.buffer,
                            1U,
                            &copy_region);
        });
    (void)builder_.Read(
        pass_,
        frame_state.graph_vertex_version,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::vertex_buffer_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = required_bytes},
        });
}

void TextRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                           render::DescriptorHost& descriptor_host_,
                                           render::PipelineHost& pipeline_host_,
                                           VkFormat color_format_,
                                           VkFormat depth_format_) {
    (void)descriptor_host_;
    RequireTextRuntimeFeatures(context_, "TextRenderer3D::EnsurePipelineObjects");

    const render::PipelineHostStats& pipeline_stats = pipeline_host_.Stats();
    if (shader_vertex_id.IsValid() && pipeline_stats.shader_module_count < shader_vertex_id.value) {
        shader_vertex_id = {};
    }
    if (shader_fragment_id.IsValid() && pipeline_stats.shader_module_count < shader_fragment_id.value) {
        shader_fragment_id = {};
    }
    if (pipeline_layout_id.IsValid() && pipeline_stats.pipeline_layout_count < pipeline_layout_id.value) {
        pipeline_layout_id = {};
    }
    for (auto& pipeline_id : graphics_pipeline_ids) {
        if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
            pipeline_id = {};
        }
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_text_3d_vert_spv;
        shader_create_info.word_count = generated::k_text_3d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_text_3d_frag_spv;
        shader_create_info.word_count = generated::k_text_3d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        if (bindless_resources == nullptr ||
            bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
            bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
            throw std::runtime_error("TextRenderer3D::EnsurePipelineObjects requires bindless resource layouts");
        }
        render::PipelineLayoutDesc pipeline_layout_desc{};
        pipeline_layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        pipeline_layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        pipeline_layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, pipeline_layout_desc);
    }

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        for (auto& pipeline_id : graphics_pipeline_ids) {
            pipeline_id = {};
        }
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
    }
}

render::GraphicsPipelineId TextRenderer3D::EnsureGraphicsPipelineForMode(VulkanContext& context_,
                                                                          render::PipelineHost& pipeline_host_,
                                                                          VkFormat color_format_,
                                                                          VkFormat depth_format_,
                                                                          DepthPipelineMode mode_) {
    const std::size_t mode_index = PipelineModeIndex(mode_);
    if (mode_index >= graphics_pipeline_ids.size()) {
        throw std::out_of_range("TextRenderer3D::EnsureGraphicsPipelineForMode mode index out of range");
    }
    if (graphics_pipeline_ids[mode_index].IsValid()) {
        return graphics_pipeline_ids[mode_index];
    }
    if (!pipeline_layout_id.IsValid() || !shader_vertex_id.IsValid() || !shader_fragment_id.IsValid()) {
        throw std::runtime_error("TextRenderer3D::EnsureGraphicsPipelineForMode requires valid pipeline objects");
    }
    if (color_format_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("TextRenderer3D::EnsureGraphicsPipelineForMode requires valid color format");
    }

    const bool depth_test_enabled =
        mode_ == DepthPipelineMode::depth_test ||
        mode_ == DepthPipelineMode::depth_test_write ||
        mode_ == DepthPipelineMode::depth_test_reverse_z ||
        mode_ == DepthPipelineMode::depth_test_write_reverse_z;
    const bool depth_write_enabled =
        mode_ == DepthPipelineMode::depth_test_write ||
        mode_ == DepthPipelineMode::depth_test_write_reverse_z;
    const bool reverse_z_enabled =
        mode_ == DepthPipelineMode::depth_test_reverse_z ||
        mode_ == DepthPipelineMode::depth_test_write_reverse_z;

    render::GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    pipeline_desc.use_dynamic_rendering = true;
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);
    pipeline_desc.rendering.depth_attachment_format = depth_format_;

    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(shader_fragment_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });

    pipeline_desc.vertex_input.bindings.push_back({
        .binding = 0U,
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Text3DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 0U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = 0U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 1U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = 16U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 2U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 32U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 48U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 64U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 5U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = 80U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 6U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = 84U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 7U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = 88U
    });

    pipeline_desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipeline_desc.input_assembly.primitive_restart_enable = false;

    pipeline_desc.viewport.viewport_count = 1U;
    pipeline_desc.viewport.scissor_count = 1U;
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    pipeline_desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
    pipeline_desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    pipeline_desc.rasterization.line_width = 1.0F;

    pipeline_desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    pipeline_desc.depth_stencil.depth_test_enable = depth_test_enabled;
    pipeline_desc.depth_stencil.depth_write_enable = depth_write_enabled;
    pipeline_desc.depth_stencil.depth_compare_op = reverse_z_enabled
        ? VK_COMPARE_OP_GREATER_OR_EQUAL
        : VK_COMPARE_OP_LESS_OR_EQUAL;
    pipeline_desc.depth_stencil.depth_bounds_test_enable = false;
    pipeline_desc.depth_stencil.stencil_test_enable = false;
    pipeline_desc.depth_stencil.min_depth_bounds = 0.0F;
    pipeline_desc.depth_stencil.max_depth_bounds = 1.0F;

    const VkPipelineColorBlendAttachmentState blend_attachment =
        render::BuildColorBlendAttachment(render::ColorBlendPreset::alpha);
    pipeline_desc.color_blend.attachments.push_back(blend_attachment);

    const render::GraphicsPipelineId pipeline_id =
        pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    graphics_pipeline_ids[mode_index] = pipeline_id;
    return pipeline_id;
}

void TextRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_resource : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_resource);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void TextRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

void TextRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
    if (depth_images.empty()) {
        return;
    }

    retired_depth_images.reserve(retired_depth_images.size() + depth_images.size());
    for (auto& depth_resource : depth_images) {
        if (depth_resource.image == VK_NULL_HANDLE) {
            continue;
        }

        RetiredDepthImage retired{};
        retired.resource = depth_resource;
        retired.retire_value = retire_value_;
        retired_depth_images.push_back(retired);
        depth_resource = {};
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void TextRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
                                                  std::uint64_t completed_value_) {
    if (retired_depth_images.empty()) {
        return;
    }
    if (context_.Device() == VK_NULL_HANDLE) {
        return;
    }

    std::size_t write_index = 0U;
    for (std::size_t read_index = 0U; read_index < retired_depth_images.size(); ++read_index) {
        auto& retired = retired_depth_images[read_index];
        if (retired.retire_value <= completed_value_) {
            resource::ImageHost::DestroyImage(context_, retired.resource);
            continue;
        }

        if (write_index != read_index) {
            retired_depth_images[write_index] = retired;
        }
        ++write_index;
    }
    retired_depth_images.resize(write_index);
}

void TextRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                          std::uint32_t image_count_,
                                          VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("TextRenderer3D::EnsureDepthResources requires initialized GpuMemoryHost");
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.preferred_depth_format);
    }

    const bool compatible_existing =
        depth_images.size() == image_count_ &&
        !depth_images.empty() &&
        depth_images[0U].format == depth_format &&
        depth_images[0U].extent.width == extent_.width &&
        depth_images[0U].extent.height == extent_.height;
    if (compatible_existing) {
        return;
    }

    RetireDepthResources(last_submitted_value_seen);
    CollectRetiredDepthResources(context_, completed_submit_value_seen);
    depth_images.resize(image_count_);
    depth_image_initialized.resize(image_count_);
    for (auto& initialized_flag : depth_image_initialized) {
        initialized_flag = 0U;
    }

    for (std::uint32_t i = 0U; i < image_count_; ++i) {
        resource::ImageCreateInfo image_create_info{};
        image_create_info.image_type = VK_IMAGE_TYPE_2D;
        image_create_info.format = depth_format;
        image_create_info.extent = VkExtent3D{extent_.width, extent_.height, 1U};
        image_create_info.mip_levels = 1U;
        image_create_info.array_layers = 1U;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image_create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        image_create_info.create_default_view = true;
        image_create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        image_create_info.default_view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        image_create_info.default_base_mip_level = 0U;
        image_create_info.default_level_count = 1U;
        image_create_info.default_base_array_layer = 0U;
        image_create_info.default_layer_count = 1U;

        depth_images[i] = resource::ImageHost::CreateImage(context_,
                                                           image_create_info,
                                                           *gpu_memory_host);
    }
}


} // namespace vr::text

