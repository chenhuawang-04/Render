#pragma once

#include "fast_math/mat4.h"

namespace MMath::D3D {

using Mat4 = ::MMath::Mat4;

[[nodiscard]] inline Mat4 mat4Identity() noexcept {
    return ::MMath::mat4Identity();
}

[[nodiscard]] inline Mat4 mat4Mul(const Mat4& left_,
                                  const Mat4& right_) noexcept {
    return ::MMath::mat4Multiply(left_, right_);
}

[[nodiscard]] inline Mat4 mat4PerspectiveD3D_RH(float fovy_,
                                                float aspect_,
                                                float near_,
                                                float far_) noexcept {
    return ::MMath::mat4PerspectiveRH(fovy_, aspect_, near_, far_);
}

[[nodiscard]] inline Mat4 mat4OrthoD3D_RH(float left_,
                                          float right_,
                                          float bottom_,
                                          float top_,
                                          float near_,
                                          float far_) noexcept {
    return ::MMath::mat4OrthoRH(left_, right_, bottom_, top_, near_, far_);
}

} // namespace MMath::D3D
