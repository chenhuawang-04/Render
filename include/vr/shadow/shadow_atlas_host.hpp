#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/bindless_types.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::render {
class DescriptorHost;
}

namespace vr::shadow {

template<typename T>
using ShadowAtlasMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ShadowAtlasRequest final {
    std::uint32_t namespace_id = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    std::uint16_t layer_count = 1U;
};

struct ShadowAtlasHostCreateInfo final {
    std::uint32_t reserve_atlas_count = 8U;
    std::uint32_t reserve_retired_count = 16U;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

struct ShadowAtlasHostStats final {
    std::uint32_t atlas_count = 0U;
    std::uint32_t created_atlas_count = 0U;
    std::uint32_t resized_atlas_count = 0U;
    std::uint32_t removed_atlas_count = 0U;
    std::uint32_t retired_atlas_count = 0U;
    std::uint32_t destroyed_atlas_count = 0U;
    std::uint32_t revision = 0U;
};

struct ShadowAtlasHostBindlessConfig final {
    render::DescriptorHost* descriptor_host = nullptr;
    render::BindlessTableId image_table{};
    std::uint64_t bindless_revision = 0U;

    [[nodiscard]] bool Enabled() const noexcept {
        return descriptor_host != nullptr && image_table.IsValid();
    }

    [[nodiscard]] bool SameBinding(const ShadowAtlasHostBindlessConfig& rhs_) const noexcept {
        return descriptor_host == rhs_.descriptor_host &&
               image_table.value == rhs_.image_table.value &&
               bindless_revision == rhs_.bindless_revision;
    }
};

class ShadowAtlasHost final {
public:
    struct AtlasRecord final {
        struct AtlasBindlessState final {
            render::BindlessSlot image_slot{};
            std::uint32_t revision_written = 0U;
            std::uint64_t retire_value = 0U;
        };

        std::uint32_t namespace_id = 0U;
        std::uint16_t width = 0U;
        std::uint16_t height = 0U;
        std::uint16_t layer_count = 1U;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        resource::ImageResource resource{};
        VkImageView array_view = VK_NULL_HANDLE;
        ShadowAtlasMcVector<VkImageView> layer_views{};
        std::uint32_t revision = 0U;
        AtlasBindlessState bindless{};
    };

    ShadowAtlasHost() = default;
    ~ShadowAtlasHost() = default;

    ShadowAtlasHost(const ShadowAtlasHost&) = delete;
    ShadowAtlasHost& operator=(const ShadowAtlasHost&) = delete;

    ShadowAtlasHost(ShadowAtlasHost&&) = delete;
    ShadowAtlasHost& operator=(ShadowAtlasHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const ShadowAtlasHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint64_t completed_submit_value_);

    void ConfigureBindless(const ShadowAtlasHostBindlessConfig& bindless_config_);

    void EnsureAtlases(VulkanContext& context_,
                       std::uint64_t last_submitted_value_,
                       std::uint64_t completed_submit_value_,
                       const ShadowAtlasRequest* requests_,
                       std::uint32_t request_count_);

    [[nodiscard]] const AtlasRecord* FindAtlas(std::uint32_t namespace_id_) const noexcept;
    [[nodiscard]] AtlasRecord* FindAtlas(std::uint32_t namespace_id_) noexcept;
    [[nodiscard]] render::BindlessSlot ResolveBindlessAtlasSlot(std::uint32_t namespace_id_) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const ShadowAtlasHostStats& Stats() const noexcept;
    [[nodiscard]] VkFormat DepthFormat() const noexcept;
    [[nodiscard]] const ShadowAtlasHostBindlessConfig& BindlessConfig() const noexcept;

private:
    struct RetiredAtlasPayload final {
        resource::ImageResource resource{};
        VkImageView array_view = VK_NULL_HANDLE;
        ShadowAtlasMcVector<VkImageView> layer_views{};
    };

    [[nodiscard]] std::size_t LowerBoundAtlasIndex(std::uint32_t namespace_id_) const noexcept;
    static void DestroyAtlasViews(VulkanContext& context_,
                                  ShadowAtlasMcVector<VkImageView>& layer_views_) noexcept;
    static void DestroyAtlasRecord(VulkanContext& context_,
                                   AtlasRecord& record_) noexcept;
    void RetireAtlas(AtlasRecord& record_,
                     std::uint64_t retire_value_,
                     bool preserve_bindless_slot_ = false);
    void CollectRetiredAtlases(VulkanContext& context_,
                               std::uint64_t completed_submit_value_);
    void DestroyRetiredAtlases(VulkanContext& context_) noexcept;
    void InvalidateBindlessRecords(const ShadowAtlasHostBindlessConfig& bindless_config_);
    [[nodiscard]] std::uint64_t ComputeBindlessRetireValue() const noexcept;
    [[nodiscard]] AtlasRecord CreateAtlasRecord(VulkanContext& context_,
                                                const ShadowAtlasRequest& request_) const;
    void SyncBindlessRecords();

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    ShadowAtlasHostBindlessConfig bindless_config{};
    ShadowAtlasHostCreateInfo create_info_cache{};
    ShadowAtlasMcVector<AtlasRecord> atlases{};
    render::RetireBus<RetiredAtlasPayload> retired_atlases{};
    ShadowAtlasHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::shadow

