#pragma once

#include "vr/ecs/component/spatial_types.hpp"

#include "fast_math/vec4.h"

#include <algorithm>
#include <cmath>

namespace vr::ecs::spatial_math {

[[nodiscard]] inline Affine2x3 IdentityAffine2x3() noexcept {
    return MMath::mat3Identity();
}

[[nodiscard]] inline Matrix4x4 IdentityMatrix4x4() noexcept {
    return MMath::mat4Identity();
}

[[nodiscard]] inline Affine2x3 ComposeAffine2x3Trs(float position_x_,
                                                    float position_y_,
                                                    float rotation_radians_,
                                                    float scale_x_,
                                                    float scale_y_) noexcept {
    const MMath::Vec2 translation{.x = position_x_, .y = position_y_};
    const MMath::Vec2 scale{.x = scale_x_, .y = scale_y_};
    return MMath::mat3FromTrs(translation, rotation_radians_, scale);
}

[[nodiscard]] inline Affine2x3 MultiplyAffine2x3(const Affine2x3& left_,
                                                  const Affine2x3& right_) noexcept {
    return MMath::mat3MultiplyAffine(left_, right_);
}

[[nodiscard]] inline bool InvertAffine2x3(const Affine2x3& in_,
                                          Affine2x3& out_) noexcept {
    if (!MMath::mat3TryInverseAffine(in_, &out_, 1e-6F)) {
        out_ = MMath::mat3Identity();
        return false;
    }
    return true;
}

[[nodiscard]] inline Matrix4x4 Affine2x3ToMatrix4x4(const Affine2x3& value_) noexcept {
    Matrix4x4 out{};

    // Column-major (D3D style):
    // | m0  m4  m8   m12 |
    // | m1  m5  m9   m13 |
    // | m2  m6  m10  m14 |
    // | m3  m7  m11  m15 |
    out.m[0] = value_.m00;
    out.m[1] = value_.m10;
    out.m[2] = 0.0F;
    out.m[3] = 0.0F;

    out.m[4] = value_.m01;
    out.m[5] = value_.m11;
    out.m[6] = 0.0F;
    out.m[7] = 0.0F;

    out.m[8] = 0.0F;
    out.m[9] = 0.0F;
    out.m[10] = 1.0F;
    out.m[11] = 0.0F;

    out.m[12] = value_.m02;
    out.m[13] = value_.m12;
    out.m[14] = 0.0F;
    out.m[15] = 1.0F;

    return out;
}

[[nodiscard]] inline Quaternion NormalizeQuaternion(Quaternion value_) noexcept {
    const MMath::Vec4 vec4{
        .x = value_.x,
        .y = value_.y,
        .z = value_.z,
        .w = value_.w,
    };
    const MMath::Vec4 normalized = MMath::vec4NormalizeSafe(vec4, 1e-8F);

    const float len_sq = normalized.x * normalized.x +
                         normalized.y * normalized.y +
                         normalized.z * normalized.z +
                         normalized.w * normalized.w;
    if (len_sq <= 1e-16F) {
        return Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    }

    return Quaternion{
        .x = normalized.x,
        .y = normalized.y,
        .z = normalized.z,
        .w = normalized.w,
    };
}

[[nodiscard]] inline Quaternion QuaternionFromEulerXyz(float pitch_x_radians_,
                                                       float yaw_y_radians_,
                                                       float roll_z_radians_) noexcept {
    const float hx = pitch_x_radians_ * 0.5F;
    const float hy = yaw_y_radians_ * 0.5F;
    const float hz = roll_z_radians_ * 0.5F;

    const float sx = std::sin(hx);
    const float cx = std::cos(hx);
    const float sy = std::sin(hy);
    const float cy = std::cos(hy);
    const float sz = std::sin(hz);
    const float cz = std::cos(hz);

    Quaternion result{};
    result.x = sx * cy * cz + cx * sy * sz;
    result.y = cx * sy * cz - sx * cy * sz;
    result.z = cx * cy * sz + sx * sy * cz;
    result.w = cx * cy * cz - sx * sy * sz;
    return NormalizeQuaternion(result);
}

[[nodiscard]] inline Matrix4x4 ComposeMatrix4x4Trs(const Float3& position_,
                                                   const Quaternion& rotation_,
                                                   const Float3& scale_) noexcept {
    const Quaternion q = NormalizeQuaternion(rotation_);

    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    const float r00 = 1.0F - 2.0F * (yy + zz);
    const float r01 = 2.0F * (xy - wz);
    const float r02 = 2.0F * (xz + wy);

    const float r10 = 2.0F * (xy + wz);
    const float r11 = 1.0F - 2.0F * (xx + zz);
    const float r12 = 2.0F * (yz - wx);

    const float r20 = 2.0F * (xz - wy);
    const float r21 = 2.0F * (yz + wx);
    const float r22 = 1.0F - 2.0F * (xx + yy);

    Matrix4x4 out{};

    // Column-major D3D style, M = T * R * S.
    out.m[0] = r00 * scale_.x;
    out.m[1] = r10 * scale_.x;
    out.m[2] = r20 * scale_.x;
    out.m[3] = 0.0F;

    out.m[4] = r01 * scale_.y;
    out.m[5] = r11 * scale_.y;
    out.m[6] = r21 * scale_.y;
    out.m[7] = 0.0F;

    out.m[8] = r02 * scale_.z;
    out.m[9] = r12 * scale_.z;
    out.m[10] = r22 * scale_.z;
    out.m[11] = 0.0F;

    out.m[12] = position_.x;
    out.m[13] = position_.y;
    out.m[14] = position_.z;
    out.m[15] = 1.0F;

    return out;
}

[[nodiscard]] inline Matrix4x4 MultiplyMatrix4x4(const Matrix4x4& left_,
                                                  const Matrix4x4& right_) noexcept {
    return MMath::mat4Multiply(left_, right_);
}

[[nodiscard]] inline bool InvertAffineMatrix4x4(const Matrix4x4& in_,
                                                Matrix4x4& out_) noexcept {
    if (!MMath::mat4TryInverse(in_, &out_, 1e-6F)) {
        out_ = MMath::mat4Identity();
        return false;
    }
    return true;
}

[[nodiscard]] inline Matrix4x4 BuildOrthographicProjection(float left_,
                                                           float right_,
                                                           float bottom_,
                                                           float top_,
                                                           float near_plane_,
                                                           float far_plane_) noexcept {
    float left = left_;
    float right = right_;
    float bottom = bottom_;
    float top = top_;

    if (std::abs(right - left) <= 1e-6F) {
        right = left + 1e-6F;
    }
    if (std::abs(top - bottom) <= 1e-6F) {
        top = bottom + 1e-6F;
    }

    const float near_plane = std::max(0.0F, near_plane_);
    const float far_plane = std::max(near_plane + 1e-3F, far_plane_);

    return MMath::mat4OrthoRH(left,
                              right,
                              bottom,
                              top,
                              near_plane,
                              far_plane);
}

[[nodiscard]] inline Matrix4x4 BuildPerspectiveProjection(float vertical_fov_radians_,
                                                          float aspect_ratio_,
                                                          float near_plane_,
                                                          float far_plane_,
                                                          bool reverse_z_) noexcept {
    const float fov = std::clamp(vertical_fov_radians_, 1e-3F, 3.13F);
    const float aspect = std::max(1e-6F, aspect_ratio_);
    const float near_plane = std::max(1e-4F, near_plane_);
    const float far_plane = std::max(near_plane + 1e-3F, far_plane_);

    if (reverse_z_) {
        return MMath::mat4PerspectiveRH(fov,
                                        aspect,
                                        far_plane,
                                        near_plane);
    }

    return MMath::mat4PerspectiveRH(fov,
                                    aspect,
                                    near_plane,
                                    far_plane);
}

[[nodiscard]] inline float Dot3(const Float3& lhs_, const Float3& rhs_) noexcept {
    return lhs_.x * rhs_.x + lhs_.y * rhs_.y + lhs_.z * rhs_.z;
}

[[nodiscard]] inline Float3 Cross3(const Float3& lhs_, const Float3& rhs_) noexcept {
    return Float3{
        .x = lhs_.y * rhs_.z - lhs_.z * rhs_.y,
        .y = lhs_.z * rhs_.x - lhs_.x * rhs_.z,
        .z = lhs_.x * rhs_.y - lhs_.y * rhs_.x,
    };
}

[[nodiscard]] inline Float3 Normalize3(const Float3& value_,
                                       const Float3& fallback_ = Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F}) noexcept {
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

[[nodiscard]] inline Matrix4x4 BuildLookAtViewRh(const Float3& eye_,
                                                 const Float3& target_,
                                                 const Float3& up_hint_) noexcept {
    const Float3 z_axis = Normalize3(Float3{
                                         .x = eye_.x - target_.x,
                                         .y = eye_.y - target_.y,
                                         .z = eye_.z - target_.z,
                                     },
                                     Float3{.x = 0.0F, .y = 0.0F, .z = 1.0F});
    Float3 x_axis = Cross3(up_hint_, z_axis);
    x_axis = Normalize3(x_axis, Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
    Float3 y_axis = Cross3(z_axis, x_axis);
    y_axis = Normalize3(y_axis, Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});

    Matrix4x4 out{};
    // Column-major, D3D style.
    out.m[0] = x_axis.x;
    out.m[1] = y_axis.x;
    out.m[2] = z_axis.x;
    out.m[3] = 0.0F;

    out.m[4] = x_axis.y;
    out.m[5] = y_axis.y;
    out.m[6] = z_axis.y;
    out.m[7] = 0.0F;

    out.m[8] = x_axis.z;
    out.m[9] = y_axis.z;
    out.m[10] = z_axis.z;
    out.m[11] = 0.0F;

    out.m[12] = -Dot3(x_axis, eye_);
    out.m[13] = -Dot3(y_axis, eye_);
    out.m[14] = -Dot3(z_axis, eye_);
    out.m[15] = 1.0F;

    return out;
}

} // namespace vr::ecs::spatial_math
