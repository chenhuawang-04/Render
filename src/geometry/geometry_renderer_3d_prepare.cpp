#include "vr/geometry/geometry_renderer_3d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/geometry/geometry_appearance_resolver.hpp"
#include "vr/geometry/generated/geometry_3d_frag_spv.hpp"
#include "vr/geometry/generated/geometry_3d_vert_spv.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/scene_3d_descriptor_contract.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::geometry {

void GeometryRenderer3D::PrepareFrame(const render::GeometryRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame called before Initialize");
    }
    if (geometry_resource_host == nullptr || !geometry_resource_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryResourceHost");
    }
    if (geometry_upload_host == nullptr || !geometry_upload_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryUploadHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    bindless_resources = prepare_view_.bindless;
    pipeline_host = &prepare_view_.pipeline;
    gpu_memory_host = &prepare_view_.gpu_memory;
    ibl_host = &prepare_view_.ibl;
    sampler_host = &prepare_view_.sampler;
    texture_host = prepare_view_.texture;
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized BindlessResourceSystem");
    }
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (texture_host != nullptr &&
        texture_host->IsInitialized() &&
        (!texture_host->BindlessConfig().Enabled() ||
         texture_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureTextureHost(*texture_host);
    }
    if (geometry_image_host != nullptr &&
        geometry_image_host->IsInitialized() &&
        (!geometry_image_host->BindlessConfig().Enabled() ||
         geometry_image_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureGeometryImageHost(*geometry_image_host);
    }
    if (shadow_atlas_host != nullptr &&
        shadow_atlas_host->IsInitialized() &&
        (!shadow_atlas_host->BindlessConfig().Enabled() ||
         shadow_atlas_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureShadowAtlasHost(*shadow_atlas_host);
    }

    active_frame_index = prepare_view_.frame.frame_index;
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
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen, prepare_view_.progress.completed_submit_value);

    CollectRetiredDepthResources(*context, completed_submit_value_seen);
    geometry_resource_host->BeginFrame(*context, completed_submit_value_seen);
    geometry_upload_host->BeginFrame(*context,
                                     active_frame_index,
                                     last_submitted_value_seen,
                                     completed_submit_value_seen);
    if (geometry_image_host != nullptr && geometry_image_host->IsInitialized()) {
        geometry_image_host->BeginFrame(*context, completed_submit_value_seen);
    }

    std::uint32_t appearance_revision_now = 0U;
    if (geometry_appearance_host != nullptr && geometry_appearance_host->IsInitialized()) {
        appearance_revision_now = geometry_appearance_host->Stats().revision;
    }
    if (appearance_revision_now != appearance_host_revision_seen) {
        resolved_appearances.clear();
        appearance_host_revision_seen = appearance_revision_now;
    }
    bindless_revision_seen = bindless_revision_now;

    stats = {};
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

    stats.component_count = component_count;
    stats.appearance_component_count = appearance_component_count;
    stats.appearance_resolve_cache_entry_count = static_cast<std::uint32_t>(resolved_appearances.size());
    instance_range = {};
    culling_stats = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_build_invoked = false;
    appearance_full_rebuild = false;
    const render::IblEnvironmentId ibl_environment_id{prepare_view_.ibl_environment_id};
    const asset::TextureId ibl_brdf_lut_texture_id{prepare_view_.ibl_brdf_lut_texture_id};
    if (ibl_environment_id.IsValid() || ibl_brdf_lut_texture_id.IsValid()) {
        ibl_host->PrepareEnvironmentFrame(render::MakeIblHostPrepareView(prepare_view_),
                                          ibl_environment_id,
                                          ibl_brdf_lut_texture_id);
    } else {
        ibl_host->PrepareFrame(render::MakeIblHostPrepareView(prepare_view_));
    }
    const auto appearance_prepare_result = appearance_prepare_bridge.PrepareGeometry(
        geometry_components,
        component_count,
        active_frame_index);
    appearance_build_invoked = appearance_prepare_result.build_invoked;
    appearance_full_rebuild =
        appearance_prepare_result.build_invoked &&
        appearance_prepare_result.runtime_stats.full_rebuild != 0U;
    if (appearance_prepare_result.has_appearance_data) {
        appearance_runtime_stats = appearance_prepare_result.runtime_stats;
        appearance_link_stats = appearance_prepare_result.link_stats;
        stats.appearance_visible_count = appearance_runtime_stats.visible_count;
        stats.appearance_updated_record_count = appearance_runtime_stats.updated_record_count;
        stats.appearance_cache_reused = appearance_prepare_result.cache_reused;
        stats.appearance_link_scanned_count = appearance_link_stats.scanned_count;
        stats.appearance_link_updated_count = appearance_link_stats.updated_count;
    }

    if (geometry_components == nullptr || component_count == 0U) {
        appearance_source_record_scratch.clear();
        EnsureLightingResourcesForFrame(*context);
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        runtime_stats = {};
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        return;
    }

    ecs::Geometry3DRuntimeBuildHint build_hint{};
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
        build_hint.visible_component_indices = culling_scratch.visible_indices.data();
        build_hint.visible_component_count = culling_stats.visible_count;
        build_hint.use_visible_component_indices = 1U;
        build_hint.external_visible_set_signature = culling_stats.visible_set_signature;
        build_hint.use_external_visible_set_signature = 1U;

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
    }
    build_hint.skeletal_outputs = skeletal_outputs;
    build_hint.skeletal_output_count = skeletal_output_count;
    build_hint.vertex_deform_outputs = vertex_deform_outputs;
    build_hint.vertex_deform_output_count = vertex_deform_output_count;
    build_hint.morph_outputs = morph_outputs;
    build_hint.morph_output_count = morph_output_count;
    build_hint.frame_sequence_outputs = frame_sequence_outputs;
    build_hint.frame_sequence_output_count = frame_sequence_output_count;

    runtime_stats = ecs::GeometryRuntimeSystem<ecs::Dim3>::Build(geometry_components,
                                                                  transforms,
                                                                  component_count,
                                                                  runtime_scratch,
                                                                  create_info_cache.runtime_build,
                                                                  build_hint);
    stats.visible_component_count = runtime_stats.batch.visible_count;
    stats.instance_count = runtime_stats.emitted_instance_count;
    stats.draw_batch_count = runtime_stats.emitted_batch_count;
    stats.depth_test_batch_count = runtime_stats.depth_test_batch_count;
    stats.depth_write_batch_count = runtime_stats.depth_write_batch_count;
    stats.shadow_cast_batch_count = runtime_stats.shadow_cast_batch_count;
    stats.skeletal_animated_instance_count = runtime_stats.skeletal_animated_instance_count;
    stats.vertex_deform_animated_instance_count = runtime_stats.vertex_deform_animated_instance_count;
    stats.morph_animated_instance_count = runtime_stats.morph_animated_instance_count;
    stats.frame_sequence_animated_instance_count = runtime_stats.frame_sequence_animated_instance_count;
    stats.cache_reused = runtime_stats.cache_reused;
    stats.transform_only_update = runtime_stats.transform_only_update;

    if (!runtime_scratch.instances.empty()) {
        const std::uint64_t appearance_override_signature = ApplyAppearanceStateOverrides();
        const std::uint64_t upload_revision =
            runtime_stats.geometry_signature ^
            (runtime_stats.transform_signature * 0x9e3779b97f4a7c15ULL) ^
            (appearance_override_signature * 0xbf58476d1ce4e5b9ULL);
        instance_range = geometry_upload_host->Upload3DInstances(*context,
                                                                 *upload_host,
                                                                 active_frame_index,
                                                                 runtime_scratch.instances.data(),
                                                                 static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                                                                 upload_revision);
        if (instance_range.uploaded) {
            stats.uploaded_instance_count = instance_range.element_count;
            stats.uploaded_bytes = instance_range.size_bytes;
        }
    } else {
        appearance_source_record_scratch.clear();
    }

    EnsureLightingResourcesForFrame(*context);
    VkFormat active_color_format = prepare_view_.frame.swapchain_format;

    VkFormat active_depth_format = VK_FORMAT_UNDEFINED;
    if (create_info_cache.enable_depth) {
        active_depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
    }

    if (active_color_format != VK_FORMAT_UNDEFINED) {
        EnsurePipelineObjects(*context, *pipeline_host, active_color_format, active_depth_format);

        if (create_info_cache.prewarm_common_pipelines) {
            PrewarmCommonPipelines(*context, *pipeline_host, active_color_format, active_depth_format);
        }
        if (create_info_cache.compile_required_pipelines_in_prepare) {
            CompileRequiredPipelinesForCurrentFrame(*context,
                                                   *pipeline_host,
                                                   active_color_format,
                                                   active_depth_format);
        }
    }

}

} // namespace vr::geometry
