#include "vr/render/scene_recorder_3d.hpp"

#include "vr/shadow/shadow_renderer_3d.hpp"

namespace vr::render {

void SceneRecorder3D::BindLightFrameCoordinator(
    render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator_) noexcept {
    light_frame_coordinator = light_frame_coordinator_;
    light_shadow_link_coordinator.Reset();
    RefreshFramePacketBinding();
    RefreshSceneLightingBindings();
}

void SceneRecorder3D::BindAnimationFrameCoordinator(
    render::AnimationFrameCoordinator<ecs::Dim3>* animation_frame_coordinator_) noexcept {
    animation_frame_coordinator = animation_frame_coordinator_;
    RefreshAnimationBindings();
}

void SceneRecorder3D::BindShadowRuntime(render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator_,
                                        shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept {
    shadow_frame_coordinator = shadow_frame_coordinator_;
    shadow_atlas_host = shadow_atlas_host_;
    light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator.Reset();
    RefreshSceneLightingBindings();
}

void SceneRecorder3D::ClearShadowRuntimeBinding() noexcept {
    BindShadowRuntime(nullptr, nullptr);
}

void SceneRecorder3D::ClearAnimationFrameBinding() noexcept {
    BindAnimationFrameCoordinator(nullptr);
}

void SceneRecorder3D::SetFramePacket(const RenderScenePacket3D* frame_packet_) noexcept {
    if (frame_packet == frame_packet_) {
        sky_environment_pass_ready = false;
        RefreshFramePacketBinding();
        return;
    }
    frame_packet = frame_packet_;
    sky_environment_pass_ready = false;
    ++stats.frame_packet_bind_count;
    RefreshFramePacketBinding();
}

void SceneRecorder3D::ClearFramePacket() noexcept {
    SetFramePacket(nullptr);
}

void SceneRecorder3D::RegisterShadowRenderer(shadow::ShadowRenderer3D& shadow_renderer_,
                                             std::uint32_t submission_layer_mask_) {
    EnsureInitialized("RegisterShadowRenderer");
    const PreSceneRendererEntry entry{
        .renderer = &shadow_renderer_,
        .kind = PreSceneRendererKind::shadow,
        .reserved0 = 0U,
        .reserved1 = 0U,
        .submission_layer_mask = submission_layer_mask_,
        .prepare_fn = &PrepareRenderer<shadow::ShadowRenderer3D>,
        .record_fn = &RecordRenderer<shadow::ShadowRenderer3D>,
        .swapchain_recreated_fn = &OptionalOnSwapchainRecreatedRenderer<shadow::ShadowRenderer3D>,
        .configure_animation_fn = &ConfigurePreSceneAnimationBinding<shadow::ShadowRenderer3D>,
    };
    UpsertPreSceneRendererEntry(entry);
    BindShadowRuntime(&shadow_renderer_.FrameCoordinatorMutable(),
                      &shadow_renderer_.AtlasHostMutable());
}

void SceneRecorder3D::ClearPreSceneRenderers() noexcept {
    pre_scene_renderer_entries.clear();
    RefreshRendererCounts();
}

void SceneRecorder3D::ClearSceneRenderers() noexcept {
    scene_renderer_entries.clear();
    RefreshRendererCounts();
}

void SceneRecorder3D::ClearOverlayRenderers() noexcept {
    overlay_renderer_entries.clear();
    RefreshRendererCounts();
}

void SceneRecorder3D::ClearRendererRegistrations() noexcept {
    ClearPreSceneRenderers();
    ClearSceneRenderers();
    ClearOverlayRenderers();
}

void SceneRecorder3D::UpsertPreSceneRendererEntry(const PreSceneRendererEntry& entry_) {
    for (PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (entry.renderer == entry_.renderer) {
            entry = entry_;
            RefreshAnimationBindings();
            RefreshRendererCounts();
            return;
        }
    }
    pre_scene_renderer_entries.push_back(entry_);
    RefreshAnimationBindings();
    RefreshRendererCounts();
}

void SceneRecorder3D::UpsertSceneRendererEntry(const SceneRendererEntry& entry_) {
    for (SceneRendererEntry& entry : scene_renderer_entries) {
        if (entry.renderer == entry_.renderer && entry.stage == entry_.stage) {
            entry = entry_;
            RefreshSceneLightingBindings();
            RefreshAnimationBindings();
            RefreshRendererCounts();
            return;
        }
    }
    scene_renderer_entries.push_back(entry_);
    RefreshSceneLightingBindings();
    RefreshAnimationBindings();
    RefreshRendererCounts();
}

void SceneRecorder3D::UpsertOverlayRendererEntry(const OverlayRendererEntry& entry_) {
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

void SceneRecorder3D::RefreshFramePacketBinding() noexcept {
    if (frame_packet == nullptr) {
        resolved_environment_gpu = {};
    }
    const ResolvedSceneViewSelection<ecs::Dim3> selection =
        (frame_packet != nullptr) ? ResolveSceneViewSelection(*frame_packet)
                                  : ResolvedSceneViewSelection<ecs::Dim3>{};
    const RenderView3D* resolved_view = selection.active_view;

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

void SceneRecorder3D::RefreshSceneLightingBindings() noexcept {
    auto configure_lighting = [&](const SceneRendererEntry& entry_) {
        if (entry_.renderer == nullptr ||
            entry_.configure_lighting_fn == nullptr ||
            !IsFirstSceneRendererEntryForRenderer(entry_)) {
            return;
        }
        entry_.configure_lighting_fn(entry_.renderer,
                                     light_frame_coordinator,
                                     &light_shadow_link_coordinator,
                                     &shadow_atlas_binding_coordinator,
                                     shadow_frame_coordinator,
                                     shadow_atlas_host);
    };
    ForEachSceneRendererInStageOrder(configure_lighting);
}

void SceneRecorder3D::RefreshAnimationBindings() noexcept {
    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (entry.renderer == nullptr || entry.configure_animation_fn == nullptr) {
            continue;
        }
        entry.configure_animation_fn(entry.renderer, animation_frame_coordinator);
    }

    auto configure_animation = [&](const SceneRendererEntry& entry_) {
        if (entry_.renderer == nullptr ||
            entry_.configure_animation_fn == nullptr ||
            !IsFirstSceneRendererEntryForRenderer(entry_)) {
            return;
        }
        entry_.configure_animation_fn(entry_.renderer, animation_frame_coordinator);
    };
    ForEachSceneRendererInStageOrder(configure_animation);
    ++stats.animation_binding_refresh_count;
}

void SceneRecorder3D::RefreshRendererCounts() noexcept {
    stats.pre_scene_renderer_count = static_cast<std::uint32_t>(pre_scene_renderer_entries.size());
    stats.scene_renderer_count = static_cast<std::uint32_t>(scene_renderer_entries.size());
    stats.opaque_scene_renderer_count = 0U;
    stats.transparent_scene_renderer_count = 0U;
    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        switch (entry.stage) {
        case SceneRecorder3DSceneStage::opaque:
            ++stats.opaque_scene_renderer_count;
            break;
        case SceneRecorder3DSceneStage::transparent:
            ++stats.transparent_scene_renderer_count;
            break;
        default:
            break;
        }
    }
    stats.overlay_renderer_count = static_cast<std::uint32_t>(overlay_renderer_entries.size());
}

} // namespace vr::render
