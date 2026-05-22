#include "vr/surface/surface_renderer_3d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/scene_3d_descriptor_contract.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/surface/generated/surface_3d_frag_spv.hpp"
#include "vr/surface/generated/surface_3d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::surface {

void SurfaceRenderer3D::PrepareFrame(const render::SurfaceRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame called before Initialize");
    }
    if (surface_upload_host == nullptr || !surface_upload_host->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame requires initialized SurfaceUploadHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    ibl_host = &prepare_view_.ibl;
    gpu_memory_host = &prepare_view_.gpu_memory;
    texture_host = prepare_view_.texture;
    if (prepare_view_.bindless != nullptr) {
        bindless_resources = prepare_view_.bindless;
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "SurfaceRenderer3D::PrepareFrame requires initialized BindlessResourceSystem");
    }
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (texture_host != nullptr &&
        texture_host->IsInitialized() &&
        (!texture_host->BindlessConfig().Enabled() ||
         texture_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureTextureHost(*texture_host);
    }
    if (surface_image_host != nullptr &&
        surface_image_host->IsInitialized() &&
        (!surface_image_host->BindlessConfig().Enabled() ||
         surface_image_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureSurfaceImageHost(*surface_image_host);
    }
    active_frame_index = prepare_view_.frame.frame_index;
    if (active_frame_index >= frame_appearance_resources.size()) {
        frame_appearance_resources.resize(active_frame_index + 1U);
    }
    {
        FrameAppearanceResources& frame_resources = frame_appearance_resources[active_frame_index];
        frame_resources.descriptor_set = VK_NULL_HANDLE;
        frame_resources.descriptor_buffer_signature = 0U;
    }
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen,
                                           prepare_view_.progress.completed_submit_value);

    surface_upload_host->BeginFrame(*context,
                                    active_frame_index,
                                    last_submitted_value_seen,
                                    completed_submit_value_seen);
    if (surface_image_host != nullptr && surface_image_host->IsInitialized()) {
        surface_image_host->BeginFrame(*context, completed_submit_value_seen);
    }
    const render::IblEnvironmentId ibl_environment_id{prepare_view_.ibl_environment_id};
    const asset::TextureId ibl_brdf_lut_texture_id{prepare_view_.ibl_brdf_lut_texture_id};
    if (ibl_environment_id.IsValid() || ibl_brdf_lut_texture_id.IsValid()) {
        ibl_host->PrepareEnvironmentFrame(render::MakeIblHostPrepareView(prepare_view_),
                                          ibl_environment_id,
                                          ibl_brdf_lut_texture_id);
    } else {
        ibl_host->PrepareFrame(render::MakeIblHostPrepareView(prepare_view_));
    }
    active_ibl_params_descriptor_set = ibl_host->ActiveParamsDescriptorSet(active_frame_index);
    active_ibl_specular_texture_slot = ibl_host->ActiveSpecularTextureSlot();
    active_ibl_brdf_lut_texture_slot = ibl_host->ActiveBrdfLutTextureSlot();
    active_ibl_sampler_slot = ibl_host->ActiveSamplerSlot();
    if (active_ibl_params_descriptor_set == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame failed to resolve IBL params descriptor set");
    }

    stats = {};
    stats.component_count = component_count;
    stats.appearance_component_count = appearance_component_count;
    last_upload_result = {};
    culling_stats = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    const auto appearance_prepare_result = appearance_prepare_bridge.PrepareSurface(
        surface_components,
        component_count,
        active_frame_index);
    appearance_build_invoked = appearance_prepare_result.build_invoked;
    if (appearance_prepare_result.has_appearance_data) {
        appearance_runtime_stats = appearance_prepare_result.runtime_stats;
        appearance_link_stats = appearance_prepare_result.link_stats;
        stats.appearance_visible_count = appearance_runtime_stats.visible_count;
        stats.appearance_updated_record_count = appearance_runtime_stats.updated_record_count;
        stats.appearance_cache_reused = appearance_prepare_result.cache_reused;
        stats.appearance_link_scanned_count = appearance_link_stats.scanned_count;
        stats.appearance_link_updated_count = appearance_link_stats.updated_count;
    }

    if (surface_components == nullptr || component_count == 0U) {
        appearance_source_record_scratch.clear();
        EnsureAppearanceResourcesForFrame(bindless_revision_now);
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        pending_dirty_component_indices = nullptr;
        pending_dirty_component_count = 0U;
        return;
    }

    ecs::Surface3DRuntimeBuildHint runtime_build_hint{};
    runtime_build_hint.transform_dirty_component_indices = pending_dirty_component_indices;
    runtime_build_hint.transform_dirty_component_count = pending_dirty_component_count;
    if (bounds_components != nullptr && camera_component != nullptr) {
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

    last_upload_result.runtime = ecs::SurfaceRuntimeSystem<ecs::Dim3>::Build(
        surface_components,
        transforms,
        component_count,
        runtime_scratch,
        create_info_cache.runtime_upload_options.runtime_build,
        runtime_build_hint);

    if (!runtime_scratch.instances.empty() &&
        last_upload_result.runtime.emitted_instance_count > 0U) {
        BuildAppearanceRecordsAndAssignIndices();
        EnsureAppearanceResourcesForFrame(bindless_revision_now);
        const std::uint64_t upload_revision = ComposeBindlessUploadRevision(
            last_upload_result.runtime,
            surface_image_host != nullptr ? surface_image_host->Stats().revision : 0U);

        if (surface::SurfaceUploadHost::ShouldAttemptPartialUpload(
                last_upload_result.runtime,
                runtime_build_hint,
                create_info_cache.runtime_upload_options)) {
            last_upload_result.plan = ecs::SurfaceUploadPlanSystem<ecs::Dim3>::BuildRangesFromDirtyComponents(
                runtime_scratch,
                runtime_build_hint.transform_dirty_component_indices,
                runtime_build_hint.transform_dirty_component_count,
                create_info_cache.runtime_upload_options.plan_build,
                plan_scratch);

            if (last_upload_result.plan.range_count > 0U &&
                last_upload_result.plan.covered_instance_count > 0U) {
                static_assert(sizeof(ecs::SurfaceUploadPatchRange) == sizeof(surface::SurfaceUploadPatch));
                static_assert(alignof(ecs::SurfaceUploadPatchRange) == alignof(surface::SurfaceUploadPatch));
                const auto* patches = reinterpret_cast<const surface::SurfaceUploadPatch*>(
                    plan_scratch.ranges.data());
                last_upload_result.upload = surface_upload_host->Upload3DInstancePatches(
                    *context,
                    *upload_host,
                    active_frame_index,
                    runtime_scratch.instances.data(),
                    static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                    patches,
                    last_upload_result.plan.range_count,
                    upload_revision);
                last_upload_result.used_partial_upload =
                    last_upload_result.upload.partial && last_upload_result.upload.uploaded;
                last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;
            } else if (last_upload_result.runtime.transform_rewritten_instance_count == 0U) {
                last_upload_result.skipped_upload = true;
            } else {
                last_upload_result.upload = surface_upload_host->Upload3DInstances(
                    *context,
                    *upload_host,
                    active_frame_index,
                    runtime_scratch.instances.data(),
                    static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                    upload_revision);
                last_upload_result.used_partial_upload = false;
                last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;
            }
        } else {
            last_upload_result.upload = surface_upload_host->Upload3DInstances(
                *context,
                *upload_host,
                active_frame_index,
                runtime_scratch.instances.data(),
                static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                upload_revision);
            last_upload_result.used_partial_upload = false;
            last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;
        }
    } else {
        appearance_source_record_scratch.clear();
        EnsureAppearanceResourcesForFrame(bindless_revision_now);
        last_upload_result.skipped_upload = true;
    }

    stats.visible_component_count = last_upload_result.runtime.batch.visible_count;
    stats.instance_count = last_upload_result.runtime.emitted_instance_count;
    stats.draw_batch_count = last_upload_result.runtime.emitted_batch_count;
    stats.depth_test_batch_count = last_upload_result.runtime.depth_test_batch_count;
    stats.depth_write_batch_count = last_upload_result.runtime.depth_write_batch_count;
    stats.uploaded_instance_count = last_upload_result.upload.element_count;
    stats.uploaded_patch_count = last_upload_result.upload.patch_count;
    stats.uploaded_bytes += last_upload_result.upload.size_bytes;
    stats.cache_reused = last_upload_result.runtime.cache_reused;
    stats.transform_only_update = last_upload_result.runtime.transform_only_update;
    stats.used_partial_upload = last_upload_result.used_partial_upload;
    stats.skipped_upload = last_upload_result.skipped_upload;

    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
}

} // namespace vr::surface
