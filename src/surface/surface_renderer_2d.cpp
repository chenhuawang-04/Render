#include "vr/surface/surface_renderer_2d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/runtime_prepare_views.hpp"
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

template<typename HandleT>
[[nodiscard]] std::uintptr_t HandleBits(const HandleT handle_) noexcept {
    if constexpr (std::is_pointer_v<HandleT>) {
        return reinterpret_cast<std::uintptr_t>(handle_);
    } else {
        return static_cast<std::uintptr_t>(handle_);
    }
}

[[nodiscard]] render_graph::ExternalBufferBindingPayload MakeExternalBufferBindingPayload(
    const VkBuffer buffer_,
    const VkDeviceSize offset_,
    const VkDeviceSize range_) noexcept {
    return render_graph::ExternalBufferBindingPayload{
        .native_buffer = HandleBits(buffer_),
        .offset_bytes = static_cast<std::uint64_t>(offset_),
        .size_bytes = static_cast<std::uint64_t>(range_),
    };
}

[[nodiscard]] render_graph::ExternalBufferBindingPayload MakeExternalBufferBindingPayload(
    const light::LightShadowBufferRange& range_) noexcept {
    return MakeExternalBufferBindingPayload(range_.buffer,
                                            range_.offset,
                                            range_.size_bytes);
}

} // namespace

std::size_t SurfaceRenderer2D::BlendModeIndex(BlendModeKind mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

SurfaceRenderer2D::BlendModeKind SurfaceRenderer2D::ResolveBlendModeFromBatchParams(
    std::uint32_t params_) noexcept {
    switch (ecs::DecodeRuntimeBlendPresetBits(params_, ecs::surface2d_runtime_blend_shift)) {
    case ecs::RuntimeBlendPreset::opaque: return BlendModeKind::opaque;
    case ecs::RuntimeBlendPreset::alpha: return BlendModeKind::alpha;
    case ecs::RuntimeBlendPreset::additive: return BlendModeKind::additive;
    case ecs::RuntimeBlendPreset::multiply: return BlendModeKind::multiply;
    case ecs::RuntimeBlendPreset::premultiplied_alpha: return BlendModeKind::premultiplied_alpha;
    case ecs::RuntimeBlendPreset::screen: return BlendModeKind::screen;
    default: break;
    }

    const bool premultiplied_alpha = (params_ & 0x10U) != 0U;
    switch (params_ & 0x3U) {
    case 0U: return premultiplied_alpha ? BlendModeKind::premultiplied_alpha : BlendModeKind::alpha;
    case 1U: return BlendModeKind::additive;
    case 2U: return BlendModeKind::multiply;
    case 3U: return BlendModeKind::screen;
    default: break;
    }
    return premultiplied_alpha ? BlendModeKind::premultiplied_alpha : BlendModeKind::alpha;
}

render_graph::ExternalBufferBindingPayload SurfaceRenderer2D::ResolveLightRecordsExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const SurfaceRenderer2D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph lighting resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.light_records.buffer == VK_NULL_HANDLE || frame.light_records.size_bytes == 0U) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph light-record resolver requires uploaded light buffers");
    }
    return MakeExternalBufferBindingPayload(frame.light_records);
}

render_graph::ExternalBufferBindingPayload SurfaceRenderer2D::ResolveClusterHeadersExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const SurfaceRenderer2D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph cluster-header resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.cluster_headers.buffer == VK_NULL_HANDLE || frame.cluster_headers.size_bytes == 0U) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph cluster-header resolver requires uploaded cluster headers");
    }
    return MakeExternalBufferBindingPayload(frame.cluster_headers);
}

render_graph::ExternalBufferBindingPayload SurfaceRenderer2D::ResolveClusterIndicesExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const SurfaceRenderer2D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph cluster-index resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.cluster_indices.buffer == VK_NULL_HANDLE || frame.cluster_indices.size_bytes == 0U) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph cluster-index resolver requires uploaded cluster indices");
    }
    return MakeExternalBufferBindingPayload(frame.cluster_indices);
}

render_graph::ExternalBufferBindingPayload SurfaceRenderer2D::ResolveShadowViewsExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const SurfaceRenderer2D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph shadow-view resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.shadow_views.buffer == VK_NULL_HANDLE || frame.shadow_views.size_bytes == 0U) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph shadow-view resolver requires uploaded shadow views");
    }
    return MakeExternalBufferBindingPayload(frame.shadow_views);
}

render_graph::ExternalBufferBindingPayload SurfaceRenderer2D::ResolveLightingUniformExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const SurfaceRenderer2D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph lighting-uniform resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.lighting_uniform.buffer == VK_NULL_HANDLE || frame.lighting_uniform.size_bytes == 0U) {
        throw std::runtime_error(
            "SurfaceRenderer2D graph lighting-uniform resolver requires uploaded uniform buffer");
    }
    return MakeExternalBufferBindingPayload(frame.lighting_uniform);
}

void SurfaceRenderer2D::Initialize(const SurfaceRenderer2DCreateInfo& create_info_) {
    create_info_cache = create_info_;

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::SurfaceRuntimeSystem<ecs::Dim2>::Reserve(
            runtime_scratch,
            create_info_cache.reserve_component_count,
            create_info_cache.reserve_instance_count);
    }
    if (create_info_cache.reserve_dirty_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::SurfaceUploadPlanSystem<ecs::Dim2>::Reserve(
            plan_scratch,
            create_info_cache.reserve_dirty_component_count,
            create_info_cache.reserve_instance_count);
    }

    lighting_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    frame_lighting_resources.clear();
    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    fallback_texture = {};
    fallback_sampler_id = {};
    fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fallback_shadow_array_view = VK_NULL_HANDLE;
    shadow_sampler_id = {};

    image_initialized.clear();
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_prepare_bridge.Reset();
    last_upload_result = {};
    stats = {};

    surface_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    appearance_component_count = 0U;
    surface_upload_host = nullptr;
    surface_image_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;

    active_frame_index = 0U;
    swapchain_extent = {};
    bindless_mapping_revision = 0U;
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    light_frame_coordinator = nullptr;
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;
    local_light_shadow_link_coordinator.Reset();
    light_shadow_link_coordinator = nullptr;
    local_shadow_atlas_binding_coordinator.Reset();
    shadow_atlas_binding_coordinator = nullptr;
    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;

    initialized = true;
}

void SurfaceRenderer2D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    if (light_shadow_upload_host.IsInitialized()) {
        light_shadow_upload_host.Shutdown(context_);
    }
    if (fallback_shadow_array_view != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyView(context_, fallback_shadow_array_view);
        fallback_shadow_array_view = VK_NULL_HANDLE;
    }
    if (context_.Device() != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyImage(context_, fallback_texture);
    } else {
        fallback_texture = {};
    }
    fallback_sampler_id = {};
    shadow_sampler_id = {};

    surface_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    appearance_component_count = 0U;
    surface_upload_host = nullptr;
    surface_image_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;

    lighting_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    frame_lighting_resources.clear();
    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    image_initialized.clear();

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.cache = {};
    appearance_prepare_bridge.Reset();
    appearance_runtime_stats = {};
    appearance_link_stats = {};

    plan_scratch.instance_indices.clear();
    plan_scratch.ranges.clear();
    plan_scratch.dense_marks.clear();

    last_upload_result = {};
    stats = {};

    active_frame_index = 0U;
    swapchain_extent = {};
    bindless_mapping_revision = 0U;
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    light_frame_coordinator = nullptr;
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;
    local_light_shadow_link_coordinator.Reset();
    light_shadow_link_coordinator = nullptr;
    local_shadow_atlas_binding_coordinator.Reset();
    shadow_atlas_binding_coordinator = nullptr;
    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
    initialized = false;
}

void SurfaceRenderer2D::SetHost(SurfaceUploadHost* upload_host_) noexcept {
    surface_upload_host = upload_host_;
}

void SurfaceRenderer2D::SetImageHost(SurfaceImageHost* image_host_) noexcept {
    surface_image_host = image_host_;
}

void SurfaceRenderer2D::SetHosts(SurfaceUploadHost* upload_host_,
                                 SurfaceImageHost* image_host_) noexcept {
    surface_upload_host = upload_host_;
    surface_image_host = image_host_;
}

void SurfaceRenderer2D::SetSceneData(ecs::Surface<ecs::Dim2>* surface_components_,
                                     ecs::Transform<ecs::Dim2>* transforms_,
                                     std::uint32_t component_count_) noexcept {
    surface_components = surface_components_;
    transforms = transforms_;
    component_count = component_count_;
}

void SurfaceRenderer2D::SetAppearanceData(ecs::Appearance<ecs::Dim2>* appearance_components_,
                                          std::uint32_t appearance_component_count_) noexcept {
    appearance_component_count = appearance_component_count_;
    appearance_prepare_bridge.SetAppearanceData(appearance_components_,
                                                appearance_component_count_);
    appearance_prepare_bridge.Reserve(appearance_component_count_);
}

void SurfaceRenderer2D::SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                                              std::uint32_t dirty_component_count_) noexcept {
    pending_dirty_component_indices = dirty_component_indices_;
    pending_dirty_component_count = dirty_component_count_;
}

void SurfaceRenderer2D::SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                               std::uint32_t dirty_component_count_) noexcept {
    appearance_prepare_bridge.SetDirtyHint(dirty_component_indices_,
                                           dirty_component_count_);
}

void SurfaceRenderer2D::SetAppearanceCoordinator(
    render::AppearanceFrameCoordinator<ecs::Dim2>* appearance_frame_coordinator_) noexcept {
    appearance_prepare_bridge.SetCoordinator(appearance_frame_coordinator_);
    appearance_prepare_bridge.Reserve(appearance_component_count);
}

void SurfaceRenderer2D::SetLightFrameCoordinator(
    render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator_) noexcept {
    light_frame_coordinator = light_frame_coordinator_;
}

void SurfaceRenderer2D::SetLightShadowLinkCoordinator(
    render::LightShadowLinkCoordinator2D* light_shadow_link_coordinator_) noexcept {
    light_shadow_link_coordinator = light_shadow_link_coordinator_;
}

void SurfaceRenderer2D::SetShadowAtlasBindingCoordinator(
    render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_) noexcept {
    if (shadow_atlas_binding_coordinator != shadow_atlas_binding_coordinator_) {
        for (auto& frame_resources : frame_lighting_resources) {
            frame_resources.descriptor_buffer_signature = 0U;
            frame_resources.descriptor_image_signature = 0U;
            frame_resources.descriptor_set_signature = 0U;
        }
        if (shadow_atlas_binding_coordinator_ == nullptr) {
            local_shadow_atlas_binding_coordinator.Reset();
        }
    }
    shadow_atlas_binding_coordinator = shadow_atlas_binding_coordinator_;
}

void SurfaceRenderer2D::SetShadowFrameCoordinator(
    render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator_) noexcept {
    shadow_frame_coordinator = shadow_frame_coordinator_;
}

void SurfaceRenderer2D::SetShadowAtlasHost(shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept {
    if (shadow_atlas_host != shadow_atlas_host_) {
        for (auto& frame_resources : frame_lighting_resources) {
            frame_resources.descriptor_buffer_signature = 0U;
            frame_resources.descriptor_image_signature = 0U;
            frame_resources.descriptor_set_signature = 0U;
        }
        if (shadow_atlas_binding_coordinator == nullptr) {
            local_shadow_atlas_binding_coordinator.Reset();
        }
    }
    shadow_atlas_host = shadow_atlas_host_;
}

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

void SurfaceRenderer2D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "SurfaceRenderer2D::BuildDirectRuntimeGraph called before Initialize");
    }

    const auto pass = graph_view_.builder.AddPass("surface_renderer_2d_direct");
    graph_view_.present_ready_version = graph_view_.builder.Write(
        pass,
        graph_view_.present_target,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::color_attachment_write,
        });
    graph_view_.builder.SetRasterPassDesc(
        pass,
        render_graph::RasterPassDesc{
            .color_attachments = {
                render_graph::RasterColorAttachmentDesc{
                    .target = graph_view_.present_target,
                    .load_op = create_info_cache.clear_swapchain
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
        });
    DescribeGraphDescriptorBindings(graph_view_.builder, pass);
    graph_view_.builder.SetExecuteCallback(
        pass,
        [this, color_target = graph_view_.present_target](render_graph::GraphCommandContext& context_) {
            RecordGraphOverlay(context_, color_target);
        });
}

void SurfaceRenderer2D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                        const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "SurfaceRenderer2D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    auto shader_contract =
        render_graph::MakeSharedBindlessFragmentShaderContract("surface_2d.frag");
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 0U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 1U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 2U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 3U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 4U,
        .kind = render_graph::DescriptorBindingKind::uniform_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    builder_.SetPassShaderContract(pass_, std::move(shader_contract));
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
    const std::uint32_t light_records_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveLightRecordsExternalBufferBinding,
            .debug_name = "surface_2d.light_records",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      0U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      light_records_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t cluster_headers_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveClusterHeadersExternalBufferBinding,
            .debug_name = "surface_2d.cluster_headers",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      1U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_headers_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t cluster_indices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveClusterIndicesExternalBufferBinding,
            .debug_name = "surface_2d.cluster_indices",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      2U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_indices_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t shadow_views_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveShadowViewsExternalBufferBinding,
            .debug_name = "surface_2d.shadow_views",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      3U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      shadow_views_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t lighting_uniform_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveLightingUniformExternalBufferBinding,
            .debug_name = "surface_2d.lighting_uniform",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      4U,
                                      render_graph::DescriptorBindingKind::uniform_buffer,
                                      lighting_uniform_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
}

void SurfaceRenderer2D::RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                                           render_graph::ResourceHandle color_target_) {
    RecordGraphInternal(context_, color_target_);
}

void SurfaceRenderer2D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                            render_graph::ResourceHandle color_target_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay requires initialized BindlessResourceSystem");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          resolved_color.format);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(context_.CommandBuffer(), 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

    RecordDrawBatches(context_.CommandBuffer(),
                      render_extent,
                      resolved_color.format,
                      &context_);
}

void SurfaceRenderer2D::RecordDrawBatches(VkCommandBuffer command_buffer_,
                                          VkExtent2D render_extent_,
                                          VkFormat color_format_,
                                          const render_graph::GraphCommandContext* graph_context_) {
    if (last_upload_result.upload.buffer == VK_NULL_HANDLE ||
        runtime_scratch.draw_batches.empty()) {
        return;
    }

    const VkBuffer vertex_buffer = last_upload_result.upload.buffer;
    const VkDeviceSize vertex_offset = last_upload_result.upload.offset;
    vkCmdBindVertexBuffers(command_buffer_,
                           0U,
                           1U,
                           &vertex_buffer,
                           &vertex_offset);

    PushConstants push_constants{};
    push_constants.viewport_width = static_cast<float>(render_extent_.width);
    push_constants.viewport_height = static_cast<float>(render_extent_.height);
    push_constants.inv_viewport_width_2x = (render_extent_.width > 0U)
        ? (2.0F / static_cast<float>(render_extent_.width))
        : 0.0F;
    push_constants.inv_viewport_height_2x = (render_extent_.height > 0U)
        ? (2.0F / static_cast<float>(render_extent_.height))
        : 0.0F;
    push_constants.params = 0U;
    push_constants.params |= create_info_cache.input_positions_pixel_space ? 0x1U : 0U;
    push_constants.params |= create_info_cache.pixel_space_origin_top_left ? 0x2U : 0U;
    push_constants.reserved0 = 0U;
    push_constants.reserved1 = 0U;
    push_constants.reserved2 = 0U;

    if (graph_context_ == nullptr) {
        PrepareLightingDescriptorSetForFrame(active_frame_index);
    }
    VkDescriptorSet frame_lighting_descriptor_set = VK_NULL_HANDLE;
    if (active_frame_index < frame_lighting_resources.size()) {
        frame_lighting_descriptor_set = frame_lighting_resources[active_frame_index].descriptor_set;
    }

    const std::array<VkDescriptorSet, 2U> bindless_sets{
        bindless_resources->SampledImageSet(),
        bindless_resources->SamplerSet()
    };
    if (bindless_sets[0U] == VK_NULL_HANDLE || bindless_sets[1U] == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer2D::RecordDrawBatches requires valid bindless descriptor sets");
    }

    if (graph_context_ == nullptr && frame_lighting_descriptor_set == VK_NULL_HANDLE) {
        stats.skipped_batch_count += static_cast<std::uint32_t>(runtime_scratch.draw_batches.size());
        return;
    }
    const std::array<VkDescriptorSet, 3U> local_descriptor_sets{
        bindless_sets[0U],
        bindless_sets[1U],
        frame_lighting_descriptor_set,
    };

    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    render::GraphicsPipelineId bound_pipeline{};
    bool descriptor_sets_bound = false;
    for (const ecs::Surface2DDrawBatch& batch : runtime_scratch.draw_batches) {
        if (batch.instance_count == 0U) {
            ++stats.skipped_batch_count;
            continue;
        }

        const BlendModeKind blend_mode = ResolveBlendModeFromBatchParams(batch.params);
        const render::GraphicsPipelineId pipeline_id = EnsurePipelineForBlendMode(
            *context,
            *pipeline_host,
            color_format_,
            blend_mode);
        if (!pipeline_id.IsValid()) {
            ++stats.skipped_batch_count;
            continue;
        }

        if (bound_pipeline.value != pipeline_id.value) {
            vkCmdBindPipeline(command_buffer_,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));
            vkCmdPushConstants(command_buffer_,
                               pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(PushConstants),
                               &push_constants);
            bound_pipeline = pipeline_id;
            descriptor_sets_bound = false;
        }

        if (!descriptor_sets_bound) {
            if (graph_context_ != nullptr) {
                graph_context_->BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                              pipeline_layout,
                                                              0U,
                                                              3U);
            } else {
                vkCmdBindDescriptorSets(command_buffer_,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_layout,
                                        0U,
                                        static_cast<std::uint32_t>(local_descriptor_sets.size()),
                                        local_descriptor_sets.data(),
                                        0U,
                                        nullptr);
            }
            descriptor_sets_bound = true;
            ++stats.descriptor_set_bind_count;
            ++stats.light_descriptor_set_bind_count;
        }

        vkCmdDraw(command_buffer_,
                  6U,
                  batch.instance_count,
                  0U,
                  batch.instance_begin);
        ++stats.draw_call_count;
    }
}

void SurfaceRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                             VkExtent2D extent_,
                                             VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void SurfaceRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                             VkExtent2D extent_,
                                             VkFormat format_,
                                             std::uint64_t last_submitted_value_,
                                             std::uint64_t completed_submit_value_) {
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);

    image_initialized.resize(image_count_);
    for (auto& value : image_initialized) {
        value = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool SurfaceRenderer2D::IsInitialized() const noexcept {
    return initialized;
}

const SurfaceRenderer2DStats& SurfaceRenderer2D::Stats() const noexcept {
    return stats;
}

void SurfaceRenderer2D::EnsurePipelineObjects(VulkanContext& context_,
                                              render::DescriptorHost& descriptor_host_,
                                              render::PipelineHost& pipeline_host_,
                                              VkFormat color_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        throw std::runtime_error("SurfaceRenderer2D requires Vulkan 1.3 dynamicRendering");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "SurfaceRenderer2D::EnsurePipelineObjects requires initialized BindlessResourceSystem");
    }

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
    for (auto& pipeline_id : pipeline_ids) {
        if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
            pipeline_id = {};
        }
    }

    EnsureLightingDescriptorObjects(context_, descriptor_host_);

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_surface_2d_vert_spv;
        shader_info.word_count = generated::k_surface_2d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_surface_2d_frag_spv;
        shader_info.word_count = generated::k_surface_2d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!pipeline_layout_id.IsValid()) {
        const VkDescriptorSetLayout sampled_image_layout = bindless_resources->SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout = bindless_resources->SamplerLayout();
        const VkDescriptorSetLayout lighting_layout = descriptor_host_.GetLayout(lighting_descriptor_layout_id);
        if (sampled_image_layout == VK_NULL_HANDLE ||
            sampler_layout == VK_NULL_HANDLE ||
            lighting_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "SurfaceRenderer2D::EnsurePipelineObjects requires valid bindless/lighting descriptor layouts");
        }

        render::PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(sampled_image_layout);
        layout_desc.set_layouts.push_back(sampler_layout);
        layout_desc.set_layouts.push_back(lighting_layout);
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_color_format != color_format_) {
        pipeline_color_format = color_format_;
        for (auto& pipeline_id : pipeline_ids) {
            pipeline_id = {};
        }
    }
}

render::GraphicsPipelineId SurfaceRenderer2D::EnsurePipelineForBlendMode(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat color_format_,
    BlendModeKind blend_mode_) {
    if (descriptor_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer2D::EnsurePipelineForBlendMode missing DescriptorHost");
    }

    EnsurePipelineObjects(context_, *descriptor_host, pipeline_host_, color_format_);

    render::GraphicsPipelineId& cached_pipeline_id = pipeline_ids[BlendModeIndex(blend_mode_)];
    if (cached_pipeline_id.IsValid() && pipeline_color_format == color_format_) {
        return cached_pipeline_id;
    }

    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    desc.use_dynamic_rendering = true;
    desc.rendering.color_attachment_formats.push_back(color_format_);
    desc.rendering.depth_attachment_format = VK_FORMAT_UNDEFINED;

    desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(shader_fragment_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });

    desc.vertex_input.bindings.push_back({
        .binding = 0U,
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Surface2DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    desc.vertex_input.attributes.push_back({
        .location = 0U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, world_m00))
    });
    desc.vertex_input.attributes.push_back({
        .location = 1U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, world_m10))
    });
    desc.vertex_input.attributes.push_back({
        .location = 2U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, size_x))
    });
    desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, pivot_x))
    });
    desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, uv_u0))
    });
    desc.vertex_input.attributes.push_back({
        .location = 5U,
        .binding = 0U,
        .format = VK_FORMAT_R32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, opacity))
    });
    desc.vertex_input.attributes.push_back({
        .location = 6U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, tint_rgba8))
    });
    desc.vertex_input.attributes.push_back({
        .location = 7U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, params))
    });
    desc.vertex_input.attributes.push_back({
        .location = 8U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, image_slot))
    });
    desc.vertex_input.attributes.push_back({
        .location = 9U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface2DGpuInstance, sampler_slot))
    });

    desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    desc.depth_stencil.depth_test_enable = false;
    desc.depth_stencil.depth_write_enable = false;

    render::ColorBlendPreset blend_preset = render::ColorBlendPreset::alpha;
    switch (blend_mode_) {
    case BlendModeKind::opaque:
        blend_preset = render::ColorBlendPreset::disabled;
        break;
    case BlendModeKind::alpha:
        blend_preset = render::ColorBlendPreset::alpha;
        break;
    case BlendModeKind::additive:
        blend_preset = render::ColorBlendPreset::additive;
        break;
    case BlendModeKind::multiply:
        blend_preset = render::ColorBlendPreset::multiply;
        break;
    case BlendModeKind::premultiplied_alpha:
        blend_preset = render::ColorBlendPreset::premultiplied_alpha;
        break;
    case BlendModeKind::screen:
        blend_preset = render::ColorBlendPreset::screen;
        break;
    default:
        break;
    }
    desc.color_blend.attachments.push_back(render::BuildColorBlendAttachment(blend_preset));

    cached_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, desc);
    pipeline_color_format = color_format_;
    return cached_pipeline_id;
}

void SurfaceRenderer2D::RemapInstancesToBindless(ecs::Surface2DGpuInstance* instances_,
                                                 std::uint32_t instance_count_) noexcept {
    if (instances_ == nullptr || instance_count_ == 0U) {
        bindless_mapping_revision = 0U;
        return;
    }

    std::uint64_t hash = 14695981039346656037ULL;
    auto hash_combine = [](std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= 1099511628211ULL;
    };

    for (std::uint32_t index = 0U; index < instance_count_; ++index) {
        ecs::Surface2DGpuInstance& instance = instances_[index];
        const std::uint32_t raw_surface_id = instance.image_slot;
        const std::uint32_t resolved_image_slot = ResolveImageSlot(raw_surface_id);
        const std::uint32_t resolved_sampler_slot = ResolveSamplerSlot(raw_surface_id);
        instance.image_slot = resolved_image_slot;
        instance.sampler_slot = resolved_sampler_slot;
        hash_combine(hash, static_cast<std::uint64_t>(resolved_image_slot));
        hash_combine(hash, static_cast<std::uint64_t>(resolved_sampler_slot));
    }

    hash_combine(hash, static_cast<std::uint64_t>(instance_count_));
    bindless_mapping_revision = static_cast<std::uint32_t>(hash ^ (hash >> 32U));
}

std::uint32_t SurfaceRenderer2D::ResolveImageSlot(std::uint32_t surface_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }

    const std::uint32_t placeholder_slot = bindless_resources->PlaceholderImageSlot().index;
    if (surface_id_ == 0U || surface_image_host == nullptr || !surface_image_host->IsInitialized()) {
        return placeholder_slot;
    }

    const render::BindlessSlot image_slot = surface_image_host->ResolveBindlessImageSlot(surface_id_);
    return image_slot.IsValid() ? image_slot.index : placeholder_slot;
}

std::uint32_t SurfaceRenderer2D::ResolveSamplerSlot(std::uint32_t surface_id_) const noexcept {
    (void)surface_id_;
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    return bindless_resources->DefaultSamplerSlot().index;
}

void SurfaceRenderer2D::EnsureFallbackTexture(VulkanContext& context_,
                                              render::UploadHost& upload_host_,
                                              std::uint32_t frame_index_) {
    if (sampler_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer2D::EnsureFallbackTexture missing SamplerHost");
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer2D::EnsureFallbackTexture missing GpuMemoryHost");
    }

    if (!fallback_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_LINEAR;
        sampler_desc.min_filter = VK_FILTER_LINEAR;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.max_lod = 0.0F;
        fallback_sampler_id = sampler_host->RegisterSampler(context_, sampler_desc);
    }
    if (!shadow_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_NEAREST;
        sampler_desc.min_filter = VK_FILTER_NEAREST;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.max_lod = 0.0F;
        shadow_sampler_id = sampler_host->RegisterSampler(context_, sampler_desc);
    }

    if (fallback_texture.image != VK_NULL_HANDLE &&
        fallback_texture.default_view != VK_NULL_HANDLE &&
        fallback_shadow_array_view != VK_NULL_HANDLE) {
        return;
    }
    if (context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("SurfaceRenderer2D::EnsureFallbackTexture requires synchronization2");
    }

    resource::ImageCreateInfo create_info{};
    create_info.image_type = VK_IMAGE_TYPE_2D;
    create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    create_info.extent = VkExtent3D{1U, 1U, 1U};
    create_info.mip_levels = 1U;
    create_info.array_layers = 1U;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    create_info.create_default_view = true;
    create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    create_info.default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.default_base_mip_level = 0U;
    create_info.default_level_count = 1U;
    create_info.default_base_array_layer = 0U;
    create_info.default_layer_count = 1U;
    fallback_texture = resource::ImageHost::CreateImage(context_, create_info, *gpu_memory_host);

    VkImageMemoryBarrier2 to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    to_transfer.srcAccessMask = 0U;
    to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = fallback_texture.image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.baseMipLevel = 0U;
    to_transfer.subresourceRange.levelCount = 1U;
    to_transfer.subresourceRange.baseArrayLayer = 0U;
    to_transfer.subresourceRange.layerCount = 1U;
    upload_host_.RecordImageBarrier2(frame_index_, to_transfer);

    constexpr std::uint32_t white_pixel = 0xFFFFFFFFU;
    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0U;
    copy_region.bufferRowLength = 0U;
    copy_region.bufferImageHeight = 0U;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0U;
    copy_region.imageSubresource.baseArrayLayer = 0U;
    copy_region.imageSubresource.layerCount = 1U;
    copy_region.imageOffset = VkOffset3D{0, 0, 0};
    copy_region.imageExtent = VkExtent3D{1U, 1U, 1U};
    upload_host_.StageAndRecordCopyImage(frame_index_,
                                         fallback_texture.image,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         copy_region,
                                         &white_pixel,
                                         sizeof(white_pixel),
                                         4U);

    VkImageMemoryBarrier2 to_shader_read{};
    to_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_shader_read.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_shader_read.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_shader_read.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    to_shader_read.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader_read.newLayout = fallback_texture_layout;
    to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.image = fallback_texture.image;
    to_shader_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_shader_read.subresourceRange.baseMipLevel = 0U;
    to_shader_read.subresourceRange.levelCount = 1U;
    to_shader_read.subresourceRange.baseArrayLayer = 0U;
    to_shader_read.subresourceRange.layerCount = 1U;
    upload_host_.RecordImageBarrier2(frame_index_, to_shader_read);

    if (fallback_shadow_array_view == VK_NULL_HANDLE) {
        VkImageViewCreateInfo array_view_create_info{};
        array_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        array_view_create_info.image = fallback_texture.image;
        array_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        array_view_create_info.format = fallback_texture.format;
        array_view_create_info.components = VkComponentMapping{
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        };
        array_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        array_view_create_info.subresourceRange.baseMipLevel = 0U;
        array_view_create_info.subresourceRange.levelCount = 1U;
        array_view_create_info.subresourceRange.baseArrayLayer = 0U;
        array_view_create_info.subresourceRange.layerCount = 1U;
        fallback_shadow_array_view = resource::ImageHost::CreateView(context_,
                                                                     fallback_texture.image,
                                                                     array_view_create_info);
    }
}

void SurfaceRenderer2D::EnsureLightingDescriptorObjects(VulkanContext& context_,
                                                        render::DescriptorHost& descriptor_host_) {
    if (lighting_descriptor_layout_id.IsValid()) {
        return;
    }

    render::DescriptorSetLayoutDesc layout_desc{};
    layout_desc.bindings.push_back({
        .binding = 0U,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1U,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    });
    layout_desc.bindings.push_back({
        .binding = 1U,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1U,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    });
    layout_desc.bindings.push_back({
        .binding = 2U,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1U,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    });
    layout_desc.bindings.push_back({
        .binding = 3U,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1U,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    });
    layout_desc.bindings.push_back({
        .binding = 4U,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1U,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    });
    lighting_descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
}

SurfaceRenderer2D::LightingParamsGpu SurfaceRenderer2D::BuildLightingParamsGpu(VkExtent2D extent_) const noexcept {
    LightingParamsGpu params{};
    const float width = static_cast<float>(std::max(extent_.width, 1U));
    const float height = static_cast<float>(std::max(extent_.height, 1U));

    if (create_info_cache.input_positions_pixel_space) {
        params.world_to_ndc_x = 2.0F / width;
        params.world_to_ndc_bias_x = -1.0F;
        if (create_info_cache.pixel_space_origin_top_left) {
            params.world_to_ndc_y = -2.0F / height;
            params.world_to_ndc_bias_y = 1.0F;
        } else {
            params.world_to_ndc_y = 2.0F / height;
            params.world_to_ndc_bias_y = -1.0F;
        }
    } else {
        params.world_to_ndc_x = 1.0F;
        params.world_to_ndc_y = 1.0F;
        params.world_to_ndc_bias_x = 0.0F;
        params.world_to_ndc_bias_y = 0.0F;
    }

    const std::uint32_t tile_count_x = std::max<std::uint32_t>(create_info_cache.light_culling_config.tile_count_x, 1U);
    const std::uint32_t tile_count_y = std::max<std::uint32_t>(create_info_cache.light_culling_config.tile_count_y, 1U);
    params.tile_count_x = tile_count_x;
    params.tile_count_y = tile_count_y;
    params.reverse_z = 0U;

    params.framebuffer_width = width;
    params.framebuffer_height = height;
    params.light_ambient = std::clamp(create_info_cache.light_ambient, 0.0F, 4.0F);
    params.max_fragment_lights = static_cast<float>(std::max<std::uint32_t>(create_info_cache.max_fragment_lights, 1U));
    return params;
}

void SurfaceRenderer2D::EnsureLightingResourcesForFrame(VulkanContext& context_) {
    if (active_frame_index >= frame_lighting_resources.size() ||
        !light_shadow_upload_host.IsInitialized()) {
        return;
    }

    FrameLightingResources& frame_resources = frame_lighting_resources[active_frame_index];
    frame_resources.shadow_namespace_id = 0U;

    stats.light_count = 0U;
    stats.visible_light_count = 0U;
    stats.shadow_view_count = 0U;
    stats.light_upload_range_count = 0U;
    stats.shadow_view_upload_range_count = 0U;
    stats.light_cluster_count = 0U;
    stats.light_cluster_index_count = 0U;
    stats.light_buffer_upload_count = 0U;
    stats.light_shadow_linked_count = 0U;
    stats.light_shadow_namespace_drop_count = 0U;
    stats.light_shadow_unmapped_count = 0U;

    auto hash_combine = [](std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= 1099511628211ULL;
    };
    auto hash_from_handle = [&](std::uint64_t& hash_, std::uintptr_t handle_) noexcept {
        hash_combine(hash_, static_cast<std::uint64_t>(handle_));
    };

    static const ecs::LightGpuRecord2D default_light_record{};
    static const ecs::ShadowViewGpuRecord default_shadow_view_record{};
    static const std::uint32_t default_cluster_index = 0U;

    SurfaceRenderer2DMcVector<ecs::LightClusterHeader> fallback_cluster_headers{};
    const std::uint32_t fallback_tile_count_x = std::max<std::uint32_t>(create_info_cache.light_culling_config.tile_count_x, 1U);
    const std::uint32_t fallback_tile_count_y = std::max<std::uint32_t>(create_info_cache.light_culling_config.tile_count_y, 1U);
    const std::uint32_t fallback_cluster_count = std::max<std::uint32_t>(fallback_tile_count_x * fallback_tile_count_y, 1U);
    fallback_cluster_headers.resize(fallback_cluster_count);

    const ecs::LightGpuRecord2D* linked_light_records = &default_light_record;
    std::uint32_t linked_light_record_count = 1U;
    const ecs::LightUploadRange* light_upload_ranges = nullptr;
    std::uint32_t light_upload_range_count = 0U;
    const ecs::LightClusterHeader* cluster_headers = fallback_cluster_headers.data();
    std::uint32_t cluster_header_count = static_cast<std::uint32_t>(fallback_cluster_headers.size());
    const std::uint32_t* cluster_indices = &default_cluster_index;
    std::uint32_t cluster_index_count = 1U;
    const ecs::ShadowViewGpuRecord* shadow_view_records = &default_shadow_view_record;
    std::uint32_t shadow_view_count = 1U;
    const ecs::ShadowUploadRange* shadow_view_upload_ranges = nullptr;
    std::uint32_t shadow_view_upload_range_count = 0U;

    const ecs::Shadow<ecs::Dim2>* shadow_components = nullptr;
    std::uint32_t shadow_component_count = 0U;
    const ecs::ShadowGpuRecord2D* shadow_records = nullptr;
    std::uint32_t shadow_record_count = 0U;
    std::uint64_t shadow_signature = 0U;
    std::uint64_t light_signature = 0U;
    std::uint32_t shadow_namespace_hint = 0U;

    bool light_cache_reused = false;
    bool shadow_cache_reused = false;

    if (create_info_cache.enable_light_shadow &&
        light_frame_coordinator != nullptr) {
        ecs::LightCullingBuildConfig<ecs::Dim2> light_culling_config = create_info_cache.light_culling_config;
        light_culling_config.tile_count_x = static_cast<std::uint16_t>(
            std::max<std::uint32_t>(light_culling_config.tile_count_x, 1U));
        light_culling_config.tile_count_y = static_cast<std::uint16_t>(
            std::max<std::uint32_t>(light_culling_config.tile_count_y, 1U));
        light_culling_config.max_lights_per_tile = static_cast<std::uint16_t>(
            std::max<std::uint32_t>(light_culling_config.max_lights_per_tile, 1U));

        const auto light_prepare_result = light_frame_coordinator->PrepareFrame(active_frame_index,
                                                                                 {},
                                                                                 light_culling_config);
        light_cache_reused = !light_prepare_result.runtime_build_invoked &&
                             !light_prepare_result.culling_build_invoked &&
                             light_prepare_result.has_light_data;
        const auto& light_runtime_scratch = light_frame_coordinator->RuntimeScratch();
        const auto& light_culling_scratch = light_frame_coordinator->CullingScratch();

        const ecs::LightGpuRecord2D* runtime_light_records =
            ecs::LightRuntimeSystem<ecs::Dim2>::GpuRecords(light_runtime_scratch);
        const std::uint32_t runtime_light_count =
            ecs::LightRuntimeSystem<ecs::Dim2>::GpuRecordCount(light_runtime_scratch);
        if (runtime_light_records != nullptr && runtime_light_count > 0U) {
            linked_light_records = runtime_light_records;
            linked_light_record_count = runtime_light_count;
            light_upload_ranges = ecs::LightRuntimeSystem<ecs::Dim2>::UploadRanges(light_runtime_scratch);
            light_upload_range_count = ecs::LightRuntimeSystem<ecs::Dim2>::UploadRangeCount(light_runtime_scratch);
            light_signature = light_prepare_result.runtime_stats.style_signature ^
                              (light_prepare_result.runtime_stats.binding_signature << 1U) ^
                              (light_prepare_result.runtime_stats.transform_signature << 7U);
            stats.light_count = runtime_light_count;
            stats.visible_light_count = light_prepare_result.culling_stats.accepted_light_count;
            stats.light_upload_range_count = light_upload_range_count;
        }

        const ecs::LightClusterHeader* runtime_cluster_headers =
            ecs::LightCullingSystem<ecs::Dim2>::ClusterHeaders(light_culling_scratch);
        const std::uint32_t runtime_cluster_header_count =
            ecs::LightCullingSystem<ecs::Dim2>::ClusterHeaderCount(light_culling_scratch);
        if (runtime_cluster_headers != nullptr && runtime_cluster_header_count > 0U) {
            cluster_headers = runtime_cluster_headers;
            cluster_header_count = runtime_cluster_header_count;
            stats.light_cluster_count = runtime_cluster_header_count;
        }

        const std::uint32_t* runtime_cluster_indices =
            ecs::LightCullingSystem<ecs::Dim2>::ClusterLightIndices(light_culling_scratch);
        const std::uint32_t runtime_cluster_index_count =
            ecs::LightCullingSystem<ecs::Dim2>::ClusterLightIndexCount(light_culling_scratch);
        if (runtime_cluster_indices != nullptr && runtime_cluster_index_count > 0U) {
            cluster_indices = runtime_cluster_indices;
            cluster_index_count = runtime_cluster_index_count;
            stats.light_cluster_index_count = runtime_cluster_index_count;
        }
    }

    if (create_info_cache.enable_light_shadow &&
        shadow_frame_coordinator != nullptr) {
        const auto shadow_prepare_result = shadow_frame_coordinator->PrepareFrame(active_frame_index);
        shadow_cache_reused = !shadow_prepare_result.runtime_build_invoked &&
                              !shadow_prepare_result.caster_build_invoked &&
                              shadow_prepare_result.has_shadow_data;
        const auto& shadow_runtime_scratch = shadow_frame_coordinator->RuntimeScratch();
        shadow_components = shadow_frame_coordinator->ShadowComponents();
        shadow_component_count = shadow_frame_coordinator->ShadowCount();
        shadow_records = ecs::ShadowRuntimeSystem<ecs::Dim2>::GpuRecords(shadow_runtime_scratch);
        shadow_record_count = ecs::ShadowRuntimeSystem<ecs::Dim2>::GpuRecordCount(shadow_runtime_scratch);

        const ecs::ShadowViewGpuRecord* runtime_shadow_view_records =
            ecs::ShadowRuntimeSystem<ecs::Dim2>::ViewRecords(shadow_runtime_scratch);
        const std::uint32_t runtime_shadow_view_count =
            ecs::ShadowRuntimeSystem<ecs::Dim2>::ViewRecordCount(shadow_runtime_scratch);
        if (runtime_shadow_view_records != nullptr && runtime_shadow_view_count > 0U) {
            shadow_view_records = runtime_shadow_view_records;
            shadow_view_count = runtime_shadow_view_count;
            shadow_view_upload_ranges = ecs::ShadowRuntimeSystem<ecs::Dim2>::ViewUploadRanges(shadow_runtime_scratch);
            shadow_view_upload_range_count = ecs::ShadowRuntimeSystem<ecs::Dim2>::ViewUploadRangeCount(shadow_runtime_scratch);
            stats.shadow_view_count = runtime_shadow_view_count;
            stats.shadow_view_upload_range_count = shadow_view_upload_range_count;
        }

        shadow_signature = shadow_prepare_result.runtime_stats.style_signature ^
                           (shadow_prepare_result.runtime_stats.binding_signature << 1U) ^
                           (shadow_prepare_result.runtime_stats.transform_signature << 7U) ^
                           (shadow_prepare_result.runtime_stats.camera_signature << 13U);
    }

    if (shadow_component_count > 0U && shadow_components != nullptr) {
        for (std::uint32_t i = 0U; i < shadow_component_count; ++i) {
            const ecs::Shadow<ecs::Dim2>& shadow_component = shadow_components[i];
            if (shadow_component.visibility.enabled == 0U || shadow_component.visibility.visible == 0U) {
                continue;
            }
            if (shadow_component.binding.atlas_namespace_id == 0U) {
                continue;
            }
            shadow_namespace_hint = shadow_component.binding.atlas_namespace_id;
            break;
        }
    }

    render::LightShadowLinkStageResult2D link_result{};
    if (create_info_cache.enable_light_shadow &&
        linked_light_records != nullptr &&
        linked_light_record_count > 0U) {
        render::LightShadowLinkCoordinator2D* link_coordinator = light_shadow_link_coordinator;
        if (link_coordinator == nullptr) {
            link_coordinator = &local_light_shadow_link_coordinator;
        }

        render::LightShadowLinkCoordinator2DPrepareInfo link_prepare_info{};
        link_prepare_info.signature = light_signature ^ (shadow_signature << 1U) ^
                                      (static_cast<std::uint64_t>(shadow_namespace_hint) << 17U);
        link_prepare_info.light_signature = light_signature;
        link_prepare_info.shadow_signature = shadow_signature;
        link_prepare_info.light_records = linked_light_records;
        link_prepare_info.light_record_count = linked_light_record_count;
        link_prepare_info.shadow_components = shadow_components;
        link_prepare_info.shadow_component_count = shadow_component_count;
        link_prepare_info.shadow_records = shadow_records;
        link_prepare_info.shadow_record_count = shadow_record_count;
        link_prepare_info.shadow_namespace_hint = shadow_namespace_hint;
        if (light_frame_coordinator != nullptr) {
            const auto& light_runtime_scratch = light_frame_coordinator->RuntimeScratch();
            link_prepare_info.light_updated_component_indices =
                ecs::LightRuntimeSystem<ecs::Dim2>::UpdatedComponentIndices(light_runtime_scratch);
            link_prepare_info.light_updated_component_count =
                ecs::LightRuntimeSystem<ecs::Dim2>::UpdatedComponentIndexCount(light_runtime_scratch);
        }

        const render::LightShadowLinkCoordinator2DResult link_prepare_result =
            link_coordinator->Prepare(link_prepare_info);
        link_result = link_prepare_result.link_result;
        if (link_prepare_result.cache_reused) {
            ++stats.light_shadow_link_cache_hit_count;
        }
        if (link_result.linked_light_records != nullptr &&
            link_result.linked_light_record_count > 0U) {
            linked_light_records = link_result.linked_light_records;
            linked_light_record_count = link_result.linked_light_record_count;
        }
        frame_resources.shadow_namespace_id = link_result.shadow_namespace_id;
        stats.light_shadow_linked_count = link_result.linked_light_count;
        stats.light_shadow_namespace_drop_count = link_result.namespace_drop_count;
        stats.light_shadow_unmapped_count = link_result.unmapped_light_count;
    } else if (light_shadow_link_coordinator == nullptr) {
        local_light_shadow_link_coordinator.Reset();
    }

    if (frame_resources.shadow_namespace_id == 0U) {
        frame_resources.shadow_namespace_id = shadow_namespace_hint;
    }

    if (linked_light_records == nullptr || linked_light_record_count == 0U) {
        linked_light_records = &default_light_record;
        linked_light_record_count = 1U;
        light_upload_ranges = nullptr;
        light_upload_range_count = 0U;
    }
    if (cluster_headers == nullptr || cluster_header_count == 0U) {
        cluster_headers = fallback_cluster_headers.data();
        cluster_header_count = static_cast<std::uint32_t>(fallback_cluster_headers.size());
    }
    if (cluster_indices == nullptr || cluster_index_count == 0U) {
        cluster_indices = &default_cluster_index;
        cluster_index_count = 1U;
    }
    if (shadow_view_records == nullptr || shadow_view_count == 0U) {
        shadow_view_records = &default_shadow_view_record;
        shadow_view_count = 1U;
        shadow_view_upload_ranges = nullptr;
        shadow_view_upload_range_count = 0U;
    }

    LightingParamsGpu lighting_params = BuildLightingParamsGpu(
        swapchain_extent.width > 0U && swapchain_extent.height > 0U
            ? swapchain_extent
            : VkExtent2D{1U, 1U});
    lighting_params.light_count = static_cast<float>(stats.light_count);
    lighting_params.shadow_view_count = static_cast<float>(stats.shadow_view_count);

    const VkSampler configured_shadow_sampler = shadow_sampler_id.IsValid()
        ? sampler_host->GetSampler(shadow_sampler_id)
        : VK_NULL_HANDLE;
    const VkSampler fallback_shadow_sampler = fallback_sampler_id.IsValid()
        ? sampler_host->GetSampler(fallback_sampler_id)
        : VK_NULL_HANDLE;

    render::ShadowAtlasBindingCoordinator* atlas_binding_coordinator = shadow_atlas_binding_coordinator;
    if (atlas_binding_coordinator == nullptr) {
        atlas_binding_coordinator = &local_shadow_atlas_binding_coordinator;
    }

    render::ShadowAtlasBindingResolveInput resolve_input{};
    resolve_input.atlas_host = shadow_atlas_host;
    resolve_input.namespace_id = frame_resources.shadow_namespace_id;
    resolve_input.fallback_namespace_id = 1U;
    resolve_input.allow_namespace_fallback = 1U;
    resolve_input.primary_sampler = configured_shadow_sampler;
    resolve_input.fallback_view = fallback_shadow_array_view;
    resolve_input.fallback_sampler = fallback_shadow_sampler;
    resolve_input.fallback_layout = fallback_texture_layout;

    const render::ShadowAtlasBindingResolveResult binding_result =
        atlas_binding_coordinator->Resolve(resolve_input);
    if (binding_result.cache_reused) {
        ++stats.light_shadow_atlas_binding_cache_hit_count;
    }

    std::uint32_t shadow_atlas_texture_slot = bindless_resources->PlaceholderImage2DArraySlot().index;
    if (binding_result.valid &&
        binding_result.atlas_namespace_id != 0U &&
        shadow_atlas_host != nullptr) {
        const auto* atlas_record = shadow_atlas_host->FindAtlas(binding_result.atlas_namespace_id);
        if (atlas_record != nullptr && atlas_record->bindless.image_slot.IsValid()) {
            shadow_atlas_texture_slot = atlas_record->bindless.image_slot.index;
        }
    }
    lighting_params.shadow_atlas_texture_slot = shadow_atlas_texture_slot;
    lighting_params.shadow_atlas_sampler_slot = shadow_sampler_id.IsValid()
        ? bindless_resources->ResolveRegisteredSamplerSlot(shadow_sampler_id).index
        : bindless_resources->DefaultSamplerSlot().index;

    std::uint64_t upload_signature = 14695981039346656037ULL;
    hash_combine(upload_signature, light_signature);
    hash_combine(upload_signature, shadow_signature);
    hash_combine(upload_signature, static_cast<std::uint64_t>(stats.light_count));
    hash_combine(upload_signature, static_cast<std::uint64_t>(stats.shadow_view_count));
    hash_combine(upload_signature, static_cast<std::uint64_t>(cluster_header_count));
    hash_combine(upload_signature, static_cast<std::uint64_t>(cluster_index_count));
    hash_combine(upload_signature, static_cast<std::uint64_t>(frame_resources.shadow_namespace_id));
    hash_combine(upload_signature, static_cast<std::uint64_t>(lighting_params.shadow_atlas_texture_slot));
    hash_combine(upload_signature, static_cast<std::uint64_t>(lighting_params.shadow_atlas_sampler_slot));
    frame_resources.upload_signature = upload_signature;

    frame_resources.light_records = light_shadow_upload_host.UploadLightRecordsRanges(
        context_,
        *upload_host,
        active_frame_index,
        linked_light_records,
        linked_light_record_count,
        light_upload_ranges,
        light_upload_range_count,
        upload_signature ^ 0x31ULL);
    frame_resources.cluster_headers = light_shadow_upload_host.UploadClusterHeaders(
        context_,
        *upload_host,
        active_frame_index,
        cluster_headers,
        cluster_header_count,
        upload_signature ^ 0x32ULL);
    frame_resources.cluster_indices = light_shadow_upload_host.UploadClusterIndices(
        context_,
        *upload_host,
        active_frame_index,
        cluster_indices,
        cluster_index_count,
        upload_signature ^ 0x33ULL);
    frame_resources.shadow_views = light_shadow_upload_host.UploadShadowViewsRanges(
        context_,
        *upload_host,
        active_frame_index,
        shadow_view_records,
        shadow_view_count,
        shadow_view_upload_ranges,
        shadow_view_upload_range_count,
        upload_signature ^ 0x34ULL);
    frame_resources.lighting_uniform = light_shadow_upload_host.UploadLightingUniform(
        context_,
        *upload_host,
        active_frame_index,
        &lighting_params,
        sizeof(LightingParamsGpu),
        upload_signature ^ 0x35ULL);

    stats.light_buffer_upload_count =
        static_cast<std::uint32_t>(frame_resources.light_records.uploaded) +
        static_cast<std::uint32_t>(frame_resources.cluster_headers.uploaded) +
        static_cast<std::uint32_t>(frame_resources.cluster_indices.uploaded) +
        static_cast<std::uint32_t>(frame_resources.shadow_views.uploaded) +
        static_cast<std::uint32_t>(frame_resources.lighting_uniform.uploaded);

    if (frame_resources.light_records.uploaded) {
        stats.uploaded_bytes += frame_resources.light_records.size_bytes;
    }
    if (frame_resources.cluster_headers.uploaded) {
        stats.uploaded_bytes += frame_resources.cluster_headers.size_bytes;
    }
    if (frame_resources.cluster_indices.uploaded) {
        stats.uploaded_bytes += frame_resources.cluster_indices.size_bytes;
    }
    if (frame_resources.shadow_views.uploaded) {
        stats.uploaded_bytes += frame_resources.shadow_views.size_bytes;
    }
    if (frame_resources.lighting_uniform.uploaded) {
        stats.uploaded_bytes += frame_resources.lighting_uniform.size_bytes;
    }

    std::uint64_t descriptor_signature = 14695981039346656037ULL;
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.light_records.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.cluster_headers.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.cluster_indices.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.shadow_views.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.lighting_uniform.buffer));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.light_records.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_headers.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_indices.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.shadow_views.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.lighting_uniform.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.light_records.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_headers.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_indices.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.shadow_views.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.lighting_uniform.size_bytes));
    frame_resources.descriptor_payload_signature = descriptor_signature;

    if (light_cache_reused || shadow_cache_reused) {
        // no-op; place-holder for future fast-path diagnostics
    }
}

void SurfaceRenderer2D::PrepareLightingDescriptorSetForFrame(std::uint32_t frame_index_) {
    if (descriptor_host == nullptr ||
        sampler_host == nullptr ||
        frame_index_ >= frame_lighting_resources.size()) {
        return;
    }
    if (!lighting_descriptor_layout_id.IsValid()) {
        return;
    }

    FrameLightingResources& frame_resources = frame_lighting_resources[frame_index_];
    if (frame_resources.light_records.buffer == VK_NULL_HANDLE ||
        frame_resources.cluster_headers.buffer == VK_NULL_HANDLE ||
        frame_resources.cluster_indices.buffer == VK_NULL_HANDLE ||
        frame_resources.shadow_views.buffer == VK_NULL_HANDLE ||
        frame_resources.lighting_uniform.buffer == VK_NULL_HANDLE) {
        return;
    }
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        frame_resources.descriptor_set = descriptor_host->AllocateSet(*context,
                                                                      frame_index_,
                                                                      lighting_descriptor_layout_id);
        frame_resources.descriptor_buffer_signature = 0U;
        frame_resources.descriptor_image_signature = 0U;
        frame_resources.descriptor_set_signature = 0U;
    }
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    auto hash_combine = [](std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= 1099511628211ULL;
    };
    const std::uint64_t buffer_signature = frame_resources.descriptor_payload_signature;
    if (frame_resources.descriptor_buffer_signature == buffer_signature) {
        ++stats.light_descriptor_set_reuse_hit_count;
        return;
    }

    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    descriptor_buffer_write_scratch.reserve(5U);
    descriptor_buffer_write_scratch.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.light_records.buffer,
        .offset = frame_resources.light_records.offset,
        .range = frame_resources.light_records.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.cluster_headers.buffer,
        .offset = frame_resources.cluster_headers.offset,
        .range = frame_resources.cluster_headers.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 2U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.cluster_indices.buffer,
        .offset = frame_resources.cluster_indices.offset,
        .range = frame_resources.cluster_indices.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 3U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.shadow_views.buffer,
        .offset = frame_resources.shadow_views.offset,
        .range = frame_resources.shadow_views.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 4U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffer = frame_resources.lighting_uniform.buffer,
        .offset = frame_resources.lighting_uniform.offset,
        .range = frame_resources.lighting_uniform.size_bytes
    });

    descriptor_host->UpdateSet(*context,
                               frame_resources.descriptor_set,
                               descriptor_buffer_write_scratch,
                               {},
                               descriptor_texel_write_scratch);
    frame_resources.descriptor_buffer_signature = buffer_signature;
    std::uint64_t descriptor_signature = 14695981039346656037ULL;
    hash_combine(descriptor_signature, buffer_signature);
    frame_resources.descriptor_set_signature = descriptor_signature;
    ++stats.descriptor_set_update_count;
}

} // namespace vr::surface

