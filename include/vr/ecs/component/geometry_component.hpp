#pragma once

#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"
#include "vr/ecs/system/visual_runtime_route_common.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

enum GeometryDirtyFlags : std::uint32_t {
    geometry_dirty_data_flag = 1U << 0U,
    geometry_dirty_style_flag = 1U << 1U,
    geometry_dirty_runtime_flag = 1U << 2U,
    geometry_dirty_bounds_flag = 1U << 3U,
};

enum class GeometryRenderPassHint : std::uint8_t {
    overlay = 0U,
    opaque = 1U,
    transparent = 2U,
};

enum class Geometry2DTopology : std::uint8_t {
    fill = 0U,
    stroke = 1U,
    fill_and_stroke = 2U,
};

enum class Geometry2DFillRule : std::uint8_t {
    non_zero = 0U,
    even_odd = 1U,
};

enum class Geometry2DLineJoin : std::uint8_t {
    miter = 0U,
    round = 1U,
    bevel = 2U,
};

enum class Geometry2DLineCap : std::uint8_t {
    butt = 0U,
    round = 1U,
    square = 2U,
};

enum class Geometry3DTopology : std::uint8_t {
    triangles = 0U,
    lines = 1U,
    points = 2U,
};

struct GeometryPathInlineData final {
    static constexpr std::uint32_t inline_capacity_bytes = 1024U;

    std::uint32_t size_bytes;
    std::uint32_t capacity_bytes;
    std::uint32_t revision;
    std::uint32_t reserved;
    std::uint8_t bytes[inline_capacity_bytes];
};

struct GeometryMeshRoute final {
    std::uint32_t submesh_index;
    std::uint16_t lod_index;
    std::uint16_t flags;
};

enum GeometryMeshAnimationFlags : std::uint16_t {
    geometry_mesh_vertex_deform_shader_flag = 1U << 0U,
    geometry_mesh_frame_sequence_submesh_flag = 1U << 1U,
    geometry_mesh_skeletal_root_motion_flag = 1U << 2U,
    geometry_mesh_morph_targets_flag = 1U << 3U,
    geometry_mesh_skeletal_skinning_flag = 1U << 4U,
};

struct GeometryRuntimeRoute final {
    VR_ECS_VISUAL_RUNTIME_ROUTE_SORT_KEY_FIELD();
    std::uint32_t geometry_id;
    VR_ECS_VISUAL_RUNTIME_ROUTE_TRAILING_FIELDS(GeometryRenderPassHint);
};

struct GeometryStyle2D final {
    float stroke_width_px;
    float miter_limit;
    Geometry2DTopology topology;
    Geometry2DFillRule fill_rule;
    Geometry2DLineJoin line_join;
    Geometry2DLineCap line_cap;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
    std::uint32_t reserved2;
};

struct GeometryStyle3D final {
    Geometry3DTopology topology;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
    float line_width;
};

struct GeometryRuntime2D final {
    GeometryRuntimeRoute route;
    std::uint32_t path_command_count;
    std::uint32_t tessellation_revision;
    std::uint32_t path_data_hash;
    std::uint32_t reserved0;
    AppearanceRuntimeBridge2D appearance;
    Float2 bounds_min;
    Float2 bounds_max;
};

struct GeometryRuntime3D final {
    GeometryRuntimeRoute route;
    std::uint32_t mesh_revision;
    std::uint32_t meshlet_count_hint;
    AppearanceRuntimeBridge3D appearance;
    Float3 bounds_min;
    Float3 bounds_max;
};

[[nodiscard]] constexpr AppearanceRuntimeBridge2D ReadAppearanceRuntimeBridge2D(
    const GeometryRuntime2D& runtime_) noexcept {
    return runtime_.appearance;
}

[[nodiscard]] constexpr bool HasSameAppearanceRuntimeBridge2D(
    const GeometryRuntime2D& runtime_,
    const AppearanceRuntimeBridge2D& bridge_) noexcept {
    return IsSameAppearanceRuntimeBridge2D(runtime_.appearance, bridge_);
}

constexpr void StoreAppearanceRuntimeBridge2D(GeometryRuntime2D& runtime_,
                                              const AppearanceRuntimeBridge2D& bridge_) noexcept {
    runtime_.appearance = bridge_;
}

[[nodiscard]] constexpr AppearanceRuntimeBridge3D ReadAppearanceRuntimeBridge3D(
    const GeometryRuntime3D& runtime_) noexcept {
    return runtime_.appearance;
}

[[nodiscard]] constexpr bool HasSameAppearanceRuntimeBridge3D(
    const GeometryRuntime3D& runtime_,
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return IsSameAppearanceRuntimeBridge3D(runtime_.appearance, bridge_);
}

constexpr void StoreAppearanceRuntimeBridge3D(GeometryRuntime3D& runtime_,
                                              const AppearanceRuntimeBridge3D& bridge_) noexcept {
    runtime_.appearance = bridge_;
}

template<DimensionTag DimensionT>
struct GeometryComponent;

template<>
struct GeometryComponent<Dim2> final {
    using StyleType = GeometryStyle2D;
    using RuntimeType = GeometryRuntime2D;

    GeometryPathInlineData path;
    StyleType style;
    RuntimeType runtime;
};

template<>
struct GeometryComponent<Dim3> final {
    using StyleType = GeometryStyle3D;
    using RuntimeType = GeometryRuntime3D;

    GeometryMeshRoute mesh;
    StyleType style;
    RuntimeType runtime;
};

template<DimensionTag DimensionT>
using Geometry = GeometryComponent<DimensionT>;

template<typename T>
concept PurePodGeometryComponent = std::is_standard_layout_v<T> &&
                                   std::is_trivial_v<T>;

static_assert(PurePodGeometryComponent<GeometryPathInlineData>);
static_assert(PurePodGeometryComponent<GeometryMeshRoute>);
static_assert(PurePodGeometryComponent<GeometryRuntimeRoute>);
static_assert(PurePodGeometryComponent<GeometryStyle2D>);
static_assert(PurePodGeometryComponent<GeometryStyle3D>);
static_assert(PurePodGeometryComponent<GeometryRuntime2D>);
static_assert(PurePodGeometryComponent<GeometryRuntime3D>);
static_assert(PurePodGeometryComponent<Geometry<Dim2>>);
static_assert(PurePodGeometryComponent<Geometry<Dim3>>);
static_assert(sizeof(GeometryRuntimeRoute) <= 48U);

} // namespace vr::ecs

