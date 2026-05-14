#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
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

namespace vr::surface {

template<typename T>
using SurfaceImageMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfaceImageHostCreateInfo final {
    std::uint32_t reserve_image_count = 128U;
    std::uint32_t reserve_retired_image_count = 128U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

struct SurfaceImageUploadInfo final {
    std::uint32_t image_id = 0U;
    const void* pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    std::uint32_t bytes_per_pixel = 4U;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    bool force_recreate = false;
};

struct SurfaceImageHostStats final {
    std::uint32_t image_count = 0U;
    std::uint32_t uploaded_image_count = 0U;
    std::uint32_t updated_image_count = 0U;
    std::uint32_t removed_image_count = 0U;
    std::uint32_t retired_image_count = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t revision = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

struct SurfaceImageHostBindlessConfig final {
    render::DescriptorHost* descriptor_host = nullptr;
    render::BindlessTableId image_table{};
    std::uint64_t bindless_revision = 0U;

    [[nodiscard]] bool Enabled() const noexcept {
        return descriptor_host != nullptr && image_table.IsValid();
    }

    [[nodiscard]] bool SameBinding(const SurfaceImageHostBindlessConfig& rhs_) const noexcept {
        return descriptor_host == rhs_.descriptor_host &&
               image_table.value == rhs_.image_table.value &&
               bindless_revision == rhs_.bindless_revision;
    }
};

class SurfaceImageHost final {
public:
    struct ImageRecord final {
        struct SurfaceImageBindlessState final {
            render::BindlessSlot image_slot{};
            std::uint32_t revision_written = 0U;
            std::uint64_t retire_value = 0U;
        };

        std::uint32_t image_id = 0U;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0U;
        VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        resource::ImageResource resource{};
        std::uint32_t revision = 0U;
        SurfaceImageBindlessState bindless{};
    };

    SurfaceImageHost() = default;
    ~SurfaceImageHost() = default;

    SurfaceImageHost(const SurfaceImageHost&) = delete;
    SurfaceImageHost& operator=(const SurfaceImageHost&) = delete;

    SurfaceImageHost(SurfaceImageHost&&) = delete;
    SurfaceImageHost& operator=(SurfaceImageHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const SurfaceImageHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint64_t completed_submit_value_);

    void ConfigureBindless(const SurfaceImageHostBindlessConfig& bindless_config_);

    void UploadImage(VulkanContext& context_,
                     render::UploadHost& upload_host_,
                     std::uint32_t frame_index_,
                     std::uint64_t last_submitted_value_,
                     std::uint64_t completed_submit_value_,
                     const SurfaceImageUploadInfo& upload_info_);

    [[nodiscard]] bool RemoveImage(VulkanContext& context_,
                                   std::uint32_t image_id_,
                                   std::uint64_t last_submitted_value_,
                                   std::uint64_t completed_submit_value_);

    [[nodiscard]] const ImageRecord* FindImage(std::uint32_t image_id_) const noexcept;
    [[nodiscard]] render::BindlessSlot ResolveBindlessImageSlot(std::uint32_t image_id_) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SurfaceImageHostStats& Stats() const noexcept;
    [[nodiscard]] const SurfaceImageHostBindlessConfig& BindlessConfig() const noexcept;

private:
    [[nodiscard]] std::size_t LowerBoundImageIndex(std::uint32_t image_id_) const noexcept;
    void RetireImage(ImageRecord& record_,
                     std::uint64_t retire_value_);
    void CollectRetiredImages(VulkanContext& context_,
                              std::uint64_t completed_submit_value_);
    void DestroyRetiredImages(VulkanContext& context_) noexcept;
    void InvalidateBindlessRecords(const SurfaceImageHostBindlessConfig& bindless_config_);
    void SyncBindlessRecords();
    [[nodiscard]] std::uint64_t ComputeBindlessRetireValue() const noexcept;

    [[nodiscard]] static resource::ImageResource CreateImageResource(
        VulkanContext& context_,
        resource::GpuMemoryHost& gpu_memory_host_,
        const SurfaceImageHostCreateInfo& create_info_,
        const SurfaceImageUploadInfo& upload_info_);

    static void RecordImageBarrier(render::UploadHost& upload_host_,
                                   std::uint32_t frame_index_,
                                   VkImage image_,
                                   VkImageLayout old_layout_,
                                   VkImageLayout new_layout_,
                                   VkImageAspectFlags aspect_mask_,
                                   VkPipelineStageFlags2 src_stage_mask_,
                                   VkAccessFlags2 src_access_mask_,
                                   VkPipelineStageFlags2 dst_stage_mask_,
                                   VkAccessFlags2 dst_access_mask_);

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    SurfaceImageHostBindlessConfig bindless_config{};
    SurfaceImageHostCreateInfo create_info_cache{};
    SurfaceImageMcVector<ImageRecord> images{};
    render::RetireBus<resource::ImageResource> retired_images{};
    SurfaceImageHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::surface

