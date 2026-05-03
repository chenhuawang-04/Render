#include "support/test_framework.hpp"
#include "vr/animation/animation_clip_host.hpp"
#include "vr/animation/animation_path_host.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"
#include "vr/ecs/system/animation_path_motion_system.hpp"

#include <array>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return (lhs_ >= rhs_ ? lhs_ - rhs_ : rhs_ - lhs_) <= epsilon_;
}

VR_TEST_CASE(AnimationClipHost_upsert_find_sample_and_remove,
             "unit;core;animation;host") {
    vr::animation::AnimationClipHost host{};
    host.Initialize();

    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float2>, 2U> position_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float2{.x = 0.0F, .y = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float2{.x = 4.0F, .y = 8.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::PropertyFloat2ChannelDesc, 1U> position_channels{{
        {
            .semantic = vr::ecs::PropertyTrackSemantic::transform_local_position,
            .channel_mask = 0x3U,
            .reserved0 = 0U,
            .curve = vr::ecs::AnimationCurveView<vr::ecs::Float2>{.keyframes = position_keys.data(), .keyframe_count = 2U},
        },
    }};
    vr::animation::PropertyAnimationClipDesc property_desc{};
    property_desc.clip_id = 10U;
    property_desc.duration_s = 1.0F;
    property_desc.float2_channels = position_channels.data();
    property_desc.float2_channel_count = static_cast<std::uint32_t>(position_channels.size());

    const vr::ecs::AnimationClipHandle property_handle = host.UpsertPropertyClip(property_desc);
    const vr::animation::AnimationClipRecord* property_clip = host.FindPropertyClipByHandle(property_handle);
    VR_REQUIRE(property_clip != nullptr);
    VR_CHECK(property_clip->kind == vr::animation::AnimationClipKind::property_track);
    VR_CHECK(host.Stats().clip_count == 1U);
    VR_CHECK(host.Stats().property_clip_count == 1U);

    const vr::animation::PropertyFloat2ChannelRecord* property_channels_ptr =
        host.PropertyFloat2Channels(*property_clip);
    VR_REQUIRE(property_channels_ptr != nullptr);
    const vr::ecs::Float2 sampled_position = vr::ecs::AnimationCurveSystem::Sample(
        host.BuildCurveView(property_channels_ptr[0U]),
        0.5F);
    VR_CHECK(NearlyEqual(sampled_position.x, 2.0F));
    VR_CHECK(NearlyEqual(sampled_position.y, 4.0F));

    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float4>, 2U> uv_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float4{.x = 1.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float4{.x = 2.0F, .y = 2.0F, .z = 0.25F, .w = 0.5F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::MaterialFloat4ChannelDesc, 1U> uv_channels{{
        {
            .semantic = vr::ecs::MaterialTrackSemantic::surface_uv_transform,
            .channel_mask = 0xFU,
            .reserved0 = 0U,
            .curve = vr::ecs::AnimationCurveView<vr::ecs::Float4>{.keyframes = uv_keys.data(), .keyframe_count = 2U},
        },
    }};
    vr::animation::MaterialAnimationClipDesc material_desc{};
    material_desc.clip_id = 20U;
    material_desc.duration_s = 1.0F;
    material_desc.float4_channels = uv_channels.data();
    material_desc.float4_channel_count = static_cast<std::uint32_t>(uv_channels.size());
    const vr::ecs::AnimationClipHandle material_handle = host.UpsertMaterialClip(material_desc);
    VR_REQUIRE(host.FindMaterialClipById(20U) != nullptr);
    VR_CHECK(host.Stats().material_clip_count == 1U);

    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> fov_keys{{
        {.time_s = 0.0F, .value = 1.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 1.2F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::CameraScalarChannelDesc, 1U> fov_channels{{
        {
            .semantic = static_cast<std::uint16_t>(vr::ecs::animation_camera_apply_vertical_fov_flag),
            .channel_mask = 0x1U,
            .reserved0 = 0U,
            .curve = vr::ecs::AnimationCurveView<float>{.keyframes = fov_keys.data(), .keyframe_count = 2U},
        },
    }};
    vr::animation::CameraAnimationClipDesc camera_desc{};
    camera_desc.clip_id = 30U;
    camera_desc.duration_s = 1.0F;
    camera_desc.scalar_channels = fov_channels.data();
    camera_desc.scalar_channel_count = static_cast<std::uint32_t>(fov_channels.size());
    const vr::ecs::AnimationClipHandle camera_handle = host.UpsertCameraClip(camera_desc);
    VR_REQUIRE(host.FindCameraClipByHandle(camera_handle) != nullptr);
    VR_CHECK(host.Stats().camera_clip_count == 1U);
    VR_CHECK(host.Stats().clip_count == 3U);

    VR_REQUIRE(host.RemoveClip(20U));
    VR_CHECK(host.FindClipByHandle(material_handle) == nullptr);
    VR_CHECK(host.FindClipById(20U) == nullptr);
    VR_CHECK(host.Stats().clip_count == 2U);
    VR_CHECK(host.Stats().material_clip_count == 0U);
}

VR_TEST_CASE(AnimationPathHost_upsert_find_view_and_remove,
             "unit;core;animation;host") {
    vr::animation::AnimationPathHost host{};
    host.Initialize();

    const std::array<vr::ecs::AnimationBezierSegment<vr::ecs::Dim2>, 1U> segments_2d{{
        {
            .p0 = vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
            .p1 = vr::ecs::Float2{.x = 1.0F, .y = 0.0F},
            .p2 = vr::ecs::Float2{.x = 2.0F, .y = 0.0F},
            .p3 = vr::ecs::Float2{.x = 3.0F, .y = 0.0F},
        },
    }};
    const vr::ecs::AnimationPathHandle handle_2d = host.UpsertPath(vr::animation::AnimationPath2DDesc{
        .path_id = 100U,
        .segments = segments_2d.data(),
        .segment_count = static_cast<std::uint32_t>(segments_2d.size()),
    });
    const vr::animation::AnimationPathRecord* record_2d = host.FindPath2DByHandle(handle_2d);
    VR_REQUIRE(record_2d != nullptr);
    const auto view_2d = host.BuildSplineView2D(*record_2d);
    VR_CHECK(view_2d.segment_count == 1U);

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::PathMotion> path_animation{};
    vr::ecs::AnimationPathMotionSystem<vr::ecs::Dim2>::Initialize(path_animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PathMotion>::SetDurationSeconds(path_animation, 1.0F);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PathMotion>::SetTimeSeconds(path_animation, 0.5F);
    VR_REQUIRE(vr::ecs::AnimationPathMotionSystem<vr::ecs::Dim2>::SampleSpline(path_animation, view_2d));
    VR_CHECK(NearlyEqual(path_animation.sample.position.x, 1.5F));

    const std::array<vr::ecs::AnimationBezierSegment<vr::ecs::Dim3>, 1U> segments_3d{{
        {
            .p0 = vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            .p1 = vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            .p2 = vr::ecs::Float3{.x = 0.0F, .y = 2.0F, .z = 0.0F},
            .p3 = vr::ecs::Float3{.x = 0.0F, .y = 3.0F, .z = 0.0F},
        },
    }};
    const vr::ecs::AnimationPathHandle handle_3d = host.UpsertPath(vr::animation::AnimationPath3DDesc{
        .path_id = 200U,
        .segments = segments_3d.data(),
        .segment_count = static_cast<std::uint32_t>(segments_3d.size()),
    });
    VR_REQUIRE(host.FindPath3DByHandle(handle_3d) != nullptr);
    VR_CHECK(host.Stats().path_count == 2U);
    VR_CHECK(host.Stats().path2d_count == 1U);
    VR_CHECK(host.Stats().path3d_count == 1U);

    VR_REQUIRE(host.RemovePath(100U));
    VR_CHECK(host.FindPathByHandle(handle_2d) == nullptr);
    VR_CHECK(host.FindPathById(100U) == nullptr);
    VR_CHECK(host.Stats().path_count == 1U);
}

} // namespace
