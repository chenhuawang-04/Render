#include "vr/render/scene_recorder_2d.hpp"

#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <stdexcept>
#include <string>

namespace vr::render {

void SceneRecorder2D::Initialize(const SceneRecorder2DCreateInfo& create_info_) noexcept {
    create_info_cache = create_info_;
    background_pass.Initialize();
    pre_scene_renderer_entries.clear();
    scene_renderer_entries.clear();
    scene_consumer_entry = {};
    overlay_renderer_entries.clear();
    pre_scene_renderer_entries.reserve(create_info_cache.reserve_pre_scene_renderer_count);
    scene_renderer_entries.reserve(create_info_cache.reserve_scene_renderer_count);
    overlay_renderer_entries.reserve(create_info_cache.reserve_overlay_renderer_count);
    stats = {};
    context = nullptr;
    render_target_host = nullptr;
    graph_runtime_service = nullptr;
    light_frame_coordinator = nullptr;
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;
    frame_packet = nullptr;
    active_view = nullptr;
    scene_view = nullptr;
    overlay_view = nullptr;
    active_view_signature = 0U;
    light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator.Reset();
    initialized = true;
}

void SceneRecorder2D::Shutdown(VulkanContext& context_) noexcept {
    if (!initialized) {
        return;
    }

    background_pass.Shutdown(context_);
    pre_scene_renderer_entries.clear();
    scene_renderer_entries.clear();
    scene_consumer_entry = {};
    overlay_renderer_entries.clear();
    stats = {};
    context = nullptr;
    render_target_host = nullptr;
    graph_runtime_service = nullptr;
    light_frame_coordinator = nullptr;
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;
    frame_packet = nullptr;
    active_view = nullptr;
    scene_view = nullptr;
    overlay_view = nullptr;
    active_view_signature = 0U;
    light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator.Reset();
    initialized = false;
}

void SceneRecorder2D::BindRuntimeResources(VulkanContext& context_,
                                           RenderTargetHost& render_target_host_) noexcept {
    context = &context_;
    render_target_host = &render_target_host_;
    graph_runtime_service = nullptr;
}

void SceneRecorder2D::ClearRuntimeBinding() noexcept {
    context = nullptr;
    render_target_host = nullptr;
    graph_runtime_service = nullptr;
}

bool SceneRecorder2D::IsInitialized() const noexcept {
    return initialized;
}

bool SceneRecorder2D::HasRuntimeBinding() const noexcept {
    return context != nullptr && render_target_host != nullptr;
}

bool SceneRecorder2D::HasSceneConsumer() const noexcept {
    return scene_consumer_entry.renderer != nullptr;
}

const SceneRecorder2DCreateInfo& SceneRecorder2D::CreateInfo() const noexcept {
    return create_info_cache;
}

const SceneRecorder2DStats& SceneRecorder2D::Stats() const noexcept {
    return stats;
}

const RenderScenePacket2D* SceneRecorder2D::FramePacket() const noexcept {
    return frame_packet;
}

const RenderView2D* SceneRecorder2D::ActiveView() const noexcept {
    return active_view;
}

std::uint32_t SceneRecorder2D::EffectiveLayerMask() const noexcept {
    if (frame_packet == nullptr) {
        return all_submission_layers;
    }
    if (scene_view == nullptr) {
        return 0U;
    }
    return ResolveSceneLayerMaskForView(*frame_packet, scene_view);
}

std::uint32_t SceneRecorder2D::OverlayLayerMask() const noexcept {
    if (frame_packet == nullptr) {
        return all_submission_layers;
    }
    if (overlay_view == nullptr) {
        return 0U;
    }
    return ResolveSceneLayerMaskForView(*frame_packet, overlay_view);
}

bool SceneRecorder2D::HasSceneViewForSubmission() const noexcept {
    return frame_packet == nullptr || scene_view != nullptr;
}

bool SceneRecorder2D::IsShadowEnabledForSubmission() const noexcept {
    if (frame_packet == nullptr) {
        return true;
    }
    return ResolveSceneShadowEnabledForView(*frame_packet, scene_view);
}

bool SceneRecorder2D::IsOverlayEnabledForSubmission() const noexcept {
    if (frame_packet == nullptr) {
        return true;
    }
    return ResolveSceneOverlayEnabledForView(*frame_packet, overlay_view);
}

bool SceneRecorder2D::IsPostProcessEnabledForSubmission() const noexcept {
    if (frame_packet == nullptr) {
        return true;
    }
    return ResolveScenePostProcessEnabledForView(*frame_packet, scene_view);
}

bool SceneRecorder2D::HasBackgroundPassForSubmission() const noexcept {
    if (frame_packet == nullptr) {
        return false;
    }
    switch (frame_packet->extra.background.mode) {
    case scene::Background2DMode::solid_color:
    case scene::Background2DMode::gradient:
        return scene_view != nullptr;
    case scene::Background2DMode::none:
    case scene::Background2DMode::sprite:
    case scene::Background2DMode::surface_entity:
    default:
        break;
    }
    return false;
}

bool SceneRecorder2D::IsLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept {
    return HasSceneViewForSubmission() &&
           (EffectiveLayerMask() & submission_layer_mask_) != 0U;
}

bool SceneRecorder2D::IsOverlayLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept {
    return frame_packet == nullptr ||
           (overlay_view != nullptr &&
            (OverlayLayerMask() & submission_layer_mask_) != 0U);
}

bool SceneRecorder2D::SupportsGraphExecution(const VulkanContext& device_) const noexcept {
    return graph_runtime_service != nullptr &&
           graph_runtime_service->SupportsGraphExecution(device_);
}

bool SceneRecorder2D::UsesGraphManagedSceneOutput() const noexcept {
    return context != nullptr && SupportsGraphExecution(*context);
}

void SceneRecorder2D::EnsureInitialized(const char* operation_) const {
    if (initialized) {
        return;
    }
    throw std::runtime_error(std::string("SceneRecorder2D::") + operation_ +
                             " called before Initialize");
}

void SceneRecorder2D::EnsureRuntimeBinding(const char* operation_) const {
    if (HasRuntimeBinding()) {
        return;
    }
    throw std::runtime_error(std::string("SceneRecorder2D::") + operation_ +
                             " requires bound runtime resources");
}


} // namespace vr::render
