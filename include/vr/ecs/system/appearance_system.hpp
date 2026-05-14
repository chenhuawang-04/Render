#pragma once

#include "vr/ecs/component/appearance_component.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>

namespace vr::ecs {

[[nodiscard]] constexpr std::uint32_t NextAppearanceRevision(std::uint32_t current_revision_) noexcept {
    return (current_revision_ == (std::numeric_limits<std::uint32_t>::max)())
               ? 1U
               : (current_revision_ + 1U);
}

template<DimensionTag DimensionT>
class AppearanceSystem final {
public:
    using AppearanceType = Appearance<DimensionT>;
    using StyleType = typename AppearanceType::StyleType;
    using BindingType = typename AppearanceType::BindingType;

    static void Initialize(AppearanceType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultBinding(component_);
        SetDefaultRuntime(component_);
    }

    static void SetDefaultStyle(AppearanceType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.fill_color = default_appearance_style2d_fill_color;
            component_.style.stroke_color = default_appearance_style2d_stroke_color;
            component_.style.opacity = default_appearance_style2d_opacity;
            component_.style.gradient_p0_x = default_appearance_style2d_gradient_p0_x;
            component_.style.gradient_p0_y = default_appearance_style2d_gradient_p0_y;
            component_.style.gradient_p1_x = default_appearance_style2d_gradient_p1_x;
            component_.style.gradient_p1_y = default_appearance_style2d_gradient_p1_y;
            component_.style.gradient_radius = default_appearance_style2d_gradient_radius;
            component_.style.layer = default_appearance_style2d_layer;
            component_.style.blend_mode = default_appearance_style2d_blend_mode;
            component_.style.alpha_mode = default_appearance_style2d_alpha_mode;
            component_.style.paint_mode = default_appearance_style2d_paint_mode;
            component_.style.antialiasing = default_appearance_style2d_antialiasing;
            component_.style.premultiplied_alpha = default_appearance_style2d_premultiplied_alpha;
            component_.style.reserved0 = 0U;
        } else {
            component_.style.base_color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.emissive_color = Rgba8{0U, 0U, 0U, 255U};
            component_.style.opacity = 1.0F;
            component_.style.metallic = 0.0F;
            component_.style.roughness = 1.0F;
            component_.style.normal_scale = 1.0F;
            component_.style.occlusion_strength = 1.0F;
            component_.style.emissive_intensity = 0.0F;
            component_.style.alpha_cutoff = 0.5F;
            component_.style.layer = 0;
            component_.style.blend_mode = AppearanceBlendMode::opaque;
            component_.style.alpha_mode = AppearanceAlphaMode::opaque;
            component_.style.shading_model = AppearanceShadingModel3D::lit_pbr;
            component_.style.depth_test = 1U;
            component_.style.depth_write = 1U;
            component_.style.double_sided = 0U;
            component_.style.cast_shadow = 1U;
            component_.style.receive_shadow = 1U;
            component_.style.reserved0 = 0U;
        }
    }

    static void SetDefaultBinding(AppearanceType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.binding.texture_base_id = 0U;
            component_.binding.texture_mask_id = 0U;
            component_.binding.texture_lut_id = 0U;
            component_.binding.sampler_state_id = 0U;
            component_.binding.binding_layout_id = 0U;
            component_.binding.reserved0 = 0U;
        } else {
            component_.binding.texture_base_color_id = 0U;
            component_.binding.texture_normal_id = 0U;
            component_.binding.texture_metal_rough_id = 0U;
            component_.binding.texture_occlusion_id = 0U;
            component_.binding.texture_emissive_id = 0U;
            component_.binding.sampler_state_id = 0U;
            component_.binding.binding_layout_id = 0U;
            component_.binding.reserved0 = 0U;
        }
    }

    static void SetDefaultRuntime(AppearanceType& component_) noexcept {
        component_.runtime.revision_style = 1U;
        component_.runtime.revision_binding = 1U;
        component_.runtime.upload_revision = 0U;
        component_.runtime.dirty_flags = appearance_dirty_style_flag |
                                         appearance_dirty_binding_flag;
        component_.runtime.pipeline_key = 0U;
        component_.runtime.resource_key = 0U;
        component_.runtime.sort_key = 0U;
        component_.runtime.gpu_record_handle = invalid_appearance_handle;
        component_.runtime.gpu_record_index = invalid_appearance_index;
        component_.runtime.visible = 1U;
        component_.runtime.reserved0 = 0U;
        component_.runtime.reserved1 = 0U;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const AppearanceType& component_) noexcept {
        return component_.runtime.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const AppearanceType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(AppearanceType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(AppearanceType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.dirty_flags &= ~clear_mask_;
    }

    [[nodiscard]] static std::uint32_t StyleRevision(const AppearanceType& component_) noexcept {
        return component_.runtime.revision_style;
    }

    [[nodiscard]] static std::uint32_t BindingRevision(const AppearanceType& component_) noexcept {
        return component_.runtime.revision_binding;
    }

    [[nodiscard]] static std::uint32_t UploadRevision(const AppearanceType& component_) noexcept {
        return component_.runtime.upload_revision;
    }

    [[nodiscard]] static bool IsVisible(const AppearanceType& component_) noexcept {
        return component_.runtime.visible != 0U;
    }

    [[nodiscard]] static bool IsVisibleForBatch(const AppearanceType& component_) noexcept {
        if (component_.runtime.visible == 0U) {
            return false;
        }
        if constexpr (std::same_as<DimensionT, Dim2>) {
            if (component_.style.paint_mode == AppearancePaintMode::pattern) {
                return component_.binding.texture_base_id != 0U;
            }
        }
        return true;
    }

    static void SetVisible(AppearanceType& component_, bool visible_) noexcept {
        const std::uint8_t visible_value = visible_ ? 1U : 0U;
        if (component_.runtime.visible == visible_value) {
            return;
        }
        component_.runtime.visible = visible_value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetRuntimeKeys(AppearanceType& component_,
                               std::uint64_t pipeline_key_,
                               std::uint64_t resource_key_,
                               std::uint64_t sort_key_) noexcept {
        component_.runtime.pipeline_key = pipeline_key_;
        component_.runtime.resource_key = resource_key_;
        component_.runtime.sort_key = sort_key_;
    }

    static void SetGpuRecordHandle(AppearanceType& component_,
                                   AppearanceHandle gpu_record_handle_) noexcept {
        component_.runtime.gpu_record_handle = gpu_record_handle_;
        component_.runtime.gpu_record_index = gpu_record_handle_.index;
    }

    static void MarkUploaded(AppearanceType& component_) noexcept {
        component_.runtime.upload_revision = NextAppearanceRevision(component_.runtime.upload_revision);
        ClearDirtyFlags(component_,
                        appearance_dirty_style_flag |
                            appearance_dirty_binding_flag);
    }

    [[nodiscard]] static std::int16_t Layer(const AppearanceType& component_) noexcept {
        return component_.style.layer;
    }

    [[nodiscard]] static AppearanceBlendMode BlendMode(const AppearanceType& component_) noexcept {
        return component_.style.blend_mode;
    }

    [[nodiscard]] static AppearanceAlphaMode AlphaMode(const AppearanceType& component_) noexcept {
        return component_.style.alpha_mode;
    }

    static void SetLayer(AppearanceType& component_, std::int16_t layer_) noexcept {
        if (component_.style.layer == layer_) {
            return;
        }
        component_.style.layer = layer_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetBlendMode(AppearanceType& component_,
                             AppearanceBlendMode blend_mode_) noexcept {
        if (component_.style.blend_mode == blend_mode_) {
            return;
        }
        component_.style.blend_mode = blend_mode_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetAlphaMode(AppearanceType& component_,
                             AppearanceAlphaMode alpha_mode_) noexcept {
        if (component_.style.alpha_mode == alpha_mode_) {
            return;
        }
        component_.style.alpha_mode = alpha_mode_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetOpacity(AppearanceType& component_, float opacity_) noexcept {
        const float clamped = std::clamp(opacity_, 0.0F, 1.0F);
        if (component_.style.opacity == clamped) {
            return;
        }
        component_.style.opacity = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetBindingLayoutId(AppearanceType& component_, std::uint32_t binding_layout_id_) noexcept {
        if (component_.binding.binding_layout_id == binding_layout_id_) {
            return;
        }
        component_.binding.binding_layout_id = binding_layout_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetSamplerStateId(AppearanceType& component_, std::uint32_t sampler_state_id_) noexcept {
        if (component_.binding.sampler_state_id == sampler_state_id_) {
            return;
        }
        component_.binding.sampler_state_id = sampler_state_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetFillColor(AppearanceType& component_, Rgba8 fill_color_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (IsSameColor(component_.style.fill_color, fill_color_)) {
            return;
        }
        component_.style.fill_color = fill_color_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetStrokeColor(AppearanceType& component_, Rgba8 stroke_color_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (IsSameColor(component_.style.stroke_color, stroke_color_)) {
            return;
        }
        component_.style.stroke_color = stroke_color_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetPaintMode(AppearanceType& component_, AppearancePaintMode paint_mode_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.paint_mode == paint_mode_) {
            return;
        }
        component_.style.paint_mode = paint_mode_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetGradientLinear(AppearanceType& component_,
                                  float p0_x_,
                                  float p0_y_,
                                  float p1_x_,
                                  float p1_y_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.gradient_p0_x == p0_x_ &&
            component_.style.gradient_p0_y == p0_y_ &&
            component_.style.gradient_p1_x == p1_x_ &&
            component_.style.gradient_p1_y == p1_y_) {
            return;
        }
        component_.style.gradient_p0_x = p0_x_;
        component_.style.gradient_p0_y = p0_y_;
        component_.style.gradient_p1_x = p1_x_;
        component_.style.gradient_p1_y = p1_y_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetGradientRadius(AppearanceType& component_, float gradient_radius_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const float clamped = std::max(0.0F, gradient_radius_);
        if (component_.style.gradient_radius == clamped) {
            return;
        }
        component_.style.gradient_radius = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetAntialiasing(AppearanceType& component_, bool antialiasing_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const std::uint8_t enabled = antialiasing_ ? 1U : 0U;
        if (component_.style.antialiasing == enabled) {
            return;
        }
        component_.style.antialiasing = enabled;
        MarkStyleRevisionDirty(component_);
    }

    static void SetPremultipliedAlpha(AppearanceType& component_, bool premultiplied_alpha_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const std::uint8_t enabled = premultiplied_alpha_ ? 1U : 0U;
        if (component_.style.premultiplied_alpha == enabled) {
            return;
        }
        component_.style.premultiplied_alpha = enabled;
        MarkStyleRevisionDirty(component_);
    }

    static void SetTextureBaseId(AppearanceType& component_, std::uint32_t texture_base_id_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.binding.texture_base_id == texture_base_id_) {
            return;
        }
        component_.binding.texture_base_id = texture_base_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetTextureMaskId(AppearanceType& component_, std::uint32_t texture_mask_id_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.binding.texture_mask_id == texture_mask_id_) {
            return;
        }
        component_.binding.texture_mask_id = texture_mask_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetTextureLutId(AppearanceType& component_, std::uint32_t texture_lut_id_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.binding.texture_lut_id == texture_lut_id_) {
            return;
        }
        component_.binding.texture_lut_id = texture_lut_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetBaseColor(AppearanceType& component_, Rgba8 base_color_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (IsSameColor(component_.style.base_color, base_color_)) {
            return;
        }
        component_.style.base_color = base_color_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetEmissiveColor(AppearanceType& component_, Rgba8 emissive_color_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (IsSameColor(component_.style.emissive_color, emissive_color_)) {
            return;
        }
        component_.style.emissive_color = emissive_color_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetMetallic(AppearanceType& component_, float metallic_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::clamp(metallic_, 0.0F, 1.0F);
        if (component_.style.metallic == clamped) {
            return;
        }
        component_.style.metallic = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetRoughness(AppearanceType& component_, float roughness_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::clamp(roughness_, 0.0F, 1.0F);
        if (component_.style.roughness == clamped) {
            return;
        }
        component_.style.roughness = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetNormalScale(AppearanceType& component_, float normal_scale_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::max(0.0F, normal_scale_);
        if (component_.style.normal_scale == clamped) {
            return;
        }
        component_.style.normal_scale = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetOcclusionStrength(AppearanceType& component_, float occlusion_strength_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::clamp(occlusion_strength_, 0.0F, 1.0F);
        if (component_.style.occlusion_strength == clamped) {
            return;
        }
        component_.style.occlusion_strength = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetEmissiveIntensity(AppearanceType& component_, float emissive_intensity_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::max(0.0F, emissive_intensity_);
        if (component_.style.emissive_intensity == clamped) {
            return;
        }
        component_.style.emissive_intensity = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetAlphaCutoff(AppearanceType& component_, float alpha_cutoff_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::clamp(alpha_cutoff_, 0.0F, 1.0F);
        if (component_.style.alpha_cutoff == clamped) {
            return;
        }
        component_.style.alpha_cutoff = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetShadingModel(AppearanceType& component_,
                                AppearanceShadingModel3D shading_model_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.style.shading_model == shading_model_) {
            return;
        }
        component_.style.shading_model = shading_model_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetDepthTest(AppearanceType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.depth_test == enabled_value) {
            return;
        }
        component_.style.depth_test = enabled_value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetDepthWrite(AppearanceType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.depth_write == enabled_value) {
            return;
        }
        component_.style.depth_write = enabled_value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetDoubleSided(AppearanceType& component_, bool double_sided_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled = double_sided_ ? 1U : 0U;
        if (component_.style.double_sided == enabled) {
            return;
        }
        component_.style.double_sided = enabled;
        MarkStyleRevisionDirty(component_);
    }

    static void SetCastShadow(AppearanceType& component_, bool cast_shadow_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled = cast_shadow_ ? 1U : 0U;
        if (component_.style.cast_shadow == enabled) {
            return;
        }
        component_.style.cast_shadow = enabled;
        MarkStyleRevisionDirty(component_);
    }

    static void SetReceiveShadow(AppearanceType& component_, bool receive_shadow_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled = receive_shadow_ ? 1U : 0U;
        if (component_.style.receive_shadow == enabled) {
            return;
        }
        component_.style.receive_shadow = enabled;
        MarkStyleRevisionDirty(component_);
    }

    static void SetTextureBaseColorId(AppearanceType& component_, std::uint32_t texture_base_color_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.texture_base_color_id == texture_base_color_id_) {
            return;
        }
        component_.binding.texture_base_color_id = texture_base_color_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetTextureNormalId(AppearanceType& component_, std::uint32_t texture_normal_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.texture_normal_id == texture_normal_id_) {
            return;
        }
        component_.binding.texture_normal_id = texture_normal_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetTextureMetalRoughId(AppearanceType& component_, std::uint32_t texture_metal_rough_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.texture_metal_rough_id == texture_metal_rough_id_) {
            return;
        }
        component_.binding.texture_metal_rough_id = texture_metal_rough_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetTextureOcclusionId(AppearanceType& component_, std::uint32_t texture_occlusion_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.texture_occlusion_id == texture_occlusion_id_) {
            return;
        }
        component_.binding.texture_occlusion_id = texture_occlusion_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetTextureEmissiveId(AppearanceType& component_, std::uint32_t texture_emissive_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.texture_emissive_id == texture_emissive_id_) {
            return;
        }
        component_.binding.texture_emissive_id = texture_emissive_id_;
        MarkBindingRevisionDirty(component_);
    }

private:
    [[nodiscard]] static bool IsSameColor(Rgba8 lhs_, Rgba8 rhs_) noexcept {
        return lhs_.r == rhs_.r &&
               lhs_.g == rhs_.g &&
               lhs_.b == rhs_.b &&
               lhs_.a == rhs_.a;
    }

    static void MarkStyleRevisionDirty(AppearanceType& component_) noexcept {
        component_.runtime.revision_style = NextAppearanceRevision(component_.runtime.revision_style);
        MarkDirty(component_, appearance_dirty_style_flag);
    }

    static void MarkBindingRevisionDirty(AppearanceType& component_) noexcept {
        component_.runtime.revision_binding = NextAppearanceRevision(component_.runtime.revision_binding);
        MarkDirty(component_, appearance_dirty_binding_flag);
    }
};

} // namespace vr::ecs

