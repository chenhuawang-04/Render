#pragma once

#include "vr/animation/animation_vertex_deform_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_resource_track_system.hpp"

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationVertexDeformEvaluationSystem final {
public:
    using AnimationType = Animation<DimensionT, VertexDeform>;
    using TrackSystem = AnimationResourceTrackSystem<DimensionT, VertexDeform>;
    using ClockSystem = AnimationClockSystem<DimensionT, VertexDeform>;

    [[nodiscard]] static bool Tick(AnimationType& component_,
                                   const animation::VertexDeformAnimationHost& host_,
                                   AnimationEvaluationContext<DimensionT>& context_,
                                   float delta_time_s_) noexcept {
        if (delta_time_s_ != 0.0F) {
            (void)ClockSystem::Advance(component_, delta_time_s_);
        }

        VertexDeformOutputState* output = context_.vertex_deform_outputs.Resolve(component_.binding.target.entity_id);
        if (output == nullptr || output->parameters == nullptr) {
            return false;
        }
        return SampleAndWrite(component_, host_, *output);
    }

    [[nodiscard]] static bool SampleAndWrite(AnimationType& component_,
                                             const animation::VertexDeformAnimationHost& host_,
                                             VertexDeformOutputState& output_) noexcept {
        const animation::VertexDeformClipRecord* clip = host_.FindClipByHandle(component_.playback.clip_handle);
        if (clip == nullptr || output_.parameter_count < clip->parameter_count) {
            return false;
        }

        const Float4* base_parameters = host_.BaseParameters(*clip);
        for (std::uint32_t i = 0U; i < clip->parameter_count; ++i) {
            output_.parameters[i] = (base_parameters != nullptr)
                ? base_parameters[i]
                : Float4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        }

        const auto* tracks = host_.Tracks(*clip);
        for (std::uint32_t i = 0U; tracks != nullptr && i < clip->track_count; ++i) {
            if (tracks[i].parameter_index >= clip->parameter_count) {
                continue;
            }
            const auto curve = host_.ParameterCurveView(tracks[i]);
            if (curve.keyframe_count == 0U) {
                continue;
            }
            output_.parameters[tracks[i].parameter_index] =
                AnimationCurveSystem::Sample(curve, component_.playback.time_s);
        }

        output_.sampled_parameter_count = clip->parameter_count;
        ++output_.revision;
        TrackSystem::SetSampleParameters(component_,
                                         Float4{
                                             .x = static_cast<float>(clip->parameter_count),
                                             .y = static_cast<float>(output_.revision),
                                             .z = 0.0F,
                                             .w = 0.0F,
                                         },
                                         Float4{.x = component_.playback.time_s, .y = 0.0F, .z = 0.0F, .w = 0.0F});
        return true;
    }
};

} // namespace vr::ecs
