#include "support/test_framework.hpp"
#include "vr/animation/animation_morph_host.hpp"
#include "vr/animation/animation_skeletal_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_morph_evaluation_system.hpp"
#include "vr/ecs/system/animation_resource_track_system.hpp"
#include "vr/ecs/system/animation_skeletal_evaluation_system.hpp"

#include <array>
#include <cmath>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

VR_TEST_CASE(EcsAnimationSkeletalEvaluationSystem_samples_clip_into_pose_output,
             "unit;core;ecs;animation") {
    vr::animation::SkeletalAnimationHost host{};
    host.Initialize();

    const std::array<vr::ecs::SkeletalJointPose<vr::ecs::Dim2>, 2U> base_pose{{
        {
            .position = vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
            .rotation_radians = 0.0F,
            .scale = vr::ecs::Float2{.x = 1.0F, .y = 1.0F},
        },
        {
            .position = vr::ecs::Float2{.x = 5.0F, .y = 0.0F},
            .rotation_radians = 0.0F,
            .scale = vr::ecs::Float2{.x = 1.0F, .y = 1.0F},
        },
    }};
    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> rotation_keys{{
        {.time_s = 0.0F, .value = 0.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 1.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::SkeletalTrackDesc<vr::ecs::Dim2>, 1U> tracks{{
        {
            .joint_index = 1U,
            .reserved0 = 0U,
            .position_curve = {.keyframes = nullptr, .keyframe_count = 0U},
            .rotation_curve = {.keyframes = rotation_keys.data(), .keyframe_count = static_cast<std::uint32_t>(rotation_keys.size())},
            .scale_curve = {.keyframes = nullptr, .keyframe_count = 0U},
        },
    }};
    const vr::ecs::AnimationClipHandle clip_handle = host.UpsertClip(vr::animation::SkeletalClipDesc<vr::ecs::Dim2>{
        .clip_id = 31U,
        .duration_s = 1.0F,
        .base_pose = base_pose.data(),
        .joint_count = static_cast<std::uint32_t>(base_pose.size()),
        .tracks = tracks.data(),
        .track_count = static_cast<std::uint32_t>(tracks.size()),
    });

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::Skeletal> animation{};
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim2, vr::ecs::Skeletal>::Initialize(animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::Skeletal>::SetClipHandle(animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::Skeletal>::SetDurationSeconds(animation, 1.0F);
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim2, vr::ecs::Skeletal>::SetTarget(
        animation,
        {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::custom, .reserved0 = 0U, .sub_index = 0U});

    std::array<vr::ecs::SkeletalPoseOutputState<vr::ecs::Dim2>, 1U> outputs{};
    std::array<vr::ecs::SkeletalJointPose<vr::ecs::Dim2>, 4U> pose_buffer{};
    outputs[0U].joints = pose_buffer.data();
    outputs[0U].joint_count = static_cast<std::uint32_t>(pose_buffer.size());

    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim2> context{};
    context.skeletal_outputs = {.components = outputs.data(), .count = static_cast<std::uint32_t>(outputs.size())};

    VR_REQUIRE(vr::ecs::AnimationSkeletalEvaluationSystem<vr::ecs::Dim2>::Tick(animation, host, context, 0.5F));
    VR_CHECK(NearlyEqual(outputs[0U].joints[0U].position.x, 0.0F));
    VR_CHECK(NearlyEqual(outputs[0U].joints[1U].rotation_radians, 0.5F));
    VR_CHECK(outputs[0U].sampled_joint_count == 2U);
}

VR_TEST_CASE(EcsAnimationMorphEvaluationSystem_samples_clip_into_weight_output,
             "unit;core;ecs;animation") {
    vr::animation::MorphAnimationHost host{};
    host.Initialize();

    const std::array<float, 3U> base_weights{0.1F, 0.2F, 0.3F};
    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> weight_keys{{
        {.time_s = 0.0F, .value = 0.3F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 0.9F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::MorphTrackDesc, 1U> tracks{{
        {
            .target_index = 1U,
            .reserved0 = 0U,
            .weight_curve = {.keyframes = weight_keys.data(), .keyframe_count = static_cast<std::uint32_t>(weight_keys.size())},
        },
    }};
    const vr::ecs::AnimationClipHandle clip_handle = host.UpsertClip(vr::animation::MorphClipDesc{
        .clip_id = 41U,
        .duration_s = 1.0F,
        .base_weights = base_weights.data(),
        .weight_count = static_cast<std::uint32_t>(base_weights.size()),
        .tracks = tracks.data(),
        .track_count = static_cast<std::uint32_t>(tracks.size()),
    });

    vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::Morph> animation{};
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim3, vr::ecs::Morph>::Initialize(animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::Morph>::SetClipHandle(animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::Morph>::SetDurationSeconds(animation, 1.0F);
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim3, vr::ecs::Morph>::SetTarget(
        animation,
        {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::custom, .reserved0 = 0U, .sub_index = 0U});

    std::array<vr::ecs::MorphWeightOutputState, 1U> outputs{};
    std::array<float, 8U> weights{};
    outputs[0U].weights = weights.data();
    outputs[0U].weight_count = static_cast<std::uint32_t>(weights.size());

    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim3> context{};
    context.morph_outputs = {.components = outputs.data(), .count = static_cast<std::uint32_t>(outputs.size())};

    VR_REQUIRE(vr::ecs::AnimationMorphEvaluationSystem<vr::ecs::Dim3>::Tick(animation, host, context, 0.5F));
    VR_CHECK(NearlyEqual(outputs[0U].weights[0U], 0.1F));
    VR_CHECK(NearlyEqual(outputs[0U].weights[1U], 0.6F));
    VR_CHECK(NearlyEqual(outputs[0U].weights[2U], 0.3F));
    VR_CHECK(outputs[0U].sampled_weight_count == 3U);
}

} // namespace

