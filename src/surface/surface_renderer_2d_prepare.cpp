#include "vr/surface/surface_renderer_2d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/surface/generated/surface_2d_frag_spv.hpp"
#include "vr/surface/generated/surface_2d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::surface {

void SurfaceRenderer2D::PrepareFrame(const render::SurfaceRenderer2DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer2D::PrepareFrame called before Initialize");
    }
    if (surface_upload_host == nullptr || !surface_upload_host->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer2D::PrepareFrame requires initialized SurfaceUploadHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    gpu_memory_host = &prepare_view_.gpu_memory;
    sampler_host = &prepare_view_.sampler;
    if (prepare_view_.bindless != nullptr) {
        bindless_resources = prepare_view_.bindless;
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer2D::PrepareFrame requires initialized BindlessResourceSystem");
    }
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (surface_image_host != nullptr &&
        surface_image_host->IsInitialized() &&
        (!surface_image_host->BindlessConfig().Enabled() ||
         surface_image_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureSurfaceImageHost(*surface_image_host);
    }
    if (shadow_atlas_host != nullptr &&
        shadow_atlas_host->IsInitialized() &&
        (!shadow_atlas_host->BindlessConfig().Enabled() ||
         shadow_atlas_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureShadowAtlasHost(*shadow_atlas_host);
    }

    active_frame_index = prepare_view_.frame.frame_index;
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
    EnsureFallbackTexture(*context, *upload_host, active_frame_index);

    if (active_frame_index >= frame_lighting_resources.size()) {
        frame_lighting_resources.resize(active_frame_index + 1U);
    }
    {
        FrameLightingResources& frame_resources = frame_lighting_resources[active_frame_index];
        frame_resources.descriptor_set = VK_NULL_HANDLE;
        frame_resources.descriptor_buffer_signature = 0U;
        frame_resources.descriptor_image_signature = 0U;
        frame_resources.descriptor_set_signature = 0U;
    }

    stats = {};
    stats.component_count = component_count;
    stats.appearance_component_count = appearance_component_count;
    last_upload_result = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    const auto appearance_prepare_result = appearance_prepare_bridge.PrepareSurface(
        surface_components,
        component_count,
        active_frame_index);
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
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        bindless_mapping_revision = 0U;
        pending_dirty_component_indices = nullptr;
        pending_dirty_component_count = 0U;
        return;
    }

    ecs::Surface2DRuntimeBuildHint runtime_build_hint{};
    runtime_build_hint.transform_dirty_component_indices = pending_dirty_component_indices;
    runtime_build_hint.transform_dirty_component_count = pending_dirty_component_count;

    last_upload_result.runtime = ecs::SurfaceRuntimeSystem<ecs::Dim2>::Build(
        surface_components,
        transforms,
        component_count,
        runtime_scratch,
        create_info_cache.runtime_upload_options.runtime_build,
        runtime_build_hint);

    if (!runtime_scratch.instances.empty() &&
        last_upload_result.runtime.emitted_instance_count > 0U) {
        RemapInstancesToBindless(runtime_scratch.instances.data(),
                                 static_cast<std::uint32_t>(runtime_scratch.instances.size()));
        const std::uint64_t upload_revision = surface::SurfaceUploadHost::ComposeUploadRevision(
            last_upload_result.runtime.surface_signature,
            last_upload_result.runtime.transform_signature) ^
            (static_cast<std::uint64_t>(bindless_mapping_revision) + 0x9e3779b97f4a7c15ULL);

        if (surface::SurfaceUploadHost::ShouldAttemptPartialUpload(
                last_upload_result.runtime,
                runtime_build_hint,
                create_info_cache.runtime_upload_options)) {
            last_upload_result.plan = ecs::SurfaceUploadPlanSystem<ecs::Dim2>::BuildRangesFromDirtyComponents(
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
                last_upload_result.upload = surface_upload_host->Upload2DInstancePatches(
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
                last_upload_result.upload = surface_upload_host->Upload2DInstances(
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
            last_upload_result.upload = surface_upload_host->Upload2DInstances(
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
        bindless_mapping_revision = 0U;
        last_upload_result.skipped_upload = true;
    }

    stats.visible_component_count = last_upload_result.runtime.batch.visible_count;
    stats.instance_count = last_upload_result.runtime.emitted_instance_count;
    stats.draw_batch_count = last_upload_result.runtime.emitted_batch_count;
    stats.uploaded_instance_count = last_upload_result.upload.element_count;
    stats.uploaded_patch_count = last_upload_result.upload.patch_count;
    stats.uploaded_bytes = last_upload_result.upload.uploaded ? last_upload_result.upload.size_bytes : 0U;
    stats.cache_reused = !last_upload_result.upload.uploaded &&
                         last_upload_result.runtime.emitted_instance_count > 0U;
    stats.transform_only_update = last_upload_result.runtime.transform_only_update;
    stats.used_partial_upload = last_upload_result.used_partial_upload;
    stats.skipped_upload = last_upload_result.skipped_upload;

    EnsureLightingDescriptorObjects(*context, *descriptor_host);
    if (!light_shadow_upload_host.IsInitialized()) {
        light::LightShadowUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = descriptor_host->FramesInFlight();
        light_shadow_upload_host.Initialize(*context, *gpu_memory_host, upload_create_info);
    }
    light_shadow_upload_host.BeginFrame(*context,
                                        active_frame_index,
                                        last_submitted_value_seen,
                                        completed_submit_value_seen);
    EnsureLightingResourcesForFrame(*context);

    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
}

} // namespace vr::surface
