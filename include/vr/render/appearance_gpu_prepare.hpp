#pragma once

#include "vr/render/appearance_sampled_surface.hpp"
#include "vr/asset/texture_host.hpp"
#include "vr/ecs/system/appearance_runtime_system.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/surface/surface_image_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vr::render {

enum AppearanceSampledSurfacePresenceFlags3D : std::uint32_t {
    appearance_sampled_surface_presence_base_color = 1U << 0U,
    appearance_sampled_surface_presence_normal = 1U << 1U,
    appearance_sampled_surface_presence_metal_rough = 1U << 2U,
    appearance_sampled_surface_presence_occlusion = 1U << 3U,
    appearance_sampled_surface_presence_emissive = 1U << 4U,
};

struct LinkedAppearanceRecord3D final {
    const ecs::AppearanceGpuRecord<ecs::Dim3>* record = nullptr;
    std::uint32_t record_index = ecs::invalid_appearance_index;
};

struct AppearanceSampledSurfaceResolver3D final {
    const BindlessResourceSystem* bindless_resources = nullptr;
    const asset::TextureHost* texture_host = nullptr;
    const surface::SurfaceImageHost* surface_image_host = nullptr;
    const geometry::GeometryImageHost* geometry_image_host = nullptr;
};

[[nodiscard]] inline LinkedAppearanceRecord3D ResolveLinkedAppearanceRecord(
    ecs::AppearanceHandle handle_,
    const ecs::AppearanceRuntimeScratch<ecs::Dim3>& appearance_runtime_scratch_) noexcept {
    LinkedAppearanceRecord3D resolved{};
    if (handle_.index == ecs::invalid_appearance_index || handle_.generation == 0U) {
        return resolved;
    }
    if (handle_.index >= appearance_runtime_scratch_.gpu_records.size() ||
        handle_.index >= appearance_runtime_scratch_.handle_generations.size()) {
        return resolved;
    }
    if (appearance_runtime_scratch_.handle_generations[handle_.index] != handle_.generation) {
        return resolved;
    }

    resolved.record = &appearance_runtime_scratch_.gpu_records[handle_.index];
    resolved.record_index = handle_.index;
    return resolved;
}

[[nodiscard]] inline float AppearanceColorChannelToFloat(std::uint8_t value_) noexcept {
    return static_cast<float>(value_) / 255.0F;
}

[[nodiscard]] inline std::uint32_t PackAppearanceRuntimeBridgeStyleFlags3D(
    const ecs::AppearanceRuntimeBridge3D& bridge_) noexcept {
    std::uint32_t flags = 0U;
    flags |= static_cast<std::uint32_t>(bridge_.blend_mode) & 0x7U;
    flags |= (static_cast<std::uint32_t>(bridge_.alpha_mode) & 0x3U) << 3U;
    flags |= (static_cast<std::uint32_t>(bridge_.shading_model) & 0x3U) << 5U;
    flags |= ecs::IsAppearanceRuntimeBridge3DDoubleSided(bridge_) ? (1U << 7U) : 0U;
    flags |= ecs::IsAppearanceRuntimeBridge3DCastShadowEnabled(bridge_) ? (1U << 8U) : 0U;
    flags |= ecs::IsAppearanceRuntimeBridge3DReceiveShadowEnabled(bridge_) ? (1U << 9U) : 0U;
    flags |= ecs::IsAppearanceRuntimeBridge3DDepthTestEnabled(bridge_) ? (1U << 10U) : 0U;
    flags |= ecs::IsAppearanceRuntimeBridge3DDepthWriteEnabled(bridge_) ? (1U << 11U) : 0U;
    return flags;
}

[[nodiscard]] inline AppearanceSampledSurfaceDomain ResolveAppearanceSampledSurfaceDomain3D(
    const ecs::AppearanceGpuRecord<ecs::Dim3>& record_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return UnpackAppearanceSampledSurfaceDomain3D(record_.flags_u32[3U], slot_);
}

[[nodiscard]] inline std::uint32_t ResolveAppearanceSampledSurfaceId3D(
    const ecs::AppearanceGpuRecord<ecs::Dim3>& record_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    switch (slot_) {
    case AppearanceSampledSurfaceSlot3D::base_color:
        return record_.textures0_u32[0U];
    case AppearanceSampledSurfaceSlot3D::normal:
        return record_.textures0_u32[1U];
    case AppearanceSampledSurfaceSlot3D::metal_rough:
        return record_.textures0_u32[2U];
    case AppearanceSampledSurfaceSlot3D::occlusion:
        return record_.textures0_u32[3U];
    case AppearanceSampledSurfaceSlot3D::emissive:
        return record_.textures1_u32[0U];
    default:
        break;
    }
    return 0U;
}

[[nodiscard]] inline AppearanceSampledSurfaceHandle ResolveAppearanceSampledSurfaceHandle3D(
    const ecs::AppearanceGpuRecord<ecs::Dim3>& record_,
    AppearanceSampledSurfaceSlot3D slot_) noexcept {
    return {
        .surface_id = ResolveAppearanceSampledSurfaceId3D(record_, slot_),
        .domain = ResolveAppearanceSampledSurfaceDomain3D(record_, slot_)
    };
}

inline void SetAppearanceGpuRecord3DAlphaMode(
    ecs::AppearanceGpuRecord<ecs::Dim3>& record_,
    ecs::AppearanceAlphaMode alpha_mode_) noexcept {
    constexpr std::uint32_t alpha_mode_shift = 3U;
    constexpr std::uint32_t alpha_mode_mask = 0x3U << alpha_mode_shift;
    record_.flags_u32[0U] =
        (record_.flags_u32[0U] & ~alpha_mode_mask) |
        ((static_cast<std::uint32_t>(alpha_mode_) & 0x3U) << alpha_mode_shift);
}

inline void BuildAppearanceGpuRecord3DFromRuntimeBridge(
    const ecs::AppearanceRuntimeBridge3D& bridge_,
    const AppearanceSampledSurfaceBinding3D& binding_,
    ecs::AppearanceGpuRecord<ecs::Dim3>& out_record_) noexcept {
    out_record_.base_rgba = {
        AppearanceColorChannelToFloat(bridge_.base_color.r),
        AppearanceColorChannelToFloat(bridge_.base_color.g),
        AppearanceColorChannelToFloat(bridge_.base_color.b),
        AppearanceColorChannelToFloat(bridge_.base_color.a)
    };
    out_record_.emissive_rgba = {
        AppearanceColorChannelToFloat(bridge_.emissive_color.r),
        AppearanceColorChannelToFloat(bridge_.emissive_color.g),
        AppearanceColorChannelToFloat(bridge_.emissive_color.b),
        AppearanceColorChannelToFloat(bridge_.emissive_color.a)
    };
    out_record_.appearance_params = {
        std::clamp(bridge_.metallic, 0.0F, 1.0F),
        std::clamp(bridge_.roughness, 0.04F, 1.0F),
        std::clamp(bridge_.normal_scale, 0.0F, 4.0F),
        std::clamp(bridge_.occlusion_strength, 0.0F, 1.0F)
    };
    out_record_.extras = {
        std::max(bridge_.emissive_intensity, 0.0F),
        std::clamp(bridge_.alpha_cutoff, 0.0F, 1.0F),
        std::clamp(bridge_.opacity, 0.0F, 1.0F),
        0.0F
    };
    out_record_.flags_u32 = {
        PackAppearanceRuntimeBridgeStyleFlags3D(bridge_),
        0U,
        0U,
        PackAppearanceSampledSurfaceDomains3D(binding_)
    };
    out_record_.textures0_u32 = {
        binding_.base_color_surface.surface_id,
        binding_.normal_surface.surface_id,
        binding_.metal_rough_surface.surface_id,
        binding_.occlusion_surface.surface_id
    };
    out_record_.textures1_u32 = {
        binding_.emissive_surface.surface_id,
        binding_.surface_sampler_id,
        0U,
        0U
    };
}

struct ResolvedAppearanceSampledSurfaceBinding3D final {
    std::uint32_t slot = 0U;
    bool present = false;
};

[[nodiscard]] inline ResolvedAppearanceSampledSurfaceBinding3D ResolveAppearanceSampledSurfaceBinding3D(
    const AppearanceSampledSurfaceResolver3D& resolver_,
    const AppearanceSampledSurfaceHandle& handle_) noexcept {
    ResolvedAppearanceSampledSurfaceBinding3D resolved{};
    if (resolver_.bindless_resources == nullptr || !resolver_.bindless_resources->IsInitialized()) {
        return resolved;
    }

    resolved.slot = resolver_.bindless_resources->PlaceholderImageSlot().index;
    if (!handle_.IsBound()) {
        return resolved;
    }

    switch (handle_.domain) {
    case AppearanceSampledSurfaceDomain::surface_image: {
        if (resolver_.surface_image_host == nullptr || !resolver_.surface_image_host->IsInitialized()) {
            return resolved;
        }
        const surface::SurfaceImageId surface_image_id{handle_.surface_id};
        if (resolver_.surface_image_host->FindImage(surface_image_id) == nullptr) {
            return resolved;
        }
        {
            const BindlessSlot slot =
                resolver_.surface_image_host->ResolveBindlessImageSlot(surface_image_id);
            if (!slot.IsValid()) {
                return resolved;
            }
            resolved.slot = slot.index;
            resolved.present = true;
            return resolved;
        }
    }
    case AppearanceSampledSurfaceDomain::geometry_image: {
        if (resolver_.geometry_image_host == nullptr || !resolver_.geometry_image_host->IsInitialized()) {
            return resolved;
        }
        const geometry::GeometryImageId geometry_image_id{handle_.surface_id};
        if (resolver_.geometry_image_host->FindImage(geometry_image_id) == nullptr) {
            return resolved;
        }
        {
            const BindlessSlot slot =
                resolver_.geometry_image_host->ResolveBindlessImageSlot(geometry_image_id);
            if (!slot.IsValid()) {
                return resolved;
            }
            resolved.slot = slot.index;
            resolved.present = true;
            return resolved;
        }
    }
    case AppearanceSampledSurfaceDomain::asset_texture:
    default:
        break;
    }

    if (resolver_.texture_host == nullptr || !resolver_.texture_host->IsInitialized()) {
        return resolved;
    }

    const asset::TextureId texture_id{handle_.surface_id};
    if (resolver_.texture_host->FindTexture(texture_id) == nullptr) {
        return resolved;
    }

    resolved.slot =
        resolver_.bindless_resources->ResolveTextureImageSlot(*resolver_.texture_host, texture_id).index;
    resolved.present = true;
    return resolved;
}

[[nodiscard]] inline std::uint32_t ResolveAppearanceSurfaceSamplerSlot3D(
    const AppearanceSampledSurfaceResolver3D& resolver_,
    std::uint32_t surface_sampler_id_,
    const AppearanceSampledSurfaceHandle& fallback_surface_) noexcept {
    if (resolver_.bindless_resources == nullptr || !resolver_.bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (surface_sampler_id_ != 0U) {
        return resolver_.bindless_resources
            ->ResolveRegisteredSamplerSlot(resource::SamplerId{surface_sampler_id_}).index;
    }
    if (fallback_surface_.domain == AppearanceSampledSurfaceDomain::asset_texture &&
        fallback_surface_.IsBound() &&
        resolver_.texture_host != nullptr &&
        resolver_.texture_host->IsInitialized()) {
        const asset::TextureId texture_id{fallback_surface_.surface_id};
        if (resolver_.texture_host->FindTexture(texture_id) != nullptr) {
            return resolver_.bindless_resources
                ->ResolveTextureSamplerSlot(*resolver_.texture_host, texture_id)
                .index;
        }
    }
    return resolver_.bindless_resources->DefaultSamplerSlot().index;
}

inline void EncodeAppearanceGpuRecord3DForSampling(
    const ecs::AppearanceGpuRecord<ecs::Dim3>& source_record_,
    const AppearanceSampledSurfaceResolver3D& resolver_,
    ecs::AppearanceGpuRecord<ecs::Dim3>& out_record_) noexcept {
    out_record_ = source_record_;
    const AppearanceSampledSurfaceHandle base_color_handle =
        ResolveAppearanceSampledSurfaceHandle3D(source_record_,
                                               AppearanceSampledSurfaceSlot3D::base_color);
    const AppearanceSampledSurfaceHandle normal_handle =
        ResolveAppearanceSampledSurfaceHandle3D(source_record_,
                                               AppearanceSampledSurfaceSlot3D::normal);
    const AppearanceSampledSurfaceHandle metal_rough_handle =
        ResolveAppearanceSampledSurfaceHandle3D(source_record_,
                                               AppearanceSampledSurfaceSlot3D::metal_rough);
    const AppearanceSampledSurfaceHandle occlusion_handle =
        ResolveAppearanceSampledSurfaceHandle3D(source_record_,
                                               AppearanceSampledSurfaceSlot3D::occlusion);
    const AppearanceSampledSurfaceHandle emissive_handle =
        ResolveAppearanceSampledSurfaceHandle3D(source_record_,
                                               AppearanceSampledSurfaceSlot3D::emissive);

    const ResolvedAppearanceSampledSurfaceBinding3D base_color =
        ResolveAppearanceSampledSurfaceBinding3D(resolver_, base_color_handle);
    const ResolvedAppearanceSampledSurfaceBinding3D normal =
        ResolveAppearanceSampledSurfaceBinding3D(resolver_, normal_handle);
    const ResolvedAppearanceSampledSurfaceBinding3D metal_rough =
        ResolveAppearanceSampledSurfaceBinding3D(resolver_, metal_rough_handle);
    const ResolvedAppearanceSampledSurfaceBinding3D occlusion =
        ResolveAppearanceSampledSurfaceBinding3D(resolver_, occlusion_handle);
    const ResolvedAppearanceSampledSurfaceBinding3D emissive =
        ResolveAppearanceSampledSurfaceBinding3D(resolver_, emissive_handle);

    AppearanceSampledSurfaceHandle sampler_fallback_surface = base_color_handle;
    if (!sampler_fallback_surface.IsBound()) {
        sampler_fallback_surface = metal_rough_handle;
    }
    if (!sampler_fallback_surface.IsBound()) {
        sampler_fallback_surface = occlusion_handle;
    }
    if (!sampler_fallback_surface.IsBound()) {
        sampler_fallback_surface = emissive_handle;
    }
    if (!sampler_fallback_surface.IsBound()) {
        sampler_fallback_surface = normal_handle;
    }

    std::uint32_t presence_mask = 0U;
    if (base_color.present) {
        presence_mask |= appearance_sampled_surface_presence_base_color;
    }
    if (normal.present) {
        presence_mask |= appearance_sampled_surface_presence_normal;
    }
    if (metal_rough.present) {
        presence_mask |= appearance_sampled_surface_presence_metal_rough;
    }
    if (occlusion.present) {
        presence_mask |= appearance_sampled_surface_presence_occlusion;
    }
    if (emissive.present) {
        presence_mask |= appearance_sampled_surface_presence_emissive;
    }

    out_record_.textures0_u32 = {
        base_color.slot,
        normal.slot,
        metal_rough.slot,
        occlusion.slot
    };
    out_record_.textures1_u32 = {
        emissive.slot,
        ResolveAppearanceSurfaceSamplerSlot3D(resolver_,
                                              source_record_.textures1_u32[1U],
                                              sampler_fallback_surface),
        source_record_.textures1_u32[2U],
        presence_mask
    };
}

[[nodiscard]] inline bool AppearanceGpuRecord3DEquals(
    const ecs::AppearanceGpuRecord<ecs::Dim3>& lhs_,
    const ecs::AppearanceGpuRecord<ecs::Dim3>& rhs_) noexcept {
    return std::memcmp(&lhs_, &rhs_, sizeof(lhs_)) == 0;
}

inline void CopyAppearanceGpuRecord3DRange(
    const ecs::AppearanceGpuRecord<ecs::Dim3>* records_,
    resource::BufferResource& buffer_,
    std::uint32_t begin_index_,
    std::uint32_t count_) noexcept {
    if (records_ == nullptr || buffer_.mapped_ptr == nullptr || count_ == 0U) {
        return;
    }

    auto* dst = static_cast<std::byte*>(buffer_.mapped_ptr) +
                static_cast<std::size_t>(begin_index_) *
                    sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>);
    std::memcpy(dst,
                records_ + begin_index_,
                static_cast<std::size_t>(count_) * sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>));
}

} // namespace vr::render
