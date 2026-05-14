#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/light_culling_system.hpp"
#include "vr/ecs/system/light_gpu_layout.hpp"
#include "vr/ecs/system/shadow_gpu_layout.hpp"
#include "vr/render/retire_bus.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::ecs {
struct LightUploadRange;
struct ShadowUploadRange;
}

namespace vr::light {

template<typename T>
using LightShadowUploadMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct LightShadowUploadHostCreateInfo final {
    std::uint32_t frames_in_flight = 2U;
    VkDeviceSize initial_light_record_buffer_bytes = 256U * 1024U;
    VkDeviceSize initial_cluster_header_buffer_bytes = 256U * 1024U;
    VkDeviceSize initial_cluster_index_buffer_bytes = 1024U * 1024U;
    VkDeviceSize initial_shadow_view_buffer_bytes = 512U * 1024U;
    VkDeviceSize initial_uniform_buffer_bytes = 4096U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool allow_growth = true;
};

struct LightShadowBufferRange final {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0U;
    VkDeviceSize size_bytes = 0U;
    std::uint32_t element_count = 0U;
    std::uint64_t uploaded_revision = 0U;
    bool uploaded = false;
};

struct LightShadowUploadHostStats final {
    std::uint64_t uploaded_bytes = 0U;
    std::uint64_t partial_uploaded_bytes = 0U;
    std::uint32_t upload_count = 0U;
    std::uint32_t partial_upload_count = 0U;
    std::uint32_t full_upload_count = 0U;
    std::uint32_t reuse_hit_count = 0U;
    std::uint32_t resized_buffer_count = 0U;
};

class LightShadowUploadHost final {
public:
    LightShadowUploadHost() = default;
    ~LightShadowUploadHost() = default;

    LightShadowUploadHost(const LightShadowUploadHost&) = delete;
    LightShadowUploadHost& operator=(const LightShadowUploadHost&) = delete;

    LightShadowUploadHost(LightShadowUploadHost&&) = delete;
    LightShadowUploadHost& operator=(LightShadowUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const LightShadowUploadHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    void BeginFrame(VulkanContext& context_,
                    std::uint32_t frame_index_,
                    std::uint64_t last_submitted_value_ = 0U,
                    std::uint64_t completed_submit_value_ = 0U);

    [[nodiscard]] LightShadowBufferRange UploadLightRecords(VulkanContext& context_,
                                                            render::UploadHost& upload_host_,
                                                            std::uint32_t frame_index_,
                                                            const ecs::LightGpuRecord2D* records_,
                                                            std::uint32_t record_count_,
                                                            std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadLightRecords(VulkanContext& context_,
                                                            render::UploadHost& upload_host_,
                                                            std::uint32_t frame_index_,
                                                            const ecs::LightGpuRecord3D* records_,
                                                            std::uint32_t record_count_,
                                                            std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadLightRecordsRanges(VulkanContext& context_,
                                                                  render::UploadHost& upload_host_,
                                                                  std::uint32_t frame_index_,
                                                                  const ecs::LightGpuRecord2D* records_,
                                                                  std::uint32_t record_count_,
                                                                  const ecs::LightUploadRange* upload_ranges_,
                                                                  std::uint32_t upload_range_count_,
                                                                  std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadLightRecordsRanges(VulkanContext& context_,
                                                                  render::UploadHost& upload_host_,
                                                                  std::uint32_t frame_index_,
                                                                  const ecs::LightGpuRecord3D* records_,
                                                                  std::uint32_t record_count_,
                                                                  const ecs::LightUploadRange* upload_ranges_,
                                                                  std::uint32_t upload_range_count_,
                                                                  std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadClusterHeaders(VulkanContext& context_,
                                                              render::UploadHost& upload_host_,
                                                              std::uint32_t frame_index_,
                                                              const ecs::LightClusterHeader* headers_,
                                                              std::uint32_t header_count_,
                                                              std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadClusterIndices(VulkanContext& context_,
                                                              render::UploadHost& upload_host_,
                                                              std::uint32_t frame_index_,
                                                              const std::uint32_t* indices_,
                                                              std::uint32_t index_count_,
                                                              std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadShadowViews(VulkanContext& context_,
                                                           render::UploadHost& upload_host_,
                                                           std::uint32_t frame_index_,
                                                           const ecs::ShadowViewGpuRecord* views_,
                                                           std::uint32_t view_count_,
                                                           std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadShadowViewsRanges(VulkanContext& context_,
                                                                 render::UploadHost& upload_host_,
                                                                 std::uint32_t frame_index_,
                                                                 const ecs::ShadowViewGpuRecord* views_,
                                                                 std::uint32_t view_count_,
                                                                 const ecs::ShadowUploadRange* upload_ranges_,
                                                                 std::uint32_t upload_range_count_,
                                                                 std::uint64_t revision_);

    [[nodiscard]] LightShadowBufferRange UploadLightingUniform(VulkanContext& context_,
                                                               render::UploadHost& upload_host_,
                                                               std::uint32_t frame_index_,
                                                               const void* data_,
                                                               VkDeviceSize data_size_,
                                                               std::uint64_t revision_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] const LightShadowUploadHostStats& Stats() const noexcept;

private:
    struct StreamBuffer final {
        resource::BufferResource buffer{};
        VkDeviceSize capacity_bytes = 0U;
        VkDeviceSize uploaded_size_bytes = 0U;
        std::uint32_t element_count = 0U;
        std::uint64_t uploaded_revision = 0U;
    };

    struct FrameState final {
        StreamBuffer light_records{};
        StreamBuffer cluster_headers{};
        StreamBuffer cluster_indices{};
        StreamBuffer shadow_views{};
        StreamBuffer lighting_uniform{};
    };

    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    static void DestroyStreamBuffer(VulkanContext& context_,
                                    StreamBuffer& stream_);
    void RetireStreamBuffer(StreamBuffer& stream_,
                            std::uint64_t retire_value_);
    void CollectRetiredBuffers(VulkanContext& context_,
                               std::uint64_t completed_submit_value_);
    void DestroyRetiredBuffers(VulkanContext& context_) noexcept;
    [[nodiscard]] std::uint64_t ComputeRetireValue() const noexcept;
    void EnsureStreamCapacity(VulkanContext& context_,
                              StreamBuffer& stream_,
                              VkDeviceSize required_bytes_,
                              VkBufferUsageFlags usage_,
                              VkDeviceSize minimum_capacity_bytes_);

    [[nodiscard]] LightShadowBufferRange UploadToStream(VulkanContext& context_,
                                                        render::UploadHost& upload_host_,
                                                        std::uint32_t frame_index_,
                                                        StreamBuffer& stream_,
                                                        const void* src_data_,
                                                        VkDeviceSize size_bytes_,
                                                        std::uint32_t element_count_,
                                                        std::uint64_t revision_,
                                                        VkDeviceSize minimum_capacity_bytes_,
                                                        VkBufferUsageFlags usage_flags_,
                                                        VkPipelineStageFlags2 dst_stage_mask_,
                                                        VkAccessFlags2 dst_access_mask_);

    template<typename UploadRangeType>
    [[nodiscard]] LightShadowBufferRange UploadToStreamRanges(VulkanContext& context_,
                                                              render::UploadHost& upload_host_,
                                                              std::uint32_t frame_index_,
                                                              StreamBuffer& stream_,
                                                              const void* src_data_,
                                                              VkDeviceSize element_size_bytes_,
                                                              std::uint32_t element_count_,
                                                              const UploadRangeType* upload_ranges_,
                                                              std::uint32_t upload_range_count_,
                                                              std::uint64_t revision_,
                                                              VkDeviceSize minimum_capacity_bytes_,
                                                              VkBufferUsageFlags usage_flags_,
                                                              VkPipelineStageFlags2 dst_stage_mask_,
                                                              VkAccessFlags2 dst_access_mask_);

    [[nodiscard]] FrameState& FrameAt(std::uint32_t frame_index_);
    [[nodiscard]] const FrameState& FrameAt(std::uint32_t frame_index_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    LightShadowUploadHostCreateInfo create_info_cache{};
    LightShadowUploadMcVector<FrameState> frames{};
    render::RetireBus<resource::BufferResource> retired_buffers{};
    LightShadowUploadHostStats stats{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool initialized = false;
};

} // namespace vr::light

