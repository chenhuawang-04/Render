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

} // namespace vr::surface

