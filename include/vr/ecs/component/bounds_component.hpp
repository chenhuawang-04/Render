#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::ecs {

enum BoundsDirtyFlags : std::uint32_t {
    bounds_dirty_local_flag = 1U << 0U,
    bounds_dirty_runtime_flag = 1U << 1U,
    bounds_dirty_visibility_flag = 1U << 2U,
};

struct BoundsStyle2D final {
    Float2 local_min;
    Float2 local_max;
};

struct BoundsRuntime2D final {
    Float2 world_min;
    Float2 world_max;
    Float2 world_center;
    Float2 world_extents;
    float world_radius;
    std::uint32_t local_revision;
    std::uint32_t world_revision;
    std::uint32_t transform_world_revision;
    std::uint32_t visibility_mask;
    std::uint32_t dirty_flags;
    std::uint32_t reserved0;
};

struct BoundsStyle3D final {
    Float3 local_min;
    float reserved0;
    Float3 local_max;
    float reserved1;
};

struct BoundsRuntime3D final {
    Float3 world_min;
    float world_radius;
    Float3 world_max;
    float reserved0;
    Float3 world_center;
    std::uint32_t local_revision;
    Float3 world_extents;
    std::uint32_t world_revision;
    std::uint32_t transform_world_revision;
    std::uint32_t visibility_mask;
    std::uint32_t dirty_flags;
    std::uint32_t reserved1;
};

template<DimensionTag DimensionT>
struct BoundsComponent;

template<>
struct BoundsComponent<Dim2> final {
    using StyleType = BoundsStyle2D;
    using RuntimeType = BoundsRuntime2D;

    StyleType style;
    RuntimeType runtime;
};

template<>
struct BoundsComponent<Dim3> final {
    using StyleType = BoundsStyle3D;
    using RuntimeType = BoundsRuntime3D;

    StyleType style;
    RuntimeType runtime;
};

template<DimensionTag DimensionT>
using Bounds = BoundsComponent<DimensionT>;

template<typename T>
concept PurePodBoundsComponent = std::is_standard_layout_v<T> &&
                                 std::is_trivial_v<T>;

[[nodiscard]] constexpr std::uint32_t NextBoundsRevision(std::uint32_t current_revision_) noexcept {
    return (current_revision_ == std::numeric_limits<std::uint32_t>::max())
        ? 1U
        : (current_revision_ + 1U);
}

static_assert(PurePodBoundsComponent<BoundsStyle2D>);
static_assert(PurePodBoundsComponent<BoundsRuntime2D>);
static_assert(PurePodBoundsComponent<BoundsStyle3D>);
static_assert(PurePodBoundsComponent<BoundsRuntime3D>);
static_assert(PurePodBoundsComponent<Bounds<Dim2>>);
static_assert(PurePodBoundsComponent<Bounds<Dim3>>);

} // namespace vr::ecs


