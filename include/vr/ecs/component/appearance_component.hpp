#pragma once

#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::ecs {

enum AppearanceDirtyFlags : std::uint32_t {
    appearance_dirty_style_flag = 1U << 0U,
    appearance_dirty_binding_flag = 1U << 1U,
};

enum class AppearanceBlendMode : std::uint8_t {
    opaque = 0U,
    alpha = 1U,
    additive = 2U,
    multiply = 3U,
    premultiplied = 4U,
};

enum class AppearanceAlphaMode : std::uint8_t {
    opaque = 0U,
    mask = 1U,
    blend = 2U,
};

enum class AppearancePaintMode : std::uint8_t {
    solid = 0U,
    linear_gradient = 1U,
    radial_gradient = 2U,
    pattern = 3U,
};

enum class AppearanceShadingModel3D : std::uint8_t {
    unlit = 0U,
    lit_pbr = 1U,
    lit_blinn = 2U,
};

struct AppearanceHandle final {
    std::uint32_t index;
    std::uint32_t generation;
};

inline constexpr std::uint32_t invalid_appearance_index = (std::numeric_limits<std::uint32_t>::max)();
inline constexpr AppearanceHandle invalid_appearance_handle{
    .index = invalid_appearance_index,
    .generation = 0U
};

struct AppearanceRuntimeCommon final {
    std::uint32_t revision_style;
    std::uint32_t revision_binding;
    std::uint32_t upload_revision;
    std::uint32_t dirty_flags;

    std::uint64_t pipeline_key;
    std::uint64_t resource_key;
    std::uint64_t sort_key;

    AppearanceHandle gpu_record_handle;
    std::uint32_t gpu_record_index;
    std::uint8_t visible;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

struct AppearanceStyle2D final {
    Rgba8 fill_color;
    Rgba8 stroke_color;
    float stroke_width_px;
    float opacity;

    float gradient_p0_x;
    float gradient_p0_y;
    float gradient_p1_x;
    float gradient_p1_y;
    float gradient_radius;

    std::int16_t layer;
    AppearanceBlendMode blend_mode;
    AppearanceAlphaMode alpha_mode;
    AppearancePaintMode paint_mode;
    std::uint8_t antialiasing;
    std::uint8_t premultiplied_alpha;
    std::uint16_t reserved0;
};

struct AppearanceStyle3D final {
    Rgba8 base_color;
    Rgba8 emissive_color;
    float opacity;
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    float emissive_intensity;
    float alpha_cutoff;

    std::int16_t layer;
    AppearanceBlendMode blend_mode;
    AppearanceAlphaMode alpha_mode;
    AppearanceShadingModel3D shading_model;
    std::uint8_t double_sided;
    std::uint8_t cast_shadow;
    std::uint8_t receive_shadow;
    std::uint8_t reserved0;
};

struct AppearanceBinding2D final {
    std::uint32_t texture_base_id;
    std::uint32_t texture_mask_id;
    std::uint32_t texture_lut_id;
    std::uint32_t sampler_state_id;
    std::uint32_t binding_layout_id;
    std::uint32_t reserved0;
};

struct AppearanceBinding3D final {
    std::uint32_t texture_base_color_id;
    std::uint32_t texture_normal_id;
    std::uint32_t texture_metal_rough_id;
    std::uint32_t texture_occlusion_id;
    std::uint32_t texture_emissive_id;
    std::uint32_t sampler_state_id;
    std::uint32_t binding_layout_id;
    std::uint32_t reserved0;
};

template<DimensionTag DimensionT>
struct AppearanceComponent;

template<>
struct AppearanceComponent<Dim2> final {
    using StyleType = AppearanceStyle2D;
    using BindingType = AppearanceBinding2D;

    StyleType style;
    BindingType binding;
    AppearanceRuntimeCommon runtime;
};

template<>
struct AppearanceComponent<Dim3> final {
    using StyleType = AppearanceStyle3D;
    using BindingType = AppearanceBinding3D;

    StyleType style;
    BindingType binding;
    AppearanceRuntimeCommon runtime;
};

template<DimensionTag DimensionT>
using Appearance = AppearanceComponent<DimensionT>;

template<typename T>
concept PurePodAppearanceComponent = std::is_standard_layout_v<T> &&
                                     std::is_trivial_v<T>;

static_assert(PurePodAppearanceComponent<AppearanceHandle>);
static_assert(PurePodAppearanceComponent<AppearanceRuntimeCommon>);
static_assert(PurePodAppearanceComponent<AppearanceStyle2D>);
static_assert(PurePodAppearanceComponent<AppearanceStyle3D>);
static_assert(PurePodAppearanceComponent<AppearanceBinding2D>);
static_assert(PurePodAppearanceComponent<AppearanceBinding3D>);
static_assert(PurePodAppearanceComponent<Appearance<Dim2>>);
static_assert(PurePodAppearanceComponent<Appearance<Dim3>>);
static_assert(sizeof(AppearanceRuntimeCommon) <= 64U);

} // namespace vr::ecs

