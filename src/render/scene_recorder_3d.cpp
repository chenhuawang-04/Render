#include "vr/render/scene_recorder_3d.hpp"

#include <stdexcept>
#include <string>

namespace vr::render {

void SceneRecorder3D::Initialize(const SceneRecorder3DCreateInfo& create_info_) noexcept {
    create_info_cache = create_info_;
    post_stack.Initialize(create_info_cache.scene_target, create_info_cache.bloom);
    pre_scene_renderer_entries.clear();
    scene_renderer_entries.clear();
    overlay_renderer_entries.clear();
    pre_scene_renderer_entries.reserve(create_info_cache.reserve_pre_scene_renderer_count);
    scene_renderer_entries.reserve(create_info_cache.reserve_scene_renderer_count);
    overlay_renderer_entries.reserve(create_info_cache.reserve_overlay_renderer_count);
    stats = {};
    context = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
    light_frame_coordinator = nullptr;
    animation_frame_coordinator = nullptr;
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

void SceneRecorder3D::Shutdown(VulkanContext& context_) noexcept {
    if (!initialized) {
        return;
    }

    post_stack.Shutdown(context_);
    pre_scene_renderer_entries.clear();
    scene_renderer_entries.clear();
    overlay_renderer_entries.clear();
    stats = {};
    context = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
    light_frame_coordinator = nullptr;
    animation_frame_coordinator = nullptr;
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

void SceneRecorder3D::BindRuntimeResources(VulkanContext& context_,
                                           RenderTargetHost& render_target_host_,
                                           RenderTargetPool* render_target_pool_) noexcept {
    context = &context_;
    render_target_host = &render_target_host_;
    render_target_pool = render_target_pool_;
}

void SceneRecorder3D::ClearRuntimeBinding() noexcept {
    context = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
}

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
        RefreshFramePacketBinding();
        return;
    }
    frame_packet = frame_packet_;
    ++stats.frame_packet_bind_count;
    RefreshFramePacketBinding();
}

void SceneRecorder3D::ClearFramePacket() noexcept {
    SetFramePacket(nullptr);
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

void SceneRecorder3D::PrepareFrame(const RuntimePrepareContext& prepare_context_) {
    EnsureInitialized("PrepareFrame");
    RefreshFramePacketBinding();
    if (frame_packet != nullptr) {
        ++stats.frame_packet_prepare_count;
    }

    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();
    const bool use_explicit_scene_target = HasExplicitSceneTargetForSubmission();
    const bool use_post_stack = ShouldUsePostStackForSubmission();

    SceneRenderTargetSet* targets = nullptr;
    if (use_post_stack) {
        targets = &post_stack.Targets();
        (void)targets->PrepareFrame(prepare_context_);
    }

    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.kind == PreSceneRendererKind::shadow && !shadow_enabled) {
            continue;
        }
        if (entry.configure_animation_fn != nullptr && entry.renderer != nullptr) {
            entry.configure_animation_fn(entry.renderer, animation_frame_coordinator);
        }
        if (entry.prepare_fn != nullptr && entry.renderer != nullptr) {
            entry.prepare_fn(entry.renderer, prepare_context_);
        }
    }

    auto configure_scene_renderer = [&](const SceneRendererEntry& entry_) {
        if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            return;
        }
        if (entry_.renderer == nullptr) {
            return;
        }
        if (use_explicit_scene_target) {
            if (entry_.configure_direct_scene_fn != nullptr) {
                const RenderTargetColorOutputConfig color_output =
                    BuildExplicitSceneOutputConfig(entry_.pass_role);
                const RenderTargetDepthOutputConfig explicit_depth_output =
                    BuildExplicitDepthOutputConfig(entry_.pass_role);
                const RenderTargetDepthOutputConfig* depth_output_config =
                    (scene_view != nullptr && IsValidRenderTargetHandle(scene_view->targets.depth_target))
                        ? &explicit_depth_output
                        : nullptr;
                entry_.configure_direct_scene_fn(entry_.renderer,
                                                 entry_.pass_role,
                                                 color_output,
                                                 depth_output_config,
                                                 depth_output_config != nullptr);
            }
        } else if (use_post_stack) {
            if (entry_.configure_scene_fn != nullptr && targets != nullptr) {
                (void)entry_.configure_scene_fn(entry_.renderer, *targets, entry_.pass_role);
            }
        } else if (entry_.configure_direct_scene_fn != nullptr) {
            const RenderTargetColorOutputConfig color_output =
                BuildDirectSceneOutputConfig(entry_.pass_role);
            const RenderTargetDepthOutputConfig direct_depth_output =
                BuildDirectDepthOutputConfig(entry_.pass_role);
            const RenderTargetDepthOutputConfig* depth_output_config =
                create_info_cache.scene_target.enable_depth ? &direct_depth_output : nullptr;
            entry_.configure_direct_scene_fn(entry_.renderer,
                                             entry_.pass_role,
                                             color_output,
                                             depth_output_config,
                                             create_info_cache.scene_target.enable_depth);
        }
        if (entry_.configure_lighting_fn != nullptr &&
            IsFirstSceneRendererEntryForRenderer(entry_)) {
            entry_.configure_lighting_fn(entry_.renderer,
                                         light_frame_coordinator,
                                         &light_shadow_link_coordinator,
                                         &shadow_atlas_binding_coordinator,
                                         shadow_frame_coordinator,
                                         shadow_atlas_host);
        }
        if (entry_.configure_animation_fn != nullptr &&
            IsFirstSceneRendererEntryForRenderer(entry_)) {
            entry_.configure_animation_fn(entry_.renderer, animation_frame_coordinator);
        }
    };
    ForEachSceneRendererInStageOrder(configure_scene_renderer);
    if (targets != nullptr) {
        (void)targets->ConfigureSceneConsumer(post_stack.Bloom());
        post_stack.Bloom().PrepareFrame(prepare_context_);
    }

    auto prepare_scene_renderer = [&](const SceneRendererEntry& entry_) {
        if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            return;
        }
        if (entry_.prepare_fn == nullptr ||
            entry_.renderer == nullptr ||
            !IsFirstSceneRendererEntryForRenderer(entry_)) {
            return;
        }
        entry_.prepare_fn(entry_.renderer, prepare_context_);
    };
    ForEachSceneRendererInStageOrder(prepare_scene_renderer);
    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!overlay_enabled || !IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.renderer != nullptr && entry.set_output_target_fn != nullptr) {
            entry.set_output_target_fn(entry.renderer, BuildOverlayOutputConfig(entry.output_target_config));
        }
        if (entry.prepare_fn != nullptr && entry.renderer != nullptr) {
            entry.prepare_fn(entry.renderer, prepare_context_);
        }
    }

    stats.prepare_count += 1U;
}

void SceneRecorder3D::PrepareFrame(const RuntimePrepareContext& prepare_context_,
                                   const RenderScenePacket3D& frame_packet_) {
    SetFramePacket(&frame_packet_);
    PrepareFrame(prepare_context_);
}

void SceneRecorder3D::Record(const FrameRecordContext& record_context_) {
    EnsureInitialized("Record");
    RefreshFramePacketBinding();
    if (frame_packet != nullptr) {
        ++stats.frame_packet_record_count;
    }

    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();
    const bool use_explicit_scene_target = HasExplicitSceneTargetForSubmission();
    const bool use_post_stack = ShouldUsePostStackForSubmission();

    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.kind == PreSceneRendererKind::shadow && !shadow_enabled) {
            continue;
        }
        if (entry.record_fn != nullptr && entry.renderer != nullptr) {
            entry.record_fn(entry.renderer, record_context_);
        }
    }

    auto record_scene_renderer = [&](const SceneRendererEntry& entry_) {
        if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            return;
        }
        if (entry_.record_fn == nullptr || entry_.renderer == nullptr) {
            return;
        }
        if (use_explicit_scene_target && entry_.configure_direct_scene_fn != nullptr) {
            const RenderTargetColorOutputConfig color_output =
                BuildExplicitSceneOutputConfig(entry_.pass_role);
            const RenderTargetDepthOutputConfig explicit_depth_output =
                BuildExplicitDepthOutputConfig(entry_.pass_role);
            const RenderTargetDepthOutputConfig* depth_output_config =
                (scene_view != nullptr && IsValidRenderTargetHandle(scene_view->targets.depth_target))
                    ? &explicit_depth_output
                    : nullptr;
            entry_.configure_direct_scene_fn(entry_.renderer,
                                             entry_.pass_role,
                                             color_output,
                                             depth_output_config,
                                             depth_output_config != nullptr);
        } else if (use_post_stack && entry_.configure_scene_fn != nullptr) {
            (void)entry_.configure_scene_fn(entry_.renderer, post_stack.Targets(), entry_.pass_role);
        }
        entry_.record_fn(entry_.renderer, record_context_, entry_.stage);
    };
    ForEachSceneRendererInStageOrder(record_scene_renderer);

    if (use_post_stack) {
        post_stack.Record(record_context_);
    }

    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!overlay_enabled || !IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.record_fn != nullptr && entry.renderer != nullptr) {
            entry.record_fn(entry.renderer, record_context_);
        }
    }

    stats.record_count += 1U;
}

void SceneRecorder3D::Record(const FrameRecordContext& record_context_,
                             const RenderScenePacket3D& frame_packet_) {
    SetFramePacket(&frame_packet_);
    Record(record_context_);
}

void SceneRecorder3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                           VkExtent2D extent_,
                                           VkFormat format_,
                                           std::uint64_t last_submitted_value_,
                                           std::uint64_t completed_submit_value_) {
    EnsureInitialized("OnSwapchainRecreated");
    EnsureRuntimeBinding("OnSwapchainRecreated");

    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();
    const bool use_explicit_scene_target = HasExplicitSceneTargetForSubmission();
    const bool use_post_stack = ShouldUsePostStackForSubmission();

    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.kind == PreSceneRendererKind::shadow && !shadow_enabled) {
            continue;
        }
        if (entry.swapchain_recreated_fn != nullptr && entry.renderer != nullptr) {
            entry.swapchain_recreated_fn(entry.renderer,
                                         image_count_,
                                         extent_,
                                         format_,
                                         last_submitted_value_,
                                         completed_submit_value_);
        }
    }

    auto recreate_scene_renderer = [&](const SceneRendererEntry& entry_) {
        if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            return;
        }
        if (entry_.swapchain_recreated_fn == nullptr ||
            entry_.renderer == nullptr ||
            !IsFirstSceneRendererEntryForRenderer(entry_)) {
            return;
        }
        entry_.swapchain_recreated_fn(entry_.renderer,
                                      image_count_,
                                      extent_,
                                      format_,
                                      last_submitted_value_,
                                      completed_submit_value_);
    };
    ForEachSceneRendererInStageOrder(recreate_scene_renderer);
    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!overlay_enabled || !IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.swapchain_recreated_fn != nullptr && entry.renderer != nullptr) {
            entry.swapchain_recreated_fn(entry.renderer,
                                         image_count_,
                                         extent_,
                                         format_,
                                         last_submitted_value_,
                                         completed_submit_value_);
        }
    }

    if (use_post_stack) {
        post_stack.Bloom().OnSwapchainRecreated(image_count_, extent_, format_);
        (void)post_stack.Targets().OnSwapchainRecreated(*context,
                                                        *render_target_host,
                                                        render_target_pool,
                                                        extent_,
                                                        last_submitted_value_,
                                                        completed_submit_value_);
    }

    auto reconfigure_scene_renderer = [&](const SceneRendererEntry& entry_) {
        if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            return;
        }
        if (entry_.renderer == nullptr) {
            return;
        }
        if (use_explicit_scene_target) {
            if (entry_.configure_direct_scene_fn != nullptr) {
                const RenderTargetColorOutputConfig color_output =
                    BuildExplicitSceneOutputConfig(entry_.pass_role);
                const RenderTargetDepthOutputConfig explicit_depth_output =
                    BuildExplicitDepthOutputConfig(entry_.pass_role);
                const RenderTargetDepthOutputConfig* depth_output_config =
                    (scene_view != nullptr && IsValidRenderTargetHandle(scene_view->targets.depth_target))
                        ? &explicit_depth_output
                        : nullptr;
                entry_.configure_direct_scene_fn(entry_.renderer,
                                                 entry_.pass_role,
                                                 color_output,
                                                 depth_output_config,
                                                 depth_output_config != nullptr);
            }
        } else if (use_post_stack) {
            if (entry_.configure_scene_fn != nullptr) {
                (void)entry_.configure_scene_fn(entry_.renderer, post_stack.Targets(), entry_.pass_role);
            }
        } else if (entry_.configure_direct_scene_fn != nullptr) {
            const RenderTargetColorOutputConfig color_output =
                BuildDirectSceneOutputConfig(entry_.pass_role);
            const RenderTargetDepthOutputConfig depth_output =
                BuildDirectDepthOutputConfig(entry_.pass_role);
            entry_.configure_direct_scene_fn(entry_.renderer,
                                             entry_.pass_role,
                                             color_output,
                                             create_info_cache.scene_target.enable_depth ? &depth_output : nullptr,
                                             create_info_cache.scene_target.enable_depth);
        }
        if (entry_.configure_lighting_fn != nullptr &&
            IsFirstSceneRendererEntryForRenderer(entry_)) {
            entry_.configure_lighting_fn(entry_.renderer,
                                         light_frame_coordinator,
                                         &light_shadow_link_coordinator,
                                         &shadow_atlas_binding_coordinator,
                                         shadow_frame_coordinator,
                                         shadow_atlas_host);
        }
        if (entry_.configure_animation_fn != nullptr &&
            IsFirstSceneRendererEntryForRenderer(entry_)) {
            entry_.configure_animation_fn(entry_.renderer, animation_frame_coordinator);
        }
    };
    ForEachSceneRendererInStageOrder(reconfigure_scene_renderer);
    if (use_post_stack) {
        (void)post_stack.Targets().ConfigureSceneConsumer(post_stack.Bloom());
    }
    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!overlay_enabled || !IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.renderer != nullptr && entry.set_output_target_fn != nullptr) {
            entry.set_output_target_fn(entry.renderer, BuildOverlayOutputConfig(entry.output_target_config));
        }
    }

    stats.swapchain_recreate_count += 1U;
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

SceneBloomPostStack& SceneRecorder3D::PostStack() noexcept {
    return post_stack;
}

const SceneBloomPostStack& SceneRecorder3D::PostStack() const noexcept {
    return post_stack;
}

RenderTargetColorOutputConfig SceneRecorder3D::MakePresentOverlayOutputConfig() noexcept {
    RenderTargetColorOutputConfig output{};
    output.final_state = RenderTargetStateKind::present_src;
    output.use_explicit_load_op = true;
    output.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    return output;
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

bool SceneRecorder3D::HasExplicitSceneTargetForSubmission() const noexcept {
    return scene_view != nullptr &&
           IsValidRenderTargetHandle(scene_view->targets.color_target);
}

bool SceneRecorder3D::HasExplicitOverlayTargetForSubmission() const noexcept {
    return overlay_view != nullptr &&
           IsValidRenderTargetHandle(overlay_view->targets.color_target);
}

RenderTargetColorOutputConfig SceneRecorder3D::BuildDirectSceneOutputConfig(
    SceneRenderPassRole pass_role_) const noexcept {
    RenderTargetColorOutputConfig output{};
    output.final_state = (pass_role_ == SceneRenderPassRole::single ||
                          pass_role_ == SceneRenderPassRole::last)
        ? RenderTargetStateKind::present_src
        : RenderTargetStateKind::color_attachment;
    output.use_explicit_load_op = true;
    output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    output.clear_color = create_info_cache.scene_target.clear_color;

    switch (pass_role_) {
    case SceneRenderPassRole::single:
    case SceneRenderPassRole::first:
        output.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        break;
    case SceneRenderPassRole::middle:
    case SceneRenderPassRole::last:
    default:
        output.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
        break;
    }
    return output;
}

RenderTargetColorOutputConfig SceneRecorder3D::BuildExplicitSceneOutputConfig(
    SceneRenderPassRole pass_role_) const noexcept {
    RenderTargetColorOutputConfig output = BuildDirectSceneOutputConfig(pass_role_);
    if (HasExplicitSceneTargetForSubmission()) {
        output.color_target = scene_view->targets.color_target;
        output.final_state = scene_view->targets.color_final_state;
    }
    return output;
}

RenderTargetDepthOutputConfig SceneRecorder3D::BuildDirectDepthOutputConfig(
    SceneRenderPassRole pass_role_) const noexcept {
    RenderTargetDepthOutputConfig output{};
    output.final_state = RenderTargetStateKind::depth_attachment;
    output.use_explicit_load_op = true;
    output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    output.clear_depth_stencil.depth = create_info_cache.scene_target.clear_depth_value;
    output.clear_depth_stencil.stencil = create_info_cache.scene_target.clear_stencil_value;

    switch (pass_role_) {
    case SceneRenderPassRole::single:
    case SceneRenderPassRole::first:
        output.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        break;
    case SceneRenderPassRole::middle:
    case SceneRenderPassRole::last:
    default:
        output.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
        break;
    }
    return output;
}

RenderTargetDepthOutputConfig SceneRecorder3D::BuildExplicitDepthOutputConfig(
    SceneRenderPassRole pass_role_) const noexcept {
    RenderTargetDepthOutputConfig output = BuildDirectDepthOutputConfig(pass_role_);
    if (scene_view != nullptr && IsValidRenderTargetHandle(scene_view->targets.depth_target)) {
        output.depth_target = scene_view->targets.depth_target;
        output.final_state = scene_view->targets.depth_final_state;
    }
    return output;
}

RenderTargetColorOutputConfig SceneRecorder3D::BuildOverlayOutputConfig(
    const RenderTargetColorOutputConfig& fallback_output_target_config_) const noexcept {
    if (!HasExplicitOverlayTargetForSubmission()) {
        return fallback_output_target_config_;
    }

    RenderTargetColorOutputConfig output = fallback_output_target_config_;
    output.color_target = overlay_view->targets.color_target;
    output.final_state = overlay_view->targets.color_final_state;
    return output;
}

bool SceneRecorder3D::ShouldUsePostStackForSubmission() const noexcept {
    if (!HasRuntimeBinding()) {
        return false;
    }
    if (HasExplicitSceneTargetForSubmission()) {
        return false;
    }
    if (!HasSceneViewForSubmission()) {
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
