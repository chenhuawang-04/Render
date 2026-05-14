#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

enum TransformDirtyFlags : std::uint32_t {
    transform_dirty_local_flag = 1U << 0U,
    transform_dirty_world_flag = 1U << 1U,
    transform_dirty_hierarchy_flag = 1U << 2U,
};

struct TransformHierarchyLink final {
    std::int32_t parent_index;
    std::int32_t first_child_index;
    std::int32_t next_sibling_index;
    std::int32_t reserved0;
};

struct TransformStyle2D final {
    Float2 position;
    float rotation_radians;
    Float2 scale;
};

struct TransformRuntime2D final {
    Affine2x3 local_matrix;
    Affine2x3 world_matrix;
    TransformHierarchyLink hierarchy;
    std::uint32_t local_revision;
    std::uint32_t world_revision;
    std::uint32_t cached_parent_world_revision;
    std::uint32_t dirty_flags;
};

struct TransformStyle3D final {
    Float3 position;
    Quaternion rotation;
    Float3 scale;
    std::uint32_t reserved0;
};

struct TransformRuntime3D final {
    Matrix4x4 local_matrix;
    Matrix4x4 world_matrix;
    TransformHierarchyLink hierarchy;
    std::uint32_t local_revision;
    std::uint32_t world_revision;
    std::uint32_t cached_parent_world_revision;
    std::uint32_t dirty_flags;
};

template<DimensionTag DimensionT>
struct TransformComponent;

template<>
struct TransformComponent<Dim2> final {
    using StyleType = TransformStyle2D;
    using RuntimeType = TransformRuntime2D;

    StyleType style;
    RuntimeType runtime;
};

template<>
struct TransformComponent<Dim3> final {
    using StyleType = TransformStyle3D;
    using RuntimeType = TransformRuntime3D;

    StyleType style;
    RuntimeType runtime;
};

template<DimensionTag DimensionT>
using Transform = TransformComponent<DimensionT>;

static_assert(std::is_standard_layout_v<TransformHierarchyLink> &&
              std::is_trivial_v<TransformHierarchyLink>);
static_assert(std::is_standard_layout_v<TransformStyle2D> &&
              std::is_trivial_v<TransformStyle2D>);
static_assert(std::is_standard_layout_v<TransformRuntime2D> &&
              std::is_trivial_v<TransformRuntime2D>);
static_assert(std::is_standard_layout_v<TransformStyle3D> &&
              std::is_trivial_v<TransformStyle3D>);
static_assert(std::is_standard_layout_v<TransformRuntime3D> &&
              std::is_trivial_v<TransformRuntime3D>);
static_assert(std::is_standard_layout_v<Transform<Dim2>> &&
              std::is_trivial_v<Transform<Dim2>>);
static_assert(std::is_standard_layout_v<Transform<Dim3>> &&
              std::is_trivial_v<Transform<Dim3>>);

} // namespace vr::ecs

