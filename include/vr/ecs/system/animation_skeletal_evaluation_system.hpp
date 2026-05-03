#pragma once

#include "vr/animation/animation_skeletal_host.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"
#include "vr/ecs/system/animation_resource_track_system.hpp"

#include <algorithm>

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationSkeletalEvaluationSystem final {
public:
    using AnimationType = Animation<DimensionT, Skeletal>;
    using TrackSystem = AnimationResourceTrackSystem<DimensionT, Skeletal>;
    using ClockSystem = AnimationClockSystem<DimensionT, Skeletal>;
    using OutputState = SkeletalPoseOutputState<DimensionT>;

    [[nodiscard]] static bool Tick(AnimationType& component_,
                                   const animation::SkeletalAnimationHost& host_,
                                   AnimationEvaluationContext<DimensionT>& context_,
                                   float delta_time_s_) noexcept {
        if (delta_time_s_ != 0.0F) {
            (void)ClockSystem::Advance(component_, delta_time_s_);
        }

        auto* output = context_.skeletal_outputs.Resolve(component_.binding.target.entity_id);
        if (output == nullptr || output->joints == nullptr) {
            return false;
        }

        return SampleAndWrite(component_, host_, *output);
    }

    [[nodiscard]] static bool SampleAndWrite(AnimationType& component_,
                                             const animation::SkeletalAnimationHost& host_,
                                             OutputState& output_) noexcept {
        const animation::SkeletalClipRecord* clip = host_.FindClipByHandle(component_.playback.clip_handle);
        if (clip == nullptr || output_.joint_count < clip->joint_count) {
            return false;
        }

        if constexpr (std::same_as<DimensionT, Dim2>) {
            const SkeletalJointPose<Dim2>* base_pose = host_.BasePose2D(*clip);
            if (base_pose == nullptr && clip->joint_count > 0U) {
                return false;
            }
            for (std::uint32_t i = 0U; i < clip->joint_count; ++i) {
                output_.joints[i] = base_pose[i];
            }

            const auto* tracks = host_.Tracks2D(*clip);
            for (std::uint32_t i = 0U; tracks != nullptr && i < clip->track_count; ++i) {
                if (tracks[i].joint_index >= clip->joint_count) {
                    continue;
                }
                SkeletalJointPose<Dim2>& pose = output_.joints[tracks[i].joint_index];
                const auto position_curve = host_.PositionCurveView(tracks[i]);
                if (position_curve.keyframe_count > 0U) {
                    pose.position = AnimationCurveSystem::Sample(position_curve, component_.playback.time_s);
                }
                const auto rotation_curve = host_.RotationCurveView(tracks[i]);
                if (rotation_curve.keyframe_count > 0U) {
                    pose.rotation_radians = AnimationCurveSystem::Sample(rotation_curve, component_.playback.time_s);
                }
                const auto scale_curve = host_.ScaleCurveView(tracks[i]);
                if (scale_curve.keyframe_count > 0U) {
                    pose.scale = AnimationCurveSystem::Sample(scale_curve, component_.playback.time_s);
                }
            }
        } else {
            const SkeletalJointPose<Dim3>* base_pose = host_.BasePose3D(*clip);
            if (base_pose == nullptr && clip->joint_count > 0U) {
                return false;
            }
            for (std::uint32_t i = 0U; i < clip->joint_count; ++i) {
                output_.joints[i] = base_pose[i];
            }

            const auto* tracks = host_.Tracks3D(*clip);
            for (std::uint32_t i = 0U; tracks != nullptr && i < clip->track_count; ++i) {
                if (tracks[i].joint_index >= clip->joint_count) {
                    continue;
                }
                SkeletalJointPose<Dim3>& pose = output_.joints[tracks[i].joint_index];
                const auto position_curve = host_.PositionCurveView(tracks[i]);
                if (position_curve.keyframe_count > 0U) {
                    pose.position = AnimationCurveSystem::Sample(position_curve, component_.playback.time_s);
                }
                const auto rotation_curve = host_.RotationCurveView(tracks[i]);
                if (rotation_curve.keyframe_count > 0U) {
                    pose.rotation = AnimationCurveSystem::Sample(rotation_curve, component_.playback.time_s);
                }
                const auto scale_curve = host_.ScaleCurveView(tracks[i]);
                if (scale_curve.keyframe_count > 0U) {
                    pose.scale = AnimationCurveSystem::Sample(scale_curve, component_.playback.time_s);
                }
            }
        }

        output_.sampled_joint_count = clip->joint_count;
        ++output_.revision;
        TrackSystem::SetSampleParameters(component_,
                                         Float4{.x = static_cast<float>(clip->joint_count), .y = 0.0F, .z = 0.0F, .w = 0.0F},
                                         Float4{.x = static_cast<float>(output_.revision), .y = 0.0F, .z = 0.0F, .w = 0.0F});
        return true;
    }
};

} // namespace vr::ecs
