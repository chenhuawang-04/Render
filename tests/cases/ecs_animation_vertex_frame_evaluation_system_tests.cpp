#include "support/test_framework.hpp"
#include "vr/animation/animation_frame_sequence_host.hpp"
#include "vr/animation/animation_vertex_deform_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_frame_sequence_evaluation_system.hpp"
#include "vr/ecs/system/animation_resource_track_system.hpp"
#include "vr/ecs/system/animation_vertex_deform_evaluation_system.hpp"

#include <array>
#include <cmath>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

VR_TEST_CASE(EcsAnimationVertexDeformEvaluationSystem_samples_clip_into_parameter_output,
             "unit;core;ecs;animation") {
    vr::animation::VertexDeformAnimationHost host{};
    host.Initialize();

    const std::array<vr::ecs::Float4, 3U> base_parameters{{
        {.x = 0.2F, .y = 0.0F, .z = 0.0F, .w = 0.0F},
        {.x = 0.4F, .y = 0.0F, .z = 0.0F, .w = 0.0F},
        {.x = 0.6F, .y = 0.0F, .z = 0.0F, .w = 0.0F},
    }};
    const std::array<vr::ecs::AnimationKeyframe<vr::ecs::Float4>, 2U> parameter_keys{{
        {.time_s = 0.0F, .value = vr::ecs::Float4{.x = 2.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = vr::ecs::Float4{.x = 4.0F, .y = 3.0F, .z = 0.0F, .w = 0.0F}, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const std::array<vr::animation::VertexDeformTrackDesc, 1U> tracks{{
        {
            .parameter_index = 1U,
            .reserved0 = 0U,
            .parameter_curve = {.keyframes = parameter_keys.data(), .keyframe_count = static_cast<std::uint32_t>(parameter_keys.size())},
        },
    }};
    const vr::ecs::AnimationClipHandle clip_handle = host.UpsertClip(vr::animation::VertexDeformClipDesc{
        .clip_id = 71U,
        .duration_s = 1.0F,
        .base_parameters = base_parameters.data(),
        .parameter_count = static_cast<std::uint32_t>(base_parameters.size()),
        .tracks = tracks.data(),
        .track_count = static_cast<std::uint32_t>(tracks.size()),
    });

    vr::ecs::Animation<vr::ecs::Dim3, vr::ecs::VertexDeform> animation{};
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim3, vr::ecs::VertexDeform>::Initialize(animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::VertexDeform>::SetClipHandle(animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim3, vr::ecs::VertexDeform>::SetDurationSeconds(animation, 1.0F);
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim3, vr::ecs::VertexDeform>::SetTarget(
        animation,
        {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::custom, .reserved0 = 0U, .sub_index = 0U});

    std::array<vr::ecs::VertexDeformOutputState, 1U> outputs{};
    std::array<vr::ecs::Float4, 4U> parameters{};
    outputs[0U].parameters = parameters.data();
    outputs[0U].parameter_count = static_cast<std::uint32_t>(parameters.size());

    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim3> context{};
    context.vertex_deform_outputs = {.components = outputs.data(), .count = static_cast<std::uint32_t>(outputs.size())};

    VR_REQUIRE(vr::ecs::AnimationVertexDeformEvaluationSystem<vr::ecs::Dim3>::Tick(animation, host, context, 0.5F));
    VR_CHECK(NearlyEqual(outputs[0U].parameters[0U].x, 0.2F));
    VR_CHECK(NearlyEqual(outputs[0U].parameters[1U].x, 3.0F));
    VR_CHECK(NearlyEqual(outputs[0U].parameters[1U].y, 2.0F));
    VR_CHECK(NearlyEqual(outputs[0U].parameters[2U].x, 0.6F));
    VR_CHECK(outputs[0U].sampled_parameter_count == 3U);
}

VR_TEST_CASE(EcsAnimationFrameSequenceEvaluationSystem_samples_clip_into_frame_output,
             "unit;core;ecs;animation") {
    vr::animation::FrameSequenceAnimationHost host{};
    host.Initialize();

    const std::array<vr::ecs::AnimationKeyframe<float>, 2U> frame_keys{{
        {.time_s = 0.0F, .value = 0.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
        {.time_s = 1.0F, .value = 7.0F, .interpolation_mode = vr::ecs::AnimationInterpolationMode::linear, .reserved0 = 0U, .reserved1 = 0U},
    }};
    const vr::ecs::AnimationClipHandle clip_handle = host.UpsertClip(vr::animation::FrameSequenceClipDesc{
        .clip_id = 81U,
        .duration_s = 1.0F,
        .frame_count = 8U,
        .frame_curve = {.keyframes = frame_keys.data(), .keyframe_count = static_cast<std::uint32_t>(frame_keys.size())},
    });

    vr::ecs::Animation<vr::ecs::Dim2, vr::ecs::FrameSequence> animation{};
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim2, vr::ecs::FrameSequence>::Initialize(animation);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::FrameSequence>::SetClipHandle(animation, clip_handle);
    vr::ecs::AnimationClockSystem<vr::ecs::Dim2, vr::ecs::FrameSequence>::SetDurationSeconds(animation, 1.0F);
    vr::ecs::AnimationResourceTrackSystem<vr::ecs::Dim2, vr::ecs::FrameSequence>::SetTarget(
        animation,
        {.entity_id = 0U, .slot = 0U, .domain = vr::ecs::AnimationTargetDomain::custom, .reserved0 = 0U, .sub_index = 0U});

    std::array<vr::ecs::FrameSequenceOutputState, 1U> outputs{};
    vr::ecs::AnimationEvaluationContext<vr::ecs::Dim2> context{};
    context.frame_sequence_outputs = {.components = outputs.data(), .count = static_cast<std::uint32_t>(outputs.size())};

    VR_REQUIRE(vr::ecs::AnimationFrameSequenceEvaluationSystem<vr::ecs::Dim2>::Tick(animation, host, context, 0.5F));
    VR_CHECK(outputs[0U].frame_index_a == 3U);
    VR_CHECK(outputs[0U].frame_index_b == 4U);
    VR_CHECK(NearlyEqual(outputs[0U].blend_alpha, 0.5F));
    VR_CHECK(NearlyEqual(outputs[0U].normalized_time, 0.5F));
    VR_CHECK(NearlyEqual(outputs[0U].frame_position, 3.5F));
    VR_CHECK(outputs[0U].frame_count == 8U);
}

} // namespace

