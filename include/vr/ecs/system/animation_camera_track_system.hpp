#pragma once

#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/animation_curve_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <algorithm>
#include <cstdint>

namespace vr::ecs {

template<DimensionTag DimensionT>
class AnimationCameraTrackSystem final {
public:
    using AnimationType = Animation<DimensionT, CameraTrack>;
    using CameraType = Camera<DimensionT>;
    using TransformType = Transform<DimensionT>;
    using SampleType = CameraTrackSample<DimensionT>;
    using ClockSystem = AnimationClockSystem<DimensionT, CameraTrack>;

    static void Initialize(AnimationType& component_) noexcept {
        ClockSystem::InitializeCommon(component_);
        SetDefaultBinding(component_);
        SetDefaultSample(component_);
    }

    static void SetDefaultBinding(AnimationType& component_) noexcept {
        component_.binding.target = AnimationTargetRef{
            .entity_id = 0U,
            .slot = 0U,
            .domain = AnimationTargetDomain::camera,
            .reserved0 = 0U,
            .sub_index = 0U,
        };
        component_.binding.apply_flags = animation_camera_apply_transform_position_flag |
                                         animation_camera_apply_transform_rotation_flag |
                                         animation_camera_apply_orthographic_height_flag;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.binding.apply_flags |= animation_camera_apply_zoom_flag;
        } else {
            component_.binding.apply_flags |= animation_camera_apply_vertical_fov_flag;
        }
        component_.binding.reserved0 = 0U;
        component_.binding.shake_weight = 1.0F;
        component_.binding.reserved1 = 0.0F;
    }

    static void SetDefaultSample(AnimationType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.sample.position = Float2{.x = 0.0F, .y = 0.0F};
            component_.sample.rotation_radians = 0.0F;
            component_.sample.orthographic_height = 20.0F;
            component_.sample.zoom = 1.0F;
            component_.sample.shake_offset = Float2{.x = 0.0F, .y = 0.0F};
        } else {
            component_.sample.position = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.sample.vertical_fov_radians = 60.0F * 0.01745329251994329577F;
            component_.sample.rotation = Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            component_.sample.shake_offset = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.sample.orthographic_height = 20.0F;
            component_.sample.reserved0 = 0.0F;
        }
    }

    static void SetApplyFlags(AnimationType& component_,
                              std::uint16_t apply_flags_) noexcept {
        if (component_.binding.apply_flags == apply_flags_) {
            return;
        }
        component_.binding.apply_flags = apply_flags_;
        component_.runtime.cached_channel_index = invalid_animation_handle_index;
        component_.runtime.cached_source_revision = 0U;
        component_.runtime.curve_hint_index = 0U;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetShakeWeight(AnimationType& component_,
                               float shake_weight_) noexcept {
        const float clamped = std::max(0.0F, shake_weight_);
        if (component_.binding.shake_weight == clamped) {
            return;
        }
        component_.binding.shake_weight = clamped;
        component_.runtime.cached_channel_index = invalid_animation_handle_index;
        component_.runtime.cached_source_revision = 0U;
        component_.runtime.curve_hint_index = 0U;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetSample(AnimationType& component_,
                          const SampleType& sample_) noexcept {
        component_.sample = sample_;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SamplePositionCurve(AnimationType& component_,
                                    const AnimationCurveView<Float2>& curve_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.position = AnimationCurveSystem::Sample(curve_,
                                                                  component_.playback.time_s,
                                                                  &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SamplePositionCurve(AnimationType& component_,
                                    const AnimationCurveView<Float3>& curve_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.position = AnimationCurveSystem::Sample(curve_,
                                                                  component_.playback.time_s,
                                                                  &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleRotationCurve(AnimationType& component_,
                                    const AnimationCurveView<float>& curve_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.rotation_radians = AnimationCurveSystem::Sample(curve_,
                                                                          component_.playback.time_s,
                                                                          &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleRotationCurve(AnimationType& component_,
                                    const AnimationCurveView<Quaternion>& curve_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.rotation = AnimationCurveSystem::Sample(curve_,
                                                                  component_.playback.time_s,
                                                                  &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleOrthographicHeightCurve(AnimationType& component_,
                                              const AnimationCurveView<float>& curve_) noexcept {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.orthographic_height = AnimationCurveSystem::Sample(curve_,
                                                                             component_.playback.time_s,
                                                                             &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleZoomCurve(AnimationType& component_,
                                const AnimationCurveView<float>& curve_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.zoom = AnimationCurveSystem::Sample(curve_,
                                                              component_.playback.time_s,
                                                              &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    static void SampleVerticalFovCurve(AnimationType& component_,
                                       const AnimationCurveView<float>& curve_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        AnimationCurveCursor cursor{.segment_index = component_.runtime.curve_hint_index};
        component_.sample.vertical_fov_radians = AnimationCurveSystem::Sample(curve_,
                                                                              component_.playback.time_s,
                                                                              &cursor);
        component_.runtime.curve_hint_index = cursor.segment_index;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    [[nodiscard]] static bool Apply(AnimationType& component_,
                                    CameraType& camera_,
                                    TransformType& transform_) noexcept {
        const std::uint16_t flags = component_.binding.apply_flags;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            if ((flags & animation_camera_apply_transform_position_flag) != 0U) {
                Float2 position = component_.sample.position;
                if ((flags & animation_camera_apply_shake_offset_flag) != 0U) {
                    position.x += component_.sample.shake_offset.x * component_.binding.shake_weight;
                    position.y += component_.sample.shake_offset.y * component_.binding.shake_weight;
                }
                TransformSystem<DimensionT>::SetLocalPosition(transform_, position.x, position.y);
            }
            if ((flags & animation_camera_apply_transform_rotation_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalRotationRadians(transform_,
                                                                     component_.sample.rotation_radians);
            }
            if ((flags & animation_camera_apply_orthographic_height_flag) != 0U) {
                CameraSystem<DimensionT>::SetOrthographicHeight(camera_,
                                                                component_.sample.orthographic_height);
            }
            if ((flags & animation_camera_apply_zoom_flag) != 0U) {
                CameraSystem<DimensionT>::SetZoom(camera_, component_.sample.zoom);
            }
        } else {
            if ((flags & animation_camera_apply_transform_position_flag) != 0U) {
                Float3 position = component_.sample.position;
                if ((flags & animation_camera_apply_shake_offset_flag) != 0U) {
                    position.x += component_.sample.shake_offset.x * component_.binding.shake_weight;
                    position.y += component_.sample.shake_offset.y * component_.binding.shake_weight;
                    position.z += component_.sample.shake_offset.z * component_.binding.shake_weight;
                }
                TransformSystem<DimensionT>::SetLocalPosition(transform_, position);
            }
            if ((flags & animation_camera_apply_transform_rotation_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalRotationQuaternion(transform_,
                                                                        component_.sample.rotation);
            }
            if ((flags & animation_camera_apply_vertical_fov_flag) != 0U) {
                CameraSystem<DimensionT>::SetVerticalFovRadians(camera_,
                                                                component_.sample.vertical_fov_radians);
            }
            if ((flags & animation_camera_apply_orthographic_height_flag) != 0U) {
                CameraSystem<DimensionT>::SetOrthographicHeight(camera_,
                                                                component_.sample.orthographic_height);
            }
        }
        return flags != 0U;
    }
};

} // namespace vr::ecs

