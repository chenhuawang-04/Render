#pragma once

#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"
#include "vr/geometry/geometry_material_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vr::geometry {

struct GeometryMaterialResolvedState final {
    ecs::Rgba8 albedo_color{255U, 255U, 255U, 255U};
    float metallic = 0.0F;
    float roughness = 1.0F;
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
    bool unlit = false;
};

namespace detail {

[[nodiscard]] inline float FiniteOr(float value_, float fallback_) noexcept {
    return std::isfinite(value_) ? value_ : fallback_;
}

[[nodiscard]] inline std::uint8_t Float01ToByte(float value_) noexcept {
    const float clamped = std::clamp(FiniteOr(value_, 1.0F), 0.0F, 1.0F);
    return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

} // namespace detail

[[nodiscard]] inline GeometryMaterialResolvedState ResolveGeometryFallbackMaterialState(
    const ecs::GeometryStyle3D& geometry_style_,
    const GeometryMaterialDesc* material_desc_ = nullptr) noexcept {
    GeometryMaterialResolvedState resolved{};
    resolved.albedo_color = geometry_style_.albedo_color;
    resolved.metallic = std::clamp(detail::FiniteOr(geometry_style_.metallic, 0.0F), 0.0F, 1.0F);
    resolved.roughness = std::clamp(detail::FiniteOr(geometry_style_.roughness, 1.0F), 0.04F, 1.0F);
    resolved.normal_scale = std::clamp(detail::FiniteOr(geometry_style_.normal_scale, 1.0F), 0.0F, 4.0F);
    resolved.occlusion_strength = 1.0F;
    resolved.unlit = geometry_style_.shading_model == ecs::Geometry3DShadingModel::unlit;

    if (material_desc_ != nullptr) {
        resolved.metallic = std::clamp(detail::FiniteOr(material_desc_->metallic_factor, resolved.metallic), 0.0F, 1.0F);
        resolved.roughness = std::clamp(detail::FiniteOr(material_desc_->roughness_factor, resolved.roughness), 0.04F, 1.0F);
        resolved.normal_scale =
            std::clamp(detail::FiniteOr(material_desc_->normal_scale, resolved.normal_scale), 0.0F, 4.0F);
        resolved.occlusion_strength =
            std::clamp(detail::FiniteOr(material_desc_->occlusion_strength, resolved.occlusion_strength), 0.0F, 1.0F);
    }

    return resolved;
}

[[nodiscard]] inline GeometryMaterialResolvedState ApplyAppearanceMaterialState(
    GeometryMaterialResolvedState resolved_,
    const ecs::AppearanceGpuRecord<ecs::Dim3>& appearance_record_) noexcept {
    resolved_.albedo_color = ecs::Rgba8{
        detail::Float01ToByte(appearance_record_.base_rgba[0U]),
        detail::Float01ToByte(appearance_record_.base_rgba[1U]),
        detail::Float01ToByte(appearance_record_.base_rgba[2U]),
        detail::Float01ToByte(appearance_record_.base_rgba[3U] *
                              std::clamp(detail::FiniteOr(appearance_record_.extras[2U], 1.0F), 0.0F, 1.0F))
    };
    resolved_.metallic =
        std::clamp(detail::FiniteOr(appearance_record_.material_params[0U], resolved_.metallic), 0.0F, 1.0F);
    resolved_.roughness =
        std::clamp(detail::FiniteOr(appearance_record_.material_params[1U], resolved_.roughness), 0.04F, 1.0F);
    resolved_.normal_scale =
        std::clamp(detail::FiniteOr(appearance_record_.material_params[2U], resolved_.normal_scale), 0.0F, 4.0F);
    resolved_.occlusion_strength =
        std::clamp(detail::FiniteOr(appearance_record_.material_params[3U], resolved_.occlusion_strength), 0.0F, 1.0F);

    constexpr std::uint32_t k_shading_model_shift = 5U;
    constexpr std::uint32_t k_shading_model_mask = 0x3U;
    const std::uint32_t shading_model =
        (appearance_record_.flags_u32[0U] >> k_shading_model_shift) & k_shading_model_mask;
    resolved_.unlit = shading_model == 0U;
    return resolved_;
}

[[nodiscard]] inline GeometryMaterialResolvedState ResolveGeometryMaterialState(
    const ecs::GeometryStyle3D& geometry_style_,
    const GeometryMaterialDesc* material_desc_ = nullptr,
    const ecs::AppearanceGpuRecord<ecs::Dim3>* appearance_record_ = nullptr) noexcept {
    GeometryMaterialResolvedState resolved =
        ResolveGeometryFallbackMaterialState(geometry_style_, material_desc_);
    if (appearance_record_ != nullptr) {
        resolved = ApplyAppearanceMaterialState(resolved, *appearance_record_);
    }
    return resolved;
}

} // namespace vr::geometry
