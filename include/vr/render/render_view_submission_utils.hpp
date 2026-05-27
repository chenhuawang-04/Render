#pragma once

#include "vr/ecs/system/camera_system.hpp"
#include "vr/render/scene_submission.hpp"

#include <cmath>
#include <cstdint>

namespace vr::render {

template<ecs::DimensionTag DimensionT>
void RefreshExtentBoundWorldSceneSubmission(
    RenderView<DimensionT>& view_,
    RenderScenePacket<DimensionT>& packet_,
    ecs::Camera<DimensionT>& camera_,
    const ecs::Transform<DimensionT>& camera_transform_,
    VkExtent2D extent_,
    SceneSubmissionId submission_id_,
    std::uint32_t view_flags_ = render_view_lighting_enabled_flag |
                                render_view_shadow_enabled_flag |
                                render_view_postprocess_enabled_flag,
    std::uint32_t packet_flags_ = render_scene_packet_allow_postprocess_flag |
                                  render_scene_packet_allow_shadow_flag,
    std::uint32_t layer_mask_ = 0xFFFF'FFFFU,
    std::uint32_t debug_flags_ = render_view_debug_none_flag,
    RenderPostProcessPolicy postprocess_policy_ = RenderPostProcessPolicy::inherit) noexcept {
    using CameraSystemType = ecs::CameraSystem<DimensionT>;

    const float width = static_cast<float>((extent_.width > 0U) ? extent_.width : 1U);
    const float height = static_cast<float>((extent_.height > 0U) ? extent_.height : 1U);
    const float aspect_ratio = width / height;

    const auto viewport_changed =
        std::fabs(camera_.style.viewport.origin_x - 0.0F) > 1e-6F ||
        std::fabs(camera_.style.viewport.origin_y - 0.0F) > 1e-6F ||
        std::fabs(camera_.style.viewport.width - width) > 1e-6F ||
        std::fabs(camera_.style.viewport.height - height) > 1e-6F;
    if (viewport_changed) {
        CameraSystemType::SetViewport(camera_, 0.0F, 0.0F, width, height);
    }

    if (std::fabs(camera_.style.aspect_ratio - aspect_ratio) > 1e-6F) {
        CameraSystemType::SetAspectRatio(camera_, aspect_ratio);
    }
    CameraSystemType::Update(camera_, camera_transform_);

    view_ = MakeRenderViewFromCamera(camera_,
                                     &camera_transform_,
                                     RenderViewKind::world,
                                     0U);
    view_.flags = view_flags_;
    view_.layer_mask = layer_mask_;
    view_.debug_flags = debug_flags_;
    view_.postprocess_policy = postprocess_policy_;
    RefreshRenderViewSignature(view_);

    BindSceneSubmissionViews(packet_, &view_, 1U, 0U);
    ApplySceneSubmissionMetadata(
        packet_,
        SceneSubmissionMetadata{
            .kind = RenderScenePacketKind::world,
            .flags = packet_flags_,
            .render_layer_mask = layer_mask_,
            .debug_flags = debug_flags_,
            .postprocess_policy = postprocess_policy_,
            .submission_id = submission_id_,
        });
    RefreshRenderScenePacketSignature(packet_);
}

template<ecs::DimensionTag DimensionT>
void RefreshExtentBoundScreenSceneSubmission(
    RenderView<DimensionT>& view_,
    RenderScenePacket<DimensionT>& packet_,
    VkExtent2D extent_,
    SceneSubmissionId submission_id_,
    RenderViewKind kind_ = RenderViewKind::ui,
    std::uint32_t view_flags_ = render_view_overlay_enabled_flag,
    std::uint32_t packet_flags_ = render_scene_packet_allow_overlay_flag,
    std::uint32_t layer_mask_ = 0xFFFF'FFFFU,
    std::uint32_t debug_flags_ = render_view_debug_none_flag,
    RenderPostProcessPolicy postprocess_policy_ = RenderPostProcessPolicy::inherit) noexcept {
    const float width = static_cast<float>((extent_.width > 0U) ? extent_.width : 1U);
    const float height = static_cast<float>((extent_.height > 0U) ? extent_.height : 1U);

    view_ = {};
    view_.kind = kind_;
    view_.flags = view_flags_;
    view_.layer_mask = layer_mask_;
    view_.debug_flags = debug_flags_;
    view_.postprocess_policy = postprocess_policy_;
    view_.viewport = RenderViewViewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = width,
        .height = height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
    };
    view_.scissor = RenderViewScissor{
        .x = 0,
        .y = 0,
        .width = (extent_.width > 0U) ? extent_.width : 1U,
        .height = (extent_.height > 0U) ? extent_.height : 1U,
    };
    RefreshRenderViewSignature(view_);

    BindSceneSubmissionViews(packet_, &view_, 1U, 0U);
    ApplySceneSubmissionMetadata(
        packet_,
        SceneSubmissionMetadata{
            .kind = (kind_ == RenderViewKind::ui)
                        ? RenderScenePacketKind::ui
                        : RenderScenePacketKind::world,
            .flags = packet_flags_,
            .render_layer_mask = layer_mask_,
            .debug_flags = debug_flags_,
            .postprocess_policy = postprocess_policy_,
            .submission_id = submission_id_,
        });
    RefreshRenderScenePacketSignature(packet_);
}

} // namespace vr::render

