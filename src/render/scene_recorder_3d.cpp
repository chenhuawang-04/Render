#include "vr/render/scene_recorder_3d.hpp"
#include "vr/geometry/geometry_renderer_3d.hpp"
#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/ibl_bake_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vr::render {

namespace {

constexpr float k_min_bloom_downsample_scale = 0.125F;
constexpr float k_max_bloom_downsample_scale = 1.0F;

[[nodiscard]] float ClampBloomDownsampleScale(const float value_) noexcept {
    if (!std::isfinite(value_)) {
        return k_min_bloom_downsample_scale;
    }
    return std::clamp(value_, k_min_bloom_downsample_scale, k_max_bloom_downsample_scale);
}

[[nodiscard]] render_graph::TextureFormat ResolveBloomIntermediateGraphFormat(
    const render::RenderTargetBloomRendererCreateInfo& bloom_create_info_) noexcept {
    switch (bloom_create_info_.intermediate_format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return render_graph::TextureFormat::r8g8b8a8_unorm;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return render_graph::TextureFormat::r16g16b16a16_sfloat;
    default:
        break;
    }
    return render_graph::TextureFormat::r16g16b16a16_sfloat;
}

[[nodiscard]] std::string BuildIndexedBloomResourceName(std::string_view prefix_,
                                                        const std::uint32_t index_) {
    return std::string(prefix_) + "_" + std::to_string(index_);
}

[[nodiscard]] render_graph::TextureDesc BuildBloomIntermediateDesc(
    const render_graph::FrameSnapshot3D& snapshot_,
    const render::RenderTargetBloomRendererCreateInfo& bloom_create_info_) noexcept {
    const float scale = ClampBloomDownsampleScale(bloom_create_info_.downsample_scale);
    const std::uint32_t width = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(snapshot_.reference_extent.width) * scale));
    const std::uint32_t height = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(snapshot_.reference_extent.height) * scale));
    return render_graph::TextureDesc{
        .dimension = render_graph::TextureDimension::image_2d,
        .format = ResolveBloomIntermediateGraphFormat(bloom_create_info_),
        .extent = {.width = width, .height = height, .depth = 1U},
        .usage = render_graph::texture_usage_color_attachment_flag |
                 render_graph::texture_usage_sampled_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = render_graph::SampleCount::x1,
        .allow_alias = true,
    };
}

[[nodiscard]] render_graph::TextureDesc BuildPostprocessOverlayIntermediateDesc(
    const render_graph::FrameSnapshot3D& snapshot_) noexcept {
    return render_graph::TextureDesc{
        .dimension = render_graph::TextureDimension::image_2d,
        .format = render_graph::TextureFormat::r16g16b16a16_sfloat,
        .extent = snapshot_.reference_extent,
        .usage = render_graph::texture_usage_color_attachment_flag |
                 render_graph::texture_usage_sampled_flag |
                 render_graph::texture_usage_transfer_src_flag,
        .mip_level_count = 1U,
        .array_layer_count = 1U,
        .sample_count = render_graph::SampleCount::x1,
        .allow_alias = true,
    };
}

[[nodiscard]] bool CanPrepareSkyEnvironmentPass(const SceneRecorder3DPrepareView& prepare_view_,
                                                const scene::SkyEnvironmentRenderState& state_) noexcept {
    if (prepare_view_.pipeline == nullptr) {
        return false;
    }

    switch (state_.mode) {
    case scene::SkyEnvironmentMode::solid_color:
    case scene::SkyEnvironmentMode::gradient:
    case scene::SkyEnvironmentMode::procedural_atmosphere:
        return true;
    case scene::SkyEnvironmentMode::cubemap:
        return prepare_view_.gpu_memory != nullptr &&
               prepare_view_.texture != nullptr &&
               prepare_view_.bindless != nullptr &&
               prepare_view_.upload != nullptr &&
               prepare_view_.descriptor != nullptr &&
               prepare_view_.ibl != nullptr;
    case scene::SkyEnvironmentMode::equirectangular_hdr:
        return prepare_view_.texture != nullptr &&
               prepare_view_.bindless != nullptr;
    case scene::SkyEnvironmentMode::none:
    default:
        break;
    }
    return false;
}

} // namespace

void SceneRecorder3D::Initialize(const SceneRecorder3DCreateInfo& create_info_) noexcept {
    create_info_cache = create_info_;
    sky_environment_pass.Initialize();
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
    post_stack.Shutdown(context_);
    pre_scene_renderer_entries.clear();
    scene_renderer_entries.clear();
    overlay_renderer_entries.clear();
    stats = {};
    context = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
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
                                           RenderTargetHost& render_target_host_,
                                           RenderTargetPool* render_target_pool_) noexcept {
    context = &context_;
    render_target_host = &render_target_host_;
    render_target_pool = render_target_pool_;
    graph_runtime_service = nullptr;
}

void SceneRecorder3D::ClearRuntimeBinding() noexcept {
    context = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
    graph_runtime_service = nullptr;
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

void SceneRecorder3D::PrepareFrame(const SceneRecorder3DPrepareView& prepare_view_) {
    EnsureInitialized("PrepareFrame");
    RefreshFramePacketBinding();
    if (frame_packet != nullptr) {
        ++stats.frame_packet_prepare_count;
    }

    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();
    const bool use_explicit_scene_target = HasExplicitSceneTargetForSubmission();
    const bool use_post_stack = ShouldUsePostStackForSubmission();
    const bool graph_execution_supported =
        (context != nullptr) ? SupportsGraphExecution(*context)
                             : SupportsGraphExecution(prepare_view_.device);
    const bool graph_managed_post_stack = UsesGraphManagedPostStack();
    const bool has_sky_environment_pass = HasSkyEnvironmentPassForSubmission();
    const bool record_sky_before_opaque = ShouldRecordSkyEnvironmentBeforeOpaque();
    const bool record_sky_after_opaque = ShouldRecordSkyEnvironmentAfterOpaque();
    resolved_environment_gpu = frame_packet != nullptr ? frame_packet->extra.environment_gpu
                                                       : scene::SkyEnvironmentGpuHandle{};
    sky_environment_pass_ready = false;
    std::uint32_t ibl_environment_id =
        frame_packet != nullptr ? frame_packet->extra.ibl_environment_id : 0U;
    std::uint32_t ibl_brdf_lut_texture_id = 0U;

    SceneRenderTargetSet* targets = nullptr;
    if (use_post_stack) {
        targets = &post_stack.Targets();
        if (!graph_managed_post_stack) {
            (void)targets->PrepareFrame(SceneRenderTargetSetPrepareView{
                .device = prepare_view_.device,
                .render_target = prepare_view_.render_target,
                .render_target_pool = prepare_view_.render_target_pool,
                .frame = prepare_view_.frame,
                .progress = prepare_view_.progress,
            });
        } else {
            targets->InvalidateFrameTargets();
            post_stack.Bloom().ClearSceneSourceTarget();
            post_stack.Bloom().ResetOutputTargetConfig();
            sky_environment_pass.ResetOutputTargetConfig();
            sky_environment_pass.ResetDepthTargetConfig();
        }
    }

    if (prepare_view_.sky_environment != nullptr &&
        frame_packet != nullptr &&
        frame_packet->extra.environment.mode != scene::SkyEnvironmentMode::none) {
        const auto sky_prepare_view = MakeSkyEnvironmentGpuPrepareView(prepare_view_);
        prepare_view_.sky_environment->PrepareFrame(sky_prepare_view);
        if (!resolved_environment_gpu.IsValid()) {
            resolved_environment_gpu =
                prepare_view_.sky_environment->RegisterOrUpdate(frame_packet->extra.environment,
                                                                sky_prepare_view);
            ++stats.environment_gpu_resolve_count;
        }

        const auto& environment_ibl =
            prepare_view_.sky_environment->IblData(resolved_environment_gpu);
        if (prepare_view_.ibl_bake != nullptr &&
            environment_ibl.uses_shared_brdf_lut != 0U &&
            !environment_ibl.brdf_lut_texture.IsValid()) {
            const asset::TextureId shared_brdf_lut =
                prepare_view_.ibl_bake->EnsureBrdfLut(MakeIblBakeHostPrepareView(prepare_view_));
            if (shared_brdf_lut.IsValid()) {
                (void)prepare_view_.sky_environment->ResolveSharedBrdfLut(resolved_environment_gpu,
                                                                         shared_brdf_lut);
            }
        }
        if (prepare_view_.ibl_bake != nullptr &&
            prepare_view_.sky_environment->HasPendingBake(resolved_environment_gpu)) {
            (void)prepare_view_.sky_environment->TryBakePendingEnvironment(
                resolved_environment_gpu,
                *prepare_view_.ibl_bake,
                MakeIblBakeHostPrepareView(prepare_view_));
        }
        ibl_brdf_lut_texture_id =
            prepare_view_.sky_environment->IblData(resolved_environment_gpu).brdf_lut_texture.value;
        if (prepare_view_.ibl != nullptr && ibl_environment_id == 0U) {
            const IblEnvironmentId environment_id =
                prepare_view_.sky_environment->EnsureIblEnvironment(prepare_view_.device,
                                                                    *prepare_view_.ibl,
                                                                    resolved_environment_gpu);
            ibl_environment_id = environment_id.value;
        }
    }

    SceneRecorder3DPrepareView resolved_prepare_view = prepare_view_;
    resolved_prepare_view.prefer_render_graph_upload_path = graph_execution_supported;
    resolved_prepare_view.prefer_render_graph_compute_path = graph_execution_supported;
    resolved_prepare_view.ibl_environment_id = ibl_environment_id;
    resolved_prepare_view.ibl_brdf_lut_texture_id = ibl_brdf_lut_texture_id;

    if (!graph_managed_post_stack) {
        ConfigureSkyEnvironmentPassForTargets();
    } else {
        sky_environment_pass.ResetOutputTargetConfig();
        sky_environment_pass.ResetDepthTargetConfig();
    }
    if (HasSkyEnvironmentPassForSubmission() &&
        frame_packet != nullptr &&
        CanPrepareSkyEnvironmentPass(resolved_prepare_view, frame_packet->extra.environment)) {
        sky_environment_pass.PrepareFrame(MakeSkyEnvironmentPassPrepareView(resolved_prepare_view),
                                          frame_packet->extra.environment,
                                          resolved_environment_gpu);
        sky_environment_pass_ready = true;
        ++stats.environment_prepare_count;
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
            entry.prepare_fn(entry.renderer, resolved_prepare_view);
        }
    }

    auto configure_scene_renderer = [&](const SceneRendererEntry& entry_) {
        if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            return;
        }
        if (entry_.renderer == nullptr) {
            return;
        }
        if (graph_managed_post_stack) {
            // Graph-managed 3D mainline binds scene targets through RenderGraph.
            // Keep only lighting/animation configuration below.
        } else if (use_explicit_scene_target) {
            if (entry_.configure_direct_scene_fn != nullptr) {
                const RenderTargetColorOutputConfig color_output =
                    BuildExplicitSceneOutputConfig(entry_.pass_role);
                const RenderTargetDepthOutputConfig explicit_depth_output =
                    BuildExplicitDepthOutputConfig(entry_.pass_role);
                const bool final_pass = (entry_.pass_role == SceneRenderPassRole::single ||
                                         entry_.pass_role == SceneRenderPassRole::last) &&
                                        !(record_sky_after_opaque &&
                                          entry_.stage == SceneRecorder3DSceneStage::opaque);
                RenderTargetColorOutputConfig scene_color_output = color_output;
                scene_color_output.final_state = final_pass
                    ? color_output.final_state
                    : RenderTargetStateKind::color_attachment;
                if ((entry_.pass_role == SceneRenderPassRole::single ||
                     entry_.pass_role == SceneRenderPassRole::first) &&
                    !record_sky_before_opaque) {
                    scene_color_output.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
                }
                RenderTargetDepthOutputConfig scene_depth_output = explicit_depth_output;
                scene_depth_output.final_state = final_pass
                    ? explicit_depth_output.final_state
                    : RenderTargetStateKind::depth_attachment;
                const RenderTargetDepthOutputConfig* depth_output_config =
                    (scene_view != nullptr && IsValidRenderTargetHandle(scene_view->targets.depth_target))
                        ? &scene_depth_output
                        : nullptr;
                entry_.configure_direct_scene_fn(entry_.renderer,
                                                 entry_.pass_role,
                                                 scene_color_output,
                                                 depth_output_config,
                                                 depth_output_config != nullptr);
            }
        } else if (use_post_stack) {
            if (has_sky_environment_pass && entry_.configure_direct_scene_fn != nullptr && targets != nullptr) {
                const bool final_pass = (entry_.pass_role == SceneRenderPassRole::single ||
                                         entry_.pass_role == SceneRenderPassRole::last) &&
                                        !(record_sky_after_opaque &&
                                          entry_.stage == SceneRecorder3DSceneStage::opaque);
                const bool clear_depth = entry_.pass_role == SceneRenderPassRole::single ||
                                         entry_.pass_role == SceneRenderPassRole::first;
                const bool clear_color = (entry_.pass_role == SceneRenderPassRole::single ||
                                          entry_.pass_role == SceneRenderPassRole::first)
                    ? !record_sky_before_opaque
                    : false;
                const RenderTargetDepthOutputConfig depth_output =
                    targets->BuildDepthOutputConfig(clear_depth);
                const bool enable_external_depth =
                    record_sky_after_opaque && create_info_cache.scene_target.enable_depth;
                entry_.configure_direct_scene_fn(entry_.renderer,
                                                 entry_.pass_role,
                                                 targets->BuildColorOutputConfig(clear_color, final_pass),
                                                 enable_external_depth ? &depth_output : nullptr,
                                                 enable_external_depth);
            } else if (entry_.configure_scene_fn != nullptr && targets != nullptr) {
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
        if (!graph_managed_post_stack) {
            (void)targets->ConfigureSceneConsumer(post_stack.Bloom());
            post_stack.Bloom().PrepareFrame(
                MakeRenderTargetBloomRendererPrepareView(
                    MakeSceneBloomPostStackPrepareView(resolved_prepare_view)));
        } else {
            post_stack.Bloom().PrepareGraphFrame(resolved_prepare_view);
        }
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
        entry_.prepare_fn(entry_.renderer, resolved_prepare_view);
    };
    ForEachSceneRendererInStageOrder(prepare_scene_renderer);
    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!overlay_enabled || !IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (!graph_managed_post_stack &&
            entry.renderer != nullptr &&
            entry.set_output_target_fn != nullptr) {
            entry.set_output_target_fn(entry.renderer, BuildOverlayOutputConfig(entry.output_target_config));
        }
        if (entry.prepare_fn != nullptr && entry.renderer != nullptr) {
            entry.prepare_fn(entry.renderer, resolved_prepare_view);
        }
    }

    stats.prepare_count += 1U;
}

void SceneRecorder3D::PrepareFrame(const SceneRecorder3DPrepareView& prepare_view_,
                                   const RenderScenePacket3D& frame_packet_) {
    SetFramePacket(&frame_packet_);
    PrepareFrame(prepare_view_);
}

void SceneRecorder3D::BuildRenderGraph(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::FrameSnapshot3D& snapshot_,
    const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_,
    render_graph::ResourceVersionHandle& color_chain_) {
    if (!initialized) {
        return;
    }

    const bool shadow_enabled = IsShadowEnabledForSubmission();
    const bool graph_overlay_present = IsValidPassHandle(build_result_.overlay_pass);
    const bool graph_post_stack_enabled = !HasExplicitSceneTargetForSubmission() &&
                                          HasSceneViewForSubmission() &&
                                          IsPostProcessEnabledForSubmission();
    const bool record_sky_after_opaque = HasSkyEnvironmentPassForSubmission() &&
                                         SkyEnvironmentDrawOrderForSubmission() ==
                                             scene::SkyEnvironmentDrawOrder::after_opaque_depth_tested &&
                                         build_result_.has_depth &&
                                         IsValidResourceHandle(build_result_.scene_depth);
    const bool record_sky_before_opaque = HasSkyEnvironmentPassForSubmission() &&
                                          !record_sky_after_opaque;

    if (record_sky_before_opaque && scene_view != nullptr && frame_packet != nullptr) {
        const auto sky_pass = builder_.AddPass("sky_environment_pre_opaque");
        if (IsValidPassHandle(build_result_.scene_pass)) {
            builder_.AddDependency(build_result_.scene_pass, sky_pass);
        }
        builder_.SetRasterPassDesc(sky_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::clear,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                               .clear_value = {.red = 0.02F, .green = 0.02F, .blue = 0.03F, .alpha = 1.0F},
                                           },
                                       },
                                   });
        sky_environment_pass.DescribeGraphDescriptorBindings(builder_, sky_pass);
        const RenderView3D captured_view = *scene_view;
        const scene::SkyEnvironmentRenderState captured_state = frame_packet->extra.environment;
        const auto scene_color = build_result_.scene_color;
        builder_.SetExecuteCallback(sky_pass,
                                    [this, captured_view, captured_state, scene_color](render_graph::GraphCommandContext& context_) {
                                        const auto color_view = context_.ResolveTextureView(scene_color);
                                        sky_environment_pass.RecordGraphPass(context_,
                                                                             captured_view,
                                                                             captured_state,
                                                                             color_view.format,
                                                                             false,
                                                                             VK_FORMAT_UNDEFINED);
                                    });
        builder_.SetRasterPassDesc(build_result_.scene_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::load,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                       .has_depth_attachment = build_result_.has_depth,
                                       .depth_attachment = render_graph::RasterDepthAttachmentDesc{
                                           .target = build_result_.scene_depth,
                                           .load_op = render_graph::AttachmentLoadOp::clear,
                                           .store_op = render_graph::AttachmentStoreOp::store,
                                           .stencil_load_op = render_graph::AttachmentLoadOp::dont_care,
                                           .stencil_store_op = render_graph::AttachmentStoreOp::dont_care,
                                           .clear_value = {.depth = 1.0F, .stencil = 0U},
                                       },
                                   });
    }

    if (record_sky_after_opaque &&
        scene_view != nullptr &&
        frame_packet != nullptr &&
        IsValidResourceHandle(build_result_.scene_color) &&
        IsValidResourceHandle(build_result_.scene_depth) &&
        IsValidResourceVersionHandle(color_chain_)) {
        const auto sky_pass = builder_.AddPass("sky_environment_post_opaque");
        (void)builder_.Read(sky_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_read});
        (void)builder_.Read(sky_pass,
                            build_result_.scene_depth,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::depth_stencil_read});
        color_chain_ = builder_.Write(sky_pass,
                                      build_result_.scene_color,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(sky_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::load,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                       .has_depth_attachment = true,
                                       .depth_attachment = render_graph::RasterDepthAttachmentDesc{
                                           .target = build_result_.scene_depth,
                                           .load_op = render_graph::AttachmentLoadOp::load,
                                           .store_op = render_graph::AttachmentStoreOp::store,
                                           .stencil_load_op = render_graph::AttachmentLoadOp::dont_care,
                                           .stencil_store_op = render_graph::AttachmentStoreOp::dont_care,
                                           .clear_value = {.depth = 1.0F, .stencil = 0U},
                                           .read_only = true,
                                       },
                                   });
        sky_environment_pass.DescribeGraphDescriptorBindings(builder_, sky_pass);
        const RenderView3D captured_view = *scene_view;
        const scene::SkyEnvironmentRenderState captured_state = frame_packet->extra.environment;
        const auto scene_color = build_result_.scene_color;
        const auto scene_depth = build_result_.scene_depth;
        builder_.SetExecuteCallback(sky_pass,
                                    [this, captured_view, captured_state, scene_color, scene_depth](render_graph::GraphCommandContext& context_) {
                                        const auto color_view = context_.ResolveTextureView(scene_color);
                                        const auto depth_view = context_.ResolveTextureView(scene_depth);
                                        sky_environment_pass.RecordGraphPass(context_,
                                                                             captured_view,
                                                                             captured_state,
                                                                             color_view.format,
                                                                             true,
                                                                             depth_view.format);
                                    });
    }

    std::vector<const SceneRendererEntry*> opaque_visible_entries{};
    for (const SceneRendererEntry& entry_ : scene_renderer_entries) {
        if (entry_.stage != SceneRecorder3DSceneStage::opaque ||
            !IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            continue;
        }
        if (entry_.renderer == nullptr) {
            continue;
        }
        opaque_visible_entries.push_back(&entry_);
    }
    if (!opaque_visible_entries.empty() &&
        IsValidPassHandle(build_result_.scene_pass)) {
        for (const auto* entry_ : opaque_visible_entries) {
            if (entry_->describe_graph_bindings_fn != nullptr) {
                entry_->describe_graph_bindings_fn(entry_->renderer,
                                                   builder_,
                                                   build_result_.scene_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry_->register_graph_imported_resources_fn != nullptr) {
                entry_->register_graph_imported_resources_fn(entry_->renderer,
                                                            *graph_runtime_service);
            }
        }
        const auto scene_color = build_result_.scene_color;
        const auto scene_depth = build_result_.scene_depth;
        builder_.SetExecuteCallback(build_result_.scene_pass,
                                    [opaque_visible_entries, scene_color, scene_depth](render_graph::GraphCommandContext& context_) {
                                        for (const auto* entry_ : opaque_visible_entries) {
                                            if (entry_->graph_record_fn == nullptr) {
                                                continue;
                                            }
                                            entry_->graph_record_fn(entry_->renderer,
                                                                    context_,
                                                                    SceneRenderStage::opaque,
                                                                    scene_color,
                                                                    scene_depth);
                                        }
                                    });
    }

    std::vector<const SceneRendererEntry*> transparent_visible_entries{};
    for (const SceneRendererEntry& entry_ : scene_renderer_entries) {
        if (entry_.stage != SceneRecorder3DSceneStage::transparent ||
            !IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            continue;
        }
        if (entry_.renderer == nullptr) {
            continue;
        }
        transparent_visible_entries.push_back(&entry_);
    }
    if (!transparent_visible_entries.empty() &&
        IsValidResourceHandle(build_result_.scene_color) &&
        IsValidResourceVersionHandle(color_chain_)) {
        const auto transparent_pass = builder_.AddPass("transparent_scene_pass");
        (void)builder_.Read(transparent_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_read});
        if (IsValidResourceHandle(build_result_.scene_depth)) {
            (void)builder_.Read(transparent_pass,
                                build_result_.scene_depth,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::depth_stencil_read});
        }
        color_chain_ = builder_.Write(transparent_pass,
                                      build_result_.scene_color,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(transparent_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = build_result_.scene_color,
                                               .load_op = render_graph::AttachmentLoadOp::load,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                       .has_depth_attachment = build_result_.has_depth,
                                       .depth_attachment = render_graph::RasterDepthAttachmentDesc{
                                           .target = build_result_.scene_depth,
                                           .load_op = render_graph::AttachmentLoadOp::load,
                                           .store_op = render_graph::AttachmentStoreOp::store,
                                           .stencil_load_op = render_graph::AttachmentLoadOp::dont_care,
                                           .stencil_store_op = render_graph::AttachmentStoreOp::dont_care,
                                           .clear_value = {.depth = 1.0F, .stencil = 0U},
                                           .read_only = false,
                                       },
                                   });
        for (const auto* entry_ : transparent_visible_entries) {
            if (entry_->describe_graph_bindings_fn != nullptr) {
                entry_->describe_graph_bindings_fn(entry_->renderer,
                                                   builder_,
                                                   transparent_pass);
            }
            if (graph_runtime_service != nullptr &&
                entry_->register_graph_imported_resources_fn != nullptr) {
                entry_->register_graph_imported_resources_fn(entry_->renderer,
                                                            *graph_runtime_service);
            }
        }
        const auto scene_color = build_result_.scene_color;
        const auto scene_depth = build_result_.scene_depth;
        builder_.SetExecuteCallback(transparent_pass,
                                    [transparent_visible_entries, scene_color, scene_depth](render_graph::GraphCommandContext& context_) {
                                        for (const auto* entry_ : transparent_visible_entries) {
                                            if (entry_->graph_record_fn == nullptr) {
                                                continue;
                                            }
                                            entry_->graph_record_fn(entry_->renderer,
                                                                    context_,
                                                                    SceneRenderStage::transparent,
                                                                    scene_color,
                                                                    scene_depth);
                                        }
                                    });
    }

    if (graph_post_stack_enabled &&
        IsValidResourceHandle(build_result_.scene_color) &&
        IsValidResourceHandle(build_result_.present_target) &&
        IsValidResourceVersionHandle(color_chain_)) {
        auto& bloom_renderer = post_stack.Bloom();
        const auto& bloom_create_info = bloom_renderer.CreateInfo();
        const auto bloom_desc = BuildBloomIntermediateDesc(snapshot_, bloom_create_info);
        const auto prefilter_target =
            builder_.CreateTexture("bloom_prefilter_target", bloom_desc);
        const auto postprocess_output_target = graph_overlay_present
            ? builder_.CreateTexture("postprocess_color",
                                     BuildPostprocessOverlayIntermediateDesc(snapshot_),
                                     render_graph::ResourceLifetime::transient)
            : build_result_.present_target;

        const auto prefilter_pass = builder_.AddPass("bloom_prefilter");
        (void)builder_.Read(prefilter_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        auto bloom_chain = builder_.Write(prefilter_pass,
                                          prefilter_target,
                                          render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        builder_.SetRasterPassDesc(prefilter_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = prefilter_target,
                                               .load_op = render_graph::AttachmentLoadOp::dont_care,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                           },
                                       },
                                   });
        bloom_renderer.DescribeGraphSingleSourceBindings(builder_, prefilter_pass);
        const auto scene_color = build_result_.scene_color;
        builder_.SetExecuteCallback(prefilter_pass,
                                    [this, scene_color, prefilter_target](render_graph::GraphCommandContext& context_) {
                                        post_stack.Bloom().RecordGraphPrefilterPass(context_, scene_color, prefilter_target);
                                    });

        auto bloom_chain_target = prefilter_target;
        const std::uint32_t blur_pair_count = std::max<std::uint32_t>(1U, bloom_create_info.blur_pass_pair_count);
        for (std::uint32_t blur_index = 0U; blur_index < blur_pair_count; ++blur_index) {
            const auto blur_h_target = builder_.CreateTexture(
                BuildIndexedBloomResourceName("bloom_blur_h_target", blur_index),
                bloom_desc);
            const auto blur_h_pass = builder_.AddPass("bloom_blur_h");
            (void)builder_.Read(blur_h_pass,
                                bloom_chain,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
            const auto blur_h_version = builder_.Write(blur_h_pass,
                                                       blur_h_target,
                                                       render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
            builder_.SetRasterPassDesc(blur_h_pass,
                                       render_graph::RasterPassDesc{
                                           .color_attachments = {
                                               render_graph::RasterColorAttachmentDesc{
                                                   .target = blur_h_target,
                                                   .load_op = render_graph::AttachmentLoadOp::dont_care,
                                                   .store_op = render_graph::AttachmentStoreOp::store,
                                               },
                                           },
                                       });
            bloom_renderer.DescribeGraphSingleSourceBindings(builder_, blur_h_pass);
            builder_.SetExecuteCallback(blur_h_pass,
                                        [this, bloom_chain_target, blur_h_target](render_graph::GraphCommandContext& context_) {
                                            post_stack.Bloom().RecordGraphBlurPass(context_, bloom_chain_target, blur_h_target);
                                        });

            const auto blur_v_target = builder_.CreateTexture(
                BuildIndexedBloomResourceName("bloom_blur_v_target", blur_index),
                bloom_desc);
            const auto blur_v_pass = builder_.AddPass("bloom_blur_v");
            (void)builder_.Read(blur_v_pass,
                                blur_h_version,
                                render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
            bloom_chain = builder_.Write(blur_v_pass,
                                         blur_v_target,
                                         render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
            builder_.SetRasterPassDesc(blur_v_pass,
                                       render_graph::RasterPassDesc{
                                           .color_attachments = {
                                               render_graph::RasterColorAttachmentDesc{
                                                   .target = blur_v_target,
                                                   .load_op = render_graph::AttachmentLoadOp::dont_care,
                                                   .store_op = render_graph::AttachmentStoreOp::store,
                                               },
                                           },
                                       });
            bloom_renderer.DescribeGraphSingleSourceBindings(builder_, blur_v_pass);
            builder_.SetExecuteCallback(blur_v_pass,
                                        [this, blur_h_target, blur_v_target](render_graph::GraphCommandContext& context_) {
                                            post_stack.Bloom().RecordGraphBlurPass(context_, blur_h_target, blur_v_target);
                                        });
            bloom_chain_target = blur_v_target;
        }

        const auto combine_pass = builder_.AddPass("bloom_combine");
        (void)builder_.Read(combine_pass,
                            color_chain_,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        (void)builder_.Read(combine_pass,
                            bloom_chain,
                            render_graph::AccessDesc{.access = render_graph::AccessKind::shader_sample_read});
        color_chain_ = builder_.Write(combine_pass,
                                      postprocess_output_target,
                                      render_graph::AccessDesc{.access = render_graph::AccessKind::color_attachment_write});
        const auto combine_output_load_op = graph_overlay_present
            ? render_graph::AttachmentLoadOp::dont_care
            : (bloom_create_info.clear_swapchain
                ? render_graph::AttachmentLoadOp::clear
                : render_graph::AttachmentLoadOp::load);
        builder_.SetRasterPassDesc(combine_pass,
                                   render_graph::RasterPassDesc{
                                       .color_attachments = {
                                           render_graph::RasterColorAttachmentDesc{
                                               .target = postprocess_output_target,
                                               .load_op = combine_output_load_op,
                                               .store_op = render_graph::AttachmentStoreOp::store,
                                               .clear_value = {
                                                   .red = bloom_create_info.clear_color.float32[0],
                                                   .green = bloom_create_info.clear_color.float32[1],
                                                   .blue = bloom_create_info.clear_color.float32[2],
                                                   .alpha = bloom_create_info.clear_color.float32[3],
                                               },
                                           },
                                       },
                                   });
        bloom_renderer.DescribeGraphDualSourceBindings(builder_, combine_pass);
        builder_.SetExecuteCallback(combine_pass,
                                    [this, scene_color, bloom_chain_target, postprocess_output_target](render_graph::GraphCommandContext& context_) {
                                        post_stack.Bloom().RecordGraphCombinePass(context_,
                                                                                  scene_color,
                                                                                  bloom_chain_target,
                                                                                  postprocess_output_target);
                                    });
    }

    if (IsValidPassHandle(build_result_.overlay_pass) &&
        IsValidResourceHandle(build_result_.scene_color)) {
        std::vector<const OverlayRendererEntry*> visible_overlay_entries{};
        for (const OverlayRendererEntry& entry_ : overlay_renderer_entries) {
            if (!IsOverlayLayerVisibleForSubmission(entry_.submission_layer_mask)) {
                continue;
            }
            if (entry_.renderer == nullptr) {
                continue;
            }
            visible_overlay_entries.push_back(&entry_);
        }
        if (!visible_overlay_entries.empty()) {
            for (const auto* entry_ : visible_overlay_entries) {
                if (entry_->describe_graph_bindings_fn != nullptr) {
                    entry_->describe_graph_bindings_fn(entry_->renderer,
                                                       builder_,
                                                       build_result_.overlay_pass);
                }
                if (graph_runtime_service != nullptr &&
                    entry_->register_graph_imported_resources_fn != nullptr) {
                    entry_->register_graph_imported_resources_fn(entry_->renderer,
                                                                *graph_runtime_service);
                }
            }
            const auto overlay_target = build_result_.scene_color;
            builder_.SetExecuteCallback(build_result_.overlay_pass,
                                        [visible_overlay_entries, overlay_target](render_graph::GraphCommandContext& context_) {
                                            for (const auto* entry_ : visible_overlay_entries) {
                                                if (entry_->graph_record_fn == nullptr) {
                                                    continue;
                                                }
                                                entry_->graph_record_fn(entry_->renderer, context_, overlay_target);
                                            }
                                        });
        }
    }

    if (!shadow_enabled) {
        return;
    }

    for (const PreSceneRendererEntry& entry : pre_scene_renderer_entries) {
        if (!IsLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (entry.kind != PreSceneRendererKind::shadow || entry.renderer == nullptr) {
            continue;
        }

        auto* shadow_renderer = static_cast<shadow::ShadowRenderer3D*>(entry.renderer);
        const auto shadow_pass = builder_.AddPass("shadow_prepass", true);
        if (IsValidPassHandle(build_result_.scene_pass)) {
            builder_.AddDependency(build_result_.scene_pass, shadow_pass);
        }
        builder_.SetExecuteCallback(shadow_pass,
                                    [shadow_renderer](render_graph::GraphCommandContext& context_) {
                                        render::FrameRecordContext record_context{};
                                        record_context.command_buffer = context_.CommandBuffer();
                                        shadow_renderer->Record(record_context);
                                    });
    }
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
    const bool has_sky_environment_pass = HasSkyEnvironmentPassForSubmission();
    const bool record_sky_before_opaque = ShouldRecordSkyEnvironmentBeforeOpaque();
    const bool record_sky_after_opaque = ShouldRecordSkyEnvironmentAfterOpaque();

    if (has_sky_environment_pass && !sky_environment_pass_ready) {
        throw std::runtime_error(
            "SceneRecorder3D::Record requires SkyEnvironmentPass to be prepared for the active submission");
    }

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

    if (record_sky_before_opaque && scene_view != nullptr) {
        sky_environment_pass.Record(record_context_,
                                    *scene_view,
                                    frame_packet->extra.environment,
                                    resolved_environment_gpu);
        ++stats.environment_record_count;
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
            const bool final_pass = (entry_.pass_role == SceneRenderPassRole::single ||
                                     entry_.pass_role == SceneRenderPassRole::last) &&
                                    !(record_sky_after_opaque &&
                                      entry_.stage == SceneRecorder3DSceneStage::opaque);
            RenderTargetColorOutputConfig scene_color_output = color_output;
            scene_color_output.final_state = final_pass
                ? color_output.final_state
                : RenderTargetStateKind::color_attachment;
            if ((entry_.pass_role == SceneRenderPassRole::single ||
                 entry_.pass_role == SceneRenderPassRole::first) &&
                !record_sky_before_opaque) {
                scene_color_output.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
            RenderTargetDepthOutputConfig scene_depth_output = explicit_depth_output;
            scene_depth_output.final_state = final_pass
                ? explicit_depth_output.final_state
                : RenderTargetStateKind::depth_attachment;
            const RenderTargetDepthOutputConfig* depth_output_config =
                (scene_view != nullptr && IsValidRenderTargetHandle(scene_view->targets.depth_target))
                    ? &scene_depth_output
                    : nullptr;
            entry_.configure_direct_scene_fn(entry_.renderer,
                                             entry_.pass_role,
                                             scene_color_output,
                                             depth_output_config,
                                             depth_output_config != nullptr);
        } else if (use_post_stack) {
            if (has_sky_environment_pass && entry_.configure_direct_scene_fn != nullptr) {
                const bool final_pass = (entry_.pass_role == SceneRenderPassRole::single ||
                                         entry_.pass_role == SceneRenderPassRole::last) &&
                                        !(record_sky_after_opaque &&
                                          entry_.stage == SceneRecorder3DSceneStage::opaque);
                const bool clear_depth = entry_.pass_role == SceneRenderPassRole::single ||
                                         entry_.pass_role == SceneRenderPassRole::first;
                const bool clear_color = (entry_.pass_role == SceneRenderPassRole::single ||
                                          entry_.pass_role == SceneRenderPassRole::first)
                    ? !record_sky_before_opaque
                    : false;
                const RenderTargetDepthOutputConfig depth_output =
                    post_stack.Targets().BuildDepthOutputConfig(clear_depth);
                const bool enable_external_depth =
                    record_sky_after_opaque && create_info_cache.scene_target.enable_depth;
                entry_.configure_direct_scene_fn(entry_.renderer,
                                                 entry_.pass_role,
                                                 post_stack.Targets().BuildColorOutputConfig(clear_color, final_pass),
                                                 enable_external_depth ? &depth_output : nullptr,
                                                 enable_external_depth);
            } else if (entry_.configure_scene_fn != nullptr) {
                (void)entry_.configure_scene_fn(entry_.renderer, post_stack.Targets(), entry_.pass_role);
            }
        }
        entry_.record_fn(entry_.renderer, record_context_, entry_.stage);
    };
    for (const SceneRecorder3DSceneStage stage : scene_stage_record_order) {
        for (const SceneRendererEntry& entry : scene_renderer_entries) {
            if (entry.stage != stage) {
                continue;
            }
            record_scene_renderer(entry);
        }
        if (stage == SceneRecorder3DSceneStage::opaque &&
            record_sky_after_opaque &&
            scene_view != nullptr) {
            sky_environment_pass.Record(record_context_,
                                        *scene_view,
                                        frame_packet->extra.environment,
                                        resolved_environment_gpu);
            ++stats.environment_record_count;
        }
    }

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
    const bool graph_managed_post_stack = UsesGraphManagedPostStack();

    sky_environment_pass.OnSwapchainRecreated(image_count_,
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
    if (!graph_managed_post_stack) {
            (void)post_stack.Targets().OnSwapchainRecreated(*context,
                                                            *render_target_host,
                                                            render_target_pool,
                                                            extent_,
                                                            last_submitted_value_,
                                                            completed_submit_value_);
        } else {
            post_stack.Targets().InvalidateFrameTargets();
            post_stack.Bloom().ClearSceneSourceTarget();
            post_stack.Bloom().ResetOutputTargetConfig();
            sky_environment_pass.ResetOutputTargetConfig();
            sky_environment_pass.ResetDepthTargetConfig();
        }
    }

    auto reconfigure_scene_renderer = [&](const SceneRendererEntry& entry_) {
        if (!IsLayerVisibleForSubmission(entry_.submission_layer_mask)) {
            return;
        }
        if (entry_.renderer == nullptr) {
            return;
        }
        if (graph_managed_post_stack) {
            // Graph-managed 3D mainline keeps scene targets in RenderGraph state only.
        } else if (use_explicit_scene_target) {
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
    if (use_post_stack && !graph_managed_post_stack) {
        (void)post_stack.Targets().ConfigureSceneConsumer(post_stack.Bloom());
    }
    for (const OverlayRendererEntry& entry : overlay_renderer_entries) {
        if (!overlay_enabled || !IsOverlayLayerVisibleForSubmission(entry.submission_layer_mask)) {
            continue;
        }
        if (!graph_managed_post_stack &&
            entry.renderer != nullptr &&
            entry.set_output_target_fn != nullptr) {
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

SkyEnvironmentPass& SceneRecorder3D::EnvironmentPass() noexcept {
    return sky_environment_pass;
}

const SkyEnvironmentPass& SceneRecorder3D::EnvironmentPass() const noexcept {
    return sky_environment_pass;
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
        output.load_op = ShouldRecordSkyEnvironmentBeforeOpaque()
            ? VK_ATTACHMENT_LOAD_OP_LOAD
            : VK_ATTACHMENT_LOAD_OP_CLEAR;
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

bool SceneRecorder3D::SupportsGraphExecution(const VulkanContext& device_) const noexcept {
    return graph_runtime_service != nullptr &&
           graph_runtime_service->SupportsGraphExecution(device_);
}

bool SceneRecorder3D::UsesGraphManagedPostStack() const noexcept {
    return context != nullptr &&
           ShouldUsePostStackForSubmission() &&
           !HasExplicitSceneTargetForSubmission() &&
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
    if (HasExplicitSceneTargetForSubmission()) {
        return scene_view != nullptr &&
               IsValidRenderTargetHandle(scene_view->targets.depth_target);
    }
    return ShouldUsePostStackForSubmission() &&
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

void SceneRecorder3D::ConfigureSkyEnvironmentPassForTargets() {
    if (!HasSkyEnvironmentPassForSubmission()) {
        sky_environment_pass.ResetOutputTargetConfig();
        sky_environment_pass.ResetDepthTargetConfig();
        return;
    }
    sky_environment_pass.SetOutputTargetConfig(BuildSkyEnvironmentOutputConfig());
    if (ShouldRecordSkyEnvironmentAfterOpaque()) {
        sky_environment_pass.SetDepthTargetConfig(BuildSkyEnvironmentDepthOutputConfig());
    } else {
        sky_environment_pass.ResetDepthTargetConfig();
    }
}

RenderTargetColorOutputConfig SceneRecorder3D::BuildSkyEnvironmentOutputConfig() const noexcept {
    const bool use_explicit_scene_target = HasExplicitSceneTargetForSubmission();
    const bool use_post_stack = ShouldUsePostStackForSubmission();
    const bool has_visible_scene_renderer = HasVisibleSceneRendererForSubmission();
    const bool record_sky_after_opaque = ShouldRecordSkyEnvironmentAfterOpaque();
    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool final_pass = use_post_stack
        ? (record_sky_after_opaque || !has_visible_scene_renderer)
        : (record_sky_after_opaque
               ? !overlay_enabled
               : (!has_visible_scene_renderer && !overlay_enabled));

    if (use_post_stack) {
        return post_stack.Targets().BuildColorOutputConfig(!record_sky_after_opaque, final_pass);
    }

    RenderTargetColorOutputConfig output{};
    output.use_explicit_load_op = true;
    output.load_op = record_sky_after_opaque
        ? VK_ATTACHMENT_LOAD_OP_LOAD
        : VK_ATTACHMENT_LOAD_OP_CLEAR;
    output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    output.clear_color = create_info_cache.scene_target.clear_color;
    output.final_state = final_pass ? RenderTargetStateKind::present_src
                                    : RenderTargetStateKind::color_attachment;
    if (use_explicit_scene_target && scene_view != nullptr) {
        output.color_target = scene_view->targets.color_target;
        output.final_state = final_pass ? scene_view->targets.color_final_state
                                        : RenderTargetStateKind::color_attachment;
    }
    return output;
}

RenderTargetDepthOutputConfig SceneRecorder3D::BuildSkyEnvironmentDepthOutputConfig() const noexcept {
    const bool use_explicit_scene_target = HasExplicitSceneTargetForSubmission();
    const bool use_post_stack = ShouldUsePostStackForSubmission();
    const bool final_pass = !IsOverlayEnabledForSubmission() && !use_post_stack;

    if (use_post_stack) {
        RenderTargetDepthOutputConfig output =
            post_stack.Targets().BuildDepthOutputConfig(false);
        output.use_explicit_load_op = true;
        output.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
        return output;
    }

    RenderTargetDepthOutputConfig output = use_explicit_scene_target
        ? BuildExplicitDepthOutputConfig(final_pass ? SceneRenderPassRole::last
                                                    : SceneRenderPassRole::middle)
        : BuildDirectDepthOutputConfig(final_pass ? SceneRenderPassRole::last
                                                  : SceneRenderPassRole::middle);
    output.use_explicit_load_op = true;
    output.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    output.final_state = final_pass
        ? output.final_state
        : RenderTargetStateKind::depth_attachment;
    return output;
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

