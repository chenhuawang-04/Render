#include "support/test_framework.hpp"
#include "vr/animation/animation_clip_host.hpp"
#include "vr/animation/animation_path_host.hpp"
#include "vr/ecs/system/animation_camera_evaluation_system.hpp"
#include "vr/ecs/system/animation_material_evaluation_system.hpp"
#include "vr/ecs/system/animation_path_evaluation_system.hpp"
#include "vr/ecs/system/animation_property_evaluation_system.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <array>
#include <cmath>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

VR_TEST_CASE(EcsAnimationPropertyEvaluationSystem_samples_clip_and_applies_transform,
             "unit;core;ecs;animation") {
    vr::animation::AnimationClipHost clip_host{};
    clip_host.Initialize();

    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float3>, 2U> keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float3{.x = 10.0F, .y = 20.0F, .z = 30.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const vr::animation::PropertyFloat3ChannelDesc channel{
        .semantic = vr::ecs::PropertyTrackSemantic::transform_local_position,
        .channel_mask = 0x7U,
        .reserved0 = 0U,
        .curve = vr::ecs::AnimationCurveView<vr::ecs::Float3>{.keyframes = keys.data(), .keyframe_count = 2U},
    };
    vr::animation::PropertyAnimationClipDesc desc{};
    desc.clip_id = 1U;
    desc.duration_s = 1.0F;
    desc.float3_channels = &channel;
    desc.float3_channel_count = 1U;
    const vr::ecs::AnimationClipHandle clip_handle = clip_host.UpsertPropertyClip(desc);

    vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::PropertyTrack> animation{};
    vr::ecs::AnimationPropertyTrackSystem<vr::ecs::Dim3>::Initialize(animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::PropertyTrack>::SetClipHandle(animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::PropertyTrack>::SetDurationSeconds(animation, 1.0F);
    vr::ecs::AnimationPropertyTrackSystem<vr::ecs::Dim3>::SetSemantic(animation,
                                                                       vr::ecs::PropertyTrackSemantic::transform_local_position);
    vr::ecs::AnimationPropertyTrackSystem<vr::ecs::Dim3>::SetValueEncoding(animation,
                                                                            vr::ecs::AnimationValueEncoding::float3);
    vr::ecs::AnimationPropertyTrackSystem<vr::ecs::Dim3>::SetTarget(animation,
                                                                     vr::ecs::AnimationTargetRef{
                                                                         .entity_id = 0U,
                                                                         .slot = 0U,
                                                                         .domain = vr::ecs::AnimationTargetDomain::transform,
                                                                         .reserved0 = 0U,
                                                                         .sub_index = 0U,
                                                                     });

    std::array<vr::ecs::Transform<vr::ecs::Dim3>, 1U> transforms{};
    vr::ecs::TransformSystem<vr::ecs::Dim3>::Initialize(transforms[0U]);
    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim3> context{};
    context.transforms = {.components = transforms.data(), .count = static_cast<std::uint32_t>(transforms.size())};

    VR_REQUIRE(vr::ecs::AnimationPropertyEvaluationSystem<vr::ecs::Dim3>::Tick(animation, clip_host, context, 0.5F));
    VR_CHECK(NearlyEqual(transforms[0U].style.position.x, 5.0F));
    VR_CHECK(NearlyEqual(transforms[0U].style.position.y, 10.0F));
    VR_CHECK(NearlyEqual(transforms[0U].style.position.z, 15.0F));
}

VR_TEST_CASE(EcsAnimationMaterialEvaluationSystem_samples_clip_and_applies_surface_and_appearance,
             "unit;core;ecs;animation") {
    vr::animation::AnimationClipHost clip_host{};
    clip_host.Initialize();

    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> opacity_keys{{
        {.time_s = 0.0F, .value = 1.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 0.5F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const vr::animation::MaterialScalarChannelDesc opacity_channel{
        .semantic = vr::ecs::MaterialTrackSemantic::surface_opacity,
        .channel_mask = 0x1U,
        .reserved0 = 0U,
        .curve = vr::ecs::AnimationCurveView<float>{.keyframes = opacity_keys.data(), .keyframe_count = 2U},
    };

    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Rgba8>, 2U> color_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Rgba8{255U, 0U, 0U, 255U}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Rgba8{0U, 0U, 255U, 255U}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const vr::animation::MaterialColorChannelDesc color_channel{
        .semantic = vr::ecs::MaterialTrackSemantic::appearance_color,
        .channel_mask = 0xFU,
        .reserved0 = 0U,
        .curve = vr::ecs::AnimationCurveView<vr::ecs::Rgba8>{.keyframes = color_keys.data(), .keyframe_count = 2U},
    };

    vr::animation::MaterialAnimationClipDesc desc{};
    desc.clip_id = 2U;
    desc.duration_s = 1.0F;
    desc.scalar_channels = &opacity_channel;
    desc.scalar_channel_count = 1U;
    desc.color_channels = &color_channel;
    desc.color_channel_count = 1U;
    const vr::ecs::AnimationClipHandle clip_handle = clip_host.UpsertMaterialClip(desc);

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::MaterialTrack> surface_animation{};
    vr::ecs::AnimationMaterialTrackSystem<vr::ecs::Dim2>::Initialize(surface_animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::MaterialTrack>::SetClipHandle(surface_animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::MaterialTrack>::SetDurationSeconds(surface_animation, 1.0F);
    vr::ecs::AnimationMaterialTrackSystem<vr::ecs::Dim2>::SetSemantic(surface_animation,
                                                                       vr::ecs::MaterialTrackSemantic::surface_opacity);
    vr::ecs::AnimationMaterialTrackSystem<vr::ecs::Dim2>::SetTarget(surface_animation,
                                                                     {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::surface, .reserved0 = 0U, .sub_index = 0U});
    vr::ecs::Surface<vr::ecs::Dim2> surface{};
    vr::ecs::SurfaceSystem<vr::ecs::Dim2>::Initialize(surface);
    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim2> context{};
    context.surfaces = {.components = &surface, .count = 1U};
    VR_REQUIRE(vr::ecs::AnimationMaterialEvaluationSystem<vr::ecs::Dim2>::Tick(surface_animation, clip_host, context, 0.5F));
    VR_CHECK(NearlyEqual(surface.style.opacity, 0.75F));

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::MaterialTrack> appearance_animation{};
    vr::ecs::AnimationMaterialTrackSystem<vr::ecs::Dim2>::Initialize(appearance_animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::MaterialTrack>::SetClipHandle(appearance_animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::MaterialTrack>::SetDurationSeconds(appearance_animation, 1.0F);
    vr::ecs::AnimationMaterialTrackSystem<vr::ecs::Dim2>::SetSemantic(appearance_animation,
                                                                       vr::ecs::MaterialTrackSemantic::appearance_color);
    vr::ecs::AnimationMaterialTrackSystem<vr::ecs::Dim2>::SetValueEncoding(appearance_animation,
                                                                            vr::ecs::AnimationValueEncoding::color_rgba8);
    vr::ecs::AnimationMaterialTrackSystem<vr::ecs::Dim2>::SetTarget(appearance_animation,
                                                                     {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::appearance, .reserved0 = 0U, .sub_index = 0U});
    vr::ecs::Appearance<vr::ecs::Dim2> appearance{};
    vr::ecs::AppearanceSystem<vr::ecs::Dim2>::Initialize(appearance);
    context.appearances = {.components = &appearance, .count = 1U};
    VR_REQUIRE(vr::ecs::AnimationMaterialEvaluationSystem<vr::ecs::Dim2>::Tick(appearance_animation, clip_host, context, 0.5F));
    VR_CHECK(appearance.style.fill_color.r == 128U);
    VR_CHECK(appearance.style.fill_color.b == 128U);
}

VR_TEST_CASE(EcsAnimationPathEvaluationSystem_samples_path_host_and_applies_transform,
             "unit;core;ecs;animation") {
    vr::animation::AnimationPathHost path_host{};
    path_host.Initialize();

    const std::array<vr::ecs::AnimationBezierSegment<vr::ecs::Dim2>, 1U> segments{{
        {
            .p0 = vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
            .p1 = vr::ecs::Float2{.x = 1.0F, .y = 0.0F},
            .p2 = vr::ecs::Float2{.x = 2.0F, .y = 0.0F},
            .p3 = vr::ecs::Float2{.x = 3.0F, .y = 0.0F},
        },
    }};
    const vr::ecs::AnimationPathHandle handle = path_host.UpsertPath(vr::animation::AnimationPath2DDesc{
        .path_id = 7U,
        .segments = segments.data(),
        .segment_count = static_cast<std::uint32_t>(segments.size()),
    });

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::PathMotion> animation{};
    vr::ecs::AnimationPathMotionSystem<vr::ecs::Dim2>::Initialize(animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PathMotion>::SetTimeSeconds(animation, 0.5F);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::PathMotion>::SetDurationSeconds(animation, 1.0F);
    vr::ecs::AnimationPathMotionSystem<vr::ecs::Dim2>::SetPathHandle(animation, handle);
    animation.binding.target = {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::transform, .reserved0 = 0U, .sub_index = 0U};

    vr::ecs::Transform<vr::ecs::Dim2> transform{};
    vr::ecs::TransformSystem<vr::ecs::Dim2>::Initialize(transform);
    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim2> context{};
    context.transforms = {.components = &transform, .count = 1U};

    VR_REQUIRE(vr::ecs::AnimationPathEvaluationSystem<vr::ecs::Dim2>::Tick(animation, path_host, context, 0.0F));
    VR_CHECK(NearlyEqual(transform.style.position.x, 1.5F));
}

VR_TEST_CASE(EcsAnimationCameraEvaluationSystem_samples_clip_and_applies_camera_and_transform,
             "unit;core;ecs;animation") {
    vr::animation::AnimationClipHost clip_host{};
    clip_host.Initialize();

    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float3>, 2U> position_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float3{.x = 2.0F, .y = 4.0F, .z = 6.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const vr::animation::CameraFloat3ChannelDesc position_channel{
        .semantic = vr::ecs::CameraTrackSemantic::transform_position,
        .channel_mask = 0x7U,
        .reserved0 = 0U,
        .curve = vr::ecs::AnimationCurveView<vr::ecs::Float3>{.keyframes = position_keys.data(), .keyframe_count = 2U},
    };

    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> fov_keys{{
        {.time_s = 0.0F, .value = 1.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 1.4F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const vr::animation::CameraScalarChannelDesc fov_channel{
        .semantic = vr::ecs::CameraTrackSemantic::vertical_fov,
        .channel_mask = 0x1U,
        .reserved0 = 0U,
        .curve = vr::ecs::AnimationCurveView<float>{.keyframes = fov_keys.data(), .keyframe_count = 2U},
    };

    vr::animation::CameraAnimationClipDesc desc{};
    desc.clip_id = 9U;
    desc.duration_s = 1.0F;
    desc.scalar_channels = &fov_channel;
    desc.scalar_channel_count = 1U;
    desc.float3_channels = &position_channel;
    desc.float3_channel_count = 1U;
    const vr::ecs::AnimationClipHandle clip_handle = clip_host.UpsertCameraClip(desc);

    vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::CameraTrack> animation{};
    vr::ecs::AnimationCameraTrackSystem<vr::ecs::Dim3>::Initialize(animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::CameraTrack>::SetClipHandle(animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::CameraTrack>::SetDurationSeconds(animation, 1.0F);
    animation.binding.target = {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::camera, .reserved0 = 0U, .sub_index = 0U};

    vr::ecs::Transform<vr::ecs::Dim3> transform{};
    vr::ecs::TransformSystem<vr::ecs::Dim3>::Initialize(transform);
    vr::ecs::Camera<vr::ecs::Dim3> camera{};
    vr::ecs::CameraSystem<vr::ecs::Dim3>::Initialize(camera);
    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim3> context{};
    context.transforms = {.components = &transform, .count = 1U};
    context.cameras = {.components = &camera, .count = 1U};

    VR_REQUIRE(vr::ecs::AnimationCameraEvaluationSystem<vr::ecs::Dim3>::Tick(animation, clip_host, context, 0.5F));
    VR_CHECK(NearlyEqual(transform.style.position.x, 1.0F));
    VR_CHECK(NearlyEqual(transform.style.position.y, 2.0F));
    VR_CHECK(NearlyEqual(transform.style.position.z, 3.0F));
    VR_CHECK(NearlyEqual(camera.style.vertical_fov_radians, 1.2F));
}

} // namespace
