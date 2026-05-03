#pragma once

#include "vr/animation/animation_morph_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_resource_track_system.hpp"

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationMorphEvaluationSystem final {
public:
    using AnimationType = Animation<DimensionT, Morph>;
    using TrackSystem = AnimationResourceTrackSystem<DimensionT, Morph>;
    using ClockSystem = AnimationClockSystem<DimensionT, Morph>;

    [[nodiscard]] static bool Tick(AnimationType& component_,
                                   const animation::MorphAnimationHost& host_,
                                   AnimationEvaluationContext<DimensionT>& context_,
                                   float delta_time_s_) noexcept {
        if (delta_time_s_ != 0.0F) {
            (void)ClockSystem::Advance(component_, delta_time_s_);
        }

        MorphWeightOutputState* output = context_.morph_outputs.Resolve(component_.binding.target.entity_id);
        if (output == nullptr || output->weights == nullptr) {
            return false;
        }
        return SampleAndWrite(component_, host_, *output);
    }

    [[nodiscard]] static bool SampleAndWrite(AnimationType& component_,
                                             const animation::MorphAnimationHost& host_,
                                             MorphWeightOutputState& output_) noexcept {
        const animation::MorphClipRecord* clip = host_.FindClipByHandle(component_.playback.clip_handle);
        if (clip == nullptr || output_.weight_count < clip->weight_count) {
            return false;
        }

        const float* base_weights = host_.BaseWeights(*clip);
        for (std::uint32_t i = 0U; i < clip->weight_count; ++i) {
            output_.weights[i] = (base_weights != nullptr) ? base_weights[i] : 0.0F;
        }

        const auto* tracks = host_.Tracks(*clip);
        for (std::uint32_t i = 0U; tracks != nullptr && i < clip->track_count; ++i) {
            if (tracks[i].target_index >= clip->weight_count) {
                continue;
            }
            const auto curve = host_.WeightCurveView(tracks[i]);
            if (curve.keyframe_count == 0U) {
                continue;
            }
            output_.weights[tracks[i].target_index] = AnimationCurveSystem::Sample(curve, component_.playback.time_s);
        }

        output_.sampled_weight_count = clip->weight_count;
        ++output_.revision;
        TrackSystem::SetSampleParameters(component_,
                                         Float4{.x = static_cast<float>(clip->weight_count), .y = 0.0F, .z = 0.0F, .w = 0.0F},
                                         Float4{.x = static_cast<float>(output_.revision), .y = 0.0F, .z = 0.0F, .w = 0.0F});
        return true;
    }
};

} // namespace vr::ecs
