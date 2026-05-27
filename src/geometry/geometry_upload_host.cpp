#include "vr/geometry/geometry_upload_host.hpp"

#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vr::geometry {

void GeometryUploadHost::Initialize(VulkanContext& context_,
                                    resource::GpuMemoryHost& gpu_memory_host_,
                                    const GeometryUploadHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    if (create_info_.frames_in_flight == 0U) {
        throw std::invalid_argument("GeometryUploadHost::Initialize frames_in_flight must be > 0");
    }

    gpu_memory_host = &gpu_memory_host_;
    create_info_cache = create_info_;

    frames.clear();
    frames.resize(create_info_cache.frames_in_flight);
    retired_buffers.Clear();
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = true;
}

void GeometryUploadHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    for (auto& frame : frames) {
        DestroyStreamBuffer(context_, frame.primitives_2d);
        DestroyStreamBuffer(context_, frame.instances_3d);
        DestroyStreamBuffer(context_, frame.temporal_motion_instances_3d);
    }
    frames.clear();
    DestroyRetiredBuffers(context_);

    gpu_memory_host = nullptr;
    create_info_cache = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    stats = {};
    initialized = false;
}

void GeometryUploadHost::BeginFrame(VulkanContext& context_,
                                    std::uint32_t frame_index_,
                                    std::uint64_t last_submitted_value_,
                                    std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("GeometryUploadHost::BeginFrame called before Initialize");
    }
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredBuffers(context_, completed_submit_value_seen);

    FrameState& frame = FrameAt(frame_index_);
    if (frame.primitives_2d.buffer.buffer != VK_NULL_HANDLE &&
        frame.primitives_2d.capacity_bytes == 0U) {
        DestroyStreamBuffer(context_, frame.primitives_2d);
    }
    if (frame.instances_3d.buffer.buffer != VK_NULL_HANDLE &&
        frame.instances_3d.capacity_bytes == 0U) {
        DestroyStreamBuffer(context_, frame.instances_3d);
    }
    if (frame.temporal_motion_instances_3d.buffer.buffer != VK_NULL_HANDLE &&
        frame.temporal_motion_instances_3d.capacity_bytes == 0U) {
        DestroyStreamBuffer(context_, frame.temporal_motion_instances_3d);
    }
}

GeometryUploadRange GeometryUploadHost::Upload2DPrimitives(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::Geometry2DPathPrimitive* primitives_,
    std::uint32_t primitive_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(primitive_count_) * sizeof(ecs::Geometry2DPathPrimitive);

    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.primitives_2d,
                          primitives_,
                          size_bytes,
                          primitive_count_,
                          revision_,
                          create_info_cache.initial_2d_primitive_buffer_bytes);
}

GeometryUploadRange GeometryUploadHost::Upload3DInstances(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::Geometry3DGpuInstance* instances_,
    std::uint32_t instance_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(instance_count_) * sizeof(ecs::Geometry3DGpuInstance);

    return UploadToStream(context_,
                          upload_host_,
                          frame_index_,
                          frame.instances_3d,
                          instances_,
                          size_bytes,
                          instance_count_,
                          revision_,
                          create_info_cache.initial_3d_instance_buffer_bytes);
}

GeometryUploadRange GeometryUploadHost::Upload3DTemporalMotionInstances(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const Geometry3DTemporalMotionInstance* instances_,
    std::uint32_t instance_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(instance_count_) *
        sizeof(Geometry3DTemporalMotionInstance);

    return UploadToStream(
        context_,
        upload_host_,
        frame_index_,
        frame.temporal_motion_instances_3d,
        instances_,
        size_bytes,
        instance_count_,
        revision_,
        create_info_cache.initial_3d_temporal_motion_buffer_bytes);
}

bool GeometryUploadHost::IsInitialized() const noexcept {
    return initialized;
}

std::uint32_t GeometryUploadHost::FramesInFlight() const noexcept {
    return static_cast<std::uint32_t>(frames.size());
}

const GeometryUploadHostStats& GeometryUploadHost::Stats() const noexcept {
    return stats;
}

VkDeviceSize GeometryUploadHost::NextPow2(VkDeviceSize value_) noexcept {
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

void GeometryUploadHost::DestroyStreamBuffer(VulkanContext& context_,
                                             StreamBuffer& stream_) {
    resource::BufferHost::DestroyBuffer(context_, stream_.buffer);
    stream_.capacity_bytes = 0U;
    stream_.uploaded_size_bytes = 0U;
    stream_.element_count = 0U;
    stream_.uploaded_revision = 0U;
}

void GeometryUploadHost::RetireStreamBuffer(StreamBuffer& stream_,
                                            std::uint64_t retire_value_) {
    if (stream_.buffer.buffer == VK_NULL_HANDLE) {
        stream_.capacity_bytes = 0U;
        stream_.uploaded_size_bytes = 0U;
        stream_.element_count = 0U;
        stream_.uploaded_revision = 0U;
        return;
    }

    retired_buffers.Retire(std::move(stream_.buffer), retire_value_);

    stream_.buffer = {};
    stream_.capacity_bytes = 0U;
    stream_.uploaded_size_bytes = 0U;
    stream_.element_count = 0U;
    stream_.uploaded_revision = 0U;
}

void GeometryUploadHost::CollectRetiredBuffers(VulkanContext& context_,
                                               std::uint64_t completed_submit_value_) {
    if (retired_buffers.Empty()) {
        return;
    }

    if (context_.Device() == VK_NULL_HANDLE) {
        return;
    }
    (void)retired_buffers.Collect(completed_submit_value_,
                                  [&](resource::BufferResource& buffer_) {
                                      resource::BufferHost::DestroyBuffer(context_, buffer_);
                                  });
}

void GeometryUploadHost::DestroyRetiredBuffers(VulkanContext& context_) noexcept {
    (void)retired_buffers.Flush([&](resource::BufferResource& buffer_) {
        resource::BufferHost::DestroyBuffer(context_, buffer_);
    });
}

std::uint64_t GeometryUploadHost::ComputeRetireValue() const noexcept {
    const std::uint64_t max_seen = std::max(last_submitted_value_seen, completed_submit_value_seen);
    if (max_seen == std::numeric_limits<std::uint64_t>::max()) {
        return max_seen;
    }
    return max_seen + 1U;
}

void GeometryUploadHost::EnsureStreamCapacity(VulkanContext& context_,
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
        throw std::runtime_error("GeometryUploadHost stream capacity exceeded while growth is disabled");
    }

    const VkDeviceSize target_capacity = NextPow2(std::max(required_bytes_, minimum_capacity_bytes_));
    RetireStreamBuffer(stream_, ComputeRetireValue());

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = target_capacity;
    buffer_create_info.usage = usage_;
    buffer_create_info.memory_properties = create_info_cache.memory_properties;
    const auto& families = context_.QueueFamilies();
    if (families.graphics.has_value() &&
        families.transfer.has_value() &&
        families.graphics.value() != families.transfer.value()) {
        buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        buffer_create_info.queue_family_indices.push_back(families.graphics.value());
        buffer_create_info.queue_family_indices.push_back(families.transfer.value());
    }
    stream_.buffer = resource::BufferHost::CreateBuffer(context_,
                                                        buffer_create_info,
                                                        *gpu_memory_host);
    stream_.capacity_bytes = target_capacity;
    ++stats.resized_buffer_count;
}

GeometryUploadRange GeometryUploadHost::UploadToStream(VulkanContext& context_,
                                                       render::UploadHost& upload_host_,
                                                       std::uint32_t frame_index_,
                                                       StreamBuffer& stream_,
                                                       const void* src_data_,
                                                       VkDeviceSize size_bytes_,
                                                       std::uint32_t element_count_,
                                                       std::uint64_t revision_,
                                                       VkDeviceSize minimum_capacity_bytes_) {
    GeometryUploadRange range{};
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
        throw std::invalid_argument("GeometryUploadHost::UploadToStream received null src_data for non-empty upload");
    }

    EnsureStreamCapacity(context_,
                         stream_,
                         size_bytes_,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
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
    stats.uploaded_bytes += static_cast<std::uint64_t>(size_bytes_);
    return range;
}

GeometryUploadHost::FrameState& GeometryUploadHost::FrameAt(std::uint32_t frame_index_) {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("GeometryUploadHost frame_index out of range");
    }
    return frames[frame_index_];
}

const GeometryUploadHost::FrameState& GeometryUploadHost::FrameAt(std::uint32_t frame_index_) const {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("GeometryUploadHost frame_index out of range");
    }
    return frames[frame_index_];
}

} // namespace vr::geometry

