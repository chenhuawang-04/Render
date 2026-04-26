#pragma once

#include "vr/ecs/component/spatial_types.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vr::ecs::spatial_math {

[[nodiscard]] inline Affine2x3 IdentityAffine2x3() noexcept {
    return Affine2x3{
        .m00 = 1.0F,
        .m01 = 0.0F,
        .m02 = 0.0F,
        .m10 = 0.0F,
        .m11 = 1.0F,
        .m12 = 0.0F,
    };
}

[[nodiscard]] inline Matrix4x4 IdentityMatrix4x4() noexcept {
    return Matrix4x4{
        .m00 = 1.0F, .m01 = 0.0F, .m02 = 0.0F, .m03 = 0.0F,
        .m10 = 0.0F, .m11 = 1.0F, .m12 = 0.0F, .m13 = 0.0F,
        .m20 = 0.0F, .m21 = 0.0F, .m22 = 1.0F, .m23 = 0.0F,
        .m30 = 0.0F, .m31 = 0.0F, .m32 = 0.0F, .m33 = 1.0F,
    };
}

[[nodiscard]] inline Affine2x3 ComposeAffine2x3Trs(float position_x_,
                                                    float position_y_,
                                                    float rotation_radians_,
                                                    float scale_x_,
                                                    float scale_y_) noexcept {
    const float c = std::cos(rotation_radians_);
    const float s = std::sin(rotation_radians_);

    return Affine2x3{
        .m00 = c * scale_x_,
        .m01 = -s * scale_y_,
        .m02 = position_x_,
        .m10 = s * scale_x_,
        .m11 = c * scale_y_,
        .m12 = position_y_,
    };
}

[[nodiscard]] inline Affine2x3 MultiplyAffine2x3(const Affine2x3& left_,
                                                  const Affine2x3& right_) noexcept {
    Affine2x3 out{};
    out.m00 = left_.m00 * right_.m00 + left_.m01 * right_.m10;
    out.m01 = left_.m00 * right_.m01 + left_.m01 * right_.m11;
    out.m02 = left_.m00 * right_.m02 + left_.m01 * right_.m12 + left_.m02;

    out.m10 = left_.m10 * right_.m00 + left_.m11 * right_.m10;
    out.m11 = left_.m10 * right_.m01 + left_.m11 * right_.m11;
    out.m12 = left_.m10 * right_.m02 + left_.m11 * right_.m12 + left_.m12;
    return out;
}

[[nodiscard]] inline bool InvertAffine2x3(const Affine2x3& in_,
                                          Affine2x3& out_) noexcept {
    const float det = in_.m00 * in_.m11 - in_.m01 * in_.m10;
    if (std::abs(det) <= 1e-8F) {
        out_ = IdentityAffine2x3();
        return false;
    }

    const float inv_det = 1.0F / det;
    out_.m00 = in_.m11 * inv_det;
    out_.m01 = -in_.m01 * inv_det;
    out_.m10 = -in_.m10 * inv_det;
    out_.m11 = in_.m00 * inv_det;

    out_.m02 = -(out_.m00 * in_.m02 + out_.m01 * in_.m12);
    out_.m12 = -(out_.m10 * in_.m02 + out_.m11 * in_.m12);
    return true;
}

[[nodiscard]] inline Matrix4x4 Affine2x3ToMatrix4x4(const Affine2x3& value_) noexcept {
    return Matrix4x4{
        .m00 = value_.m00,
        .m01 = value_.m01,
        .m02 = 0.0F,
        .m03 = value_.m02,

        .m10 = value_.m10,
        .m11 = value_.m11,
        .m12 = 0.0F,
        .m13 = value_.m12,

        .m20 = 0.0F,
        .m21 = 0.0F,
        .m22 = 1.0F,
        .m23 = 0.0F,

        .m30 = 0.0F,
        .m31 = 0.0F,
        .m32 = 0.0F,
        .m33 = 1.0F,
    };
}

[[nodiscard]] inline Quaternion NormalizeQuaternion(Quaternion value_) noexcept {
    const float len_sq = value_.x * value_.x +
                         value_.y * value_.y +
                         value_.z * value_.z +
                         value_.w * value_.w;
    if (len_sq <= 1e-16F) {
        return Quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    }

    const float inv_len = 1.0F / std::sqrt(len_sq);
    return Quaternion{
        .x = value_.x * inv_len,
        .y = value_.y * inv_len,
        .z = value_.z * inv_len,
        .w = value_.w * inv_len,
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

    return Matrix4x4{
        .m00 = r00 * scale_.x,
        .m01 = r01 * scale_.y,
        .m02 = r02 * scale_.z,
        .m03 = position_.x,

        .m10 = r10 * scale_.x,
        .m11 = r11 * scale_.y,
        .m12 = r12 * scale_.z,
        .m13 = position_.y,

        .m20 = r20 * scale_.x,
        .m21 = r21 * scale_.y,
        .m22 = r22 * scale_.z,
        .m23 = position_.z,

        .m30 = 0.0F,
        .m31 = 0.0F,
        .m32 = 0.0F,
        .m33 = 1.0F,
    };
}

[[nodiscard]] inline Matrix4x4 MultiplyMatrix4x4(const Matrix4x4& left_,
                                                  const Matrix4x4& right_) noexcept {
    Matrix4x4 out{};

    out.m00 = left_.m00 * right_.m00 + left_.m01 * right_.m10 + left_.m02 * right_.m20 + left_.m03 * right_.m30;
    out.m01 = left_.m00 * right_.m01 + left_.m01 * right_.m11 + left_.m02 * right_.m21 + left_.m03 * right_.m31;
    out.m02 = left_.m00 * right_.m02 + left_.m01 * right_.m12 + left_.m02 * right_.m22 + left_.m03 * right_.m32;
    out.m03 = left_.m00 * right_.m03 + left_.m01 * right_.m13 + left_.m02 * right_.m23 + left_.m03 * right_.m33;

    out.m10 = left_.m10 * right_.m00 + left_.m11 * right_.m10 + left_.m12 * right_.m20 + left_.m13 * right_.m30;
    out.m11 = left_.m10 * right_.m01 + left_.m11 * right_.m11 + left_.m12 * right_.m21 + left_.m13 * right_.m31;
    out.m12 = left_.m10 * right_.m02 + left_.m11 * right_.m12 + left_.m12 * right_.m22 + left_.m13 * right_.m32;
    out.m13 = left_.m10 * right_.m03 + left_.m11 * right_.m13 + left_.m12 * right_.m23 + left_.m13 * right_.m33;

    out.m20 = left_.m20 * right_.m00 + left_.m21 * right_.m10 + left_.m22 * right_.m20 + left_.m23 * right_.m30;
    out.m21 = left_.m20 * right_.m01 + left_.m21 * right_.m11 + left_.m22 * right_.m21 + left_.m23 * right_.m31;
    out.m22 = left_.m20 * right_.m02 + left_.m21 * right_.m12 + left_.m22 * right_.m22 + left_.m23 * right_.m32;
    out.m23 = left_.m20 * right_.m03 + left_.m21 * right_.m13 + left_.m22 * right_.m23 + left_.m23 * right_.m33;

    out.m30 = left_.m30 * right_.m00 + left_.m31 * right_.m10 + left_.m32 * right_.m20 + left_.m33 * right_.m30;
    out.m31 = left_.m30 * right_.m01 + left_.m31 * right_.m11 + left_.m32 * right_.m21 + left_.m33 * right_.m31;
    out.m32 = left_.m30 * right_.m02 + left_.m31 * right_.m12 + left_.m32 * right_.m22 + left_.m33 * right_.m32;
    out.m33 = left_.m30 * right_.m03 + left_.m31 * right_.m13 + left_.m32 * right_.m23 + left_.m33 * right_.m33;

    return out;
}

[[nodiscard]] inline bool InvertAffineMatrix4x4(const Matrix4x4& in_,
                                                Matrix4x4& out_) noexcept {
    const float a00 = in_.m00;
    const float a01 = in_.m01;
    const float a02 = in_.m02;
    const float a10 = in_.m10;
    const float a11 = in_.m11;
    const float a12 = in_.m12;
    const float a20 = in_.m20;
    const float a21 = in_.m21;
    const float a22 = in_.m22;

    const float c00 = a11 * a22 - a12 * a21;
    const float c01 = a02 * a21 - a01 * a22;
    const float c02 = a01 * a12 - a02 * a11;

    const float c10 = a12 * a20 - a10 * a22;
    const float c11 = a00 * a22 - a02 * a20;
    const float c12 = a02 * a10 - a00 * a12;

    const float c20 = a10 * a21 - a11 * a20;
    const float c21 = a01 * a20 - a00 * a21;
    const float c22 = a00 * a11 - a01 * a10;

    const float det = a00 * c00 + a01 * c10 + a02 * c20;
    if (std::abs(det) <= 1e-10F) {
        out_ = IdentityMatrix4x4();
        return false;
    }

    const float inv_det = 1.0F / det;
    const float i00 = c00 * inv_det;
    const float i01 = c01 * inv_det;
    const float i02 = c02 * inv_det;
    const float i10 = c10 * inv_det;
    const float i11 = c11 * inv_det;
    const float i12 = c12 * inv_det;
    const float i20 = c20 * inv_det;
    const float i21 = c21 * inv_det;
    const float i22 = c22 * inv_det;

    const float tx = in_.m03;
    const float ty = in_.m13;
    const float tz = in_.m23;

    out_.m00 = i00;
    out_.m01 = i01;
    out_.m02 = i02;
    out_.m03 = -(i00 * tx + i01 * ty + i02 * tz);

    out_.m10 = i10;
    out_.m11 = i11;
    out_.m12 = i12;
    out_.m13 = -(i10 * tx + i11 * ty + i12 * tz);

    out_.m20 = i20;
    out_.m21 = i21;
    out_.m22 = i22;
    out_.m23 = -(i20 * tx + i21 * ty + i22 * tz);

    out_.m30 = 0.0F;
    out_.m31 = 0.0F;
    out_.m32 = 0.0F;
    out_.m33 = 1.0F;

    return true;
}

[[nodiscard]] inline Matrix4x4 BuildOrthographicProjection(float left_,
                                                           float right_,
                                                           float bottom_,
                                                           float top_,
                                                           float near_plane_,
                                                           float far_plane_) noexcept {
    float width = right_ - left_;
    float height = top_ - bottom_;
    float depth = near_plane_ - far_plane_;

    if (std::abs(width) <= 1e-6F) {
        width = (width >= 0.0F) ? 1e-6F : -1e-6F;
    }
    if (std::abs(height) <= 1e-6F) {
        height = (height >= 0.0F) ? 1e-6F : -1e-6F;
    }
    if (std::abs(depth) <= 1e-6F) {
        depth = (depth >= 0.0F) ? 1e-6F : -1e-6F;
    }

    Matrix4x4 out{};
    out.m00 = 2.0F / width;
    out.m01 = 0.0F;
    out.m02 = 0.0F;
    out.m03 = -(right_ + left_) / width;

    out.m10 = 0.0F;
    out.m11 = 2.0F / height;
    out.m12 = 0.0F;
    out.m13 = -(top_ + bottom_) / height;

    out.m20 = 0.0F;
    out.m21 = 0.0F;
    out.m22 = 1.0F / depth;
    out.m23 = near_plane_ / depth;

    out.m30 = 0.0F;
    out.m31 = 0.0F;
    out.m32 = 0.0F;
    out.m33 = 1.0F;
    return out;
}

[[nodiscard]] inline Matrix4x4 BuildPerspectiveProjection(float vertical_fov_radians_,
                                                          float aspect_ratio_,
                                                          float near_plane_,
                                                          float far_plane_,
                                                          bool reverse_z_) noexcept {
    const float fov = std::clamp(vertical_fov_radians_,
                                 1e-3F,
                                 3.13F);
    const float aspect = std::max(1e-6F, aspect_ratio_);
    const float near_plane = std::max(1e-4F, near_plane_);
    const float far_plane = std::max(near_plane + 1e-3F, far_plane_);

    const float cot = 1.0F / std::tan(fov * 0.5F);

    Matrix4x4 out{};
    out.m00 = cot / aspect;
    out.m01 = 0.0F;
    out.m02 = 0.0F;
    out.m03 = 0.0F;

    out.m10 = 0.0F;
    out.m11 = cot;
    out.m12 = 0.0F;
    out.m13 = 0.0F;

    out.m20 = 0.0F;
    out.m21 = 0.0F;
    out.m30 = 0.0F;
    out.m31 = 0.0F;
    out.m33 = 0.0F;

    if (reverse_z_) {
        out.m22 = near_plane / (far_plane - near_plane);
        out.m23 = (far_plane * near_plane) / (far_plane - near_plane);
    } else {
        out.m22 = far_plane / (near_plane - far_plane);
        out.m23 = (far_plane * near_plane) / (near_plane - far_plane);
    }

    out.m32 = -1.0F;
    return out;
}

} // namespace vr::ecs::spatial_math
