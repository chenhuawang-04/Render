#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::text {

template<typename T>
using GlyphUploadMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GlyphUploadHostCreateInfo {
    VkFormat atlas_format = VK_FORMAT_R8_UNORM;
    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bool prefer_shader_read_layout = true;
    bool force_general_layout = false;
    bool use_linear_sampler = true;
    bool clamp_to_edge = true;
    std::uint32_t reserve_page_count = 8U;
};

struct GlyphUploadHostStats {
    std::uint32_t page_count = 0U;
    std::uint32_t uploaded_page_count = 0U;
    std::uint32_t uploaded_rect_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t skipped_clean_page_count = 0U;
};

class GlyphUploadHost final {
public:
    GlyphUploadHost() = default;
    ~GlyphUploadHost() = default;

    GlyphUploadHost(const GlyphUploadHost&) = delete;
    GlyphUploadHost& operator=(const GlyphUploadHost&) = delete;

    GlyphUploadHost(GlyphUploadHost&&) = delete;
    GlyphUploadHost& operator=(GlyphUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    resource::SamplerHost& sampler_host_,
                    const GlyphUploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void UploadDirtyPages(VulkanContext& context_,
                          render::UploadHost& upload_host_,
                          std::uint32_t frame_index_,
                          GlyphAtlasHost& atlas_host_,
                          std::uint64_t last_submitted_value_ = 0U,
                          std::uint64_t completed_submit_value_ = 0U);

    [[nodiscard]] VkImageView PageImageView(std::uint32_t page_index_) const;
    [[nodiscard]] VkImage PageImage(std::uint32_t page_index_) const;
    [[nodiscard]] VkImageLayout PageShaderLayout(std::uint32_t page_index_) const;
    [[nodiscard]] VkSampler Sampler() const;
    [[nodiscard]] resource::SamplerId SamplerId() const noexcept;
    [[nodiscard]] std::uint32_t PageCount() const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GlyphUploadHostStats& Stats() const noexcept;

private:
    struct PageResource {
        resource::ImageResource image{};
        VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::uint32_t generation = 0U;
    };

    [[nodiscard]] bool ShouldUseGeneralLayout(const VulkanContext& context_) const noexcept;
    void EnsurePageResources(VulkanContext& context_, const GlyphAtlasHost& atlas_host_);
    void RetirePageResource(PageResource& page_resource_,
                            std::uint64_t retire_value_);
    void CollectRetiredPageResources(VulkanContext& context_,
                                     std::uint64_t completed_submit_value_);
    void DestroyRetiredPageResources(VulkanContext& context_) noexcept;
    [[nodiscard]] std::uint64_t ComputeRetireValue() const noexcept;
    void DestroyPageResources(VulkanContext& context_) noexcept;
    void TransitionImageLayoutIfNeeded(render::UploadHost& upload_host_,
                                       std::uint32_t frame_index_,
                                       PageResource& page_resource_,
                                       VkImageLayout new_layout_,
                                       std::uint32_t& barrier_count_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;

    GlyphUploadHostCreateInfo create_info_cache{};
    GlyphUploadMcVector<PageResource> pages{};
    render::RetireBus<resource::ImageResource> retired_pages{};
    GlyphUploadMcVector<std::uint8_t> rect_upload_scratch{};

    resource::SamplerId sampler_id{};
    GlyphUploadHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::text
