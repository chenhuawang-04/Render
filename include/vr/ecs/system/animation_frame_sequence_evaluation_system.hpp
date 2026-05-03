#pragma once

#include "vr/animation/animation_frame_sequence_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_resource_track_system.hpp"

#include <algorithm>
#include <cmath>

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationFrameSequenceEvaluationSystem final {
public:
    using AnimationType = Animation<DimensionT, FrameSequence>;
    using TrackSystem = AnimationResourceTrackSystem<DimensionT, FrameSequence>;
    using ClockSystem = AnimationClockSystem<DimensionT, FrameSequence>;

    [[nodiscard]] static bool Tick(AnimationType& component_,
                                   const animation::FrameSequenceAnimationHost& host_,
                                   AnimationEvaluationContext<DimensionT>& context_,
                                   float delta_time_s_) noexcept {
        if (delta_time_s_ != 0.0F) {
            (void)ClockSystem::Advance(component_, delta_time_s_);
        }

        FrameSequenceOutputState* output = context_.frame_sequence_outputs.Resolve(component_.binding.target.entity_id);
        if (output == nullptr) {
            return false;
        }
        return SampleAndWrite(component_, host_, *output);
    }

    [[nodiscard]] static bool SampleAndWrite(AnimationType& component_,
                                             const animation::FrameSequenceAnimationHost& host_,
                                             FrameSequenceOutputState& output_) noexcept {
        const animation::FrameSequenceClipRecord* clip = host_.FindClipByHandle(component_.playback.clip_handle);
        if (clip == nullptr || clip->frame_count == 0U) {
            return false;
        }

        const float duration_s = std::max(1e-6F, component_.playback.duration_s);
        const float normalized_time = std::clamp(component_.playback.time_s / duration_s, 0.0F, 1.0F);
        const auto curve = host_.FrameCurveView(*clip);
        const float max_frame_index = static_cast<float>(clip->frame_count - 1U);
        float frame_position = 0.0F;
        if (curve.keyframe_count > 0U) {
            frame_position = AnimationCurveSystem::Sample(curve, component_.playback.time_s);
        } else {
            frame_position = normalized_time * max_frame_index;
        }
        frame_position = std::clamp(frame_position, 0.0F, max_frame_index);

        const float frame_floor = std::floor(frame_position);
        const std::uint32_t frame_index_a = static_cast<std::uint32_t>(frame_floor);
        const std::uint32_t frame_index_b = std::min(frame_index_a + 1U, clip->frame_count - 1U);

        output_.frame_index_a = frame_index_a;
        output_.frame_index_b = frame_index_b;
        output_.frame_count = clip->frame_count;
        output_.blend_alpha = std::clamp(frame_position - frame_floor, 0.0F, 1.0F);
        output_.normalized_time = normalized_time;
        output_.frame_position = frame_position;
        ++output_.revision;

        TrackSystem::SetSampleParameters(component_,
                                         Float4{
                                             .x = static_cast<float>(frame_index_a),
                                             .y = static_cast<float>(frame_index_b),
                                             .z = output_.blend_alpha,
                                             .w = normalized_time,
                                         },
                                         Float4{
                                             .x = static_cast<float>(clip->frame_count),
                                             .y = frame_position,
                                             .z = static_cast<float>(output_.revision),
                                             .w = 0.0F,
                                         });
        return true;
    }
};

} // namespace vr::ecs
