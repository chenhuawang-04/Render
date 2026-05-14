#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/spatial_types.hpp"
#include "vr/render/bindless_types.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::render {
class DescriptorHost;
}

namespace vr::asset {

template<typename T>
using TextureMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct TextureHostCreateInfo final {
    std::uint32_t reserve_texture_count = 128U;
    std::uint32_t reserve_retired_texture_count = 128U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool prefer_bc6h_hdr_format = true;
};

struct TextureId final {
    std::uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct TextureCreateInfo final {
    TextureId texture_id{};
    VkImageCreateFlags image_flags = 0U;
    VkImageType image_type = VK_IMAGE_TYPE_2D;
    VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent3D extent{1U, 1U, 1U};
    std::uint32_t mip_levels = 1U;
    std::uint32_t array_layers = 1U;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    bool force_recreate = false;
    bool retain_cpu_upload_data = false;
};

struct TextureSubresourceUploadInfo final {
    const void* pixels = nullptr;
    VkDeviceSize size_bytes = 0U;
    std::uint32_t mip_level = 0U;
    std::uint32_t base_array_layer = 0U;
    std::uint32_t layer_count = 1U;
    std::uint32_t buffer_row_length = 0U;
    std::uint32_t buffer_image_height = 0U;
    VkOffset3D image_offset{0, 0, 0};
    VkExtent3D image_extent{1U, 1U, 1U};
};

struct TextureUploadInfo final {
    TextureCreateInfo create{};
    const TextureSubresourceUploadInfo* subresources = nullptr;
    std::uint32_t subresource_count = 0U;
};

struct TextureHostStats final {
    std::uint32_t texture_count = 0U;
    std::uint32_t uploaded_texture_count = 0U;
    std::uint32_t updated_texture_count = 0U;
    std::uint32_t removed_texture_count = 0U;
    std::uint32_t retired_texture_count = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t revision = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

struct TextureHostBindlessConfig final {
    render::DescriptorHost* descriptor_host = nullptr;
    render::BindlessTableId image_table{};
    render::BindlessSlot default_sampler_slot{};
    std::uint64_t bindless_revision = 0U;

    [[nodiscard]] bool Enabled() const noexcept {
        return descriptor_host != nullptr && image_table.IsValid();
    }

    [[nodiscard]] bool SameBinding(const TextureHostBindlessConfig& rhs_) const noexcept {
        return descriptor_host == rhs_.descriptor_host &&
               image_table.value == rhs_.image_table.value &&
               default_sampler_slot.index == rhs_.default_sampler_slot.index &&
               default_sampler_slot.generation == rhs_.default_sampler_slot.generation &&
               bindless_revision == rhs_.bindless_revision;
    }
};

class TextureHost final {
public:
    struct CpuFloatLayerSnapshot final {
        std::uint32_t array_layer = 0U;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::uint32_t row_pitch_pixels = 0U;
        TextureMcVector<ecs::Float4> pixels{};
    };

    struct CpuFloatBaseLevelSnapshot final {
        bool valid = false;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        TextureMcVector<CpuFloatLayerSnapshot> layers{};
    };

    struct TextureRecord final {
        struct TextureBindlessState final {
            render::BindlessSlot image_slot{};
            std::uint32_t image_revision_written = 0U;
            std::uint64_t retire_value = 0U;
        };

        TextureId texture_id{};
        VkImageCreateFlags image_flags = 0U;
        VkImageType image_type = VK_IMAGE_TYPE_2D;
        VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        VkExtent3D extent{};
        std::uint32_t mip_levels = 1U;
        std::uint32_t array_layers = 1U;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0U;
        VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        resource::ImageResource resource{};
        std::uint32_t revision = 0U;
        bool retain_cpu_upload_data = false;
        CpuFloatBaseLevelSnapshot cpu_base_level_snapshot{};
        TextureBindlessState bindless{};
    };

    TextureHost() = default;
    ~TextureHost() = default;

    TextureHost(const TextureHost&) = delete;
    TextureHost& operator=(const TextureHost&) = delete;

    TextureHost(TextureHost&&) = delete;
    TextureHost& operator=(TextureHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const TextureHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint64_t completed_submit_value_);

    void ConfigureBindless(const TextureHostBindlessConfig& bindless_config_);

    void UploadTexture(VulkanContext& context_,
                       render::UploadHost& upload_host_,
                       std::uint32_t frame_index_,
                       std::uint64_t last_submitted_value_,
                       std::uint64_t completed_submit_value_,
                       const TextureUploadInfo& upload_info_);

    [[nodiscard]] bool RemoveTexture(VulkanContext& context_,
                                     TextureId texture_id_,
                                     std::uint64_t last_submitted_value_,
                                     std::uint64_t completed_submit_value_);

    [[nodiscard]] const TextureRecord* FindTexture(TextureId texture_id_) const noexcept;
    [[nodiscard]] render::BindlessSlot ResolveBindlessImageSlot(TextureId texture_id_) const noexcept;
    [[nodiscard]] render::BindlessSlot ResolveBindlessSamplerSlot(TextureId texture_id_) const noexcept;
    [[nodiscard]] const CpuFloatBaseLevelSnapshot* FindCpuFloatBaseLevelSnapshot(
        TextureId texture_id_) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const TextureHostStats& Stats() const noexcept;
    [[nodiscard]] const TextureHostBindlessConfig& BindlessConfig() const noexcept;

    [[nodiscard]] static bool SupportsSampledFormat(VulkanContext& context_,
                                                    VkFormat format_) noexcept;
    [[nodiscard]] static bool SupportsHdrEnvironmentFormat(VulkanContext& context_,
                                                           VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveHdrEnvironmentFormat(VulkanContext& context_,
                                                              bool prefer_bc6h_) noexcept;

private:
    [[nodiscard]] std::size_t LowerBoundTextureIndex(TextureId texture_id_) const noexcept;
    void RetireTexture(TextureRecord& record_,
                       std::uint64_t retire_value_);
    void CollectRetiredTextures(VulkanContext& context_,
                                std::uint64_t completed_submit_value_);
    void DestroyRetiredTextures(VulkanContext& context_) noexcept;
    void InvalidateBindlessRecords(const TextureHostBindlessConfig& bindless_config_);
    void SyncBindlessRecords();
    [[nodiscard]] std::uint64_t ComputeBindlessRetireValue() const noexcept;
    [[nodiscard]] static resource::ImageResource CreateImageResource(VulkanContext& context_,
                                                                     resource::GpuMemoryHost& gpu_memory_host_,
                                                                     const TextureHostCreateInfo& create_info_,
                                                                     const TextureCreateInfo& create_info_desc_);
    static void RecordImageBarrier(render::UploadHost& upload_host_,
                                   std::uint32_t frame_index_,
                                   VkImage image_,
                                   VkImageLayout old_layout_,
                                   VkImageLayout new_layout_,
                                   VkImageAspectFlags aspect_mask_,
                                   std::uint32_t base_mip_level_,
                                   std::uint32_t level_count_,
                                   std::uint32_t base_array_layer_,
                                   std::uint32_t layer_count_,
                                   VkPipelineStageFlags2 src_stage_mask_,
                                   VkAccessFlags2 src_access_mask_,
                                   VkPipelineStageFlags2 dst_stage_mask_,
                                   VkAccessFlags2 dst_access_mask_);

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    TextureHostBindlessConfig bindless_config{};
    TextureHostCreateInfo create_info_cache{};
    TextureMcVector<TextureRecord> textures{};
    render::RetireBus<resource::ImageResource> retired_textures{};
    TextureHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::asset

