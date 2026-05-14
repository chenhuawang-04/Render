#pragma once

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

enum AppearanceTexturePresenceFlags3D : std::uint32_t {
    appearance_texture_presence_base_color = 1U << 0U,
    appearance_texture_presence_normal = 1U << 1U,
    appearance_texture_presence_metal_rough = 1U << 2U,
    appearance_texture_presence_occlusion = 1U << 3U,
    appearance_texture_presence_emissive = 1U << 4U,
};

struct LinkedAppearanceRecord3D final {
    const ecs::AppearanceGpuRecord<ecs::Dim3>* record = nullptr;
    std::uint32_t record_index = ecs::invalid_appearance_index;
};

enum class AppearanceTextureSource3D : std::uint32_t {
    asset_texture = 0U,
    surface_image = 1U,
    geometry_image = 2U,
};

struct AppearanceTextureBindingIds3D final {
    std::uint32_t base_color_texture_id = 0U;
    std::uint32_t normal_texture_id = 0U;
    std::uint32_t metal_rough_texture_id = 0U;
    std::uint32_t occlusion_texture_id = 0U;
    std::uint32_t emissive_texture_id = 0U;
    std::uint32_t sampler_state_id = 0U;
    std::uint32_t binding_layout_id = 0U;
    AppearanceTextureSource3D texture_source = AppearanceTextureSource3D::asset_texture;
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

[[nodiscard]] inline std::uint32_t PackAppearanceTextureSource3D(
    AppearanceTextureSource3D source_) noexcept {
    return static_cast<std::uint32_t>(source_) & 0x3U;
}

[[nodiscard]] inline AppearanceTextureSource3D ResolveAppearanceTextureSource3D(
    const ecs::AppearanceGpuRecord<ecs::Dim3>& record_) noexcept {
    switch (record_.flags_u32[1U] & 0x3U) {
    case 1U:
        return AppearanceTextureSource3D::surface_image;
    case 2U:
        return AppearanceTextureSource3D::geometry_image;
    case 0U:
    default:
        break;
    }
    return AppearanceTextureSource3D::asset_texture;
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
    const AppearanceTextureBindingIds3D& binding_,
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
        PackAppearanceTextureSource3D(binding_.texture_source),
        0U,
        0U
    };
    out_record_.textures0_u32 = {
        binding_.base_color_texture_id,
        binding_.normal_texture_id,
        binding_.metal_rough_texture_id,
        binding_.occlusion_texture_id
    };
    out_record_.textures1_u32 = {
        binding_.emissive_texture_id,
        binding_.sampler_state_id,
        binding_.binding_layout_id,
        0U
    };
}

struct ResolvedAppearanceTextureBinding3D final {
    std::uint32_t slot = 0U;
    bool present = false;
};

[[nodiscard]] inline ResolvedAppearanceTextureBinding3D ResolveAppearanceTextureBinding3D(
    const BindlessResourceSystem* bindless_resources_,
    const asset::TextureHost* texture_host_,
    const surface::SurfaceImageHost* surface_image_host_,
    const geometry::GeometryImageHost* geometry_image_host_,
    std::uint32_t texture_id_,
    AppearanceTextureSource3D texture_source_) noexcept {
    ResolvedAppearanceTextureBinding3D resolved{};
    if (bindless_resources_ == nullptr || !bindless_resources_->IsInitialized()) {
        return resolved;
    }

    resolved.slot = bindless_resources_->PlaceholderImageSlot().index;
    if (texture_id_ == 0U) {
        return resolved;
    }

    switch (texture_source_) {
    case AppearanceTextureSource3D::surface_image:
        if (surface_image_host_ == nullptr || !surface_image_host_->IsInitialized()) {
            return resolved;
        }
        if (surface_image_host_->FindImage(texture_id_) == nullptr) {
            return resolved;
        }
        {
            const BindlessSlot slot = surface_image_host_->ResolveBindlessImageSlot(texture_id_);
            if (!slot.IsValid()) {
                return resolved;
            }
            resolved.slot = slot.index;
            resolved.present = true;
            return resolved;
        }
    case AppearanceTextureSource3D::geometry_image:
        if (geometry_image_host_ == nullptr || !geometry_image_host_->IsInitialized()) {
            return resolved;
        }
        if (geometry_image_host_->FindImage(texture_id_) == nullptr) {
            return resolved;
        }
        {
            const BindlessSlot slot = geometry_image_host_->ResolveBindlessImageSlot(texture_id_);
            if (!slot.IsValid()) {
                return resolved;
            }
            resolved.slot = slot.index;
            resolved.present = true;
            return resolved;
        }
    case AppearanceTextureSource3D::asset_texture:
    default:
        break;
    }

    if (texture_host_ == nullptr || !texture_host_->IsInitialized()) {
        return resolved;
    }

    const asset::TextureId texture_id{texture_id_};
    if (texture_host_->FindTexture(texture_id) == nullptr) {
        return resolved;
    }

    resolved.slot = bindless_resources_->ResolveTextureImageSlot(*texture_host_, texture_id).index;
    resolved.present = true;
    return resolved;
}

[[nodiscard]] inline std::uint32_t ResolveAppearanceSamplerSlot3D(
    const BindlessResourceSystem* bindless_resources_,
    const asset::TextureHost* texture_host_,
    AppearanceTextureSource3D texture_source_,
    std::uint32_t sampler_state_id_,
    std::uint32_t fallback_texture_id_) noexcept {
    if (bindless_resources_ == nullptr || !bindless_resources_->IsInitialized()) {
        return 0U;
    }
    if (sampler_state_id_ != 0U) {
        return bindless_resources_
            ->ResolveRegisteredSamplerSlot(resource::SamplerId{sampler_state_id_}).index;
    }
    if (texture_source_ == AppearanceTextureSource3D::asset_texture &&
        fallback_texture_id_ != 0U &&
        texture_host_ != nullptr &&
        texture_host_->IsInitialized()) {
        const asset::TextureId texture_id{fallback_texture_id_};
        if (texture_host_->FindTexture(texture_id) != nullptr) {
            return bindless_resources_->ResolveTextureSamplerSlot(*texture_host_, texture_id).index;
        }
    }
    return bindless_resources_->DefaultSamplerSlot().index;
}

inline void EncodeAppearanceGpuRecord3DForSampling(
    const ecs::AppearanceGpuRecord<ecs::Dim3>& source_record_,
    const BindlessResourceSystem* bindless_resources_,
    const asset::TextureHost* texture_host_,
    const surface::SurfaceImageHost* surface_image_host_,
    const geometry::GeometryImageHost* geometry_image_host_,
    ecs::AppearanceGpuRecord<ecs::Dim3>& out_record_) noexcept {
    out_record_ = source_record_;
    const AppearanceTextureSource3D texture_source =
        ResolveAppearanceTextureSource3D(source_record_);

    const ResolvedAppearanceTextureBinding3D base_color =
        ResolveAppearanceTextureBinding3D(bindless_resources_,
                                         texture_host_,
                                         surface_image_host_,
                                         geometry_image_host_,
                                         source_record_.textures0_u32[0U],
                                         texture_source);
    const ResolvedAppearanceTextureBinding3D normal =
        ResolveAppearanceTextureBinding3D(bindless_resources_,
                                         texture_host_,
                                         surface_image_host_,
                                         geometry_image_host_,
                                         source_record_.textures0_u32[1U],
                                         texture_source);
    const ResolvedAppearanceTextureBinding3D metal_rough =
        ResolveAppearanceTextureBinding3D(bindless_resources_,
                                         texture_host_,
                                         surface_image_host_,
                                         geometry_image_host_,
                                         source_record_.textures0_u32[2U],
                                         texture_source);
    const ResolvedAppearanceTextureBinding3D occlusion =
        ResolveAppearanceTextureBinding3D(bindless_resources_,
                                         texture_host_,
                                         surface_image_host_,
                                         geometry_image_host_,
                                         source_record_.textures0_u32[3U],
                                         texture_source);
    const ResolvedAppearanceTextureBinding3D emissive =
        ResolveAppearanceTextureBinding3D(bindless_resources_,
                                         texture_host_,
                                         surface_image_host_,
                                         geometry_image_host_,
                                         source_record_.textures1_u32[0U],
                                         texture_source);

    std::uint32_t sampler_fallback_texture_id = source_record_.textures0_u32[0U];
    if (sampler_fallback_texture_id == 0U) {
        sampler_fallback_texture_id = source_record_.textures0_u32[2U];
    }
    if (sampler_fallback_texture_id == 0U) {
        sampler_fallback_texture_id = source_record_.textures0_u32[3U];
    }
    if (sampler_fallback_texture_id == 0U) {
        sampler_fallback_texture_id = source_record_.textures1_u32[0U];
    }
    if (sampler_fallback_texture_id == 0U) {
        sampler_fallback_texture_id = source_record_.textures0_u32[1U];
    }

    std::uint32_t presence_mask = 0U;
    if (base_color.present) {
        presence_mask |= appearance_texture_presence_base_color;
    }
    if (normal.present) {
        presence_mask |= appearance_texture_presence_normal;
    }
    if (metal_rough.present) {
        presence_mask |= appearance_texture_presence_metal_rough;
    }
    if (occlusion.present) {
        presence_mask |= appearance_texture_presence_occlusion;
    }
    if (emissive.present) {
        presence_mask |= appearance_texture_presence_emissive;
    }

    out_record_.textures0_u32 = {
        base_color.slot,
        normal.slot,
        metal_rough.slot,
        occlusion.slot
    };
    out_record_.textures1_u32 = {
        emissive.slot,
        ResolveAppearanceSamplerSlot3D(bindless_resources_,
                                       texture_host_,
                                       texture_source,
                                       source_record_.textures1_u32[1U],
                                       sampler_fallback_texture_id),
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
