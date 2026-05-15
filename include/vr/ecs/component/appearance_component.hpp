#pragma once

#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"
#include "vr/render/appearance_sampled_surface.hpp"

#include <algorithm>
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
    screen = 5U,
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

inline constexpr Rgba8 default_appearance_style2d_fill_color{255U, 255U, 255U, 255U};
inline constexpr Rgba8 default_appearance_style2d_stroke_color{0U, 0U, 0U, 255U};
inline constexpr float default_appearance_style2d_opacity = 1.0F;
inline constexpr float default_appearance_style2d_gradient_p0_x = 0.0F;
inline constexpr float default_appearance_style2d_gradient_p0_y = 0.0F;
inline constexpr float default_appearance_style2d_gradient_p1_x = 1.0F;
inline constexpr float default_appearance_style2d_gradient_p1_y = 0.0F;
inline constexpr float default_appearance_style2d_gradient_radius = 1.0F;
inline constexpr std::int16_t default_appearance_style2d_layer = 0;
inline constexpr AppearanceBlendMode default_appearance_style2d_blend_mode =
    AppearanceBlendMode::alpha;
inline constexpr AppearanceAlphaMode default_appearance_style2d_alpha_mode =
    AppearanceAlphaMode::blend;
inline constexpr AppearancePaintMode default_appearance_style2d_paint_mode =
    AppearancePaintMode::solid;
inline constexpr std::uint8_t default_appearance_style2d_antialiasing = 1U;
inline constexpr std::uint8_t default_appearance_style2d_premultiplied_alpha = 0U;

struct AppearanceRuntimeBridge2D final {
    Rgba8 fill_color;
    Rgba8 stroke_color;
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

[[nodiscard]] constexpr AppearanceRuntimeBridge2D MakeAppearanceRuntimeBridge2D(
    const AppearanceStyle2D& style_) noexcept {
    return AppearanceRuntimeBridge2D{
        .fill_color = style_.fill_color,
        .stroke_color = style_.stroke_color,
        .opacity = std::clamp(style_.opacity, 0.0F, 1.0F),
        .gradient_p0_x = style_.gradient_p0_x,
        .gradient_p0_y = style_.gradient_p0_y,
        .gradient_p1_x = style_.gradient_p1_x,
        .gradient_p1_y = style_.gradient_p1_y,
        .gradient_radius = std::max(style_.gradient_radius, 0.0F),
        .layer = style_.layer,
        .blend_mode = style_.blend_mode,
        .alpha_mode = style_.alpha_mode,
        .paint_mode = style_.paint_mode,
        .antialiasing = style_.antialiasing,
        .premultiplied_alpha = style_.premultiplied_alpha,
        .reserved0 = 0U
    };
}

[[nodiscard]] constexpr AppearanceRuntimeBridge2D MakeAppearanceRuntimeBridge2D(
    const AppearanceStyle2D* style_) noexcept {
    return (style_ != nullptr)
        ? MakeAppearanceRuntimeBridge2D(*style_)
        : AppearanceRuntimeBridge2D{
              .fill_color = default_appearance_style2d_fill_color,
              .stroke_color = default_appearance_style2d_stroke_color,
              .opacity = default_appearance_style2d_opacity,
              .gradient_p0_x = default_appearance_style2d_gradient_p0_x,
              .gradient_p0_y = default_appearance_style2d_gradient_p0_y,
              .gradient_p1_x = default_appearance_style2d_gradient_p1_x,
              .gradient_p1_y = default_appearance_style2d_gradient_p1_y,
              .gradient_radius = default_appearance_style2d_gradient_radius,
              .layer = default_appearance_style2d_layer,
              .blend_mode = default_appearance_style2d_blend_mode,
              .alpha_mode = default_appearance_style2d_alpha_mode,
              .paint_mode = default_appearance_style2d_paint_mode,
              .antialiasing = default_appearance_style2d_antialiasing,
              .premultiplied_alpha = default_appearance_style2d_premultiplied_alpha,
              .reserved0 = 0U};
}

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
    std::uint8_t depth_test;
    std::uint8_t depth_write;
    std::uint8_t double_sided;
    std::uint8_t cast_shadow;
    std::uint8_t receive_shadow;
    std::uint16_t reserved0;
};

enum AppearanceStyle3DStateFlags : std::uint32_t {
    appearance_style3d_depth_test_flag = 1U << 0U,
    appearance_style3d_depth_write_flag = 1U << 1U,
    appearance_style3d_double_sided_flag = 1U << 2U,
    appearance_style3d_cast_shadow_flag = 1U << 3U,
    appearance_style3d_receive_shadow_flag = 1U << 4U,
};

inline constexpr std::uint32_t default_appearance_style3d_state_flags =
    appearance_style3d_depth_test_flag |
    appearance_style3d_depth_write_flag |
    appearance_style3d_cast_shadow_flag |
    appearance_style3d_receive_shadow_flag;
inline constexpr Rgba8 default_appearance_style3d_base_color{255U, 255U, 255U, 255U};
inline constexpr Rgba8 default_appearance_style3d_emissive_color{0U, 0U, 0U, 255U};
inline constexpr float default_appearance_style3d_opacity = 1.0F;
inline constexpr float default_appearance_style3d_metallic = 0.0F;
inline constexpr float default_appearance_style3d_roughness = 1.0F;
inline constexpr float default_appearance_style3d_normal_scale = 1.0F;
inline constexpr float default_appearance_style3d_occlusion_strength = 1.0F;
inline constexpr float default_appearance_style3d_emissive_intensity = 0.0F;
inline constexpr float default_appearance_style3d_alpha_cutoff = 0.5F;
inline constexpr AppearanceBlendMode default_appearance_style3d_blend_mode =
    AppearanceBlendMode::opaque;
inline constexpr AppearanceAlphaMode default_appearance_style3d_alpha_mode =
    AppearanceAlphaMode::opaque;
inline constexpr AppearanceShadingModel3D default_appearance_style3d_shading_model =
    AppearanceShadingModel3D::lit_pbr;

[[nodiscard]] constexpr std::uint32_t PackAppearanceStyle3DStateFlags(
    const AppearanceStyle3D& style_) noexcept {
    std::uint32_t flags = 0U;
    flags |= (style_.depth_test != 0U) ? appearance_style3d_depth_test_flag : 0U;
    flags |= (style_.depth_write != 0U) ? appearance_style3d_depth_write_flag : 0U;
    flags |= (style_.double_sided != 0U) ? appearance_style3d_double_sided_flag : 0U;
    flags |= (style_.cast_shadow != 0U) ? appearance_style3d_cast_shadow_flag : 0U;
    flags |= (style_.receive_shadow != 0U) ? appearance_style3d_receive_shadow_flag : 0U;
    return flags;
}

struct AppearanceRuntimeBridge3D final {
    Rgba8 base_color;
    Rgba8 emissive_color;
    float opacity;
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    float emissive_intensity;
    float alpha_cutoff;
    std::uint32_t state_flags;
    AppearanceBlendMode blend_mode;
    AppearanceAlphaMode alpha_mode;
    AppearanceShadingModel3D shading_model;
    std::uint8_t reserved0;
};

[[nodiscard]] constexpr AppearanceRuntimeBridge3D MakeAppearanceRuntimeBridge3D(
    const AppearanceStyle3D& style_) noexcept {
    return AppearanceRuntimeBridge3D{
        .base_color = style_.base_color,
        .emissive_color = style_.emissive_color,
        .opacity = std::clamp(style_.opacity, 0.0F, 1.0F),
        .metallic = std::clamp(style_.metallic, 0.0F, 1.0F),
        .roughness = std::clamp(style_.roughness, 0.04F, 1.0F),
        .normal_scale = std::clamp(style_.normal_scale, 0.0F, 4.0F),
        .occlusion_strength = std::clamp(style_.occlusion_strength, 0.0F, 1.0F),
        .emissive_intensity = std::max(0.0F, style_.emissive_intensity),
        .alpha_cutoff = std::clamp(style_.alpha_cutoff, 0.0F, 1.0F),
        .state_flags = PackAppearanceStyle3DStateFlags(style_),
        .blend_mode = style_.blend_mode,
        .alpha_mode = style_.alpha_mode,
        .shading_model = style_.shading_model,
        .reserved0 = 0U
    };
}

[[nodiscard]] constexpr AppearanceRuntimeBridge3D MakeAppearanceRuntimeBridge3D(
    const AppearanceStyle3D* style_) noexcept {
    return (style_ != nullptr)
        ? MakeAppearanceRuntimeBridge3D(*style_)
        : AppearanceRuntimeBridge3D{
              .base_color = default_appearance_style3d_base_color,
              .emissive_color = default_appearance_style3d_emissive_color,
              .opacity = default_appearance_style3d_opacity,
              .metallic = default_appearance_style3d_metallic,
              .roughness = default_appearance_style3d_roughness,
              .normal_scale = default_appearance_style3d_normal_scale,
              .occlusion_strength = default_appearance_style3d_occlusion_strength,
              .emissive_intensity = default_appearance_style3d_emissive_intensity,
              .alpha_cutoff = default_appearance_style3d_alpha_cutoff,
              .state_flags = default_appearance_style3d_state_flags,
              .blend_mode = default_appearance_style3d_blend_mode,
              .alpha_mode = default_appearance_style3d_alpha_mode,
              .shading_model = default_appearance_style3d_shading_model,
              .reserved0 = 0U};
}

[[nodiscard]] constexpr bool HasAppearanceStyle3DStateFlag(std::uint32_t flags_,
                                                           std::uint32_t mask_) noexcept {
    return (flags_ & mask_) != 0U;
}

[[nodiscard]] constexpr bool IsSameRgba8(const Rgba8& lhs_,
                                         const Rgba8& rhs_) noexcept {
    return lhs_.r == rhs_.r &&
           lhs_.g == rhs_.g &&
           lhs_.b == rhs_.b &&
           lhs_.a == rhs_.a;
}

[[nodiscard]] constexpr bool IsSameAppearanceRuntimeBridge2D(
    const AppearanceRuntimeBridge2D& lhs_,
    const AppearanceRuntimeBridge2D& rhs_) noexcept {
    return IsSameRgba8(lhs_.fill_color, rhs_.fill_color) &&
           IsSameRgba8(lhs_.stroke_color, rhs_.stroke_color) &&
           lhs_.opacity == rhs_.opacity &&
           lhs_.gradient_p0_x == rhs_.gradient_p0_x &&
           lhs_.gradient_p0_y == rhs_.gradient_p0_y &&
           lhs_.gradient_p1_x == rhs_.gradient_p1_x &&
           lhs_.gradient_p1_y == rhs_.gradient_p1_y &&
           lhs_.gradient_radius == rhs_.gradient_radius &&
           lhs_.layer == rhs_.layer &&
           lhs_.blend_mode == rhs_.blend_mode &&
           lhs_.alpha_mode == rhs_.alpha_mode &&
           lhs_.paint_mode == rhs_.paint_mode &&
           lhs_.antialiasing == rhs_.antialiasing &&
           lhs_.premultiplied_alpha == rhs_.premultiplied_alpha;
}

[[nodiscard]] constexpr Rgba8 ResolveAppearanceRuntimeBridge2DFillColor(
    const AppearanceRuntimeBridge2D& bridge_) noexcept {
    const float scaled_alpha =
        static_cast<float>(bridge_.fill_color.a) * std::clamp(bridge_.opacity, 0.0F, 1.0F);
    return Rgba8{
        .r = bridge_.fill_color.r,
        .g = bridge_.fill_color.g,
        .b = bridge_.fill_color.b,
        .a = static_cast<std::uint8_t>(scaled_alpha + 0.5F)
    };
}

[[nodiscard]] constexpr Rgba8 ResolveAppearanceRuntimeBridge2DStrokeColor(
    const AppearanceRuntimeBridge2D& bridge_) noexcept {
    const float scaled_alpha =
        static_cast<float>(bridge_.stroke_color.a) * std::clamp(bridge_.opacity, 0.0F, 1.0F);
    return Rgba8{
        .r = bridge_.stroke_color.r,
        .g = bridge_.stroke_color.g,
        .b = bridge_.stroke_color.b,
        .a = static_cast<std::uint8_t>(scaled_alpha + 0.5F)
    };
}

[[nodiscard]] constexpr bool AppearanceRuntimeBridge2DUsesTransparency(
    const AppearanceRuntimeBridge2D& bridge_) noexcept {
    return bridge_.alpha_mode == AppearanceAlphaMode::blend ||
           bridge_.blend_mode != AppearanceBlendMode::opaque;
}

[[nodiscard]] constexpr bool IsSameAppearanceRuntimeBridge3D(
    const AppearanceRuntimeBridge3D& lhs_,
    const AppearanceRuntimeBridge3D& rhs_) noexcept {
    return IsSameRgba8(lhs_.base_color, rhs_.base_color) &&
           IsSameRgba8(lhs_.emissive_color, rhs_.emissive_color) &&
           lhs_.opacity == rhs_.opacity &&
           lhs_.metallic == rhs_.metallic &&
           lhs_.roughness == rhs_.roughness &&
           lhs_.normal_scale == rhs_.normal_scale &&
           lhs_.occlusion_strength == rhs_.occlusion_strength &&
           lhs_.emissive_intensity == rhs_.emissive_intensity &&
           lhs_.alpha_cutoff == rhs_.alpha_cutoff &&
           lhs_.state_flags == rhs_.state_flags &&
           lhs_.blend_mode == rhs_.blend_mode &&
           lhs_.alpha_mode == rhs_.alpha_mode &&
           lhs_.shading_model == rhs_.shading_model;
}

[[nodiscard]] constexpr Rgba8 ResolveAppearanceRuntimeBridge3DBaseColor(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    const float scaled_alpha =
        static_cast<float>(bridge_.base_color.a) * std::clamp(bridge_.opacity, 0.0F, 1.0F);
    return Rgba8{
        .r = bridge_.base_color.r,
        .g = bridge_.base_color.g,
        .b = bridge_.base_color.b,
        .a = static_cast<std::uint8_t>(scaled_alpha + 0.5F)
    };
}

[[nodiscard]] constexpr bool AppearanceRuntimeBridge3DUsesTransparency(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return bridge_.alpha_mode == AppearanceAlphaMode::blend ||
           bridge_.blend_mode != AppearanceBlendMode::opaque;
}

[[nodiscard]] constexpr bool IsAppearanceRuntimeBridge3DDepthTestEnabled(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return HasAppearanceStyle3DStateFlag(bridge_.state_flags,
                                         appearance_style3d_depth_test_flag);
}

[[nodiscard]] constexpr bool IsAppearanceRuntimeBridge3DDepthWriteEnabled(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return HasAppearanceStyle3DStateFlag(bridge_.state_flags,
                                         appearance_style3d_depth_write_flag);
}

[[nodiscard]] constexpr bool IsAppearanceRuntimeBridge3DDoubleSided(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return HasAppearanceStyle3DStateFlag(bridge_.state_flags,
                                         appearance_style3d_double_sided_flag);
}

[[nodiscard]] constexpr bool IsAppearanceRuntimeBridge3DCastShadowEnabled(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return HasAppearanceStyle3DStateFlag(bridge_.state_flags,
                                         appearance_style3d_cast_shadow_flag);
}

[[nodiscard]] constexpr bool IsAppearanceRuntimeBridge3DReceiveShadowEnabled(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return HasAppearanceStyle3DStateFlag(bridge_.state_flags,
                                         appearance_style3d_receive_shadow_flag);
}

[[nodiscard]] constexpr bool IsAppearanceRuntimeBridge3DUnlit(
    const AppearanceRuntimeBridge3D& bridge_) noexcept {
    return bridge_.shading_model == AppearanceShadingModel3D::unlit;
}

struct AppearanceBinding2D final {
    std::uint32_t pattern_surface;
    std::uint32_t mask_surface;
    std::uint32_t lut_surface;
    std::uint32_t surface_sampler_id;
    std::uint32_t reserved0;
};

using AppearanceBinding3D = vr::render::AppearanceSampledSurfaceBinding3D;

[[nodiscard]] constexpr vr::render::AppearanceSampledSurfaceHandle* ResolveAppearanceBinding3DSurfaceStorage(
    AppearanceBinding3D& binding_,
    vr::render::AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return vr::render::ResolveAppearanceSampledSurfaceStorage3D(binding_, slot_);
}

[[nodiscard]] constexpr const vr::render::AppearanceSampledSurfaceHandle* ResolveAppearanceBinding3DSurfaceStorage(
    const AppearanceBinding3D& binding_,
    vr::render::AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return vr::render::ResolveAppearanceSampledSurfaceStorage3D(binding_, slot_);
}

[[nodiscard]] constexpr std::uint32_t ResolveAppearanceBinding3DSurfaceId(
    const AppearanceBinding3D& binding_,
    vr::render::AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return ResolveAppearanceBinding3DSurfaceStorage(binding_, slot_)->surface_id;
}

[[nodiscard]] constexpr vr::render::AppearanceSampledSurfaceDomain ResolveAppearanceBinding3DSurfaceDomain(
    const AppearanceBinding3D& binding_,
    vr::render::AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return ResolveAppearanceBinding3DSurfaceStorage(binding_, slot_)->domain;
}

[[nodiscard]] constexpr vr::render::AppearanceSampledSurfaceHandle ResolveAppearanceBinding3DSampledSurface(
    const AppearanceBinding3D& binding_,
    vr::render::AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return *ResolveAppearanceBinding3DSurfaceStorage(binding_, slot_);
}

constexpr bool SetAppearanceBinding3DSampledSurface(
    AppearanceBinding3D& binding_,
    vr::render::AppearanceSampledSurfaceSlot3D slot_,
    const vr::render::AppearanceSampledSurfaceHandle& handle_) noexcept {
    return vr::render::SetAppearanceSampledSurface3D(binding_, slot_, handle_);
}

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
static_assert(PurePodAppearanceComponent<AppearanceRuntimeBridge2D>);
static_assert(PurePodAppearanceComponent<AppearanceRuntimeBridge3D>);
static_assert(PurePodAppearanceComponent<AppearanceBinding2D>);
static_assert(PurePodAppearanceComponent<AppearanceBinding3D>);
static_assert(PurePodAppearanceComponent<Appearance<Dim2>>);
static_assert(PurePodAppearanceComponent<Appearance<Dim3>>);
static_assert(sizeof(AppearanceRuntimeCommon) <= 64U);

} // namespace vr::ecs


