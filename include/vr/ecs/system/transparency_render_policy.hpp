#pragma once

#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/geometry_component.hpp"
#include "vr/ecs/component/particle_component.hpp"
#include "vr/ecs/component/surface_component.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/system/visual_runtime_route_common.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::ecs {

enum class RuntimeBlendPreset : std::uint8_t {
    opaque = 0U,
    alpha = 1U,
    additive = 2U,
    multiply = 3U,
    premultiplied_alpha = 4U,
    screen = 5U,
};

template<typename PassHintT>
struct PassHintTraits;

template<>
struct PassHintTraits<GeometryRenderPassHint> final {
    static constexpr GeometryRenderPassHint opaque = GeometryRenderPassHint::opaque;
    static constexpr GeometryRenderPassHint transparent = GeometryRenderPassHint::transparent;
    static constexpr GeometryRenderPassHint overlay = GeometryRenderPassHint::overlay;
};

template<>
struct PassHintTraits<SurfaceRenderPassHint> final {
    static constexpr SurfaceRenderPassHint opaque = SurfaceRenderPassHint::opaque;
    static constexpr SurfaceRenderPassHint transparent = SurfaceRenderPassHint::transparent;
    static constexpr SurfaceRenderPassHint overlay = SurfaceRenderPassHint::overlay;
};

template<>
struct PassHintTraits<ParticleRenderPassHint> final {
    static constexpr ParticleRenderPassHint opaque = ParticleRenderPassHint::opaque;
    static constexpr ParticleRenderPassHint transparent = ParticleRenderPassHint::transparent;
    static constexpr ParticleRenderPassHint overlay = ParticleRenderPassHint::overlay;
};

template<>
struct PassHintTraits<TextRenderPassHint> final {
    static constexpr TextRenderPassHint opaque = TextRenderPassHint::opaque;
    static constexpr TextRenderPassHint transparent = TextRenderPassHint::transparent;
    static constexpr TextRenderPassHint overlay = TextRenderPassHint::overlay;
};

template<typename PassHintT>
[[nodiscard]] constexpr bool IsTransparentPassHint(PassHintT pass_hint_) noexcept {
    return pass_hint_ == PassHintTraits<PassHintT>::transparent;
}

template<typename PassHintT>
[[nodiscard]] constexpr bool IsOverlayPassHint(PassHintT pass_hint_) noexcept {
    return pass_hint_ == PassHintTraits<PassHintT>::overlay;
}

template<typename PassHintT>
[[nodiscard]] constexpr std::uint32_t SortPassBucket(PassHintT pass_hint_) noexcept {
    if (pass_hint_ == PassHintTraits<PassHintT>::opaque) {
        return 0U;
    }
    if (pass_hint_ == PassHintTraits<PassHintT>::transparent) {
        return 1U;
    }
    return 2U;
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT PassHintFromSortBucket(std::uint32_t bucket_) noexcept {
    switch (bucket_) {
    case 0U: return PassHintTraits<PassHintT>::opaque;
    case 1U: return PassHintTraits<PassHintT>::transparent;
    case 2U: return PassHintTraits<PassHintT>::overlay;
    default: break;
    }
    return PassHintTraits<PassHintT>::opaque;
}

[[nodiscard]] constexpr std::uint16_t EncodeDepthMinorBucket(std::uint16_t depth_bin_,
                                                             bool transparent_) noexcept {
    return transparent_
        ? static_cast<std::uint16_t>((std::numeric_limits<std::uint16_t>::max)() - depth_bin_)
        : depth_bin_;
}

template<typename PassHintT>
[[nodiscard]] constexpr std::uint16_t EncodeDepthMinorBucket(std::uint16_t depth_bin_,
                                                             PassHintT pass_hint_) noexcept {
    return EncodeDepthMinorBucket(depth_bin_, IsTransparentPassHint(pass_hint_));
}

inline constexpr std::uint32_t appearance_pipeline_alpha_shift = 25U;
inline constexpr std::uint32_t appearance_pipeline_alpha_mask = 0x3U;
inline constexpr std::uint32_t appearance_pipeline_blend_shift = 27U;
inline constexpr std::uint32_t appearance_pipeline_blend_mask = 0xFU;
inline constexpr std::uint32_t appearance_pipeline_premultiplied_shift = 19U;
inline constexpr std::uint32_t appearance_pipeline_premultiplied_mask = 0x1U;
inline constexpr std::uint32_t runtime_blend_mask = 0x7U;
inline constexpr std::uint32_t geometry2d_runtime_blend_shift = 8U;
inline constexpr std::uint32_t surface2d_runtime_blend_shift = 8U;
inline constexpr std::uint32_t geometry_runtime_blend_shift = 24U;
inline constexpr std::uint32_t surface3d_runtime_blend_shift = 11U;

[[nodiscard]] constexpr AppearanceAlphaMode DecodeAppearanceAlphaMode(
    std::uint32_t appearance_pipeline_bucket_) noexcept {
    return static_cast<AppearanceAlphaMode>(
        (appearance_pipeline_bucket_ >> appearance_pipeline_alpha_shift) &
        appearance_pipeline_alpha_mask);
}

[[nodiscard]] constexpr AppearanceBlendMode DecodeAppearanceBlendMode(
    std::uint32_t appearance_pipeline_bucket_) noexcept {
    return static_cast<AppearanceBlendMode>(
        (appearance_pipeline_bucket_ >> appearance_pipeline_blend_shift) &
        appearance_pipeline_blend_mask);
}

[[nodiscard]] constexpr bool DecodeAppearancePremultipliedAlpha(
    std::uint32_t appearance_pipeline_bucket_) noexcept {
    return ((appearance_pipeline_bucket_ >> appearance_pipeline_premultiplied_shift) &
            appearance_pipeline_premultiplied_mask) != 0U;
}

[[nodiscard]] constexpr RuntimeBlendPreset ResolveRuntimeBlendPreset(
    AppearanceBlendMode blend_mode_,
    AppearanceAlphaMode alpha_mode_,
    bool premultiplied_alpha_ = false) noexcept {
    switch (blend_mode_) {
    case AppearanceBlendMode::alpha:
        return premultiplied_alpha_
            ? RuntimeBlendPreset::premultiplied_alpha
            : RuntimeBlendPreset::alpha;
    case AppearanceBlendMode::additive: return RuntimeBlendPreset::additive;
    case AppearanceBlendMode::multiply: return RuntimeBlendPreset::multiply;
    case AppearanceBlendMode::premultiplied: return RuntimeBlendPreset::premultiplied_alpha;
    case AppearanceBlendMode::screen: return RuntimeBlendPreset::screen;
    case AppearanceBlendMode::opaque:
    default: break;
    }
    if (premultiplied_alpha_ && alpha_mode_ == AppearanceAlphaMode::blend) {
        return RuntimeBlendPreset::premultiplied_alpha;
    }
    return alpha_mode_ == AppearanceAlphaMode::blend
        ? RuntimeBlendPreset::alpha
        : RuntimeBlendPreset::opaque;
}

[[nodiscard]] constexpr RuntimeBlendPreset ResolveRuntimeBlendPreset(
    const AppearanceRuntimeBridge2D& appearance_bridge_) noexcept {
    return ResolveRuntimeBlendPreset(appearance_bridge_.blend_mode,
                                     appearance_bridge_.alpha_mode,
                                     appearance_bridge_.premultiplied_alpha != 0U);
}

[[nodiscard]] constexpr RuntimeBlendPreset ResolveRuntimeBlendPreset(
    const AppearanceRuntimeBridge3D& appearance_bridge_) noexcept {
    return ResolveRuntimeBlendPreset(appearance_bridge_.blend_mode,
                                     appearance_bridge_.alpha_mode);
}

[[nodiscard]] constexpr RuntimeBlendPreset ResolveRuntimeBlendPreset(
    std::uint32_t appearance_pipeline_bucket_) noexcept {
    return ResolveRuntimeBlendPreset(DecodeAppearanceBlendMode(appearance_pipeline_bucket_),
                                     DecodeAppearanceAlphaMode(appearance_pipeline_bucket_),
                                     DecodeAppearancePremultipliedAlpha(appearance_pipeline_bucket_));
}

[[nodiscard]] constexpr RuntimeBlendPreset ResolveRuntimeBlendPreset(
    ParticleBlendMode blend_mode_,
    bool premultiplied_alpha_) noexcept {
    switch (blend_mode_) {
    case ParticleBlendMode::additive: return RuntimeBlendPreset::additive;
    case ParticleBlendMode::multiply: return RuntimeBlendPreset::multiply;
    case ParticleBlendMode::screen: return RuntimeBlendPreset::screen;
    case ParticleBlendMode::premultiplied_alpha: return RuntimeBlendPreset::premultiplied_alpha;
    case ParticleBlendMode::alpha:
    default:
        return premultiplied_alpha_
            ? RuntimeBlendPreset::premultiplied_alpha
            : RuntimeBlendPreset::alpha;
    }
}

[[nodiscard]] constexpr bool IsTransparentBlendPreset(RuntimeBlendPreset preset_) noexcept {
    return preset_ == RuntimeBlendPreset::alpha ||
           preset_ == RuntimeBlendPreset::additive ||
           preset_ == RuntimeBlendPreset::multiply ||
           preset_ == RuntimeBlendPreset::premultiplied_alpha ||
           preset_ == RuntimeBlendPreset::screen;
}

[[nodiscard]] constexpr std::uint32_t EncodeRuntimeBlendPresetBits(RuntimeBlendPreset preset_,
                                                                   std::uint32_t shift_) noexcept {
    return (static_cast<std::uint32_t>(preset_) & runtime_blend_mask) << shift_;
}

[[nodiscard]] constexpr RuntimeBlendPreset DecodeRuntimeBlendPresetBits(std::uint32_t params_,
                                                                        std::uint32_t shift_) noexcept {
    return static_cast<RuntimeBlendPreset>((params_ >> shift_) & runtime_blend_mask);
}

template<typename RouteT>
[[nodiscard]] constexpr std::uint32_t ResolveEffectiveVisualResourceId(const RouteT& route_) noexcept {
    // Keep the authoring/base visual resource route intact and layer linked appearance resources on
    // top. This lets clear/unlink fall back to the original resource without restoration state.
    return HasLinkedAppearanceHandle(route_)
        ? route_.appearance_visual_resource_id
        : route_.visual_resource_id;
}

[[nodiscard]] constexpr bool AppearanceUsesTransparency(
    std::uint32_t appearance_pipeline_bucket_) noexcept {
    if (DecodeAppearanceAlphaMode(appearance_pipeline_bucket_) == AppearanceAlphaMode::blend) {
        return true;
    }
    return IsTransparentBlendPreset(ResolveRuntimeBlendPreset(appearance_pipeline_bucket_));
}

[[nodiscard]] constexpr bool AppearanceUsesTransparency(
    const AppearanceRuntimeBridge2D& appearance_bridge_) noexcept {
    return IsTransparentBlendPreset(ResolveRuntimeBlendPreset(appearance_bridge_));
}

[[nodiscard]] constexpr bool AppearanceUsesTransparency(
    const AppearanceRuntimeBridge3D& appearance_bridge_) noexcept {
    return IsTransparentBlendPreset(ResolveRuntimeBlendPreset(appearance_bridge_));
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT ResolveAppearancePassHint(
    PassHintT current_pass_hint_,
    bool transparent_) noexcept {
    if (IsOverlayPassHint(current_pass_hint_)) {
        return current_pass_hint_;
    }
    return transparent_
        ? PassHintTraits<PassHintT>::transparent
        : PassHintTraits<PassHintT>::opaque;
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT ResolveAppearancePassHint(
    PassHintT current_pass_hint_,
    const AppearanceRuntimeBridge2D& appearance_bridge_) noexcept {
    return ResolveAppearancePassHint(current_pass_hint_,
                                     AppearanceUsesTransparency(appearance_bridge_));
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT ResolveAppearancePassHint(
    PassHintT current_pass_hint_,
    const AppearanceRuntimeBridge3D& appearance_bridge_) noexcept {
    return ResolveAppearancePassHint(current_pass_hint_,
                                     AppearanceUsesTransparency(appearance_bridge_));
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT ResolveFallbackAppearancePassHint(
    PassHintT current_pass_hint_,
    bool transparent_) noexcept {
    if (IsOverlayPassHint(current_pass_hint_) ||
        IsTransparentPassHint(current_pass_hint_)) {
        return current_pass_hint_;
    }
    return transparent_
        ? PassHintTraits<PassHintT>::transparent
        : PassHintTraits<PassHintT>::opaque;
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT ResolveFallbackAppearancePassHint(
    PassHintT current_pass_hint_,
    const AppearanceRuntimeBridge2D& appearance_bridge_) noexcept {
    return ResolveFallbackAppearancePassHint(current_pass_hint_,
                                             AppearanceUsesTransparency(appearance_bridge_));
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT ResolveFallbackAppearancePassHint(
    PassHintT current_pass_hint_,
    const AppearanceRuntimeBridge3D& appearance_bridge_) noexcept {
    return ResolveFallbackAppearancePassHint(current_pass_hint_,
                                             AppearanceUsesTransparency(appearance_bridge_));
}

template<typename PassHintT>
[[nodiscard]] constexpr PassHintT ResolveLinkedPassHint(
    PassHintT current_pass_hint_,
    std::uint32_t appearance_pipeline_bucket_) noexcept {
    return ResolveAppearancePassHint(current_pass_hint_,
                                     AppearanceUsesTransparency(appearance_pipeline_bucket_));
}

[[nodiscard]] constexpr std::uint16_t FoldPipelineSortBucket(
    std::uint64_t pipeline_key_) noexcept {
    std::uint64_t folded = pipeline_key_;
    folded ^= (pipeline_key_ >> 16U);
    folded ^= (pipeline_key_ >> 32U);
    folded ^= (pipeline_key_ >> 48U);
    return static_cast<std::uint16_t>(folded & 0xFFFFU);
}

template<typename StyleT>
[[nodiscard]] constexpr bool StyleUsesTransparency(const StyleT& style_) noexcept {
    if (style_.alpha_mode == AppearanceAlphaMode::blend) {
        return true;
    }
    return style_.blend_mode != AppearanceBlendMode::opaque;
}

template<typename StyleT>
[[nodiscard]] constexpr std::uint8_t DefaultAppearanceQueueBucket(
    const StyleT& style_) noexcept {
    if (style_.alpha_mode == AppearanceAlphaMode::mask) {
        return 1U;
    }
    return StyleUsesTransparency(style_) ? 2U : 0U;
}

} // namespace vr::ecs

