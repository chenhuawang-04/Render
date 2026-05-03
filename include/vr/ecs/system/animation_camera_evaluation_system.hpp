#pragma once

#include "vr/animation/animation_clip_host.hpp"
#include "vr/ecs/system/animation_camera_track_system.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_evaluation_context.hpp"

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationCameraEvaluationSystem final {
public:
    using AnimationType = Animation<DimensionT, CameraTrack>;
    using TrackSystem = AnimationCameraTrackSystem<DimensionT>;
    using ClockSystem = AnimationClockSystem<DimensionT, CameraTrack>;

    [[nodiscard]] static bool Tick(AnimationType& component_,
                                   const animation::AnimationClipHost& clip_host_,
                                   AnimationEvaluationContext<DimensionT>& context_,
                                   float delta_time_s_) noexcept {
        if (delta_time_s_ != 0.0F) {
            (void)ClockSystem::Advance(component_, delta_time_s_);
        }
        if (!SampleFromClip(component_, clip_host_)) {
            return false;
        }
        auto* camera = context_.cameras.Resolve(component_.binding.target.entity_id);
        auto* transform = context_.transforms.Resolve(component_.binding.target.entity_id);
        return camera != nullptr &&
               transform != nullptr &&
               TrackSystem::Apply(component_, *camera, *transform);
    }

    [[nodiscard]] static bool SampleFromClip(AnimationType& component_,
                                             const animation::AnimationClipHost& clip_host_) noexcept {
        const animation::AnimationClipRecord* clip = clip_host_.FindCameraClipByHandle(component_.playback.clip_handle);
        if (clip == nullptr) {
            return false;
        }

        bool sampled_any = false;

        const auto* scalar_channels = clip_host_.CameraScalarChannels(*clip);
        for (std::uint32_t i = 0U; scalar_channels != nullptr && i < clip->camera.scalar.count; ++i) {
            const auto curve = clip_host_.BuildCurveView(scalar_channels[i]);
            switch (scalar_channels[i].semantic) {
                case CameraTrackSemantic::transform_rotation:
                    if constexpr (std::same_as<DimensionT, Dim2>) {
                        TrackSystem::SampleRotationCurve(component_, curve);
                        sampled_any = true;
                    }
                    break;
                case CameraTrackSemantic::orthographic_height:
                    TrackSystem::SampleOrthographicHeightCurve(component_, curve);
                    sampled_any = true;
                    break;
                case CameraTrackSemantic::zoom:
                    if constexpr (std::same_as<DimensionT, Dim2>) {
                        TrackSystem::SampleZoomCurve(component_, curve);
                        sampled_any = true;
                    }
                    break;
                case CameraTrackSemantic::vertical_fov:
                    if constexpr (std::same_as<DimensionT, Dim3>) {
                        TrackSystem::SampleVerticalFovCurve(component_, curve);
                        sampled_any = true;
                    }
                    break;
                case CameraTrackSemantic::none:
                case CameraTrackSemantic::transform_position:
                case CameraTrackSemantic::shake_offset:
                default:
                    break;
            }
        }

        if constexpr (std::same_as<DimensionT, Dim2>) {
            const auto* float2_channels = clip_host_.CameraFloat2Channels(*clip);
            for (std::uint32_t i = 0U; float2_channels != nullptr && i < clip->camera.float2.count; ++i) {
                const auto curve = clip_host_.BuildCurveView(float2_channels[i]);
                switch (float2_channels[i].semantic) {
                    case CameraTrackSemantic::transform_position:
                        TrackSystem::SamplePositionCurve(component_, curve);
                        sampled_any = true;
                        break;
                    case CameraTrackSemantic::shake_offset:
                        component_.sample.shake_offset = AnimationCurveSystem::Sample(curve, component_.playback.time_s);
                        ClockSystem::MarkSampleRevisionDirty(component_);
                        sampled_any = true;
                        break;
                    default:
                        break;
                }
            }
        } else {
            const auto* float3_channels = clip_host_.CameraFloat3Channels(*clip);
            for (std::uint32_t i = 0U; float3_channels != nullptr && i < clip->camera.float3.count; ++i) {
                const auto curve = clip_host_.BuildCurveView(float3_channels[i]);
                switch (float3_channels[i].semantic) {
                    case CameraTrackSemantic::transform_position:
                        TrackSystem::SamplePositionCurve(component_, curve);
                        sampled_any = true;
                        break;
                    case CameraTrackSemantic::shake_offset:
                        component_.sample.shake_offset = AnimationCurveSystem::Sample(curve, component_.playback.time_s);
                        ClockSystem::MarkSampleRevisionDirty(component_);
                        sampled_any = true;
                        break;
                    default:
                        break;
                }
            }

            const auto* quaternion_channels = clip_host_.CameraQuaternionChannels(*clip);
            for (std::uint32_t i = 0U; quaternion_channels != nullptr && i < clip->camera.quaternion.count; ++i) {
                if (quaternion_channels[i].semantic != CameraTrackSemantic::transform_rotation) {
                    continue;
                }
                TrackSystem::SampleRotationCurve(component_, clip_host_.BuildCurveView(quaternion_channels[i]));
                sampled_any = true;
            }
        }

        return sampled_any;
    }
};

} // namespace vr::ecs
