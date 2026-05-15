#pragma once

#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"
#include "vr/geometry/geometry_appearance_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vr::geometry {

struct GeometryAppearanceResolvedState final {
    ecs::Rgba8 base_color{255U, 255U, 255U, 255U};
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

[[nodiscard]] inline GeometryAppearanceResolvedState ResolveGeometryAuthoredAppearanceState(
    const ecs::AppearanceStyle3D* appearance_style_ = nullptr,
    const GeometryAppearanceDesc* appearance_desc_ = nullptr) noexcept {
    GeometryAppearanceResolvedState resolved{};
    if (appearance_style_ != nullptr) {
        resolved.base_color = appearance_style_->base_color;
        resolved.metallic =
            std::clamp(detail::FiniteOr(appearance_style_->metallic, 0.0F), 0.0F, 1.0F);
        resolved.roughness =
            std::clamp(detail::FiniteOr(appearance_style_->roughness, 1.0F), 0.04F, 1.0F);
        resolved.normal_scale =
            std::clamp(detail::FiniteOr(appearance_style_->normal_scale, 1.0F), 0.0F, 4.0F);
        resolved.occlusion_strength =
            std::clamp(detail::FiniteOr(appearance_style_->occlusion_strength, 1.0F), 0.0F, 1.0F);
        resolved.unlit = appearance_style_->shading_model == ecs::AppearanceShadingModel3D::unlit;
    }

    if (appearance_desc_ != nullptr) {
        resolved.metallic = std::clamp(detail::FiniteOr(appearance_desc_->metallic_factor, resolved.metallic), 0.0F, 1.0F);
        resolved.roughness = std::clamp(detail::FiniteOr(appearance_desc_->roughness_factor, resolved.roughness), 0.04F, 1.0F);
        resolved.normal_scale =
            std::clamp(detail::FiniteOr(appearance_desc_->normal_scale, resolved.normal_scale), 0.0F, 4.0F);
        resolved.occlusion_strength =
            std::clamp(detail::FiniteOr(appearance_desc_->occlusion_strength, resolved.occlusion_strength), 0.0F, 1.0F);
    }

    return resolved;
}

[[nodiscard]] inline GeometryAppearanceResolvedState ResolveGeometryAuthoredAppearanceStateFromRuntimeBridge(
    const ecs::AppearanceRuntimeBridge3D* appearance_bridge_ = nullptr,
    const GeometryAppearanceDesc* appearance_desc_ = nullptr) noexcept {
    GeometryAppearanceResolvedState resolved{};
    if (appearance_bridge_ != nullptr) {
        resolved.base_color = ecs::ResolveAppearanceRuntimeBridge3DBaseColor(*appearance_bridge_);
        resolved.metallic =
            std::clamp(detail::FiniteOr(appearance_bridge_->metallic, 0.0F), 0.0F, 1.0F);
        resolved.roughness =
            std::clamp(detail::FiniteOr(appearance_bridge_->roughness, 1.0F), 0.04F, 1.0F);
        resolved.normal_scale =
            std::clamp(detail::FiniteOr(appearance_bridge_->normal_scale, 1.0F), 0.0F, 4.0F);
        resolved.occlusion_strength =
            std::clamp(detail::FiniteOr(appearance_bridge_->occlusion_strength, 1.0F), 0.0F, 1.0F);
        resolved.unlit = ecs::IsAppearanceRuntimeBridge3DUnlit(*appearance_bridge_);
    }

    if (appearance_desc_ != nullptr) {
        resolved.metallic =
            std::clamp(detail::FiniteOr(appearance_desc_->metallic_factor, resolved.metallic), 0.0F, 1.0F);
        resolved.roughness =
            std::clamp(detail::FiniteOr(appearance_desc_->roughness_factor, resolved.roughness), 0.04F, 1.0F);
        resolved.normal_scale =
            std::clamp(detail::FiniteOr(appearance_desc_->normal_scale, resolved.normal_scale), 0.0F, 4.0F);
        resolved.occlusion_strength =
            std::clamp(detail::FiniteOr(appearance_desc_->occlusion_strength, resolved.occlusion_strength), 0.0F, 1.0F);
    }

    return resolved;
}

[[nodiscard]] inline GeometryAppearanceResolvedState OverlayLinkedAppearanceRecordState(
    GeometryAppearanceResolvedState resolved_,
    const ecs::AppearanceGpuRecord<ecs::Dim3>& appearance_record_) noexcept {
    resolved_.base_color = ecs::Rgba8{
        detail::Float01ToByte(appearance_record_.base_rgba[0U]),
        detail::Float01ToByte(appearance_record_.base_rgba[1U]),
        detail::Float01ToByte(appearance_record_.base_rgba[2U]),
        detail::Float01ToByte(appearance_record_.base_rgba[3U] *
                              std::clamp(detail::FiniteOr(appearance_record_.extras[2U], 1.0F), 0.0F, 1.0F))
    };
    resolved_.metallic =
        std::clamp(detail::FiniteOr(appearance_record_.appearance_params[0U], resolved_.metallic), 0.0F, 1.0F);
    resolved_.roughness =
        std::clamp(detail::FiniteOr(appearance_record_.appearance_params[1U], resolved_.roughness), 0.04F, 1.0F);
    resolved_.normal_scale =
        std::clamp(detail::FiniteOr(appearance_record_.appearance_params[2U], resolved_.normal_scale), 0.0F, 4.0F);
    resolved_.occlusion_strength =
        std::clamp(detail::FiniteOr(appearance_record_.appearance_params[3U], resolved_.occlusion_strength), 0.0F, 1.0F);

    constexpr std::uint32_t k_shading_model_shift = 5U;
    constexpr std::uint32_t k_shading_model_mask = 0x3U;
    const std::uint32_t shading_model =
        (appearance_record_.flags_u32[0U] >> k_shading_model_shift) & k_shading_model_mask;
    resolved_.unlit = shading_model == 0U;
    return resolved_;
}

[[nodiscard]] inline GeometryAppearanceResolvedState ResolveFinalGeometryAppearanceState(
    const ecs::AppearanceStyle3D* appearance_style_ = nullptr,
    const GeometryAppearanceDesc* appearance_desc_ = nullptr,
    const ecs::AppearanceGpuRecord<ecs::Dim3>* appearance_record_ = nullptr) noexcept {
    GeometryAppearanceResolvedState resolved =
        ResolveGeometryAuthoredAppearanceState(appearance_style_, appearance_desc_);
    if (appearance_record_ != nullptr) {
        resolved = OverlayLinkedAppearanceRecordState(resolved, *appearance_record_);
    }
    return resolved;
}

[[nodiscard]] inline GeometryAppearanceResolvedState ResolveFinalGeometryAppearanceStateFromRuntimeBridge(
    const ecs::AppearanceRuntimeBridge3D* appearance_bridge_ = nullptr,
    const GeometryAppearanceDesc* appearance_desc_ = nullptr,
    const ecs::AppearanceGpuRecord<ecs::Dim3>* appearance_record_ = nullptr) noexcept {
    GeometryAppearanceResolvedState resolved =
        ResolveGeometryAuthoredAppearanceStateFromRuntimeBridge(appearance_bridge_, appearance_desc_);
    if (appearance_record_ != nullptr) {
        resolved = OverlayLinkedAppearanceRecordState(resolved, *appearance_record_);
    }
    return resolved;
}

} // namespace vr::geometry

