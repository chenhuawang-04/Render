#pragma once

#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"
#include "vr/ecs/system/visual_runtime_route_common.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

enum SurfaceDirtyFlags : std::uint32_t {
    surface_dirty_source_flag = 1U << 0U,
    surface_dirty_style_flag = 1U << 1U,
    surface_dirty_runtime_flag = 1U << 2U,
};

enum class SurfaceRenderPassHint : std::uint8_t {
    overlay = 0U,
    opaque = 1U,
    transparent = 2U,
};

enum class Surface2DSourceKind : std::uint8_t {
    none = 0U,
    image = 1U,
    sprite = 2U,
};

enum class Surface3DFilterMode : std::uint8_t {
    linear = 0U,
    nearest = 1U,
    anisotropic = 2U,
};

enum class Surface3DAddressMode : std::uint8_t {
    wrap = 0U,
    clamp = 1U,
    mirror = 2U,
};

struct SurfaceRuntimeRoute final {
    VR_ECS_VISUAL_RUNTIME_ROUTE_SORT_KEY_FIELD();
    std::uint32_t surface_id;
    VR_ECS_VISUAL_RUNTIME_ROUTE_TRAILING_FIELDS(SurfaceRenderPassHint);
};

struct Surface2DSourceRoute final {
    std::uint32_t surface_id;
    std::uint32_t atlas_page_id;
    Surface2DSourceKind source_kind;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

struct Surface3DSourceRoute final {
    std::uint32_t surface_id;
    std::uint32_t sampler_id;
    std::uint16_t uv_set;
    std::uint16_t flags;
};

struct SurfaceStyle2D final {
    float uv_u0;
    float uv_v0;
    float uv_u1;
    float uv_v1;
    std::uint8_t flip_x;
    std::uint8_t flip_y;
    std::uint8_t reserved0;
    std::uint32_t reserved1;
};

struct SurfaceStyle3D final {
    float uv_scale_u;
    float uv_scale_v;
    float uv_bias_u;
    float uv_bias_v;
    Surface3DFilterMode filter_mode;
    Surface3DAddressMode address_u;
    Surface3DAddressMode address_v;
    Surface3DAddressMode address_w;
    std::uint8_t reserved0;
    std::uint32_t reserved1;
};

struct SurfaceRuntime2D final {
    SurfaceRuntimeRoute route;
    Surface2DSourceRoute source;
    std::uint32_t source_revision;
    std::uint32_t reserved0;
    AppearanceRuntimeBridge2D appearance;
    Float2 size;
    Float2 pivot;
};

struct SurfaceRuntime3D final {
    SurfaceRuntimeRoute route;
    Surface3DSourceRoute source;
    std::uint32_t source_revision;
    AppearanceRuntimeBridge3D appearance;
};

[[nodiscard]] constexpr AppearanceRuntimeBridge2D ReadAppearanceRuntimeBridge2D(
    const SurfaceRuntime2D& runtime_) noexcept {
    return runtime_.appearance;
}

[[nodiscard]] constexpr bool HasSameAppearanceRuntimeBridge2D(
    const SurfaceRuntime2D& runtime_,
    const AppearanceRuntimeBridge2D& bridge_) noexcept {
    return IsSameAppearanceRuntimeBridge2D(runtime_.appearance, bridge_);
}

constexpr void StoreAppearanceRuntimeBridge2D(SurfaceRuntime2D& runtime_,
                                              const AppearanceRuntimeBridge2D& bridge_) noexcept {
    runtime_.appearance = bridge_;
}

[[nodiscard]] constexpr AppearanceRuntimeBridge3D ReadAppearanceRuntimeBridge3D(
    const SurfaceRuntime3D& runtime_) noexcept {
    return runtime_.appearance;
}

[[nodiscard]] constexpr bool HasSameAppearanceRuntimeBridge3D(
    const SurfaceRuntime3D& runtime_,
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return IsSameAppearanceRuntimeBridge3D(runtime_.appearance, bridge_);
}

constexpr void StoreAppearanceRuntimeBridge3D(SurfaceRuntime3D& runtime_,
                                              const AppearanceRuntimeBridge3D& bridge_) noexcept {
    runtime_.appearance = bridge_;
}

template<DimensionTag DimensionT>
struct SurfaceComponent;

template<>
struct SurfaceComponent<Dim2> final {
    using StyleType = SurfaceStyle2D;
    using RuntimeType = SurfaceRuntime2D;

    StyleType style;
    RuntimeType runtime;
};

template<>
struct SurfaceComponent<Dim3> final {
    using StyleType = SurfaceStyle3D;
    using RuntimeType = SurfaceRuntime3D;

    StyleType style;
    RuntimeType runtime;
};

template<DimensionTag DimensionT>
using Surface = SurfaceComponent<DimensionT>;

template<typename T>
concept PurePodSurfaceComponent = std::is_standard_layout_v<T> &&
                                  std::is_trivial_v<T>;

static_assert(PurePodSurfaceComponent<SurfaceRuntimeRoute>);
static_assert(PurePodSurfaceComponent<Surface2DSourceRoute>);
static_assert(PurePodSurfaceComponent<Surface3DSourceRoute>);
static_assert(PurePodSurfaceComponent<SurfaceStyle2D>);
static_assert(PurePodSurfaceComponent<SurfaceStyle3D>);
static_assert(PurePodSurfaceComponent<SurfaceRuntime2D>);
static_assert(PurePodSurfaceComponent<SurfaceRuntime3D>);
static_assert(PurePodSurfaceComponent<Surface<Dim2>>);
static_assert(PurePodSurfaceComponent<Surface<Dim3>>);
static_assert(sizeof(SurfaceRuntimeRoute) <= 48U);

} // namespace vr::ecs

