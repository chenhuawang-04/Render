#include "vr/render/scene_recorder_2d.hpp"

namespace vr::render {

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


} // namespace vr::render
