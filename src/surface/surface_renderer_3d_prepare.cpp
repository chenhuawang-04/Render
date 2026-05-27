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

void SurfaceRenderer3D::PrepareFrame(
    const render::SurfaceRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "SurfaceRenderer3D::PrepareFrame called before Initialize");
    }
    if (surface_upload_host == nullptr || !surface_upload_host->IsInitialized()) {
        throw std::runtime_error(
            "SurfaceRenderer3D::PrepareFrame requires initialized SurfaceUploadHost");
    }

    BindPrepareFrameRuntime(prepare_view_);
    CpuRuntimeFrameBuildResult cpu_build_result =
        BuildCpuRuntimeFrameStage(prepare_view_);
    ApplyPreparedFrameState(prepare_view_, std::move(cpu_build_result));
}

void SurfaceRenderer3D::BindPrepareFrameRuntime(
    const render::SurfaceRenderer3DPrepareView& prepare_view_) {
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
    ResetActiveFrameRuntimeTruth();

    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (texture_host != nullptr &&
        texture_host->IsInitialized() &&
        (!texture_host->BindlessConfig().Enabled() ||
         texture_host->BindlessConfig().bindless_revision !=
             bindless_revision_now)) {
        bindless_resources->ConfigureTextureHost(*texture_host);
    }
    if (surface_image_host != nullptr &&
        surface_image_host->IsInitialized() &&
        (!surface_image_host->BindlessConfig().Enabled() ||
         surface_image_host->BindlessConfig().bindless_revision !=
             bindless_revision_now)) {
        bindless_resources->ConfigureSurfaceImageHost(*surface_image_host);
    }

    active_frame_index = prepare_view_.frame.frame_index;
    if (active_frame_index >= frame_appearance_resources.size()) {
        frame_appearance_resources.resize(active_frame_index + 1U);
    }
    {
        FrameAppearanceResources& frame_resources =
            frame_appearance_resources[active_frame_index];
        frame_resources.descriptor_set = VK_NULL_HANDLE;
        frame_resources.descriptor_buffer_signature = 0U;
    }
    last_submitted_value_seen = std::max(
        last_submitted_value_seen, prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(
        completed_submit_value_seen,
        prepare_view_.progress.completed_submit_value);

    surface_upload_host->BeginFrame(*context,
                                    active_frame_index,
                                    last_submitted_value_seen,
                                    completed_submit_value_seen);
    if (surface_image_host != nullptr && surface_image_host->IsInitialized()) {
        surface_image_host->BeginFrame(*context, completed_submit_value_seen);
    }
    const render::IblEnvironmentId ibl_environment_id =
        prepare_view_.ibl_environment_id;
    const asset::TextureId ibl_brdf_lut_texture_id =
        prepare_view_.ibl_brdf_lut_texture_id;
    if (ibl_environment_id.IsValid() || ibl_brdf_lut_texture_id.IsValid()) {
        ibl_host->PrepareEnvironmentFrame(
            render::MakeIblHostPrepareView(prepare_view_),
            ibl_environment_id,
            ibl_brdf_lut_texture_id);
    } else {
        ibl_host->PrepareFrame(render::MakeIblHostPrepareView(prepare_view_));
    }
    active_ibl_params_descriptor_set =
        ibl_host->ActiveParamsDescriptorSet(active_frame_index);
    active_ibl_specular_texture_slot = ibl_host->ActiveSpecularTextureSlot();
    active_ibl_brdf_lut_texture_slot = ibl_host->ActiveBrdfLutTextureSlot();
    active_ibl_sampler_slot = ibl_host->ActiveSamplerSlot();
    if (active_ibl_params_descriptor_set == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "SurfaceRenderer3D::PrepareFrame failed to resolve IBL params descriptor set");
    }

    stats = {};
    stats.component_count = component_count;
    stats.appearance_component_count = appearance_component_count;
    culling_stats = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_build_invoked = false;
    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    appearance_source_record_scratch.clear();
}

SurfaceRenderer3D::CpuRuntimeFrameBuildResult
SurfaceRenderer3D::BuildCpuRuntimeFrameStage(
    const render::SurfaceRenderer3DPrepareView& prepare_view_) {
    (void)prepare_view_;

    ReclaimPreparedFrameArtifactScratchStorage();

    CpuRuntimeFrameBuildResult result{};
    CpuRuntimeFrameDispatchPayload& payload = result.dispatch_payload;
    PreparedFrameArtifacts& artifacts = result.prepared_artifacts;
    payload.bindless_revision =
        bindless_resources != nullptr ? bindless_resources->Revision() : 0U;

    const auto appearance_prepare_result = appearance_prepare_bridge.PrepareSurface(
        surface_components, component_count, active_frame_index);
    appearance_build_invoked = appearance_prepare_result.build_invoked;
    if (appearance_prepare_result.has_appearance_data) {
        appearance_runtime_stats = appearance_prepare_result.runtime_stats;
        appearance_link_stats = appearance_prepare_result.link_stats;
        stats.appearance_visible_count = appearance_runtime_stats.visible_count;
        stats.appearance_updated_record_count =
            appearance_runtime_stats.updated_record_count;
        stats.appearance_cache_reused = appearance_prepare_result.cache_reused;
        stats.appearance_link_scanned_count =
            appearance_link_stats.scanned_count;
        stats.appearance_link_updated_count =
            appearance_link_stats.updated_count;
    }

    if (surface_components == nullptr || component_count == 0U) {
        runtime_scratch.draw_batches.clear();
        appearance_source_record_scratch.clear();
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        return result;
    }

    ecs::Surface3DRuntimeBuildHint runtime_build_hint{};
    runtime_build_hint.transform_dirty_component_indices =
        pending_dirty_component_indices;
    runtime_build_hint.transform_dirty_component_count =
        pending_dirty_component_count;
    if (bounds_components != nullptr && camera_component != nullptr) {
        const ecs::CullingBuildOptions culling_options{
            .enable_culling_mask_filter = true,
            .enable_frustum_culling = true,
            .enable_aabb_refine = true,
            .write_visibility_bits = false};
        culling_stats = ecs::CullingSystem<ecs::Dim3>::BuildVisibleSet(
            bounds_components,
            component_count,
            camera_component,
            culling_scratch,
            culling_options);
        runtime_build_hint.visible_component_indices =
            culling_scratch.visible_indices.data();
        runtime_build_hint.visible_component_count = culling_stats.visible_count;
        runtime_build_hint.use_visible_component_indices = 1U;
        runtime_build_hint.external_visible_set_signature =
            culling_stats.visible_set_signature;
        runtime_build_hint.use_external_visible_set_signature = 1U;

        stats.used_bounds_culling = true;
        stats.culling_input_count = culling_stats.input_count;
        stats.culling_visible_count = culling_stats.visible_count;
        stats.culling_culled_count =
            culling_stats.culled_by_mask_count +
            culling_stats.culled_by_frustum_count +
            culling_stats.culled_by_invalid_bounds_count;
        stats.culling_mask_reject_count = culling_stats.culled_by_mask_count;
        stats.culling_frustum_reject_count =
            culling_stats.culled_by_frustum_count;
        stats.culling_invalid_bounds_count =
            culling_stats.culled_by_invalid_bounds_count;
        stats.culling_plane_test_count = culling_stats.plane_test_count;
    } else {
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
    }

    payload.runtime_stats = ecs::SurfaceRuntimeSystem<ecs::Dim3>::Build(
        surface_components,
        transforms,
        component_count,
        runtime_scratch,
        create_info_cache.runtime_upload_options.runtime_build,
        runtime_build_hint);

    const auto& runtime_stats = payload.runtime_stats;
    stats.visible_component_count = runtime_stats.batch.visible_count;
    stats.instance_count = runtime_stats.emitted_instance_count;
    stats.draw_batch_count = runtime_stats.emitted_batch_count;
    stats.depth_test_batch_count = runtime_stats.depth_test_batch_count;
    stats.depth_write_batch_count = runtime_stats.depth_write_batch_count;
    stats.cache_reused = runtime_stats.cache_reused;
    stats.transform_only_update = runtime_stats.transform_only_update;
    payload.has_scene_data = true;
    artifacts.has_scene_data = true;

    if (!runtime_scratch.instances.empty() &&
        runtime_stats.emitted_instance_count > 0U) {
        BuildAppearanceRecordsAndAssignIndices();
        payload.instance_upload_revision = ComposeBindlessUploadRevision(
            runtime_stats,
            surface_image_host != nullptr ? surface_image_host->Stats().revision
                                          : 0U);

        if (surface::SurfaceUploadHost::ShouldAttemptPartialUpload(
                runtime_stats,
                runtime_build_hint,
                create_info_cache.runtime_upload_options)) {
            payload.upload_plan = ecs::SurfaceUploadPlanSystem<ecs::Dim3>::
                BuildRangesFromDirtyComponents(
                    runtime_scratch,
                    runtime_build_hint.transform_dirty_component_indices,
                    runtime_build_hint.transform_dirty_component_count,
                    create_info_cache.runtime_upload_options.plan_build,
                    plan_scratch);

            if (payload.upload_plan.range_count > 0U &&
                payload.upload_plan.covered_instance_count > 0U) {
                payload.upload_mode = UploadDispatchMode::partial;
                artifacts.upload_patch_ranges.resize(payload.upload_plan.range_count);
                std::copy(plan_scratch.ranges.begin(),
                          plan_scratch.ranges.begin() +
                              static_cast<std::ptrdiff_t>(
                                  payload.upload_plan.range_count),
                          artifacts.upload_patch_ranges.begin());
            } else if (runtime_stats.transform_rewritten_instance_count > 0U) {
                payload.upload_mode = UploadDispatchMode::full;
            }
        } else {
            payload.upload_mode = UploadDispatchMode::full;
        }
    }

    artifacts.instance_upload_source.swap(runtime_scratch.instances);
    artifacts.draw_batches.swap(runtime_scratch.draw_batches);
    artifacts.appearance_source_records.swap(appearance_source_record_scratch);
    return result;
}

void SurfaceRenderer3D::ApplyPreparedFrameState(
    const render::SurfaceRenderer3DPrepareView& prepare_view_,
    CpuRuntimeFrameBuildResult&& cpu_build_result_) {
    (void)prepare_view_;

    active_prepared_frame_state.dispatch_payload =
        std::move(cpu_build_result_.dispatch_payload);
    active_prepared_frame_state.artifacts =
        std::move(cpu_build_result_.prepared_artifacts);
    CpuRuntimeFrameDispatchPayload& payload =
        active_prepared_frame_state.dispatch_payload;
    PreparedFrameArtifacts& artifacts = active_prepared_frame_state.artifacts;

    last_upload_result = {};
    last_upload_result.runtime = payload.runtime_stats;
    last_upload_result.plan = payload.upload_plan;

    EnsureAppearanceResourcesForFrame(payload.bindless_revision);
    PrepareAppearanceDescriptorSetForFrame(active_frame_index);

    if (!payload.has_scene_data || !artifacts.has_scene_data) {
        ResetActivePreparedFrameState();
        ResetActiveFrameRuntimeTruth();
        pending_dirty_component_indices = nullptr;
        pending_dirty_component_count = 0U;
        return;
    }

    ActiveFrameRuntimeTruth installed_truth{};
    installed_truth.frame_index = active_frame_index;
    if (payload.upload_mode != UploadDispatchMode::none) {
        if (payload.upload_mode == UploadDispatchMode::partial) {
            static_assert(sizeof(ecs::SurfaceUploadPatchRange) ==
                          sizeof(surface::SurfaceUploadPatch));
            static_assert(alignof(ecs::SurfaceUploadPatchRange) ==
                          alignof(surface::SurfaceUploadPatch));
            const auto* patches =
                reinterpret_cast<const surface::SurfaceUploadPatch*>(
                    artifacts.upload_patch_ranges.data());
            last_upload_result.upload =
                surface_upload_host->Upload3DInstancePatches(
                    *context,
                    *upload_host,
                    active_frame_index,
                    artifacts.instance_upload_source.data(),
                    static_cast<std::uint32_t>(
                        artifacts.instance_upload_source.size()),
                    patches,
                    static_cast<std::uint32_t>(
                        artifacts.upload_patch_ranges.size()),
                    payload.instance_upload_revision);
        } else {
            last_upload_result.upload = surface_upload_host->Upload3DInstances(
                *context,
                *upload_host,
                active_frame_index,
                artifacts.instance_upload_source.data(),
                static_cast<std::uint32_t>(
                    artifacts.instance_upload_source.size()),
                payload.instance_upload_revision);
        }
        installed_truth.instance_upload_range = last_upload_result.upload;
    }

    artifacts.upload_patch_ranges.clear();
    last_upload_result.used_partial_upload =
        last_upload_result.upload.partial && last_upload_result.upload.uploaded;
    last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;
    stats.uploaded_instance_count = last_upload_result.upload.element_count;
    stats.uploaded_patch_count = last_upload_result.upload.patch_count;
    stats.uploaded_bytes += last_upload_result.upload.size_bytes;
    stats.used_partial_upload = last_upload_result.used_partial_upload;
    stats.skipped_upload = last_upload_result.skipped_upload;
    active_frame_runtime_truth = installed_truth;
    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
}

} // namespace vr::surface
