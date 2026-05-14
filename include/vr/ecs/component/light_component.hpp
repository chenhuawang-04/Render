#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::ecs {

enum LightDirtyFlags : std::uint32_t {
    light_dirty_style_flag = 1U << 0U,
    light_dirty_binding_flag = 1U << 1U,
    light_dirty_runtime_flag = 1U << 2U,
};

enum class LightKind : std::uint8_t {
    global = 0U,
    directional = 1U,
    point = 2U,
    spot = 3U,
};

enum class LightFalloffMode : std::uint8_t {
    inverse_square = 0U,
    smooth = 1U,
    linear = 2U,
};

enum class Light2DBlendMode : std::uint8_t {
    additive = 0U,
    alpha = 1U,
    multiply = 2U,
};

enum class ShadowFilterMode : std::uint8_t {
    none = 0U,
    pcf3x3 = 1U,
    pcf5x5 = 2U,
};

struct LightHandle final {
    std::uint32_t index;
    std::uint32_t generation;
};

inline constexpr std::uint32_t invalid_light_index = (std::numeric_limits<std::uint32_t>::max)();
inline constexpr LightHandle invalid_light_handle{
    .index = invalid_light_index,
    .generation = 0U,
};

struct ResourceRef final {
    std::uint32_t texture_id;
    std::uint32_t sampler_id;
};

struct ShadowConfig final {
    std::uint16_t resolution;
    std::uint8_t cascade_count;
    ShadowFilterMode filter_mode;
};

struct LightRuntimeState final {
    std::uint32_t revision_style;
    std::uint32_t revision_binding;
    std::uint32_t upload_revision;
    std::uint32_t dirty_flags;
};

struct LightRuntimeGpu final {
    std::uint64_t pipeline_key;
    std::uint64_t resource_key;
    std::uint64_t sort_key;
    std::uint32_t gpu_record_index;
    LightHandle handle;
};

struct LightRuntimeVisibility final {
    std::uint32_t light_channel_mask;
    std::uint8_t visible;
    std::uint8_t cast_shadow_resolved;
    std::uint16_t reserved0;
};

struct LightStyle2D final {
    Rgba8 color;
    float intensity;
    float range;
    float falloff_exponent;
    float inner_angle_radians;
    float outer_angle_radians;
    float source_height;
    std::int16_t layer;
    LightKind kind;
    Light2DBlendMode blend_mode;
    std::uint8_t cast_shadow;
    std::uint8_t affect_normals_only;
    std::uint16_t reserved0;
};

struct LightStyle3D final {
    Rgba8 color;
    float intensity;
    float range;
    float falloff_exponent;
    float inner_angle_radians;
    float outer_angle_radians;
    float source_height;
    float source_radius;
    float source_length;
    float temperature_kelvin;
    float volumetric_strength;
    LightKind kind;
    LightFalloffMode falloff_mode;
    std::uint8_t cast_shadow;
    std::uint8_t reserved0;
};

struct LightBinding2D final {
    ResourceRef cookie;
    ResourceRef occluder;
    std::uint32_t reserved0;
};

struct LightBinding3D final {
    ResourceRef cookie;
    ResourceRef ies;
    ResourceRef shadow;
    ShadowConfig shadow_config;
};

template<DimensionTag DimensionT>
struct LightComponent;

template<>
struct LightComponent<Dim2> final {
    using StyleType = LightStyle2D;
    using BindingType = LightBinding2D;

    StyleType style;
    BindingType binding;
    LightRuntimeState state;
    LightRuntimeGpu gpu;
    LightRuntimeVisibility visibility;
};

template<>
struct LightComponent<Dim3> final {
    using StyleType = LightStyle3D;
    using BindingType = LightBinding3D;

    StyleType style;
    BindingType binding;
    LightRuntimeState state;
    LightRuntimeGpu gpu;
    LightRuntimeVisibility visibility;
};

template<DimensionTag DimensionT>
using Light = LightComponent<DimensionT>;

template<typename T>
concept PurePodLightComponent = std::is_standard_layout_v<T> &&
                                std::is_trivial_v<T>;

static_assert(PurePodLightComponent<LightHandle>);
static_assert(PurePodLightComponent<ResourceRef>);
static_assert(PurePodLightComponent<ShadowConfig>);
static_assert(PurePodLightComponent<LightRuntimeState>);
static_assert(PurePodLightComponent<LightRuntimeGpu>);
static_assert(PurePodLightComponent<LightRuntimeVisibility>);
static_assert(PurePodLightComponent<LightStyle2D>);
static_assert(PurePodLightComponent<LightStyle3D>);
static_assert(PurePodLightComponent<LightBinding2D>);
static_assert(PurePodLightComponent<LightBinding3D>);
static_assert(PurePodLightComponent<Light<Dim2>>);
static_assert(PurePodLightComponent<Light<Dim3>>);

} // namespace vr::ecs


