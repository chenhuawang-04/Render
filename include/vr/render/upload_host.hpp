#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "Center/Memory/Vulkan/Types.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <limits>
#include <vulkan/vulkan.h>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::render {

template<typename T>
using UploadMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct UploadHostCreateInfo {
    uint32_t frames_in_flight = 2U;
    VkDeviceSize staging_buffer_size = 64U * 1024U * 1024U;
    VkBufferUsageFlags staging_buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags staging_memory_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkCommandPoolCreateFlags command_pool_flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    bool prefer_transfer_queue = true;
    bool fallback_to_graphics_queue = true;
    bool allow_staging_page_growth = true;
    uint32_t max_staging_page_count = 4U;
};

struct UploadSubmitInfo {
    VkSemaphore wait_semaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSemaphore signal_semaphore = VK_NULL_HANDLE;
};

struct UploadEndFrameResult {
    VkResult result = VK_SUCCESS;
    bool submitted = false;
};

struct UploadAllocation {
    void* mapped_data = nullptr;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceSize staging_offset = 0U;
    VkDeviceSize size = 0U;
};

struct UploadFrameStats {
    VkDeviceSize used_bytes = 0U;
    VkDeviceSize capacity_bytes = 0U;
    uint32_t staging_page_count = 0U;
    uint32_t staging_page_growth_count = 0U;
    uint32_t buffer_copy_count = 0U;
    uint32_t image_copy_count = 0U;
    uint32_t barrier_count = 0U;
};

class UploadHost final {
public:
    UploadHost() = default;
    ~UploadHost() = default;

    UploadHost(const UploadHost&) = delete;
    UploadHost& operator=(const UploadHost&) = delete;

    UploadHost(UploadHost&&) = delete;
    UploadHost& operator=(UploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const UploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_, uint32_t frame_index_);

    [[nodiscard]] UploadAllocation Allocate(uint32_t frame_index_,
                                            VkDeviceSize size_,
                                            VkDeviceSize alignment_ = 16U);

    [[nodiscard]] UploadAllocation Write(uint32_t frame_index_,
                                         const void* src_data_,
                                         VkDeviceSize size_,
                                         VkDeviceSize alignment_ = 16U);

    void RecordCopyBuffer(uint32_t frame_index_,
                          VkBuffer dst_buffer_,
                          VkDeviceSize dst_offset_,
                          const UploadAllocation& allocation_);

    void StageAndRecordCopyBuffer(uint32_t frame_index_,
                                  VkBuffer dst_buffer_,
                                  VkDeviceSize dst_offset_,
                                  const void* src_data_,
                                  VkDeviceSize size_,
                                  VkDeviceSize alignment_ = 16U);

    void RecordCopyImage(uint32_t frame_index_,
                         VkImage dst_image_,
                         VkImageLayout dst_layout_,
                         const VkBufferImageCopy& copy_region_,
                         const UploadAllocation& allocation_);

    void StageAndRecordCopyImage(uint32_t frame_index_,
                                 VkImage dst_image_,
                                 VkImageLayout dst_layout_,
                                 const VkBufferImageCopy& copy_region_,
                                 const void* src_data_,
                                 VkDeviceSize size_,
                                 VkDeviceSize alignment_ = 16U);

    void RecordMemoryBarrier2(uint32_t frame_index_, const VkMemoryBarrier2& barrier_);
    void RecordBufferBarrier2(uint32_t frame_index_, const VkBufferMemoryBarrier2& barrier_);
    void RecordImageBarrier2(uint32_t frame_index_, const VkImageMemoryBarrier2& barrier_);

    [[nodiscard]] UploadEndFrameResult EndFrameAndSubmit(VulkanContext& context_,
                                                         uint32_t frame_index_,
                                                         const UploadSubmitInfo& submit_info_ = {});

    void WaitFrame(VulkanContext& context_,
                   uint32_t frame_index_,
                   uint64_t timeout_ns_ = std::numeric_limits<uint64_t>::max());

    void WaitIdle(VulkanContext& context_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] uint32_t QueueFamilyIndex() const noexcept;
    [[nodiscard]] uint32_t GraphicsQueueFamilyIndex() const noexcept;
    [[nodiscard]] VkQueue SubmitQueue() const noexcept;
    [[nodiscard]] VkQueueFlags SubmitQueueFlags() const noexcept;
    [[nodiscard]] bool UsesCrossQueueSubmit() const noexcept;
    [[nodiscard]] std::uint64_t LastSubmittedValue() const noexcept;
    [[nodiscard]] std::uint64_t CompletedSubmitValue() const noexcept;
    [[nodiscard]] std::uint64_t NextSignalValue() const noexcept;
    [[nodiscard]] const UploadFrameStats& FrameStats(uint32_t frame_index_) const;
    [[nodiscard]] VkDeviceSize CapacityBytes() const noexcept;

private:
    struct UploadStagingPage {
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        Center::Memory::Vulkan::Slice allocation_slice{};
        void* mapped_ptr = nullptr;
        VkDeviceSize capacity_bytes = 0U;
        VkDeviceSize write_head = 0U;
    };

    struct UploadFrameSlot {
        UploadMcVector<UploadStagingPage> pages{};
        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkFence in_flight_fence = VK_NULL_HANDLE;
        std::uint64_t submit_value = 0U;
        bool recording_active = false;
        bool recorded_work = false;
        UploadFrameStats stats{};
    };

    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);
    static VkDeviceSize AlignUp(VkDeviceSize value_, VkDeviceSize alignment_);
    static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    static VkPipelineStageFlags2 PromoteLegacyStageMask(VkPipelineStageFlags stage_mask_) noexcept;

    UploadFrameSlot& SlotAt(uint32_t frame_index_);
    const UploadFrameSlot& SlotAt(uint32_t frame_index_) const;

    void CreateSlotResources(VulkanContext& context_, UploadFrameSlot& slot_);
    void DestroySlotResources(VulkanContext& context_, UploadFrameSlot& slot_);
    void CreateStagingPage(VulkanContext& context_,
                           UploadStagingPage& page_,
                           VkDeviceSize capacity_bytes_);
    void DestroyStagingPage(VulkanContext& context_, UploadStagingPage& page_);
    [[nodiscard]] UploadStagingPage& AcquireWritablePage(UploadFrameSlot& slot_,
                                                         VkDeviceSize size_,
                                                         VkDeviceSize alignment_);
    [[nodiscard]] VkDeviceSize SlotCapacityBytes(const UploadFrameSlot& slot_) const noexcept;
    void FlushAllocationIfNeeded(VulkanContext& context_,
                                 const UploadStagingPage& page_,
                                 VkDeviceSize offset_,
                                 VkDeviceSize size_) const;
    [[nodiscard]] VkPipelineStageFlags2 SanitizeStageMaskForSubmitQueue(
        VkPipelineStageFlags2 stage_mask_) const noexcept;
    [[nodiscard]] VkPipelineStageFlags SanitizeLegacyStageMaskForSubmitQueue(
        VkPipelineStageFlags stage_mask_) const noexcept;
    static bool HasUnsupportedStageForQueue(VkPipelineStageFlags2 stage_mask_,
                                            VkQueueFlags queue_flags_) noexcept;
    static VkAccessFlags2 SanitizeAccessMaskForStage(VkPipelineStageFlags2 stage_mask_,
                                                     VkAccessFlags2 access_mask_) noexcept;

private:
    UploadMcVector<UploadFrameSlot> slots{};
    UploadHostCreateInfo create_info_cache{};
    VulkanContext* context = nullptr;
    resource::GpuMemoryHost* memory_host = nullptr;
    VkQueue submit_queue = VK_NULL_HANDLE;
    uint32_t queue_family_index = 0U;
    uint32_t graphics_queue_family_index = 0U;
    VkQueueFlags submit_queue_flags = 0U;
    bool cross_queue_submit = false;
    bool synchronization2_enabled = false;
    std::uint64_t next_submit_value = 1U;
    std::uint64_t last_submitted_value = 0U;
    std::uint64_t completed_submit_value = 0U;
    bool initialized = false;
};

} // namespace vr::render
