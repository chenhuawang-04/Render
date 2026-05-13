#pragma once

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <immintrin.h>
#endif

#include "fast_math/mat3.h"
#include "fast_math/mat4.h"
#include "fast_math/types.h"

#include <type_traits>

namespace vr::ecs {

using Float2 = MMath::Vec2;
using Float3 = MMath::Vec3;
using Float4 = MMath::Vec4;
using Quaternion = MMath::Quat;

using Affine2x3 = MMath::Mat3;
using Matrix4x4 = MMath::Mat4;

static_assert(std::is_standard_layout_v<Float2> && std::is_trivial_v<Float2>);
static_assert(std::is_standard_layout_v<Float3> && std::is_trivial_v<Float3>);
static_assert(std::is_standard_layout_v<Float4> && std::is_trivial_v<Float4>);
static_assert(std::is_standard_layout_v<Quaternion> && std::is_trivial_v<Quaternion>);
static_assert(std::is_standard_layout_v<Affine2x3> && std::is_trivial_v<Affine2x3>);
static_assert(std::is_standard_layout_v<Matrix4x4> && std::is_trivial_v<Matrix4x4>);

} // namespace vr::ecs
