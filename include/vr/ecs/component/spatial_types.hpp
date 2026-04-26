#pragma once

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

struct Float2 final {
    float x;
    float y;
};

struct Float3 final {
    float x;
    float y;
    float z;
};

struct Float4 final {
    float x;
    float y;
    float z;
    float w;
};

struct Quaternion final {
    float x;
    float y;
    float z;
    float w;
};

// Row-major 2x3 affine matrix:
// [ m00 m01 m02 ]
// [ m10 m11 m12 ]
struct Affine2x3 final {
    float m00;
    float m01;
    float m02;

    float m10;
    float m11;
    float m12;
};

// Row-major 4x4 matrix.
struct Matrix4x4 final {
    float m00;
    float m01;
    float m02;
    float m03;

    float m10;
    float m11;
    float m12;
    float m13;

    float m20;
    float m21;
    float m22;
    float m23;

    float m30;
    float m31;
    float m32;
    float m33;
};

static_assert(std::is_standard_layout_v<Float2> && std::is_trivial_v<Float2>);
static_assert(std::is_standard_layout_v<Float3> && std::is_trivial_v<Float3>);
static_assert(std::is_standard_layout_v<Float4> && std::is_trivial_v<Float4>);
static_assert(std::is_standard_layout_v<Quaternion> && std::is_trivial_v<Quaternion>);
static_assert(std::is_standard_layout_v<Affine2x3> && std::is_trivial_v<Affine2x3>);
static_assert(std::is_standard_layout_v<Matrix4x4> && std::is_trivial_v<Matrix4x4>);

} // namespace vr::ecs
