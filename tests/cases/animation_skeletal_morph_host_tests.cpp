#include "support/test_framework.hpp"
#include "vr/animation/animation_morph_host.hpp"
#include "vr/animation/animation_skeletal_host.hpp"

#include <array>
#include <cmath>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

VR_TEST_CASE(AnimationSkeletalHost_upsert_find_sample_and_remove,
             "unit;core;animation;host") {
    vr::animation::SkeletalAnimationHost host{};
    host.Initialize();

    const std::array<vr::ecs::SkeletalJointPose<vr::ecs::Dim3>, 2U> base_pose{{
        {
            .position = vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            .rotation = vr::ecs::Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
        },
        {
            .position = vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            .rotation = vr::ecs::Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
        },
    }};

    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float3>, 2U> position_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float3{.x = 0.0F, .y = 3.0F, .z = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::SkeletalTrackDesc<vr::ecs::Dim3>, 1U> tracks{{
        {
            .joint_index = 1U,
            .reserved0 = 0U,
            .position_curve = {.keyframes = position_keys.data(), .keyframe_count = static_cast<std::uint32_t>(position_keys.size())},
            .rotation_curve = {.keyframes = nullptr, .keyframe_count = 0U},
            .scale_curve = {.keyframes = nullptr, .keyframe_count = 0U},
        },
    }};

    const vr::ecs::AnimationClipHandle handle = host.UpsertClip(vr::animation::SkeletalClipDesc<vr::ecs::Dim3>{
        .clip_id = 11U,
        .duration_s = 1.0F,
        .base_pose = base_pose.data(),
        .joint_count = static_cast<std::uint32_t>(base_pose.size()),
        .tracks = tracks.data(),
        .track_count = static_cast<std::uint32_t>(tracks.size()),
    });
    const auto* clip = host.FindClip3DByHandle(handle);
    VR_REQUIRE(clip != nullptr);
    VR_CHECK(host.Stats().clip_count == 1U);
    VR_CHECK(host.Stats().clip3d_count == 1U);

    const auto* clip_tracks = host.Tracks3D(*clip);
    VR_REQUIRE(clip_tracks != nullptr);
    const vr::ecs::Float3 sampled = vr::ecs::AnimationCurveSystem::Sample(
        host.PositionCurveView(clip_tracks[0U]),
        0.5F);
    VR_CHECK(NearlyEqual(sampled.y, 2.0F));

    VR_REQUIRE(host.RemoveClip(11U));
    VR_CHECK(host.FindClipByHandle(handle) == nullptr);
}

VR_TEST_CASE(AnimationMorphHost_upsert_find_sample_and_remove,
             "unit;core;animation;host") {
    vr::animation::MorphAnimationHost host{};
    host.Initialize();

    const std::array<float, 3U> base_weights{0.1F, 0.2F, 0.3F};
    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> weight_keys{{
        {.time_s = 0.0F, .value = 0.2F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 0.8F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::MorphTrackDesc, 1U> tracks{{
        {
            .target_index = 2U,
            .reserved0 = 0U,
            .weight_curve = {.keyframes = weight_keys.data(), .keyframe_count = static_cast<std::uint32_t>(weight_keys.size())},
        },
    }};

    const vr::ecs::AnimationClipHandle handle = host.UpsertClip(vr::animation::MorphClipDesc{
        .clip_id = 21U,
        .duration_s = 1.0F,
        .base_weights = base_weights.data(),
        .weight_count = static_cast<std::uint32_t>(base_weights.size()),
        .tracks = tracks.data(),
        .track_count = static_cast<std::uint32_t>(tracks.size()),
    });
    const auto* clip = host.FindClipByHandle(handle);
    VR_REQUIRE(clip != nullptr);
    const float* weights = host.BaseWeights(*clip);
    VR_REQUIRE(weights != nullptr);
    VR_CHECK(NearlyEqual(weights[1U], 0.2F));

    const auto* morph_tracks = host.Tracks(*clip);
    VR_REQUIRE(morph_tracks != nullptr);
    const float sampled = vr::ecs::AnimationCurveSystem::Sample(host.WeightCurveView(morph_tracks[0U]), 0.5F);
    VR_CHECK(NearlyEqual(sampled, 0.5F));

    VR_REQUIRE(host.RemoveClip(21U));
    VR_CHECK(host.FindClipById(21U) == nullptr);
}

} // namespace

