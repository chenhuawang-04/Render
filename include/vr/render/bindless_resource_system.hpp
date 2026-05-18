#pragma once

#include "vr/render/bindless_types.hpp"
#include "vr/resource/image_host.hpp"

#include <cstdint>
#include <vector>

namespace vr {
class VulkanContext;
}

namespace vr::asset {
class TextureHost;
struct TextureId;
}

namespace vr::surface {
class SurfaceImageHost;
}

namespace vr::geometry {
class GeometryImageHost;
}

namespace vr::shadow {
class ShadowAtlasHost;
}

namespace vr::text {
class GlyphUploadHost;
}

namespace vr::render {
class RenderTargetHost;
}

namespace vr::resource {
class GpuMemoryHost;
class SamplerHost;
struct SamplerId;
}

namespace vr::render {

class DescriptorHost;

enum class BindlessUpdateAfterBindPolicy : std::uint8_t {
    auto_if_supported = 0U,
    disabled = 1U,
    required = 2U,
};

struct BindlessResourceSystemCreateInfo final {
    std::uint32_t sampled_image_capacity = 8192U;
    std::uint32_t sampler_capacity = 256U;
    VkShaderStageFlags sampled_image_stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkShaderStageFlags sampler_stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT;
    BindlessUpdateAfterBindPolicy update_after_bind_policy =
        BindlessUpdateAfterBindPolicy::disabled;
};

struct BindlessResourceSystemStats final {
    bool initialized = false;
    std::uint64_t revision = 0U;
    BindlessTableId sampled_image_table{};
    BindlessTableId sampler_table{};
    BindlessSlot placeholder_image_slot{};
    BindlessSlot placeholder_image_array_slot{};
    BindlessSlot default_sampler_slot{};
    BindlessTableStats sampled_image{};
    BindlessTableStats sampler{};
};

class BindlessResourceSystem final {
public:
    [[nodiscard]] static constexpr BindlessTableId SampledImageTableContractId() noexcept {
        return BindlessTableId{.value = 1U};
    }

    [[nodiscard]] static constexpr BindlessTableId SamplerTableContractId() noexcept {
        return BindlessTableId{.value = 2U};
    }

    BindlessResourceSystem() = default;
    ~BindlessResourceSystem() = default;

    BindlessResourceSystem(const BindlessResourceSystem&) = delete;
    BindlessResourceSystem& operator=(const BindlessResourceSystem&) = delete;
    BindlessResourceSystem(BindlessResourceSystem&&) = delete;
    BindlessResourceSystem& operator=(BindlessResourceSystem&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    DescriptorHost& descriptor_host_,
                    resource::SamplerHost& sampler_host_,
                    const BindlessResourceSystemCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_) noexcept;

    void ConfigureTextureHost(asset::TextureHost& texture_host_) const;
    void ConfigureSurfaceImageHost(surface::SurfaceImageHost& surface_image_host_) const;
    void ConfigureGeometryImageHost(geometry::GeometryImageHost& geometry_image_host_) const;
    void ConfigureShadowAtlasHost(shadow::ShadowAtlasHost& shadow_atlas_host_) const;
    void ConfigureRenderTargetHost(render::RenderTargetHost& render_target_host_) const;
    void ConfigureGlyphUploadHost(text::GlyphUploadHost& glyph_upload_host_) const;

    [[nodiscard]] BindlessSlot ResolveTextureImageSlot(const asset::TextureHost& texture_host_,
                                                       asset::TextureId texture_id_) const noexcept;
    [[nodiscard]] BindlessSlot ResolveTextureSamplerSlot(const asset::TextureHost& texture_host_,
                                                         asset::TextureId texture_id_) const noexcept;
    [[nodiscard]] BindlessSlot ResolveRegisteredSamplerSlot(resource::SamplerId sampler_id_) const;

    [[nodiscard]] VkDescriptorSet SampledImageSet() const noexcept;
    [[nodiscard]] VkDescriptorSet SamplerSet() const noexcept;
    [[nodiscard]] VkDescriptorSetLayout SampledImageLayout() const noexcept;
    [[nodiscard]] VkDescriptorSetLayout SamplerLayout() const noexcept;

    [[nodiscard]] BindlessTableId SampledImageTable() const noexcept;
    [[nodiscard]] BindlessTableId SamplerTable() const noexcept;
    [[nodiscard]] BindlessSlot PlaceholderImageSlot() const noexcept;
    [[nodiscard]] BindlessSlot PlaceholderImage2DArraySlot() const noexcept;
    [[nodiscard]] BindlessSlot DefaultSamplerSlot() const noexcept;
    [[nodiscard]] VkImageLayout PlaceholderImageLayout() const noexcept;
    [[nodiscard]] VkSampler DefaultSampler() const noexcept;
    [[nodiscard]] const resource::ImageResource& PlaceholderImage() const noexcept;
    [[nodiscard]] const BindlessResourceSystemStats& Stats() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;

private:
    void RefreshStats(DescriptorHost* descriptor_host_) const noexcept;

private:
    DescriptorHost* descriptor_host_ = nullptr;
    resource::SamplerHost* sampler_host_ = nullptr;
    BindlessResourceSystemCreateInfo create_info_cache{};
    resource::ImageResource placeholder_image_{};
    VkImageView placeholder_image_array_view_ = VK_NULL_HANDLE;
    VkImageLayout placeholder_image_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    BindlessTableId sampled_image_table_{};
    BindlessTableId sampler_table_{};
    BindlessSlot placeholder_image_slot_{.index = 0U, .generation = 1U};
    BindlessSlot placeholder_image_array_slot_{};
    BindlessSlot default_sampler_slot_{.index = 0U, .generation = 1U};
    VkSampler default_sampler_ = VK_NULL_HANDLE;
    std::uint64_t revision_ = 0U;
    mutable std::vector<BindlessSlot> registered_sampler_slots_{};
    mutable BindlessResourceSystemStats stats_{};
    bool initialized_ = false;
};

} // namespace vr::render

