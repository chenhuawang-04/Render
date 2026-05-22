#include "vr/render/scene_recorder_3d.hpp"

#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/ibl_bake_host.hpp"

namespace vr::render {

namespace {

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

void SceneRecorder3D::PrepareFrame(const SceneRecorder3DPrepareView& prepare_view_) {
    EnsureInitialized("PrepareFrame");
    RefreshFramePacketBinding();
    if (frame_packet != nullptr) {
        ++stats.frame_packet_prepare_count;
    }

    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();
    const bool use_bloom_chain = ShouldUseBloomChainForSubmission();
    resolved_environment_gpu = frame_packet != nullptr ? frame_packet->extra.environment_gpu
                                                       : scene::SkyEnvironmentGpuHandle{};
    sky_environment_pass_ready = false;
    std::uint32_t ibl_environment_id =
        frame_packet != nullptr ? frame_packet->extra.ibl_environment_id : 0U;
    std::uint32_t ibl_brdf_lut_texture_id = 0U;

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
    resolved_prepare_view.render_graph_upload_active = true;
    resolved_prepare_view.render_graph_compute_active = true;
    resolved_prepare_view.ibl_environment_id = ibl_environment_id;
    resolved_prepare_view.ibl_brdf_lut_texture_id = ibl_brdf_lut_texture_id;
    sky_environment_pass.ResetOutputTargetConfig();
    sky_environment_pass.ResetDepthTargetConfig();
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
    if (use_bloom_chain) {
        bloom_renderer.PrepareGraphFrame(resolved_prepare_view);
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

void SceneRecorder3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                           VkExtent2D extent_,
                                           VkFormat format_,
                                           std::uint64_t last_submitted_value_,
                                           std::uint64_t completed_submit_value_) {
    EnsureInitialized("OnSwapchainRecreated");
    EnsureRuntimeBinding("OnSwapchainRecreated");

    const bool overlay_enabled = IsOverlayEnabledForSubmission();
    const bool shadow_enabled = IsShadowEnabledForSubmission();

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

    if (ShouldUseBloomChainForSubmission()) {
        bloom_renderer.OnSwapchainRecreated(image_count_, extent_, format_);
    }
    sky_environment_pass.ResetOutputTargetConfig();
    sky_environment_pass.ResetDepthTargetConfig();
    stats.swapchain_recreate_count += 1U;
}

} // namespace vr::render
