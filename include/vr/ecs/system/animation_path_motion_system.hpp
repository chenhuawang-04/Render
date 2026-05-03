#pragma once

#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/animation_clock_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vr::ecs {

template<DimensionTag DimensionT>
struct AnimationBezierSegment;

template<>
struct AnimationBezierSegment<Dim2> final {
    Float2 p0;
    Float2 p1;
    Float2 p2;
    Float2 p3;
};

template<>
struct AnimationBezierSegment<Dim3> final {
    Float3 p0;
    Float3 p1;
    Float3 p2;
    Float3 p3;
};

template<DimensionTag DimensionT>
struct AnimationSplineView final {
    const AnimationBezierSegment<DimensionT>* segments;
    std::uint32_t segment_count;
};

template<DimensionTag DimensionT>
class AnimationPathMotionSystem final {
public:
    using AnimationType = Animation<DimensionT, PathMotion>;
    using TransformType = Transform<DimensionT>;
    using SampleType = PathMotionSample<DimensionT>;
    using ClockSystem = AnimationClockSystem<DimensionT, PathMotion>;

    static void Initialize(AnimationType& component_) noexcept {
        ClockSystem::InitializeCommon(component_);
        SetDefaultBinding(component_);
        SetDefaultSample(component_);
    }

    static void SetDefaultBinding(AnimationType& component_) noexcept {
        component_.binding.target = AnimationTargetRef{
            .entity_id = 0U,
            .slot = 0U,
            .domain = AnimationTargetDomain::transform,
            .reserved0 = 0U,
            .sub_index = 0U,
        };
        component_.binding.path_handle = invalid_animation_path_handle;
        component_.binding.orientation_mode = AnimationPathOrientationMode::tangent;
        component_.binding.apply_flags = animation_path_apply_position_flag |
                                         animation_path_apply_rotation_flag;
        component_.binding.reserved0 = 0U;
        component_.binding.roll_offset_radians = 0.0F;
        component_.binding.up_hint = Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }

    static void SetDefaultSample(AnimationType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.sample.position = Float2{.x = 0.0F, .y = 0.0F};
            component_.sample.tangent = Float2{.x = 1.0F, .y = 0.0F};
            component_.sample.scale = Float2{.x = 1.0F, .y = 1.0F};
            component_.sample.rotation_radians = 0.0F;
            component_.sample.normalized_t = 0.0F;
            component_.sample.reserved0 = 0U;
        } else {
            component_.sample.position = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
            component_.sample.normalized_t = 0.0F;
            component_.sample.tangent = Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F};
            component_.sample.reserved0 = 0.0F;
            component_.sample.rotation = Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            component_.sample.scale = Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
            component_.sample.reserved1 = 0.0F;
        }
    }

    static void SetPathHandle(AnimationType& component_,
                              AnimationPathHandle path_handle_) noexcept {
        if (component_.binding.path_handle.index == path_handle_.index &&
            component_.binding.path_handle.generation == path_handle_.generation) {
            return;
        }
        component_.binding.path_handle = path_handle_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetOrientationMode(AnimationType& component_,
                                   AnimationPathOrientationMode orientation_mode_) noexcept {
        if (component_.binding.orientation_mode == orientation_mode_) {
            return;
        }
        component_.binding.orientation_mode = orientation_mode_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetApplyFlags(AnimationType& component_,
                              std::uint8_t apply_flags_) noexcept {
        if (component_.binding.apply_flags == apply_flags_) {
            return;
        }
        component_.binding.apply_flags = apply_flags_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetRollOffsetRadians(AnimationType& component_,
                                     float roll_offset_radians_) noexcept {
        if (component_.binding.roll_offset_radians == roll_offset_radians_) {
            return;
        }
        component_.binding.roll_offset_radians = roll_offset_radians_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetUpHint(AnimationType& component_,
                          const Float3& up_hint_) noexcept {
        if (component_.binding.up_hint.x == up_hint_.x &&
            component_.binding.up_hint.y == up_hint_.y &&
            component_.binding.up_hint.z == up_hint_.z) {
            return;
        }
        component_.binding.up_hint = up_hint_;
        ClockSystem::MarkBindingRevisionDirty(component_);
    }

    static void SetSample(AnimationType& component_,
                          const SampleType& sample_) noexcept {
        component_.sample = sample_;
        ClockSystem::MarkSampleRevisionDirty(component_);
    }

    [[nodiscard]] static bool SampleSpline(AnimationType& component_,
                                           const AnimationSplineView<DimensionT>& spline_) noexcept {
        if (spline_.segments == nullptr || spline_.segment_count == 0U) {
            return false;
        }

        const float duration_s = std::max(1e-6F, component_.playback.duration_s);
        const float normalized_t = std::clamp(component_.playback.time_s / duration_s, 0.0F, 1.0F);
        const float scaled_t = normalized_t * static_cast<float>(spline_.segment_count);
        const std::uint32_t segment_index = std::min(static_cast<std::uint32_t>(scaled_t),
                                                     spline_.segment_count - 1U);
        const float segment_t = std::clamp(scaled_t - static_cast<float>(segment_index), 0.0F, 1.0F);

        if constexpr (std::same_as<DimensionT, Dim2>) {
            const auto sampled = SampleBezierSegment(spline_.segments[segment_index], segment_t);
            component_.sample.position = sampled.position;
            component_.sample.tangent = sampled.tangent;
            component_.sample.rotation_radians = Resolve2DRotation(component_, sampled.tangent);
            component_.sample.scale = Float2{.x = 1.0F, .y = 1.0F};
            component_.sample.normalized_t = normalized_t;
        } else {
            const auto sampled = SampleBezierSegment(spline_.segments[segment_index], segment_t);
            component_.sample.position = sampled.position;
            component_.sample.tangent = sampled.tangent;
            component_.sample.rotation = Resolve3DRotation(component_, sampled.tangent);
            component_.sample.scale = Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
            component_.sample.normalized_t = normalized_t;
        }

        ClockSystem::MarkSampleRevisionDirty(component_);
        return true;
    }

    [[nodiscard]] static bool ApplyToTransform(AnimationType& component_,
                                               TransformType& transform_) noexcept {
        const std::uint8_t flags = component_.binding.apply_flags;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            if ((flags & animation_path_apply_position_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalPosition(transform_,
                                                              component_.sample.position.x,
                                                              component_.sample.position.y);
            }
            if ((flags & animation_path_apply_rotation_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalRotationRadians(transform_,
                                                                     component_.sample.rotation_radians);
            }
            if ((flags & animation_path_apply_scale_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalScale(transform_,
                                                           component_.sample.scale.x,
                                                           component_.sample.scale.y);
            }
        } else {
            if ((flags & animation_path_apply_position_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalPosition(transform_,
                                                              component_.sample.position);
            }
            if ((flags & animation_path_apply_rotation_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalRotationQuaternion(transform_,
                                                                        component_.sample.rotation);
            }
            if ((flags & animation_path_apply_scale_flag) != 0U) {
                TransformSystem<DimensionT>::SetLocalScale(transform_,
                                                           component_.sample.scale);
            }
        }
        return flags != 0U;
    }

private:
    struct BezierSample2D final {
        Float2 position;
        Float2 tangent;
    };

    struct BezierSample3D final {
        Float3 position;
        Float3 tangent;
    };

    [[nodiscard]] static BezierSample2D SampleBezierSegment(const AnimationBezierSegment<Dim2>& segment_,
                                                            float t_) noexcept {
        const float omt = 1.0F - t_;
        const float omt2 = omt * omt;
        const float t2 = t_ * t_;

        const float b0 = omt2 * omt;
        const float b1 = 3.0F * omt2 * t_;
        const float b2 = 3.0F * omt * t2;
        const float b3 = t2 * t_;

        const Float2 position{
            .x = segment_.p0.x * b0 + segment_.p1.x * b1 + segment_.p2.x * b2 + segment_.p3.x * b3,
            .y = segment_.p0.y * b0 + segment_.p1.y * b1 + segment_.p2.y * b2 + segment_.p3.y * b3,
        };

        const Float2 tangent{
            .x = 3.0F * omt2 * (segment_.p1.x - segment_.p0.x) +
                 6.0F * omt * t_ * (segment_.p2.x - segment_.p1.x) +
                 3.0F * t2 * (segment_.p3.x - segment_.p2.x),
            .y = 3.0F * omt2 * (segment_.p1.y - segment_.p0.y) +
                 6.0F * omt * t_ * (segment_.p2.y - segment_.p1.y) +
                 3.0F * t2 * (segment_.p3.y - segment_.p2.y),
        };
        return BezierSample2D{
            .position = position,
            .tangent = tangent,
        };
    }

    [[nodiscard]] static BezierSample3D SampleBezierSegment(const AnimationBezierSegment<Dim3>& segment_,
                                                            float t_) noexcept {
        const float omt = 1.0F - t_;
        const float omt2 = omt * omt;
        const float t2 = t_ * t_;

        const float b0 = omt2 * omt;
        const float b1 = 3.0F * omt2 * t_;
        const float b2 = 3.0F * omt * t2;
        const float b3 = t2 * t_;

        const Float3 position{
            .x = segment_.p0.x * b0 + segment_.p1.x * b1 + segment_.p2.x * b2 + segment_.p3.x * b3,
            .y = segment_.p0.y * b0 + segment_.p1.y * b1 + segment_.p2.y * b2 + segment_.p3.y * b3,
            .z = segment_.p0.z * b0 + segment_.p1.z * b1 + segment_.p2.z * b2 + segment_.p3.z * b3,
        };

        const Float3 tangent{
            .x = 3.0F * omt2 * (segment_.p1.x - segment_.p0.x) +
                 6.0F * omt * t_ * (segment_.p2.x - segment_.p1.x) +
                 3.0F * t2 * (segment_.p3.x - segment_.p2.x),
            .y = 3.0F * omt2 * (segment_.p1.y - segment_.p0.y) +
                 6.0F * omt * t_ * (segment_.p2.y - segment_.p1.y) +
                 3.0F * t2 * (segment_.p3.y - segment_.p2.y),
            .z = 3.0F * omt2 * (segment_.p1.z - segment_.p0.z) +
                 6.0F * omt * t_ * (segment_.p2.z - segment_.p1.z) +
                 3.0F * t2 * (segment_.p3.z - segment_.p2.z),
        };
        return BezierSample3D{
            .position = position,
            .tangent = tangent,
        };
    }

    [[nodiscard]] static float Resolve2DRotation(const AnimationType& component_,
                                                 const Float2& tangent_) noexcept {
        if (component_.binding.orientation_mode == AnimationPathOrientationMode::sampled) {
            return component_.sample.rotation_radians;
        }
        if (component_.binding.orientation_mode != AnimationPathOrientationMode::tangent) {
            return component_.sample.rotation_radians;
        }
        return std::atan2(tangent_.y, tangent_.x) + component_.binding.roll_offset_radians;
    }

    [[nodiscard]] static Quaternion Resolve3DRotation(const AnimationType& component_,
                                                      const Float3& tangent_) noexcept {
        if (component_.binding.orientation_mode == AnimationPathOrientationMode::sampled) {
            return component_.sample.rotation;
        }
        if (component_.binding.orientation_mode != AnimationPathOrientationMode::tangent) {
            return component_.sample.rotation;
        }

        const Float3 forward = Normalize3(tangent_, Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
        Quaternion rotation = QuaternionFromForwardUp(forward, component_.binding.up_hint);
        if (component_.binding.roll_offset_radians != 0.0F) {
            const Quaternion roll = QuaternionFromAxisAngle(forward, component_.binding.roll_offset_radians);
            rotation = MultiplyQuaternion(rotation, roll);
        }
        return rotation;
    }

    [[nodiscard]] static Float3 Normalize3(const Float3& value_,
                                           const Float3& fallback_) noexcept {
        const float len_sq = value_.x * value_.x + value_.y * value_.y + value_.z * value_.z;
        if (len_sq <= 1e-12F) {
            return fallback_;
        }
        const float inv_len = 1.0F / std::sqrt(len_sq);
        return Float3{
            .x = value_.x * inv_len,
            .y = value_.y * inv_len,
            .z = value_.z * inv_len,
        };
    }

    [[nodiscard]] static Float3 Cross3(const Float3& lhs_,
                                       const Float3& rhs_) noexcept {
        return Float3{
            .x = lhs_.y * rhs_.z - lhs_.z * rhs_.y,
            .y = lhs_.z * rhs_.x - lhs_.x * rhs_.z,
            .z = lhs_.x * rhs_.y - lhs_.y * rhs_.x,
        };
    }

    [[nodiscard]] static Quaternion QuaternionFromAxisAngle(const Float3& axis_,
                                                            float radians_) noexcept {
        const Float3 normalized_axis = Normalize3(axis_, Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
        const float half = radians_ * 0.5F;
        const float sin_half = std::sin(half);
        return Quaternion{
            .x = normalized_axis.x * sin_half,
            .y = normalized_axis.y * sin_half,
            .z = normalized_axis.z * sin_half,
            .w = std::cos(half),
        };
    }

    [[nodiscard]] static Quaternion MultiplyQuaternion(const Quaternion& lhs_,
                                                       const Quaternion& rhs_) noexcept {
        return Quaternion{
            .x = lhs_.w * rhs_.x + lhs_.x * rhs_.w + lhs_.y * rhs_.z - lhs_.z * rhs_.y,
            .y = lhs_.w * rhs_.y - lhs_.x * rhs_.z + lhs_.y * rhs_.w + lhs_.z * rhs_.x,
            .z = lhs_.w * rhs_.z + lhs_.x * rhs_.y - lhs_.y * rhs_.x + lhs_.z * rhs_.w,
            .w = lhs_.w * rhs_.w - lhs_.x * rhs_.x - lhs_.y * rhs_.y - lhs_.z * rhs_.z,
        };
    }

    [[nodiscard]] static Quaternion QuaternionFromForwardUp(const Float3& forward_,
                                                            const Float3& up_hint_) noexcept {
        const Float3 forward = Normalize3(forward_, Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
        Float3 right = Cross3(up_hint_, forward);
        right = Normalize3(right, Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
        Float3 up = Cross3(forward, right);
        up = Normalize3(up, Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});

        const float m00 = right.x;
        const float m01 = up.x;
        const float m02 = forward.x;
        const float m10 = right.y;
        const float m11 = up.y;
        const float m12 = forward.y;
        const float m20 = right.z;
        const float m21 = up.z;
        const float m22 = forward.z;

        Quaternion q{};
        const float trace = m00 + m11 + m22;
        if (trace > 0.0F) {
            const float s = std::sqrt(trace + 1.0F) * 2.0F;
            q.w = 0.25F * s;
            q.x = (m21 - m12) / s;
            q.y = (m02 - m20) / s;
            q.z = (m10 - m01) / s;
        } else if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
            q.w = (m21 - m12) / s;
            q.x = 0.25F * s;
            q.y = (m01 + m10) / s;
            q.z = (m02 + m20) / s;
        } else if (m11 > m22) {
            const float s = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
            q.w = (m02 - m20) / s;
            q.x = (m01 + m10) / s;
            q.y = 0.25F * s;
            q.z = (m12 + m21) / s;
        } else {
            const float s = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
            q.w = (m10 - m01) / s;
            q.x = (m02 + m20) / s;
            q.y = (m12 + m21) / s;
            q.z = 0.25F * s;
        }
        return q;
    }
};

static_assert(std::is_standard_layout_v<AnimationBezierSegment<Dim2>> &&
              std::is_trivial_v<AnimationBezierSegment<Dim2>>);
static_assert(std::is_standard_layout_v<AnimationBezierSegment<Dim3>> &&
              std::is_trivial_v<AnimationBezierSegment<Dim3>>);
static_assert(std::is_standard_layout_v<AnimationSplineView<Dim2>> &&
              std::is_trivial_v<AnimationSplineView<Dim2>>);
static_assert(std::is_standard_layout_v<AnimationSplineView<Dim3>> &&
              std::is_trivial_v<AnimationSplineView<Dim3>>);

} // namespace vr::ecs
