#pragma once

#include "vr/ecs/component/light_component.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>

namespace vr::ecs {

[[nodiscard]] constexpr std::uint32_t NextLightRevision(std::uint32_t current_revision_) noexcept {
    return (current_revision_ == (std::numeric_limits<std::uint32_t>::max)()) ? 1U : (current_revision_ + 1U);
}

template<DimensionTag DimensionT>
class LightSystem final {
public:
    using LightType = Light<DimensionT>;
    using StyleType = typename LightType::StyleType;
    using BindingType = typename LightType::BindingType;

    static void Initialize(LightType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultBinding(component_);
        SetDefaultRuntime(component_);
    }

    static void SetDefaultStyle(LightType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.intensity = 1.0F;
            component_.style.range = 256.0F;
            component_.style.falloff_exponent = 2.0F;
            component_.style.inner_angle_radians = 0.35F;
            component_.style.outer_angle_radians = 0.75F;
            component_.style.source_height = 32.0F;
            component_.style.layer = 0;
            component_.style.kind = LightKind::point;
            component_.style.blend_mode = Light2DBlendMode::additive;
            component_.style.cast_shadow = 0U;
            component_.style.affect_normals_only = 0U;
            component_.style.reserved0 = 0U;
        } else {
            component_.style.color = Rgba8{255U, 255U, 255U, 255U};
            component_.style.intensity = 1500.0F;
            component_.style.range = 10.0F;
            component_.style.falloff_exponent = 2.0F;
            component_.style.inner_angle_radians = 0.35F;
            component_.style.outer_angle_radians = 0.75F;
            component_.style.source_height = 0.0F;
            component_.style.source_radius = 0.0F;
            component_.style.source_length = 0.0F;
            component_.style.temperature_kelvin = 6500.0F;
            component_.style.volumetric_strength = 0.0F;
            component_.style.kind = LightKind::point;
            component_.style.falloff_mode = LightFalloffMode::inverse_square;
            component_.style.cast_shadow = 0U;
            component_.style.reserved0 = 0U;
        }
    }

    static void SetDefaultBinding(LightType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.binding.cookie.texture_id = 0U;
            component_.binding.cookie.sampler_id = 0U;
            component_.binding.occluder.texture_id = 0U;
            component_.binding.occluder.sampler_id = 0U;
            component_.binding.reserved0 = 0U;
        } else {
            component_.binding.cookie.texture_id = 0U;
            component_.binding.cookie.sampler_id = 0U;
            component_.binding.ies.texture_id = 0U;
            component_.binding.ies.sampler_id = 0U;
            component_.binding.shadow.texture_id = 0U;
            component_.binding.shadow.sampler_id = 0U;
            component_.binding.shadow_config.resolution = 1024U;
            component_.binding.shadow_config.cascade_count = 1U;
            component_.binding.shadow_config.filter_mode = ShadowFilterMode::pcf3x3;
        }
    }

    static void SetDefaultRuntime(LightType& component_) noexcept {
        component_.state.revision_style = 1U;
        component_.state.revision_binding = 1U;
        component_.state.upload_revision = 0U;
        component_.state.dirty_flags = light_dirty_style_flag | light_dirty_binding_flag | light_dirty_runtime_flag;

        component_.gpu.pipeline_key = 0U;
        component_.gpu.resource_key = 0U;
        component_.gpu.sort_key = 0U;
        component_.gpu.gpu_record_index = invalid_light_index;
        component_.gpu.handle = invalid_light_handle;

        component_.visibility.light_channel_mask = 0xFFFFFFFFU;
        component_.visibility.visible = 1U;
        component_.visibility.cast_shadow_resolved = 0U;
        component_.visibility.reserved0 = 0U;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const LightType& component_) noexcept {
        return component_.state.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const LightType& component_, std::uint32_t dirty_mask_) noexcept {
        return (component_.state.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(LightType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.state.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(LightType& component_, std::uint32_t clear_mask_) noexcept {
        component_.state.dirty_flags &= ~clear_mask_;
    }

    [[nodiscard]] static bool IsVisibleForCulling(const LightType& component_) noexcept {
        return component_.visibility.visible != 0U && component_.style.intensity > 0.0F && component_.style.range > 0.0F;
    }

    static void SetVisible(LightType& component_, bool visible_) noexcept {
        const std::uint8_t visible_value = visible_ ? 1U : 0U;
        if (component_.visibility.visible == visible_value) {
            return;
        }
        component_.visibility.visible = visible_value;
        MarkRuntimeRevisionDirty(component_);
    }

    static void SetChannelMask(LightType& component_, std::uint32_t light_channel_mask_) noexcept {
        if (component_.visibility.light_channel_mask == light_channel_mask_) {
            return;
        }
        component_.visibility.light_channel_mask = light_channel_mask_;
        MarkRuntimeRevisionDirty(component_);
    }

    static void SetLightKind(LightType& component_, LightKind kind_) noexcept {
        if (component_.style.kind == kind_) {
            return;
        }
        component_.style.kind = kind_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetColor(LightType& component_, Rgba8 color_) noexcept {
        if (IsSameColor(component_.style.color, color_)) {
            return;
        }
        component_.style.color = color_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetIntensity(LightType& component_, float intensity_) noexcept {
        const float clamped = std::max(0.0F, intensity_);
        if (component_.style.intensity == clamped) {
            return;
        }
        component_.style.intensity = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetRange(LightType& component_, float range_) noexcept {
        const float clamped = std::max(0.0F, range_);
        if (component_.style.range == clamped) {
            return;
        }
        component_.style.range = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetFalloffExponent(LightType& component_, float falloff_exponent_) noexcept {
        const float clamped = std::max(0.0F, falloff_exponent_);
        if (component_.style.falloff_exponent == clamped) {
            return;
        }
        component_.style.falloff_exponent = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetConeAngles(LightType& component_,
                              float inner_angle_radians_,
                              float outer_angle_radians_) noexcept {
        const float inner_clamped = std::clamp(inner_angle_radians_, 0.0F, 3.13F);
        const float outer_clamped = std::clamp(outer_angle_radians_, inner_clamped, 3.13F);
        if (component_.style.inner_angle_radians == inner_clamped &&
            component_.style.outer_angle_radians == outer_clamped) {
            return;
        }
        component_.style.inner_angle_radians = inner_clamped;
        component_.style.outer_angle_radians = outer_clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetSourceHeight(LightType& component_, float source_height_) noexcept {
        if (component_.style.source_height == source_height_) {
            return;
        }
        component_.style.source_height = source_height_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetCastShadow(LightType& component_, bool cast_shadow_) noexcept {
        const std::uint8_t cast_shadow_value = cast_shadow_ ? 1U : 0U;
        if (component_.style.cast_shadow == cast_shadow_value) {
            return;
        }
        component_.style.cast_shadow = cast_shadow_value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetCookieResource(LightType& component_,
                                  std::uint32_t texture_id_,
                                  std::uint32_t sampler_id_) noexcept {
        if (component_.binding.cookie.texture_id == texture_id_ &&
            component_.binding.cookie.sampler_id == sampler_id_) {
            return;
        }
        component_.binding.cookie.texture_id = texture_id_;
        component_.binding.cookie.sampler_id = sampler_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetAffectNormalsOnly(LightType& component_, bool affect_normals_only_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const std::uint8_t value = affect_normals_only_ ? 1U : 0U;
        if (component_.style.affect_normals_only == value) {
            return;
        }
        component_.style.affect_normals_only = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetBlendMode(LightType& component_, Light2DBlendMode blend_mode_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.blend_mode == blend_mode_) {
            return;
        }
        component_.style.blend_mode = blend_mode_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetOccluderResource(LightType& component_,
                                    std::uint32_t texture_id_,
                                    std::uint32_t sampler_id_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.binding.occluder.texture_id == texture_id_ &&
            component_.binding.occluder.sampler_id == sampler_id_) {
            return;
        }
        component_.binding.occluder.texture_id = texture_id_;
        component_.binding.occluder.sampler_id = sampler_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetSourceRadius(LightType& component_, float source_radius_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::max(0.0F, source_radius_);
        if (component_.style.source_radius == clamped) {
            return;
        }
        component_.style.source_radius = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetSourceLength(LightType& component_, float source_length_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::max(0.0F, source_length_);
        if (component_.style.source_length == clamped) {
            return;
        }
        component_.style.source_length = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetVolumetricStrength(LightType& component_, float volumetric_strength_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::max(0.0F, volumetric_strength_);
        if (component_.style.volumetric_strength == clamped) {
            return;
        }
        component_.style.volumetric_strength = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetTemperatureKelvin(LightType& component_, float temperature_kelvin_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float clamped = std::max(1000.0F, temperature_kelvin_);
        if (component_.style.temperature_kelvin == clamped) {
            return;
        }
        component_.style.temperature_kelvin = clamped;
        MarkStyleRevisionDirty(component_);
    }

    static void SetFalloffMode(LightType& component_, LightFalloffMode falloff_mode_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.style.falloff_mode == falloff_mode_) {
            return;
        }
        component_.style.falloff_mode = falloff_mode_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetIesResource(LightType& component_,
                               std::uint32_t texture_id_,
                               std::uint32_t sampler_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.ies.texture_id == texture_id_ &&
            component_.binding.ies.sampler_id == sampler_id_) {
            return;
        }
        component_.binding.ies.texture_id = texture_id_;
        component_.binding.ies.sampler_id = sampler_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetShadowResource(LightType& component_,
                                  std::uint32_t texture_id_,
                                  std::uint32_t sampler_id_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.shadow.texture_id == texture_id_ &&
            component_.binding.shadow.sampler_id == sampler_id_) {
            return;
        }
        component_.binding.shadow.texture_id = texture_id_;
        component_.binding.shadow.sampler_id = sampler_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetShadowConfig(LightType& component_,
                                const ShadowConfig& shadow_config_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.shadow_config.resolution == shadow_config_.resolution &&
            component_.binding.shadow_config.cascade_count == shadow_config_.cascade_count &&
            component_.binding.shadow_config.filter_mode == shadow_config_.filter_mode) {
            return;
        }
        component_.binding.shadow_config = shadow_config_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetRuntimeKeys(LightType& component_,
                               std::uint64_t pipeline_key_,
                               std::uint64_t resource_key_,
                               std::uint64_t sort_key_) noexcept {
        component_.gpu.pipeline_key = pipeline_key_;
        component_.gpu.resource_key = resource_key_;
        component_.gpu.sort_key = sort_key_;
    }

    static void SetGpuRecordHandle(LightType& component_, LightHandle handle_) noexcept {
        component_.gpu.handle = handle_;
        component_.gpu.gpu_record_index = handle_.index;
    }

    static void MarkUploaded(LightType& component_) noexcept {
        component_.state.upload_revision = NextLightRevision(component_.state.upload_revision);
        ClearDirtyFlags(component_,
                        light_dirty_style_flag |
                            light_dirty_binding_flag |
                            light_dirty_runtime_flag);
    }

private:
    static void MarkStyleRevisionDirty(LightType& component_) noexcept {
        component_.state.revision_style = NextLightRevision(component_.state.revision_style);
        MarkDirty(component_, light_dirty_style_flag);
    }

    static void MarkBindingRevisionDirty(LightType& component_) noexcept {
        component_.state.revision_binding = NextLightRevision(component_.state.revision_binding);
        MarkDirty(component_, light_dirty_binding_flag);
    }

    static void MarkRuntimeRevisionDirty(LightType& component_) noexcept {
        component_.state.revision_style = NextLightRevision(component_.state.revision_style);
        MarkDirty(component_, light_dirty_runtime_flag);
    }

    [[nodiscard]] static bool IsSameColor(const Rgba8& lhs_, const Rgba8& rhs_) noexcept {
        return lhs_.r == rhs_.r &&
               lhs_.g == rhs_.g &&
               lhs_.b == rhs_.b &&
               lhs_.a == rhs_.a;
    }
};

} // namespace vr::ecs


