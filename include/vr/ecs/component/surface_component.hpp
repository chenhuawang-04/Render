#pragma once

#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"

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

enum class Surface2DBlendMode : std::uint8_t {
    alpha = 0U,
    additive = 1U,
    multiply = 2U,
    screen = 3U,
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
    std::uint64_t sort_key;
    std::uint32_t surface_id;
    std::uint32_t material_id;
    std::uint32_t batch_tag;
    std::uint32_t user_data;
    AppearanceHandle appearance_handle;
    std::uint32_t appearance_pipeline_bucket;
    std::uint32_t appearance_resource_bucket;
    std::uint16_t depth_bin;
    std::uint8_t visible;
    SurfaceRenderPassHint pass_hint;
    std::uint32_t dirty_flags;
};

struct Surface2DSourceRoute final {
    std::uint32_t image_id;
    std::uint32_t sprite_id;
    std::uint32_t atlas_page_id;
    Surface2DSourceKind source_kind;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

struct Surface3DTextureRoute final {
    std::uint32_t texture_id;
    std::uint32_t sampler_id;
    std::uint16_t uv_set;
    std::uint16_t flags;
};

struct SurfaceStyle2D final {
    float uv_u0;
    float uv_v0;
    float uv_u1;
    float uv_v1;
    Rgba8 tint_color;
    float opacity;
    std::int16_t layer;
    Surface2DBlendMode blend_mode;
    std::uint8_t flip_x;
    std::uint8_t flip_y;
    std::uint8_t premultiplied_alpha;
    std::uint8_t reserved0;
};

struct SurfaceStyle3D final {
    Rgba8 tint_color;
    float uv_scale_u;
    float uv_scale_v;
    float uv_bias_u;
    float uv_bias_v;
    float opacity;
    std::uint8_t depth_test;
    std::uint8_t depth_write;
    std::uint8_t double_sided;
    Surface3DFilterMode filter_mode;
    Surface3DAddressMode address_u;
    Surface3DAddressMode address_v;
    Surface3DAddressMode address_w;
    std::uint8_t reserved0;
};

struct SurfaceRuntime2D final {
    SurfaceRuntimeRoute route;
    Surface2DSourceRoute source;
    std::uint32_t source_revision;
    std::uint32_t reserved0;
    Float2 size;
    Float2 pivot;
};

struct SurfaceRuntime3D final {
    SurfaceRuntimeRoute route;
    Surface3DTextureRoute texture;
    std::uint32_t texture_revision;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};

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
static_assert(PurePodSurfaceComponent<Surface3DTextureRoute>);
static_assert(PurePodSurfaceComponent<SurfaceStyle2D>);
static_assert(PurePodSurfaceComponent<SurfaceStyle3D>);
static_assert(PurePodSurfaceComponent<SurfaceRuntime2D>);
static_assert(PurePodSurfaceComponent<SurfaceRuntime3D>);
static_assert(PurePodSurfaceComponent<Surface<Dim2>>);
static_assert(PurePodSurfaceComponent<Surface<Dim3>>);
static_assert(sizeof(SurfaceRuntimeRoute) <= 48U);

} // namespace vr::ecs
