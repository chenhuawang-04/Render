#include "vr/render/scene_recorder_2d.hpp"

namespace vr::render {

void SceneRecorder2D::RegisterShadowRenderer(shadow::ShadowRenderer2D& shadow_renderer_,
                                             std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterShadowRenderer");
    const PreSceneRendererEntry entry{
        .renderer = &shadow_renderer_,
        .kind = PreSceneRendererKind::shadow,
        .reserved0 = 0U,
        .reserved1 = 0U,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<shadow::ShadowRenderer2D>,
        .record_fn = &RecordRenderer<shadow::ShadowRenderer2D>,
        .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<shadow::ShadowRenderer2D>,
    };
    UpsertPreSceneRendererEntry(entry);
    BindShadowRuntime(&shadow_renderer_.FrameCoordinatorMutable(),
                      &shadow_renderer_.AtlasHostMutable());
}

void SceneRecorder2D::BindLightFrameCoordinator(
    render::LightFrameCoordinator<ecs::Dim2>* light_frame_coordinator_) noexcept {
    light_frame_coordinator = light_frame_coordinator_;
    light_shadow_link_coordinator.Reset();
    RefreshFramePacketBinding();
    RefreshSceneLightingBindings();
}

void SceneRecorder2D::BindShadowRuntime(render::ShadowFrameCoordinator<ecs::Dim2>* shadow_frame_coordinator_,
                                        shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept {
    shadow_frame_coordinator = shadow_frame_coordinator_;
    shadow_atlas_host = shadow_atlas_host_;
    light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator.Reset();
    RefreshSceneLightingBindings();
}

void SceneRecorder2D::ClearShadowRuntimeBinding() noexcept {
    BindShadowRuntime(nullptr, nullptr);
}

void SceneRecorder2D::SetFramePacket(const RenderScenePacket2D* frame_packet_) noexcept {
    if (frame_packet == frame_packet_) {
        RefreshFramePacketBinding();
        return;
    }
    frame_packet = frame_packet_;
    ++stats.frame_packet_bind_count;
    RefreshFramePacketBinding();
}

void SceneRecorder2D::ClearFramePacket() noexcept {
    SetFramePacket(nullptr);
}

void SceneRecorder2D::ClearPreSceneRenderers() noexcept {
    pre_scene_renderer_entries.clear();
    RefreshRendererCounts();
}

void SceneRecorder2D::ClearSceneRenderers() noexcept {
    scene_renderer_entries.clear();
    RefreshRendererCounts();
}

void SceneRecorder2D::ClearSceneConsumer() noexcept {
    scene_consumer_entry = {};
    RefreshRendererCounts();
}

void SceneRecorder2D::ClearOverlayRenderers() noexcept {
    overlay_renderer_entries.clear();
    RefreshRendererCounts();
}

void SceneRecorder2D::ClearRendererRegistrations() noexcept {
    ClearPreSceneRenderers();
    ClearSceneRenderers();
    ClearSceneConsumer();
    ClearOverlayRenderers();
}

void SceneRecorder2D::UpsertPreSceneRendererEntry(const PreSceneRendererEntry& entry_) {
    for (PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (entry.renderer == entry_.renderer) {
            entry = entry_;
            RefreshRendererCounts();
            return;
        }
    }
    pre_scene_renderer_entries.push_back(entry_);
    RefreshRendererCounts();
}

void SceneRecorder2D::UpsertSceneRendererEntry(const SceneRendererEntry& entry_) {
    for (SceneRendererEntry& entry : scene_renderer_entries) {
        if (entry.renderer == entry_.renderer) {
            entry = entry_;
            RefreshSceneLightingBindings();
            RefreshRendererCounts();
            return;
        }
    }
    scene_renderer_entries.push_back(entry_);
    RefreshSceneLightingBindings();
    RefreshRendererCounts();
}

void SceneRecorder2D::SetSceneConsumerEntry(const SceneConsumerEntry& entry_) noexcept {
    scene_consumer_entry = entry_;
    RefreshRendererCounts();
}

void SceneRecorder2D::UpsertOverlayRendererEntry(const OverlayRendererEntry& entry_) {
    for (OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (entry.renderer == entry_.renderer) {
            entry = entry_;
            RefreshRendererCounts();
            return;
        }
    }
    overlay_renderer_entries.push_back(entry_);
    RefreshRendererCounts();
}

void SceneRecorder2D::RefreshFramePacketBinding() noexcept {
    const ResolvedSceneViewSelection<ecs::Dim2> selection =
        (frame_packet != nullptr) ? ResolveSceneViewSelection(*frame_packet)
                                  : ResolvedSceneViewSelection<ecs::Dim2>{};
    const RenderView2D* resolved_view = selection.active_view;

    const std::uint64_t resolved_signature =
        (resolved_view != nullptr) ? resolved_view->signature : 0U;
    if (active_view != resolved_view || active_view_signature != resolved_signature) {
        active_view = resolved_view;
        active_view_signature = resolved_signature;
    }
    scene_view = selection.scene_view;
    overlay_view = selection.overlay_view;
    stats.frame_view_count = (frame_packet != nullptr) ? frame_packet->view_count : 0U;
    stats.active_view_index = selection.active_view_index;
    stats.scene_view_index = selection.scene_view_index;
    stats.overlay_view_index = selection.overlay_view_index;
    stats.effective_layer_mask = (frame_packet != nullptr)
        ? ((scene_view != nullptr) ? ResolveSceneLayerMaskForView(*frame_packet, scene_view) : 0U)
        : all_submission_layers;
    stats.overlay_layer_mask = (frame_packet != nullptr)
        ? ((overlay_view != nullptr) ? ResolveSceneLayerMaskForView(*frame_packet, overlay_view) : 0U)
        : 0U;
    stats.debug_flags = (frame_packet != nullptr)
        ? ResolveSceneDebugFlags(*frame_packet)
        : render_view_debug_none_flag;
    stats.active_view_kind =
        static_cast<std::uint8_t>((active_view != nullptr) ? active_view->kind : RenderViewKind::custom);
    stats.has_active_view = (active_view != nullptr) ? 1U : 0U;
    stats.shadow_enabled = IsShadowEnabledForSubmission() ? 1U : 0U;
    stats.overlay_enabled = IsOverlayEnabledForSubmission() ? 1U : 0U;
    stats.postprocess_enabled = IsPostProcessEnabledForSubmission() ? 1U : 0U;
    if (light_frame_coordinator != nullptr) {
        light_frame_coordinator->SetCamera(scene_view != nullptr ? scene_view->camera : nullptr);
    }
}

void SceneRecorder2D::RefreshSceneLightingBindings() noexcept {
    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (entry.renderer == nullptr || entry.configure_lighting_fn == nullptr) {
            continue;
        }
        entry.configure_lighting_fn(entry.renderer,
                                    light_frame_coordinator,
                                    &light_shadow_link_coordinator,
                                    &shadow_atlas_binding_coordinator,
                                    shadow_frame_coordinator,
                                    shadow_atlas_host);
    }
}

void SceneRecorder2D::RefreshRendererCounts() noexcept {
    stats.pre_scene_renderer_count = static_cast<std::uint32_t>(pre_scene_renderer_entries.size());
    stats.scene_renderer_count = static_cast<std::uint32_t>(scene_renderer_entries.size());
    stats.scene_consumer_count = (scene_consumer_entry.renderer != nullptr) ? 1U : 0U;
    stats.overlay_renderer_count = static_cast<std::uint32_t>(overlay_renderer_entries.size());
}


} // namespace vr::render
