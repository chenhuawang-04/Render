#pragma once

#include "vr/render/bindless_types.hpp"
#include "vr/resource/image_host.hpp"

#include <cstdint>

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
        BindlessUpdateAfterBindPolicy::auto_if_supported;
};

struct BindlessResourceSystemStats final {
    bool initialized = false;
    BindlessTableId sampled_image_table{};
    BindlessTableId sampler_table{};
    BindlessSlot placeholder_image_slot{};
    BindlessSlot default_sampler_slot{};
    BindlessTableStats sampled_image{};
    BindlessTableStats sampler{};
};

class BindlessResourceSystem final {
public:
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

    void ConfigureTextureHost(asset::TextureHost& texture_host_) const noexcept;
    void ConfigureSurfaceImageHost(surface::SurfaceImageHost& surface_image_host_) const noexcept;

    [[nodiscard]] BindlessSlot ResolveTextureImageSlot(const asset::TextureHost& texture_host_,
                                                       asset::TextureId texture_id_) const noexcept;
    [[nodiscard]] BindlessSlot ResolveTextureSamplerSlot(const asset::TextureHost& texture_host_,
                                                         asset::TextureId texture_id_) const noexcept;

    [[nodiscard]] VkDescriptorSet SampledImageSet() const noexcept;
    [[nodiscard]] VkDescriptorSet SamplerSet() const noexcept;
    [[nodiscard]] VkDescriptorSetLayout SampledImageLayout() const noexcept;
    [[nodiscard]] VkDescriptorSetLayout SamplerLayout() const noexcept;

    [[nodiscard]] BindlessTableId SampledImageTable() const noexcept;
    [[nodiscard]] BindlessTableId SamplerTable() const noexcept;
    [[nodiscard]] BindlessSlot PlaceholderImageSlot() const noexcept;
    [[nodiscard]] BindlessSlot DefaultSamplerSlot() const noexcept;
    [[nodiscard]] VkImageLayout PlaceholderImageLayout() const noexcept;
    [[nodiscard]] VkSampler DefaultSampler() const noexcept;
    [[nodiscard]] const resource::ImageResource& PlaceholderImage() const noexcept;
    [[nodiscard]] const BindlessResourceSystemStats& Stats() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    void RefreshStats(DescriptorHost* descriptor_host_) noexcept;

private:
    DescriptorHost* descriptor_host_ = nullptr;
    BindlessResourceSystemCreateInfo create_info_cache{};
    resource::ImageResource placeholder_image_{};
    VkImageLayout placeholder_image_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    BindlessTableId sampled_image_table_{};
    BindlessTableId sampler_table_{};
    BindlessSlot placeholder_image_slot_{.index = 0U, .generation = 1U};
    BindlessSlot default_sampler_slot_{.index = 0U, .generation = 1U};
    VkSampler default_sampler_ = VK_NULL_HANDLE;
    BindlessResourceSystemStats stats_{};
    bool initialized_ = false;
};

} // namespace vr::render
