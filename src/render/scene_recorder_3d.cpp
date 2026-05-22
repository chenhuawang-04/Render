#include "vr/render/scene_recorder_3d.hpp"

#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <stdexcept>
#include <string>

namespace vr::render {

void SceneRecorder3D::Initialize(const SceneRecorder3DCreateInfo& create_info_) noexcept {
    create_info_cache = create_info_;
    sky_environment_pass.Initialize();
    bloom_renderer.Initialize(create_info_cache.bloom);
    pre_scene_renderer_entries.clear();
    scene_renderer_entries.clear();
    overlay_renderer_entries.clear();
    pre_scene_renderer_entries.reserve(create_info_cache.reserve_pre_scene_renderer_count);
    scene_renderer_entries.reserve(create_info_cache.reserve_scene_renderer_count);
    overlay_renderer_entries.reserve(create_info_cache.reserve_overlay_renderer_count);
    stats = {};
    context = nullptr;
    render_target_host = nullptr;
    graph_runtime_service = nullptr;
    light_frame_coordinator = nullptr;
    animation_frame_coordinator = nullptr;
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;
    frame_packet = nullptr;
    active_view = nullptr;
    scene_view = nullptr;
    overlay_view = nullptr;
    sky_environment_pass_ready = false;
    resolved_environment_gpu = {};
    active_view_signature = 0U;
    light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator.Reset();
    initialized = true;
}

void SceneRecorder3D::Shutdown(VulkanContext& context_) noexcept {
    if (!initialized) {
        return;
    }

    sky_environment_pass.Shutdown(context_);
    bloom_renderer.Shutdown(context_);
    pre_scene_renderer_entries.clear();
    scene_renderer_entries.clear();
    overlay_renderer_entries.clear();
    stats = {};
    context = nullptr;
    render_target_host = nullptr;
    graph_runtime_service = nullptr;
    light_frame_coordinator = nullptr;
    animation_frame_coordinator = nullptr;
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;
    frame_packet = nullptr;
    active_view = nullptr;
    scene_view = nullptr;
    overlay_view = nullptr;
    sky_environment_pass_ready = false;
    resolved_environment_gpu = {};
    active_view_signature = 0U;
    light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator.Reset();
    initialized = false;
}

void SceneRecorder3D::BindRuntimeResources(VulkanContext& context_,
                                           RenderTargetHost& render_target_host_) noexcept {
    context = &context_;
    render_target_host = &render_target_host_;
    graph_runtime_service = nullptr;
}

void SceneRecorder3D::ClearRuntimeBinding() noexcept {
    context = nullptr;
    render_target_host = nullptr;
    graph_runtime_service = nullptr;
}

bool SceneRecorder3D::IsInitialized() const noexcept {
    return initialized;
}

bool SceneRecorder3D::HasRuntimeBinding() const noexcept {
    return context != nullptr && render_target_host != nullptr;
}

const SceneRecorder3DCreateInfo& SceneRecorder3D::CreateInfo() const noexcept {
    return create_info_cache;
}

const SceneRecorder3DStats& SceneRecorder3D::Stats() const noexcept {
    return stats;
}

const RenderScenePacket3D* SceneRecorder3D::FramePacket() const noexcept {
    return frame_packet;
}

const RenderView3D* SceneRecorder3D::ActiveView() const noexcept {
    return active_view;
}

SkyEnvironmentPass& SceneRecorder3D::EnvironmentPass() noexcept {
    return sky_environment_pass;
}

const SkyEnvironmentPass& SceneRecorder3D::EnvironmentPass() const noexcept {
    return sky_environment_pass;
}

const RenderTargetBloomRendererStats& SceneRecorder3D::BloomStats() const noexcept {
    return bloom_renderer.Stats();
}

bool SceneRecorder3D::ShouldUseBloomChainForSubmission() const noexcept {
    if (!HasRuntimeBinding() || !HasSceneViewForSubmission()) {
        return false;
    }
    if (frame_packet == nullptr) {
        return true;
    }
    return ResolveScenePostProcessEnabled(*frame_packet);
}

std::uint32_t SceneRecorder3D::EffectiveLayerMask() const noexcept {
    if (frame_packet == nullptr) {
        return all_submission_layers;
    }
    if (scene_view == nullptr) {
        return 0U;
    }
    return ResolveSceneLayerMaskForView(*frame_packet, scene_view);
}

std::uint32_t SceneRecorder3D::OverlayLayerMask() const noexcept {
    if (frame_packet == nullptr) {
        return all_submission_layers;
    }
    if (overlay_view == nullptr) {
        return 0U;
    }
    return ResolveSceneLayerMaskForView(*frame_packet, overlay_view);
}

bool SceneRecorder3D::HasSceneViewForSubmission() const noexcept {
    return frame_packet == nullptr || scene_view != nullptr;
}

bool SceneRecorder3D::IsShadowEnabledForSubmission() const noexcept {
    if (frame_packet == nullptr) {
        return true;
    }
    return ResolveSceneShadowEnabledForView(*frame_packet, scene_view);
}

bool SceneRecorder3D::IsOverlayEnabledForSubmission() const noexcept {
    if (frame_packet == nullptr) {
        return true;
    }
    return ResolveSceneOverlayEnabledForView(*frame_packet, overlay_view);
}

bool SceneRecorder3D::IsPostProcessEnabledForSubmission() const noexcept {
    if (frame_packet == nullptr) {
        return true;
    }
    return ResolveScenePostProcessEnabledForView(*frame_packet, scene_view);
}

bool SceneRecorder3D::SupportsGraphExecution(const VulkanContext& device_) const noexcept {
    return graph_runtime_service != nullptr &&
           graph_runtime_service->SupportsGraphExecution(device_);
}

bool SceneRecorder3D::UsesGraphManagedBloomChain() const noexcept {
    return context != nullptr &&
           ShouldUseBloomChainForSubmission() &&
           SupportsGraphExecution(*context);
}

bool SceneRecorder3D::HasSkyEnvironmentPassForSubmission() const noexcept {
    if (frame_packet == nullptr || scene_view == nullptr) {
        return false;
    }
    switch (frame_packet->extra.environment.mode) {
    case scene::SkyEnvironmentMode::solid_color:
    case scene::SkyEnvironmentMode::gradient:
    case scene::SkyEnvironmentMode::cubemap:
    case scene::SkyEnvironmentMode::equirectangular_hdr:
    case scene::SkyEnvironmentMode::procedural_atmosphere:
        return true;
    case scene::SkyEnvironmentMode::none:
    default:
        break;
    }
    return false;
}

bool SceneRecorder3D::HasVisibleSceneRendererForSubmission() const noexcept {
    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (entry.renderer != nullptr &&
            IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            return true;
        }
    }
    return false;
}

bool SceneRecorder3D::HasVisibleOpaqueSceneRendererForSubmission() const noexcept {
    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (entry.stage == SceneRecorder3DSceneStage::opaque &&
            entry.renderer != nullptr &&
            IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            return true;
        }
    }
    return false;
}

bool SceneRecorder3D::HasDepthTargetForSkyAfterOpaqueSubmission() const noexcept {
    return ShouldUseBloomChainForSubmission() &&
           create_info_cache.scene_target.enable_depth;
}

scene::SkyEnvironmentDrawOrder SceneRecorder3D::SkyEnvironmentDrawOrderForSubmission() const noexcept {
    if (!HasSkyEnvironmentPassForSubmission() || frame_packet == nullptr) {
        return scene::SkyEnvironmentDrawOrder::before_opaque;
    }
    return frame_packet->extra.environment.draw_order;
}

bool SceneRecorder3D::ShouldRecordSkyEnvironmentBeforeOpaque() const noexcept {
    return HasSkyEnvironmentPassForSubmission() &&
           !ShouldRecordSkyEnvironmentAfterOpaque();
}

bool SceneRecorder3D::ShouldRecordSkyEnvironmentAfterOpaque() const noexcept {
    return HasSkyEnvironmentPassForSubmission() &&
           SkyEnvironmentDrawOrderForSubmission() ==
               scene::SkyEnvironmentDrawOrder::after_opaque_depth_tested &&
           HasVisibleOpaqueSceneRendererForSubmission() &&
           HasDepthTargetForSkyAfterOpaqueSubmission();
}

bool SceneRecorder3D::IsLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept {
    return HasSceneViewForSubmission() &&
           (EffectiveLayerMask() & submission_layer_mask_) != 0U;
}

bool SceneRecorder3D::IsOverlayLayerVisibleForSubmission(std::uint32_t submission_layer_mask_) const noexcept {
    return frame_packet == nullptr ||
           (overlay_view != nullptr &&
            (OverlayLayerMask() & submission_layer_mask_) != 0U);
}

bool SceneRecorder3D::IsFirstSceneRendererEntryForRenderer(
    const SceneRendererEntry& entry_) const noexcept {
    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (&entry == &entry_) {
            return true;
        }
        if (entry.renderer == entry_.renderer) {
            return false;
        }
    }
    return false;
}

void SceneRecorder3D::EnsureInitialized(const char* operation_) const {
    if (initialized) {
        return;
    }
    throw std::runtime_error(std::string("SceneRecorder3D::") + operation_ +
                             " called before Initialize");
}

void SceneRecorder3D::EnsureRuntimeBinding(const char* operation_) const {
    if (HasRuntimeBinding()) {
        return;
    }
    throw std::runtime_error(std::string("SceneRecorder3D::") + operation_ +
                             " requires bound runtime resources");
}

} // namespace vr::render
