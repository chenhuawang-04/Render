#include "support/test_framework.hpp"
#include "vr/animation/animation_frame_sequence_host.hpp"
#include "vr/animation/animation_vertex_deform_host.hpp"

#include <array>
#include <cmath>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

VR_TEST_CASE(AnimationVertexDeformHost_upsert_find_sample_and_remove,
             "unit;core;animation;host") {
    vr::animation::VertexDeformAnimationHost host{};
    host.Initialize();

    const std::array<vr::ecs::Float4, 2U> base_parameters{{
        {.x = 0.1F, .y = 0.2F, .z = 0.3F, .w = 0.4F},
        {.x = 1.0F, .y = 1.0F, .z = 1.0F, .w = 1.0F},
    }};
    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float4>, 2U> parameter_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float4{.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float4{.x = 3.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::VertexDeformTrackDesc, 1U> tracks{{
        {
            .parameter_index = 1U,
            .reserved0 = 0U,
            .parameter_curve = {.keyframes = parameter_keys.data(), .keyframe_count = static_cast<std::uint32_t>(parameter_keys.size())},
        },
    }};

    const vr::ecs::AnimationClipHandle handle = host.UpsertClip(vr::animation::VertexDeformClipDesc{
        .clip_id = 51U,
        .duration_s = 1.0F,
        .base_parameters = base_parameters.data(),
        .parameter_count = static_cast<std::uint32_t>(base_parameters.size()),
        .tracks = tracks.data(),
        .track_count = static_cast<std::uint32_t>(tracks.size()),
    });
    const auto* clip = host.FindClipByHandle(handle);
    VR_REQUIRE(clip != nullptr);

    const vr::ecs::Float4* parameters = host.BaseParameters(*clip);
    VR_REQUIRE(parameters != nullptr);
    VR_CHECK(NearlyEqual(parameters[0U].x, 0.1F));

    const auto* clip_tracks = host.Tracks(*clip);
    VR_REQUIRE(clip_tracks != nullptr);
    const vr::ecs::Float4 sampled = vr::ecs::AnimationCurveSystem::Sample(
        host.ParameterCurveView(clip_tracks[0U]),
        0.5F);
    VR_CHECK(NearlyEqual(sampled.x, 2.0F));

    VR_REQUIRE(host.RemoveClip(51U));
    VR_CHECK(host.FindClipByHandle(handle) == nullptr);
}

VR_TEST_CASE(AnimationFrameSequenceHost_upsert_find_sample_and_remove,
             "unit;core;animation;host") {
    vr::animation::FrameSequenceAnimationHost host{};
    host.Initialize();

    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> frame_keys{{
        {.time_s = 0.0F, .value = 0.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 7.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};

    const vr::ecs::AnimationClipHandle handle = host.UpsertClip(vr::animation::FrameSequenceClipDesc{
        .clip_id = 61U,
        .duration_s = 1.0F,
        .frame_count = 8U,
        .frame_curve = {.keyframes = frame_keys.data(), .keyframe_count = static_cast<std::uint32_t>(frame_keys.size())},
    });
    const auto* clip = host.FindClipByHandle(handle);
    VR_REQUIRE(clip != nullptr);
    VR_CHECK(clip->frame_count == 8U);

    const float sampled = vr::ecs::AnimationCurveSystem::Sample(host.FrameCurveView(*clip), 0.5F);
    VR_CHECK(NearlyEqual(sampled, 3.5F));

    VR_REQUIRE(host.RemoveClip(61U));
    VR_CHECK(host.FindClipById(61U) == nullptr);
}

} // namespace
