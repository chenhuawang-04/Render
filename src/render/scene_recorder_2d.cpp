#include "vr/render/scene_recorder_2d.hpp"

#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vr::render {

namespace {

[[nodiscard]] render_graph::TextureDesc BuildSceneConsumerOverlayIntermediateDesc(
    const render_graph::FrameSnapshot2D& snapshot_) noexcept {
    return render_graph::TextureDesc{
        .dimension = render_graph::TextureDimension::image_2d,
        .format = render_graph::TextureFormat::r8g8b8a8_unorm,
        .extent = snapshot_.reference_extent,
        .usage = render_graph::texture_usage_color_attachment_flag |
                 render_graph::texture_usage_transfer_src_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = render_graph::SampleCount::x1,
        .allow_alias = true,
    };
}

[[nodiscard]] constexpr bool IsTerminalScenePass(const SceneRenderPassRole pass_role_) noexcept {
    return pass_role_ == SceneRenderPassRole::single ||
           pass_role_ == SceneRenderPassRole::last;
}

[[nodiscard]] constexpr RenderTargetStateKind ResolveExplicitColorTerminalState(
    const bool final_pass_) noexcept {
    return final_pass_
        ? RenderTargetStateKind::shader_read
        : RenderTargetStateKind::color_attachment;
}

[[nodiscard]] constexpr RenderTargetStateKind ResolveExplicitDepthTerminalState(
    const bool final_pass_) noexcept {
    return final_pass_
        ? RenderTargetStateKind::depth_read_only
        : RenderTargetStateKind::depth_attachment;
}

} // namespace

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

void SceneRecorder2D::PrepareFrame(const SceneRecorder2DPrepareView& prepare_view_) {
    EnsureInitialized("PrepareFrame");
    RefreshFramePacketBinding();
    if (frame_packet != nullptr) {
        ++stats.frame_packet_prepare_count;
    }

    const bool use_scene_consumer_chain =
        HasSceneConsumer() && IsPostProcessEnabledForSubmission();
    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();
    SceneRecorder2DPrepareView resolved_prepare_view = prepare_view_;
    resolved_prepare_view.render_graph_upload_active = true;
    resolved_prepare_view.render_graph_compute_active = true;
    background_pass.ResetOutputTargetConfig();

    if (HasBackgroundPassForSubmission()) {
        background_pass.PrepareFrame(MakeBackgroundPass2DPrepareView(resolved_prepare_view));
        ++stats.background_prepare_count;
    }

    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.kind == PreSceneRendererKind::shadow && !shadow_enabled) {
            continue;
        }
        if (entry.prepare_fn != nullptr && entry.renderer != nullptr) {
            entry.prepare_fn(entry.renderer, resolved_prepare_view);
        }
    }

    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.prepare_fn != nullptr && entry.renderer != nullptr) {
            entry.prepare_fn(entry.renderer, resolved_prepare_view);
        }
    }

    if (use_scene_consumer_chain &&
        scene_consumer_entry.renderer != nullptr &&
        IsLayerVisibleForSubmission(scene_consumer_entry.submission_layer_mask) &&
        scene_consumer_entry.prepare_fn != nullptr) {
        scene_consumer_entry.prepare_fn(scene_consumer_entry.renderer, resolved_prepare_view);
    }

    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!overlay_enabled || !IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.prepare_fn != nullptr && entry.renderer != nullptr) {
            entry.prepare_fn(entry.renderer, resolved_prepare_view);
        }
    }

    stats.prepare_count += 1U;
}

void SceneRecorder2D::PrepareFrame(const SceneRecorder2DPrepareView& prepare_view_,
                                   const RenderScenePacket2D& frame_packet_) {
    SetFramePacket(&frame_packet_);
    PrepareFrame(prepare_view_);
}

void SceneRecorder2D::BuildRenderGraph(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::FrameSnapshot2D& snapshot_,
    const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_,
    render_graph::ResourceVersionHandle& color_chain_) {
    if (!initialized) {
        return;
    }

    const bool shadow_enabled = IsShadowEnabledForSubmission();
    std::vector<const PreSceneRendererEntry*> visible_pre_scene_entries{};
    visible_pre_scene_entries.reserve(pre_scene_renderer_entries.size());
    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.kind == PreSceneRendererKind::shadow && !shadow_enabled) {
            continue;
        }
        if (entry.renderer == nullptr || entry.record_fn == nullptr) {
            continue;
        }
        visible_pre_scene_entries.push_back(&entry);
    }

    const bool scene_consumer_enabled =
        scene_consumer_entry.renderer != nullptr &&
        IsLayerVisibleForSubmission(scene_consumer_entry.submission_layer_mask) &&
        IsPostProcessEnabledForSubmission();
    if (scene_consumer_enabled &&
        (scene_consumer_entry.graph_record_fn == nullptr ||
         scene_consumer_entry.build_graph_color_attachment_fn == nullptr)) {
        throw std::runtime_error(
            "SceneRecorder2D::BuildRenderGraph requires graph-capable scene consumer registration");
    }

    std::vector<const SceneRendererEntry*> visible_scene_entries{};
    visible_scene_entries.reserve(scene_renderer_entries.size());
    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask) ||
            entry.renderer == nullptr) {
            continue;
        }
        if (entry.graph_record_fn == nullptr) {
            throw std::runtime_error("SceneRecorder2D::BuildRenderGraph requires graph-capable scene renderer registration");
        }
        visible_scene_entries.push_back(&entry);
    }

    const bool has_background_pass = HasBackgroundPassForSubmission() &&
                                     scene_view != nullptr &&
                                     frame_packet != nullptr;
    if ((has_background_pass || !visible_scene_entries.empty()) &&
        !IsValidPassHandle(build_result_.scene_pass)) {
        throw std::runtime_error(
            "SceneRecorder2D::BuildRenderGraph requires a valid scene pass for visible scene content");
    }

    if (IsValidPassHandle(build_result_.scene_pass)) {
        builder_.SetRasterPassDesc(build_result_.scene_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::clear,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                               .clear_value = {
                                                   .red = create_info_cache.scene_target.clear_color.float32[0],
                                                   .green = create_info_cache.scene_target.clear_color.float32[1],
                                                   .blue = create_info_cache.scene_target.clear_color.float32[2],
                                                   .alpha = create_info_cache.scene_target.clear_color.float32[3],
                                               },
                                           },
                                       },
                                   });

        for (const auto* entry : visible_scene_entries) {
            if (entry->describe_graph_bindings_fn != nullptr) {
                entry->describe_graph_bindings_fn(entry->renderer,
                                                  builder_,
                                                  build_result_.scene_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry->register_graph_imported_resources_fn != nullptr) {
                entry->register_graph_imported_resources_fn(entry->renderer,
                                                            *graph_runtime_service);
            }
        }

        if (has_background_pass || !visible_scene_entries.empty()) {
            const RenderView2D captured_view = has_background_pass ? *scene_view : RenderView2D{};
            const scene::Background2DRenderState captured_background =
                has_background_pass ? frame_packet->extra.background
                                    : scene::Background2DRenderState{};
            const auto scene_color = build_result_.scene_color;
            builder_.SetExecuteCallback(
                build_result_.scene_pass,
                [this,
                 has_background_pass,
                 captured_view,
                 captured_background,
                 visible_scene_entries,
                 scene_color](render_graph::GraphCommandContext& context_) {
                    if (has_background_pass) {
                        background_pass.RecordGraphPass(context_,
                                                        captured_view,
                                                        captured_background,
                                                        scene_color);
                    }
                    for (const auto* entry : visible_scene_entries) {
                        entry->graph_record_fn(entry->renderer, context_, scene_color);
                    }
                });
        }
    }

    for (const auto* entry : visible_pre_scene_entries) {
        const auto pre_scene_pass = builder_.AddPass(
            entry->kind == PreSceneRendererKind::shadow ? "shadow_prepass" : "pre_scene_pass",
            true);
        if (IsValidPassHandle(build_result_.scene_pass)) {
            builder_.AddDependency(build_result_.scene_pass, pre_scene_pass);
        }
        builder_.SetExecuteCallback(
            pre_scene_pass,
            [entry](render_graph::GraphCommandContext& context_) {
                FrameRecordContext record_context{};
                record_context.frame_index = context_.FrameIndex();
                record_context.command_buffer = context_.CommandBuffer();
                record_context.render_target_host = &context_.RenderTargets();
                entry->record_fn(entry->renderer, record_context);
            });
    }

    const bool graph_overlay_present = IsValidPassHandle(build_result_.overlay_pass) &&
                                       IsValidResourceHandle(build_result_.scene_color);
    if (scene_consumer_enabled) {
        if (!IsValidResourceHandle(build_result_.scene_color) ||
            !IsValidResourceHandle(build_result_.present_target) ||
            !IsValidResourceVersionHandle(color_chain_)) {
            throw std::runtime_error(
                "SceneRecorder2D::BuildRenderGraph requires a valid scene color chain and present target for scene consumers");
        }

        const auto scene_consumer_output = graph_overlay_present
            ? builder_.CreateTexture("scene_consumer_color",
                                     BuildSceneConsumerOverlayIntermediateDesc(snapshot_),
                                     render_graph::ResourceLifetime::transient)
            : build_result_.present_target;
        const auto scene_consumer_pass = builder_.AddPass("scene_consumer_pass");
        (void)builder_.Read(scene_consumer_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        color_chain_ = builder_.Write(scene_consumer_pass,
                                      scene_consumer_output,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(scene_consumer_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           scene_consumer_entry.build_graph_color_attachment_fn(
                                               scene_consumer_entry.renderer,
                                               scene_consumer_output,
                                               false),
                                       },
                                   });
        if (scene_consumer_entry.describe_graph_bindings_fn != nullptr) {
            scene_consumer_entry.describe_graph_bindings_fn(scene_consumer_entry.renderer,
                                                            builder_,
                                                            scene_consumer_pass);
        }
        if (graph_runtime_service != nullptr &&
            scene_consumer_entry.register_graph_imported_resources_fn != nullptr) {
            scene_consumer_entry.register_graph_imported_resources_fn(
                scene_consumer_entry.renderer,
                *graph_runtime_service);
        }
        const auto scene_source = build_result_.scene_color;
        builder_.SetExecuteCallback(scene_consumer_pass,
                                    [this, scene_source, scene_consumer_output](render_graph::GraphCommandContext& context_) {
                                        scene_consumer_entry.graph_record_fn(scene_consumer_entry.renderer,
                                                                             context_,
                                                                             scene_source,
                                                                             scene_consumer_output);
                                    });

        if (graph_overlay_present) {
            const auto scene_consumer_copyback_pass = builder_.AddPass("scene_consumer_copyback");
            (void)builder_.Read(scene_consumer_copyback_pass,
                                color_chain_,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::transfer_read});
            color_chain_ = builder_.Write(scene_consumer_copyback_pass,
                                          build_result_.scene_color,
                                          render_graph::AccessDesc{.access = render_graph::AccessKind::transfer_write});
            builder_.SetExecuteCallback(
                scene_consumer_copyback_pass,
                [scene_consumer_output, scene_color = build_result_.scene_color](render_graph::GraphCommandContext& context_) {
                    render_graph::detail::RecordMinimalPresentCopyPass(context_,
                                                                       scene_consumer_output,
                                                                       scene_color);
                });
        }
    }

    std::vector<const OverlayRendererEntry*> visible_overlay_entries{};
    visible_overlay_entries.reserve(overlay_renderer_entries.size());
    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask) ||
            entry.renderer == nullptr) {
            continue;
        }
        if (entry.graph_record_fn == nullptr) {
            throw std::runtime_error("SceneRecorder2D::BuildRenderGraph requires graph-capable overlay renderer registration");
        }
        visible_overlay_entries.push_back(&entry);
    }

    if (!visible_overlay_entries.empty() &&
        !IsValidPassHandle(build_result_.overlay_pass)) {
        throw std::runtime_error(
            "SceneRecorder2D::BuildRenderGraph requires a dedicated overlay pass for visible overlay renderers");
    }

    if (!visible_overlay_entries.empty() &&
        IsValidPassHandle(build_result_.overlay_pass)) {
        for (const auto* entry : visible_overlay_entries) {
            if (entry->describe_graph_bindings_fn != nullptr) {
                entry->describe_graph_bindings_fn(entry->renderer,
                                                  builder_,
                                                  build_result_.overlay_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry->register_graph_imported_resources_fn != nullptr) {
                entry->register_graph_imported_resources_fn(entry->renderer,
                                                            *graph_runtime_service);
            }
        }
        const auto overlay_target = build_result_.scene_color;
        builder_.SetExecuteCallback(
            build_result_.overlay_pass,
            [visible_overlay_entries, overlay_target](render_graph::GraphCommandContext& context_) {
                for (const auto* entry : visible_overlay_entries) {
                    entry->graph_record_fn(entry->renderer, context_, overlay_target);
                }
            });
    }
}

void SceneRecorder2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                           VkExtent2D extent_,
                                           VkFormat format_,
                                           std::uint64_t last_submitted_value_,
                                           std::uint64_t completed_submit_value_) {
    EnsureInitialized("OnSwapchainRecreated");

    const bool use_scene_consumer_chain =
        HasSceneConsumer() && IsPostProcessEnabledForSubmission();
    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();

    background_pass.OnSwapchainRecreated(image_count_,
                                         extent_,
                                         format_,
                                         last_submitted_value_,
                                         completed_submit_value_);

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

    for (const SceneRendererEntry& entry : scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
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

    if (use_scene_consumer_chain &&
        scene_consumer_entry.swapchain_recreated_fn != nullptr &&
        scene_consumer_entry.renderer != nullptr &&
        IsLayerVisibleForSubmission(scene_consumer_entry.submission_layer_mask)) {
        scene_consumer_entry.swapchain_recreated_fn(scene_consumer_entry.renderer,
                                                    image_count_,
                                                    extent_,
                                                    format_,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
    }

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

    background_pass.ResetOutputTargetConfig();
    stats.swapchain_recreate_count += 1U;
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

