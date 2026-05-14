#pragma once

#include "vr/animation/animation_path_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_path_motion_system.hpp"

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationPathEvaluationSystem final {
public:
    using AnimationType = Animation<DimensionT, PathMotion>;
    using TrackSystem = AnimationPathMotionSystem<DimensionT>;
    using ClockSystem = AnimationClockSystem<DimensionT, PathMotion>;

    [[nodiscard]] static bool Tick(AnimationType& component_,
                                   const animation::AnimationPathHost& path_host_,
                                   AnimationEvaluationContext<DimensionT>& context_,
                                   float delta_time_s_) noexcept {
        if (delta_time_s_ != 0.0F) {
            (void)ClockSystem::Advance(component_, delta_time_s_);
        }
        if (!SampleFromPath(component_, path_host_)) {
            return false;
        }
        auto* transform = context_.transforms.Resolve(component_.binding.target.entity_id);
        return transform != nullptr && TrackSystem::ApplyToTransform(component_, *transform);
    }

    [[nodiscard]] static bool SampleFromPath(AnimationType& component_,
                                             const animation::AnimationPathHost& path_host_) noexcept {
        const animation::AnimationPathRecord* path = path_host_.FindPathByHandle(component_.binding.path_handle);
        if (path == nullptr) {
            return false;
        }
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return TrackSystem::SampleSpline(component_, path_host_.BuildSplineView2D(*path));
        } else {
            return TrackSystem::SampleSpline(component_, path_host_.BuildSplineView3D(*path));
        }
    }
};

} // namespace vr::ecs

