#include "support/test_framework.hpp"
#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_camera_track_system.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"
#include "vr/ecs/system/animation_visual_track_system.hpp"
#include "vr/ecs/system/animation_path_motion_system.hpp"
#include "vr/ecs/system/animation_property_track_system.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cmath>
#include <type_traits>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

VR_TEST_CASE(EcsAnimationComponents_are_pure_pod_and_future_kinds_exist,
             "unit;core;ecs;animation") {
    using AnimationProperty2D = vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::PropertyTrack>;
    using AnimationVisual3D = vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::VisualTrack>;
    using AnimationPath2D = vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::PathMotion>;
    using AnimationCamera3D = vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::CameraTrack>;
    using AnimationMorph2D = vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::Morph>;
    using AnimationParticle3D = vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::ParticleSimulation>;

    VR_CHECK(std::is_standard_layout_v<AnimationProperty2D>);
    VR_CHECK(std::is_trivial_v<AnimationProperty2D>);
    VR_CHECK(std::is_standard_layout_v<AnimationVisual3D>);
    VR_CHECK(std::is_trivial_v<AnimationVisual3D>);
    VR_CHECK(std::is_standard_layout_v<AnimationPath2D>);
    VR_CHECK(std::is_trivial_v<AnimationPath2D>);
    VR_CHECK(std::is_standard_layout_v<AnimationCamera3D>);
    VR_CHECK(std::is_trivial_v<AnimationCamera3D>);
    VR_CHECK(std::is_standard_layout_v<AnimationMorph2D>);
    VR_CHECK(std::is_trivial_v<AnimationMorph2D>);
    VR_CHECK(std::is_standard_layout_v<AnimationParticle3D>);
    VR_CHECK(std::is_trivial_v<AnimationParticle3D>);
}

VR_TEST_CASE(EcsAnimationClockSystem_advances_loop_and_ping_pong,
             "unit;core;ecs;animation") {
    using AnimationType = vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::PropertyTrack>;
    using ClockSystem = vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PropertyTrack>;

    AnimationType component{};
    ClockSystem::InitializeCommon(component);
    ClockSystem::SetDurationSeconds(component, 2.0F);
    ClockSystem::ClearDirtyFlags(component, 0xFFFFFFFFU);

    ClockSystem::SetTimeSeconds(component, 1.5F);
    ClockSystem::SetLoopMode(component, vr::ecs::AnimationLoopMode::loop);
    VR_REQUIRE(ClockSystem::Advance(component, 1.0F));
    VR_CHECK(NearlyEqual(component.playback.time_s, 0.5F));
    VR_CHECK(ClockSystem::HasDirtyFlags(component, vr::ecs::animation_dirty_sample_flag));

    ClockSystem::Restart(component);
    ClockSystem::SetTimeSeconds(component, 1.75F);
    ClockSystem::SetLoopMode(component, vr::ecs::AnimationLoopMode::ping_pong);
    VR_REQUIRE(ClockSystem::Advance(component, 0.5F));
    VR_CHECK(NearlyEqual(component.playback.time_s, 1.75F));
    VR_CHECK(ClockSystem::IsPingPongBackward(component));
}

VR_TEST_CASE(EcsAnimationCurveSystem_samples_float3_and_quaternion,
             "unit;core;ecs;animation") {
    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float3>, 2U> position_keys{{
        {
            .time_s = 0.0F,
            .value = vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear,
            .reserved0 = 0U,
            .reserved1 = 0U,
        },
        {
            .time_s = 1.0F,
            .value = vr::ecs::Float3{.x = 10.0F, .y = 20.0F, .z = 30.0F},
            .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear,
            .reserved0 = 0U,
            .reserved1 = 0U,
        },
    }};
    vr::ecs::AnimationCurveCursor cursor{};
    const vr::ecs::Float3 sampled_position = vr::ecs::AnimationCurveSystem::Sample(
        vr::ecs::AnimationCurveView<vr::ecs::Float3>{.keyframes = position_keys.data(), .keyframe_count = 2U},
        0.25F,
        &cursor);

    VR_CHECK(NearlyEqual(sampled_position.x, 2.5F));
    VR_CHECK(NearlyEqual(sampled_position.y, 5.0F));
    VR_CHECK(NearlyEqual(sampled_position.z, 7.5F));
    VR_CHECK(cursor.segment_index == 0U);

    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Quaternion>, 2U> rotation_keys{{
        {
            .time_s = 0.0F,
            .value = vr::ecs::Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear,
            .reserved0 = 0U,
            .reserved1 = 0U,
        },
        {
            .time_s = 1.0F,
            .value = vr::ecs::Quaternion{.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F},
            .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear,
            .reserved0 = 0U,
            .reserved1 = 0U,
        },
    }};
    const vr::ecs::Quaternion sampled_rotation = vr::ecs::AnimationCurveSystem::Sample(
        vr::ecs::AnimationCurveView<vr::ecs::Quaternion>{.keyframes = rotation_keys.data(), .keyframe_count = 2U},
        0.5F);
    const float norm_sq = sampled_rotation.x * sampled_rotation.x +
                          sampled_rotation.y * sampled_rotation.y +
                          sampled_rotation.z * sampled_rotation.z +
                          sampled_rotation.w * sampled_rotation.w;
    VR_CHECK(NearlyEqual(norm_sq, 1.0F, 1e-3F));
}

VR_TEST_CASE(EcsAnimationPropertyTrackSystem_applies_transform_and_camera,
             "unit;core;ecs;animation") {
    using AnimationType = vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::PropertyTrack>;
    using PropertySystem = vr::ecs::AnimationPropertyTrackSystem<vr::ecs::Dim2>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
    using CameraSystem2D = vr::ecs::CameraSystem<vr::ecs::Dim2>;

    AnimationType transform_track{};
    PropertySystem::Initialize(transform_track);
    PropertySystem::SetSemantic(transform_track, vr::ecs::PropertyTrackSemantic::transform_local_position);
    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float2>, 2U> position_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float2{.x = 0.0F, .y = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float2{.x = 8.0F, .y = 4.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    vr::ecs::Transform<vr::ecs::Dim2> transform{};
    TransformSystem2D::Initialize(transform);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PropertyTrack>::SetDurationSeconds(transform_track, 1.0F);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PropertyTrack>::SetTimeSeconds(transform_track, 0.5F);
    PropertySystem::SampleFloat2Curve(transform_track,
                                      vr::ecs::AnimationCurveView<vr::ecs::Float2>{.keyframes = position_keys.data(), .keyframe_count = 2U});
    VR_REQUIRE(PropertySystem::ApplyToTransform(transform_track, transform));
    VR_CHECK(NearlyEqual(transform.style.position.x, 4.0F));
    VR_CHECK(NearlyEqual(transform.style.position.y, 2.0F));

    AnimationType camera_track{};
    PropertySystem::Initialize(camera_track);
    PropertySystem::SetSemantic(camera_track, vr::ecs::PropertyTrackSemantic::camera_zoom);
    vr::ecs::Camera<vr::ecs::Dim2> camera{};
    CameraSystem2D::Initialize(camera);
    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> zoom_keys{{
        {.time_s = 0.0F, .value = 1.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 2.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PropertyTrack>::SetDurationSeconds(camera_track, 1.0F);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PropertyTrack>::SetTimeSeconds(camera_track, 0.25F);
    PropertySystem::SampleScalarCurve(camera_track,
                                      vr::ecs::AnimationCurveView<float>{.keyframes = zoom_keys.data(), .keyframe_count = 2U});
    VR_REQUIRE(PropertySystem::ApplyToCamera(camera_track, camera));
    VR_CHECK(NearlyEqual(camera.style.zoom, 1.25F));
}

VR_TEST_CASE(EcsAnimationVisualTrackSystem_applies_surface_and_appearance,
             "unit;core;ecs;animation") {
    using VisualSystem2D = vr::ecs::AnimationVisualTrackSystem<vr::ecs::Dim2>;
    using VisualSystem3D = vr::ecs::AnimationVisualTrackSystem<vr::ecs::Dim3>;

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::VisualTrack> surface_track_2d{};
    VisualSystem2D::Initialize(surface_track_2d);
    VisualSystem2D::SetSemantic(surface_track_2d, vr::ecs::VisualTrackSemantic::surface_uv_rect);
    surface_track_2d.sample.value = vr::ecs::Float4{.x = 0.1F, .y = 0.2F, .z = 0.9F, .w = 0.8F};
    vr::ecs::Surface<vr::ecs::Dim2> surface_2d{};
    vr::ecs::SurfaceSystem<vr::ecs::Dim2>::Initialize(surface_2d);
    VR_REQUIRE(VisualSystem2D::ApplyToSurface(surface_track_2d, surface_2d));
    VR_CHECK(NearlyEqual(surface_2d.style.uv_u0, 0.1F));
    VR_CHECK(NearlyEqual(surface_2d.style.uv_v1, 0.8F));

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::VisualTrack> surface_appearance_track_2d{};
    VisualSystem2D::Initialize(surface_appearance_track_2d);
    VisualSystem2D::SetSemantic(surface_appearance_track_2d, vr::ecs::VisualTrackSemantic::appearance_color);
    surface_appearance_track_2d.sample.value = vr::ecs::Float4{.x = 0.25F, .y = 0.5F, .z = 0.75F, .w = 1.0F};
    VR_REQUIRE(VisualSystem2D::ApplyToSurface(surface_appearance_track_2d, surface_2d));
    const vr::ecs::AppearanceRuntimeBridge2D surface_appearance_2d =
        vr::ecs::ReadAppearanceRuntimeBridge2D(surface_2d.runtime);
    VR_CHECK(surface_appearance_2d.fill_color.r == 64U);
    VR_CHECK(surface_appearance_2d.fill_color.g == 128U);
    VR_CHECK(surface_appearance_2d.fill_color.b == 191U);

    vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::VisualTrack> surface_appearance_track_3d{};
    VisualSystem3D::Initialize(surface_appearance_track_3d);
    VisualSystem3D::SetSemantic(surface_appearance_track_3d, vr::ecs::VisualTrackSemantic::appearance_opacity);
    surface_appearance_track_3d.sample.value = vr::ecs::Float4{.x = 0.35F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    vr::ecs::Surface<vr::ecs::Dim3> surface_3d{};
    vr::ecs::SurfaceSystem<vr::ecs::Dim3>::Initialize(surface_3d);
    VR_REQUIRE(VisualSystem3D::ApplyToSurface(surface_appearance_track_3d, surface_3d));
    VR_CHECK(NearlyEqual(vr::ecs::ReadAppearanceRuntimeBridge3D(surface_3d.runtime).opacity, 0.35F));

    vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::VisualTrack> appearance_track_3d{};
    VisualSystem3D::Initialize(appearance_track_3d);
    VisualSystem3D::SetSemantic(appearance_track_3d, vr::ecs::VisualTrackSemantic::appearance_emissive_intensity);
    appearance_track_3d.sample.value = vr::ecs::Float4{.x = 3.5F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    vr::ecs::Appearance<vr::ecs::Dim3> appearance_3d{};
    vr::ecs::AppearanceSystem<vr::ecs::Dim3>::Initialize(appearance_3d);
    VR_REQUIRE(VisualSystem3D::ApplyToAppearance(appearance_track_3d, appearance_3d));
    VR_CHECK(NearlyEqual(appearance_3d.style.emissive_intensity, 3.5F));
}

VR_TEST_CASE(EcsAnimationVisualTrackSystem_semantic_target_classification,
             "unit;core;ecs;animation") {
    using VisualSystem2D = vr::ecs::AnimationVisualTrackSystem<vr::ecs::Dim2>;
    using VisualSystem3D = vr::ecs::AnimationVisualTrackSystem<vr::ecs::Dim3>;

    VR_CHECK(VisualSystem2D::IsSurfaceSourceSemantic(vr::ecs::VisualTrackSemantic::surface_uv_rect));
    VR_CHECK(VisualSystem2D::IsSurfaceTargetSemantic(vr::ecs::VisualTrackSemantic::surface_uv_rect));
    VR_CHECK(!VisualSystem2D::IsAppearanceTargetSemantic(vr::ecs::VisualTrackSemantic::surface_uv_rect));

    VR_CHECK(VisualSystem2D::IsSurfaceFallbackAppearanceSemantic(vr::ecs::VisualTrackSemantic::appearance_color));
    VR_CHECK(VisualSystem2D::IsSurfaceTargetSemantic(vr::ecs::VisualTrackSemantic::appearance_color));
    VR_CHECK(VisualSystem2D::IsAppearanceTargetSemantic(vr::ecs::VisualTrackSemantic::appearance_color));

    VR_CHECK(VisualSystem3D::IsAppearanceSemantic(vr::ecs::VisualTrackSemantic::appearance_emissive_intensity));
    VR_CHECK(VisualSystem3D::IsAppearanceTargetSemantic(vr::ecs::VisualTrackSemantic::appearance_emissive_intensity));
    VR_CHECK(!VisualSystem3D::IsSurfaceTargetSemantic(vr::ecs::VisualTrackSemantic::appearance_emissive_intensity));
}

VR_TEST_CASE(EcsAnimationPathMotionSystem_samples_bezier_and_applies_transform,
             "unit;core;ecs;animation") {
    using PathSystem2D = vr::ecs::AnimationPathMotionSystem<vr::ecs::Dim2>;
    using AnimationType = vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::PathMotion>;
    using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

    AnimationType component{};
    PathSystem2D::Initialize(component);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PathMotion>::SetDurationSeconds(component, 1.0F);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PathMotion>::SetTimeSeconds(component, 0.5F);

    const std::array<vr::ecs::AnimationBezierSegment<vr::ecs::Dim2>, 1U> segments{{
        {
            .p0 = vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
            .p1 = vr::ecs::Float2{.x = 1.0F, .y = 0.0F},
            .p2 = vr::ecs::Float2{.x = 2.0F, .y = 0.0F},
            .p3 = vr::ecs::Float2{.x = 3.0F, .y = 0.0F},
        },
    }};
    VR_REQUIRE(PathSystem2D::SampleSpline(component,
                                          vr::ecs::AnimationSplineView<vr::ecs::Dim2>{.segments = segments.data(), .segment_count = 1U}));
    VR_CHECK(NearlyEqual(component.sample.position.x, 1.5F));
    VR_CHECK(NearlyEqual(component.sample.rotation_radians, 0.0F));

    vr::ecs::Transform<vr::ecs::Dim2> transform{};
    TransformSystem2D::Initialize(transform);
    VR_REQUIRE(PathSystem2D::ApplyToTransform(component, transform));
    VR_CHECK(NearlyEqual(transform.style.position.x, 1.5F));
}

VR_TEST_CASE(EcsAnimationCameraTrackSystem_applies_pose_and_projection,
             "unit;core;ecs;animation") {
    using CameraTrackSystem3D = vr::ecs::AnimationCameraTrackSystem<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

    vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::CameraTrack> track{};
    CameraTrackSystem3D::Initialize(track);
    track.sample.position = vr::ecs::Float3{.x = 1.0F, .y = 2.0F, .z = 3.0F};
    track.sample.rotation = vr::ecs::Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    track.sample.vertical_fov_radians = 1.1F;
    track.sample.shake_offset = vr::ecs::Float3{.x = 0.5F, .y = 0.0F, .z = -0.5F};
    CameraTrackSystem3D::SetShakeWeight(track, 2.0F);
    CameraTrackSystem3D::SetApplyFlags(track,
                                       vr::ecs::animation_camera_apply_transform_position_flag |
                                           vr::ecs::animation_camera_apply_vertical_fov_flag |
                                           vr::ecs::animation_camera_apply_shake_offset_flag);

    vr::ecs::Transform<vr::ecs::Dim3> transform{};
    TransformSystem3D::Initialize(transform);
    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    CameraSystem3D::Initialize(camera);

    VR_REQUIRE(CameraTrackSystem3D::Apply(track, camera, transform));
    VR_CHECK(NearlyEqual(transform.style.position.x, 2.0F));
    VR_CHECK(NearlyEqual(transform.style.position.y, 2.0F));
    VR_CHECK(NearlyEqual(transform.style.position.z, 2.0F));
    VR_CHECK(NearlyEqual(camera.style.vertical_fov_radians, 1.1F));
}

} // namespace


