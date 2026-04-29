#include "vr/light/light_shadow_upload_host.hpp"

#include "vr/ecs/system/light_runtime_system.hpp"
#include "vr/ecs/system/shadow_runtime_system.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vr::light {

void LightShadowUploadHost::Initialize(VulkanContext& context_,
                                       resource::GpuMemoryHost& gpu_memory_host_,
                                       const LightShadowUploadHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }
    if (create_info_.frames_in_flight == 0U) {
        throw std::invalid_argument("LightShadowUploadHost::Initialize frames_in_flight must be > 0");
    }
    gpu_memory_host = &gpu_memory_host_;
    create_info_cache = create_info_;
    frames.clear();
    frames.resize(create_info_cache.frames_in_flight);
    stats = {};
    initialized = true;
}

void LightShadowUploadHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    for (auto& frame : frames) {
        DestroyStreamBuffer(context_, frame.light_records);
        DestroyStreamBuffer(context_, frame.cluster_headers);
        DestroyStreamBuffer(context_, frame.cluster_indices);
        DestroyStreamBuffer(context_, frame.shadow_views);
        DestroyStreamBuffer(context_, frame.lighting_uniform);
    }
    frames.clear();

    gpu_memory_host = nullptr;
    create_info_cache = {};
    stats = {};
    initialized = false;
}

void LightShadowUploadHost::BeginFrame(VulkanContext& context_,
                                       std::uint32_t frame_index_) {
    if (!initialized) {
        throw std::runtime_error("LightShadowUploadHost::BeginFrame called before Initialize");
    }
    FrameState& frame = FrameAt(frame_index_);
    auto clear_if_invalid = [&](StreamBuffer& stream_) {
        if (stream_.buffer.buffer != VK_NULL_HANDLE && stream_.capacity_bytes == 0U) {
            DestroyStreamBuffer(context_, stream_);
        }
    };
    clear_if_invalid(frame.light_records);
    clear_if_invalid(frame.cluster_headers);
    clear_if_invalid(frame.cluster_indices);
    clear_if_invalid(frame.shadow_views);
    clear_if_invalid(frame.lighting_uniform);
}

LightShadowBufferRange LightShadowUploadHost::UploadLightRecords(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::LightGpuRecord2D* records_,
    std::uint32_t record_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(record_count_) * sizeof(ecs::LightGpuRecord2D);
    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.light_records,
                          records_,
                          size_bytes,
                          record_count_,
                          revision_,
                          create_info_cache.initial_light_record_buffer_bytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadLightRecords(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::LightGpuRecord3D* records_,
    std::uint32_t record_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(record_count_) * sizeof(ecs::LightGpuRecord3D);
    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.light_records,
                          records_,
                          size_bytes,
                          record_count_,
                          revision_,
                          create_info_cache.initial_light_record_buffer_bytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadLightRecordsRanges(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::LightGpuRecord2D* records_,
    std::uint32_t record_count_,
    const ecs::LightUploadRange* upload_ranges_,
    std::uint32_t upload_range_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    return UploadToStreamRanges(context_,
                                upload_host_,
                                frame_index_,
                                frame.light_records,
                                records_,
                                static_cast<VkDeviceSize>(sizeof(ecs::LightGpuRecord2D)),
                                record_count_,
                                upload_ranges_,
                                upload_range_count_,
                                revision_,
                                create_info_cache.initial_light_record_buffer_bytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadLightRecordsRanges(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::LightGpuRecord3D* records_,
    std::uint32_t record_count_,
    const ecs::LightUploadRange* upload_ranges_,
    std::uint32_t upload_range_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    return UploadToStreamRanges(context_,
                                upload_host_,
                                frame_index_,
                                frame.light_records,
                                records_,
                                static_cast<VkDeviceSize>(sizeof(ecs::LightGpuRecord3D)),
                                record_count_,
                                upload_ranges_,
                                upload_range_count_,
                                revision_,
                                create_info_cache.initial_light_record_buffer_bytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadClusterHeaders(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::LightClusterHeader* headers_,
    std::uint32_t header_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(header_count_) * sizeof(ecs::LightClusterHeader);
    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.cluster_headers,
                          headers_,
                          size_bytes,
                          header_count_,
                          revision_,
                          create_info_cache.initial_cluster_header_buffer_bytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadClusterIndices(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const std::uint32_t* indices_,
    std::uint32_t index_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(index_count_) * sizeof(std::uint32_t);
    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.cluster_indices,
                          indices_,
                          size_bytes,
                          index_count_,
                          revision_,
                          create_info_cache.initial_cluster_index_buffer_bytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadShadowViews(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::ShadowViewGpuRecord* views_,
    std::uint32_t view_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(view_count_) * sizeof(ecs::ShadowViewGpuRecord);
    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.shadow_views,
                          views_,
                          size_bytes,
                          view_count_,
                          revision_,
                          create_info_cache.initial_shadow_view_buffer_bytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadShadowViewsRanges(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::ShadowViewGpuRecord* views_,
    std::uint32_t view_count_,
    const ecs::ShadowUploadRange* upload_ranges_,
    std::uint32_t upload_range_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    return UploadToStreamRanges(context_,
                                upload_host_,
                                frame_index_,
                                frame.shadow_views,
                                views_,
                                static_cast<VkDeviceSize>(sizeof(ecs::ShadowViewGpuRecord)),
                                view_count_,
                                upload_ranges_,
                                upload_range_count_,
                                revision_,
                                create_info_cache.initial_shadow_view_buffer_bytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_READ_BIT);
}

LightShadowBufferRange LightShadowUploadHost::UploadLightingUniform(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const void* data_,
    VkDeviceSize data_size_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.lighting_uniform,
                          data_,
                          data_size_,
                          1U,
                          revision_,
                          create_info_cache.initial_uniform_buffer_bytes,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_UNIFORM_READ_BIT);
}

bool LightShadowUploadHost::IsInitialized() const noexcept {
    return initialized;
}

std::uint32_t LightShadowUploadHost::FramesInFlight() const noexcept {
    return static_cast<std::uint32_t>(frames.size());
}

const LightShadowUploadHostStats& LightShadowUploadHost::Stats() const noexcept {
    return stats;
}

VkDeviceSize LightShadowUploadHost::NextPow2(VkDeviceSize value_) noexcept {
    if (value_ <= 1U) {
        return 1U;
    }

    VkDeviceSize result = 1U;
    while (result < value_) {
        if (result > (std::numeric_limits<VkDeviceSize>::max() >> 1U)) {
            return std::numeric_limits<VkDeviceSize>::max();
        }
        result <<= 1U;
    }
    return result;
}

void LightShadowUploadHost::DestroyStreamBuffer(VulkanContext& context_,
                                                StreamBuffer& stream_) {
    resource::BufferHost::DestroyBuffer(context_, stream_.buffer);
    stream_.capacity_bytes = 0U;
    stream_.uploaded_size_bytes = 0U;
    stream_.element_count = 0U;
    stream_.uploaded_revision = 0U;
}

void LightShadowUploadHost::EnsureStreamCapacity(VulkanContext& context_,
                                                 StreamBuffer& stream_,
                                                 VkDeviceSize required_bytes_,
                                                 VkBufferUsageFlags usage_,
                                                 VkDeviceSize minimum_capacity_bytes_) {
    if (required_bytes_ == 0U) {
        return;
    }
    if (stream_.capacity_bytes >= required_bytes_ && stream_.buffer.buffer != VK_NULL_HANDLE) {
        return;
    }
    if (!create_info_cache.allow_growth &&
        stream_.capacity_bytes > 0U &&
        required_bytes_ > stream_.capacity_bytes) {
        throw std::runtime_error("LightShadowUploadHost stream capacity exceeded while growth is disabled");
    }

    const VkDeviceSize target_capacity = NextPow2(std::max(required_bytes_, minimum_capacity_bytes_));
    DestroyStreamBuffer(context_, stream_);

    resource::BufferCreateInfo buffer_create{};
    buffer_create.size = target_capacity;
    buffer_create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage_;
    buffer_create.memory_properties = create_info_cache.memory_properties;
    stream_.buffer = resource::BufferHost::CreateBuffer(context_, buffer_create, *gpu_memory_host);
    stream_.capacity_bytes = target_capacity;
    ++stats.resized_buffer_count;
}

LightShadowBufferRange LightShadowUploadHost::UploadToStream(
    VulkanContext& context_,
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
    VkAccessFlags2 dst_access_mask_) {
    LightShadowBufferRange range{};
    range.element_count = element_count_;
    range.size_bytes = size_bytes_;
    range.uploaded_revision = revision_;

    if (size_bytes_ == 0U || element_count_ == 0U) {
        stream_.uploaded_size_bytes = 0U;
        stream_.element_count = 0U;
        stream_.uploaded_revision = revision_;
        return range;
    }
    if (src_data_ == nullptr) {
        throw std::invalid_argument("LightShadowUploadHost::UploadToStream null src_data for non-empty upload");
    }

    EnsureStreamCapacity(context_,
                         stream_,
                         size_bytes_,
                         usage_flags_,
                         minimum_capacity_bytes_);

    if (stream_.uploaded_revision == revision_ &&
        stream_.uploaded_size_bytes == size_bytes_ &&
        stream_.buffer.buffer != VK_NULL_HANDLE) {
        range.buffer = stream_.buffer.buffer;
        range.uploaded = false;
        ++stats.reuse_hit_count;
        return range;
    }

    upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                          stream_.buffer.buffer,
                                          0U,
                                          src_data_,
                                          size_bytes_,
                                          16U);

    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = dst_stage_mask_;
        barrier.dstAccessMask = dst_access_mask_;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = stream_.buffer.buffer;
        barrier.offset = 0U;
        barrier.size = size_bytes_;
        upload_host_.RecordBufferBarrier2(frame_index_, barrier);
    }

    stream_.uploaded_revision = revision_;
    stream_.uploaded_size_bytes = size_bytes_;
    stream_.element_count = element_count_;

    range.buffer = stream_.buffer.buffer;
    range.uploaded = true;
    ++stats.upload_count;
    ++stats.full_upload_count;
    stats.uploaded_bytes += static_cast<std::uint64_t>(size_bytes_);
    return range;
}

template<typename UploadRangeType>
LightShadowBufferRange LightShadowUploadHost::UploadToStreamRanges(
    VulkanContext& context_,
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
    VkAccessFlags2 dst_access_mask_) {
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(element_count_) * element_size_bytes_;
    LightShadowBufferRange range{};
    range.element_count = element_count_;
    range.size_bytes = size_bytes;
    range.uploaded_revision = revision_;

    if (size_bytes == 0U || element_count_ == 0U) {
        stream_.uploaded_size_bytes = 0U;
        stream_.element_count = 0U;
        stream_.uploaded_revision = revision_;
        return range;
    }
    if (src_data_ == nullptr) {
        throw std::invalid_argument("LightShadowUploadHost::UploadToStreamRanges null src_data for non-empty upload");
    }

    EnsureStreamCapacity(context_,
                         stream_,
                         size_bytes,
                         usage_flags_,
                         minimum_capacity_bytes_);

    if (stream_.uploaded_revision == revision_ &&
        stream_.uploaded_size_bytes == size_bytes &&
        stream_.buffer.buffer != VK_NULL_HANDLE) {
        range.buffer = stream_.buffer.buffer;
        range.uploaded = false;
        ++stats.reuse_hit_count;
        return range;
    }

    const bool can_use_partial_ranges =
        upload_ranges_ != nullptr &&
        upload_range_count_ > 0U &&
        upload_range_count_ < element_count_;
    bool used_partial_ranges = false;
    VkDeviceSize barrier_begin = size_bytes;
    VkDeviceSize barrier_end = 0U;
    VkDeviceSize uploaded_bytes = 0U;

    if (can_use_partial_ranges) {
        const std::byte* src_bytes = static_cast<const std::byte*>(src_data_);
        for (std::uint32_t i = 0U; i < upload_range_count_; ++i) {
            const std::uint32_t begin_index = upload_ranges_[i].begin_index;
            const std::uint32_t count = upload_ranges_[i].count;
            if (count == 0U || begin_index >= element_count_) {
                continue;
            }
            if (begin_index > (std::numeric_limits<std::uint32_t>::max)() - count ||
                begin_index + count > element_count_) {
                used_partial_ranges = false;
                barrier_begin = size_bytes;
                barrier_end = 0U;
                uploaded_bytes = 0U;
                break;
            }
            const VkDeviceSize dst_offset = static_cast<VkDeviceSize>(begin_index) * element_size_bytes_;
            const VkDeviceSize copy_size = static_cast<VkDeviceSize>(count) * element_size_bytes_;
            upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                                  stream_.buffer.buffer,
                                                  dst_offset,
                                                  src_bytes + dst_offset,
                                                  copy_size,
                                                  16U);
            if (!used_partial_ranges || dst_offset < barrier_begin) {
                barrier_begin = dst_offset;
            }
            const VkDeviceSize copy_end = dst_offset + copy_size;
            if (!used_partial_ranges || copy_end > barrier_end) {
                barrier_end = copy_end;
            }
            uploaded_bytes += copy_size;
            used_partial_ranges = true;
        }
    }

    if (!used_partial_ranges) {
        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              stream_.buffer.buffer,
                                              0U,
                                              src_data_,
                                              size_bytes,
                                              16U);
        barrier_begin = 0U;
        barrier_end = size_bytes;
        uploaded_bytes = size_bytes;
    }

    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = dst_stage_mask_;
        barrier.dstAccessMask = dst_access_mask_;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = stream_.buffer.buffer;
        barrier.offset = barrier_begin;
        barrier.size = barrier_end > barrier_begin ? (barrier_end - barrier_begin) : 0U;
        upload_host_.RecordBufferBarrier2(frame_index_, barrier);
    }

    stream_.uploaded_revision = revision_;
    stream_.uploaded_size_bytes = size_bytes;
    stream_.element_count = element_count_;

    range.buffer = stream_.buffer.buffer;
    range.uploaded = true;
    ++stats.upload_count;
    if (used_partial_ranges) {
        ++stats.partial_upload_count;
        stats.partial_uploaded_bytes += static_cast<std::uint64_t>(uploaded_bytes);
    } else {
        ++stats.full_upload_count;
    }
    stats.uploaded_bytes += static_cast<std::uint64_t>(uploaded_bytes);
    return range;
}

template LightShadowBufferRange LightShadowUploadHost::UploadToStreamRanges<ecs::LightUploadRange>(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    StreamBuffer& stream_,
    const void* src_data_,
    VkDeviceSize element_size_bytes_,
    std::uint32_t element_count_,
    const ecs::LightUploadRange* upload_ranges_,
    std::uint32_t upload_range_count_,
    std::uint64_t revision_,
    VkDeviceSize minimum_capacity_bytes_,
    VkBufferUsageFlags usage_flags_,
    VkPipelineStageFlags2 dst_stage_mask_,
    VkAccessFlags2 dst_access_mask_);

template LightShadowBufferRange LightShadowUploadHost::UploadToStreamRanges<ecs::ShadowUploadRange>(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    StreamBuffer& stream_,
    const void* src_data_,
    VkDeviceSize element_size_bytes_,
    std::uint32_t element_count_,
    const ecs::ShadowUploadRange* upload_ranges_,
    std::uint32_t upload_range_count_,
    std::uint64_t revision_,
    VkDeviceSize minimum_capacity_bytes_,
    VkBufferUsageFlags usage_flags_,
    VkPipelineStageFlags2 dst_stage_mask_,
    VkAccessFlags2 dst_access_mask_);

LightShadowUploadHost::FrameState& LightShadowUploadHost::FrameAt(std::uint32_t frame_index_) {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("LightShadowUploadHost frame_index out of range");
    }
    return frames[frame_index_];
}

const LightShadowUploadHost::FrameState& LightShadowUploadHost::FrameAt(std::uint32_t frame_index_) const {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("LightShadowUploadHost frame_index out of range");
    }
    return frames[frame_index_];
}

} // namespace vr::light
