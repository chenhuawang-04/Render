#pragma once

#include "vr/runtime/runtime_ingress_ids.hpp"

#include <cstdint>

namespace vr::render {

enum class AppearanceSampledSurfaceDomain : std::uint32_t {
    asset_texture = 0U,
    surface_image = 1U,
    geometry_image = 2U,
};

struct AppearanceSampledSurfaceHandle final {
    std::uint32_t surface_id;
    AppearanceSampledSurfaceDomain domain;

    [[nodiscard]] bool IsBound() const noexcept {
        return surface_id != 0U;
    }
};

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle MakeAppearanceSampledSurfaceHandle(
    std::uint32_t surface_id_,
    AppearanceSampledSurfaceDomain domain_ = AppearanceSampledSurfaceDomain::asset_texture) noexcept {
    return AppearanceSampledSurfaceHandle{
        .surface_id = surface_id_,
        .domain = domain_,
    };
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle MakeAppearanceTextureHandle(
    asset::TextureId texture_id_) noexcept {
    return MakeAppearanceSampledSurfaceHandle(texture_id_.value,
                                              AppearanceSampledSurfaceDomain::asset_texture);
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle MakeAppearanceSurfaceImageHandle(
    surface::SurfaceImageId image_id_) noexcept {
    return MakeAppearanceSampledSurfaceHandle(image_id_.value,
                                              AppearanceSampledSurfaceDomain::surface_image);
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle MakeAppearanceGeometryImageHandle(
    geometry::GeometryImageId image_id_) noexcept {
    return MakeAppearanceSampledSurfaceHandle(image_id_.value,
                                              AppearanceSampledSurfaceDomain::geometry_image);
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle MakeAppearanceSampledSurfaceHandle(
    asset::TextureId texture_id_) noexcept {
    return MakeAppearanceTextureHandle(texture_id_);
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle MakeAppearanceSampledSurfaceHandle(
    surface::SurfaceImageId image_id_) noexcept {
    return MakeAppearanceSurfaceImageHandle(image_id_);
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle MakeAppearanceSampledSurfaceHandle(
    geometry::GeometryImageId image_id_) noexcept {
    return MakeAppearanceGeometryImageHandle(image_id_);
}

enum class AppearanceSampledSurfaceSlot3D : std::uint32_t {
    base_color = 0U,
    normal = 1U,
    metal_rough = 2U,
    occlusion = 3U,
    emissive = 4U,
};

constexpr std::uint32_t kAppearanceSampledSurfaceDomainBitsPerSlot3D = 2U;
constexpr std::uint32_t kAppearanceSampledSurfaceDomainMask3D = 0x3U;

[[nodiscard]] constexpr std::uint32_t AppearanceSampledSurfaceSlotShift3D(
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return static_cast<std::uint32_t>(slot_) * kAppearanceSampledSurfaceDomainBitsPerSlot3D;
}

[[nodiscard]] constexpr std::uint32_t PackAppearanceSampledSurfaceDomain3D(
    AppearanceSampledSurfaceDomain domain_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return (static_cast<std::uint32_t>(domain_) & kAppearanceSampledSurfaceDomainMask3D)
           << AppearanceSampledSurfaceSlotShift3D(slot_);
}

[[nodiscard]] constexpr std::uint32_t PackAppearanceSampledSurfaceHandle3D(
    const AppearanceSampledSurfaceHandle& handle_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return PackAppearanceSampledSurfaceDomain3D(handle_.domain, slot_);
}

[[nodiscard]] constexpr AppearanceSampledSurfaceDomain UnpackAppearanceSampledSurfaceDomain3D(
    std::uint32_t packed_domains_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    const std::uint32_t domain_bits =
        (packed_domains_ >> AppearanceSampledSurfaceSlotShift3D(slot_)) &
        kAppearanceSampledSurfaceDomainMask3D;
    switch (domain_bits) {
    case 1U:
        return AppearanceSampledSurfaceDomain::surface_image;
    case 2U:
        return AppearanceSampledSurfaceDomain::geometry_image;
    case 0U:
    default:
        break;
    }
    return AppearanceSampledSurfaceDomain::asset_texture;
}

struct AppearanceSampledSurfaceBinding3D final {
    AppearanceSampledSurfaceHandle base_color_surface;
    AppearanceSampledSurfaceHandle normal_surface;
    AppearanceSampledSurfaceHandle metal_rough_surface;
    AppearanceSampledSurfaceHandle occlusion_surface;
    AppearanceSampledSurfaceHandle emissive_surface;
    std::uint32_t surface_sampler_id;
};

[[nodiscard]] constexpr AppearanceSampledSurfaceBinding3D MakeAppearanceSampledSurfaceBinding3D(
    AppearanceSampledSurfaceDomain domain_ = AppearanceSampledSurfaceDomain::asset_texture,
    std::uint32_t surface_sampler_id_ = 0U) noexcept {
    return AppearanceSampledSurfaceBinding3D{
        .base_color_surface = MakeAppearanceSampledSurfaceHandle(0U, domain_),
        .normal_surface = MakeAppearanceSampledSurfaceHandle(0U, domain_),
        .metal_rough_surface = MakeAppearanceSampledSurfaceHandle(0U, domain_),
        .occlusion_surface = MakeAppearanceSampledSurfaceHandle(0U, domain_),
        .emissive_surface = MakeAppearanceSampledSurfaceHandle(0U, domain_),
        .surface_sampler_id = surface_sampler_id_,
    };
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle* ResolveAppearanceSampledSurfaceStorage3D(
    AppearanceSampledSurfaceBinding3D& binding_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    switch (slot_) {
    case AppearanceSampledSurfaceSlot3D::base_color:
        return &binding_.base_color_surface;
    case AppearanceSampledSurfaceSlot3D::normal:
        return &binding_.normal_surface;
    case AppearanceSampledSurfaceSlot3D::metal_rough:
        return &binding_.metal_rough_surface;
    case AppearanceSampledSurfaceSlot3D::occlusion:
        return &binding_.occlusion_surface;
    case AppearanceSampledSurfaceSlot3D::emissive:
    default:
        break;
    }
    return &binding_.emissive_surface;
}

[[nodiscard]] constexpr const AppearanceSampledSurfaceHandle* ResolveAppearanceSampledSurfaceStorage3D(
    const AppearanceSampledSurfaceBinding3D& binding_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    switch (slot_) {
    case AppearanceSampledSurfaceSlot3D::base_color:
        return &binding_.base_color_surface;
    case AppearanceSampledSurfaceSlot3D::normal:
        return &binding_.normal_surface;
    case AppearanceSampledSurfaceSlot3D::metal_rough:
        return &binding_.metal_rough_surface;
    case AppearanceSampledSurfaceSlot3D::occlusion:
        return &binding_.occlusion_surface;
    case AppearanceSampledSurfaceSlot3D::emissive:
    default:
        break;
    }
    return &binding_.emissive_surface;
}

[[nodiscard]] constexpr AppearanceSampledSurfaceHandle ResolveAppearanceSampledSurface3D(
    const AppearanceSampledSurfaceBinding3D& binding_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return *ResolveAppearanceSampledSurfaceStorage3D(binding_, slot_);
}

constexpr bool SetAppearanceSampledSurface3D(AppearanceSampledSurfaceBinding3D& binding_,
                                             AppearanceSampledSurfaceSlot3D slot_,
                                             const AppearanceSampledSurfaceHandle& handle_) noexcept {
    AppearanceSampledSurfaceHandle* const storage =
        ResolveAppearanceSampledSurfaceStorage3D(binding_, slot_);
    if (storage->surface_id == handle_.surface_id &&
        storage->domain == handle_.domain) {
        return false;
    }
    *storage = handle_;
    return true;
}

[[nodiscard]] constexpr std::uint32_t PackAppearanceSampledSurfaceDomains3D(
    const AppearanceSampledSurfaceBinding3D& binding_) noexcept {
    return PackAppearanceSampledSurfaceHandle3D(binding_.base_color_surface,
                                                AppearanceSampledSurfaceSlot3D::base_color) |
           PackAppearanceSampledSurfaceHandle3D(binding_.normal_surface,
                                                AppearanceSampledSurfaceSlot3D::normal) |
           PackAppearanceSampledSurfaceHandle3D(binding_.metal_rough_surface,
                                                AppearanceSampledSurfaceSlot3D::metal_rough) |
           PackAppearanceSampledSurfaceHandle3D(binding_.occlusion_surface,
                                                AppearanceSampledSurfaceSlot3D::occlusion) |
           PackAppearanceSampledSurfaceHandle3D(binding_.emissive_surface,
                                                AppearanceSampledSurfaceSlot3D::emissive);
}

} // namespace vr::render
