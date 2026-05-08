#include "vr/particle/particle_upload_host.hpp"

#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vr::particle {

void ParticleUploadHost::Initialize(VulkanContext& context_,
                                    resource::GpuMemoryHost& gpu_memory_host_,
                                    const ParticleUploadHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    if (create_info_.frames_in_flight == 0U) {
        throw std::invalid_argument("ParticleUploadHost::Initialize frames_in_flight must be > 0");
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

void ParticleUploadHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    for (auto& frame : frames) {
        DestroyStreamBuffer(context_, frame.instances_2d);
        DestroyStreamBuffer(context_, frame.instances_3d);
    }
    frames.clear();
    DestroyRetiredBuffers(context_);

    gpu_memory_host = nullptr;
    create_info_cache = {};
    retired_buffers.Clear();
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = false;
}

void ParticleUploadHost::BeginFrame(VulkanContext& context_,
                                    std::uint32_t frame_index_,
                                    std::uint64_t last_submitted_value_,
                                    std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("ParticleUploadHost::BeginFrame called before Initialize");
    }

    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredBuffers(context_, completed_submit_value_seen);

    FrameState& frame = FrameAt(frame_index_);
    if (frame.instances_2d.buffer.buffer != VK_NULL_HANDLE &&
        frame.instances_2d.capacity_bytes == 0U) {
        DestroyStreamBuffer(context_, frame.instances_2d);
    }
    if (frame.instances_3d.buffer.buffer != VK_NULL_HANDLE &&
        frame.instances_3d.capacity_bytes == 0U) {
        DestroyStreamBuffer(context_, frame.instances_3d);
    }
}

ParticleUploadRange ParticleUploadHost::Upload2DInstances(VulkanContext& context_,
                                                          render::UploadHost& upload_host_,
                                                          std::uint32_t frame_index_,
                                                          const ecs::Particle2DGpuInstance* instances_,
                                                          std::uint32_t instance_count_,
                                                          std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(instance_count_) * sizeof(ecs::Particle2DGpuInstance);
    return UploadFullBuffer(context_,
                            upload_host_,
                            frame_index_,
                            frame.instances_2d,
                            instances_,
                            size_bytes,
                            instance_count_,
                            revision_,
                            create_info_cache.initial_2d_instance_buffer_bytes);
}

ParticleUploadRange ParticleUploadHost::Upload3DInstances(VulkanContext& context_,
                                                          render::UploadHost& upload_host_,
                                                          std::uint32_t frame_index_,
                                                          const ecs::Particle3DGpuInstance* instances_,
                                                          std::uint32_t instance_count_,
                                                          std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(instance_count_) * sizeof(ecs::Particle3DGpuInstance);
    return UploadFullBuffer(context_,
                            upload_host_,
                            frame_index_,
                            frame.instances_3d,
                            instances_,
                            size_bytes,
                            instance_count_,
                            revision_,
                            create_info_cache.initial_3d_instance_buffer_bytes);
}

Particle2DRuntimeUploadResult ParticleUploadHost::PrepareRuntimeAndUpload2D(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    std::uint64_t last_submitted_value_,
    std::uint64_t completed_submit_value_,
    const ecs::Particle<ecs::Dim2>* particles_,
    const ecs::ParticleEmitter<ecs::Dim2>* emitters_,
    const ecs::Transform<ecs::Dim2>* transforms_,
    std::uint32_t component_count_,
    ecs::Particle2DRuntimeScratch& runtime_scratch_,
    const ecs::Particle2DRuntimeBuildHint& runtime_build_hint_,
    const Particle2DRuntimeUploadOptions& options_) {
    if (!initialized) {
        throw std::runtime_error("ParticleUploadHost::PrepareRuntimeAndUpload2D called before Initialize");
    }

    BeginFrame(context_, frame_index_, last_submitted_value_, completed_submit_value_);

    Particle2DRuntimeUploadResult result{};
    result.runtime = ecs::ParticleRuntimeSystem<ecs::Dim2>::Build(particles_,
                                                                  emitters_,
                                                                  transforms_,
                                                                  component_count_,
                                                                  runtime_scratch_,
                                                                  options_.runtime_build,
                                                                  runtime_build_hint_);

    if (runtime_scratch_.instances.empty() || result.runtime.emitted_instance_count == 0U) {
        result.skipped_upload = true;
        return result;
    }

    const std::uint64_t upload_revision = ComposeUploadRevision(result.runtime.component_signature,
                                                                result.runtime.transform_signature,
                                                                result.runtime.visible_signature,
                                                                result.runtime.runtime_state_signature);
    result.upload = Upload2DInstances(context_,
                                      upload_host_,
                                      frame_index_,
                                      runtime_scratch_.instances.data(),
                                      static_cast<std::uint32_t>(runtime_scratch_.instances.size()),
                                      upload_revision);
    result.skipped_upload = !result.upload.uploaded;
    return result;
}

Particle3DRuntimeUploadResult ParticleUploadHost::PrepareRuntimeAndUpload3D(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    std::uint64_t last_submitted_value_,
    std::uint64_t completed_submit_value_,
    const ecs::Particle<ecs::Dim3>* particles_,
    const ecs::ParticleEmitter<ecs::Dim3>* emitters_,
    const ecs::Transform<ecs::Dim3>* transforms_,
    std::uint32_t component_count_,
    ecs::Particle3DRuntimeScratch& runtime_scratch_,
    const ecs::Particle3DRuntimeBuildHint& runtime_build_hint_,
    const Particle3DRuntimeUploadOptions& options_) {
    if (!initialized) {
        throw std::runtime_error("ParticleUploadHost::PrepareRuntimeAndUpload3D called before Initialize");
    }

    BeginFrame(context_, frame_index_, last_submitted_value_, completed_submit_value_);

    Particle3DRuntimeUploadResult result{};
    result.runtime = ecs::ParticleRuntimeSystem<ecs::Dim3>::Build(particles_,
                                                                  emitters_,
                                                                  transforms_,
                                                                  component_count_,
                                                                  runtime_scratch_,
                                                                  options_.runtime_build,
                                                                  runtime_build_hint_);

    if (runtime_scratch_.instances.empty() || result.runtime.emitted_instance_count == 0U) {
        result.skipped_upload = true;
        return result;
    }

    const std::uint64_t upload_revision = ComposeUploadRevision(result.runtime.component_signature,
                                                                result.runtime.transform_signature,
                                                                result.runtime.visible_signature,
                                                                result.runtime.runtime_state_signature);
    result.upload = Upload3DInstances(context_,
                                      upload_host_,
                                      frame_index_,
                                      runtime_scratch_.instances.data(),
                                      static_cast<std::uint32_t>(runtime_scratch_.instances.size()),
                                      upload_revision);
    result.skipped_upload = !result.upload.uploaded;
    return result;
}

std::uint64_t ParticleUploadHost::ComposeUploadRevision(std::uint64_t component_signature_,
                                                        std::uint64_t transform_signature_,
                                                        std::uint64_t visible_signature_,
                                                        std::uint64_t runtime_state_signature_) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    HashCombineRevision(hash, component_signature_);
    HashCombineRevision(hash, transform_signature_);
    HashCombineRevision(hash, visible_signature_);
    HashCombineRevision(hash, runtime_state_signature_);
    return hash;
}

bool ParticleUploadHost::IsInitialized() const noexcept {
    return initialized;
}

std::uint32_t ParticleUploadHost::FramesInFlight() const noexcept {
    return create_info_cache.frames_in_flight;
}

const ParticleUploadHostStats& ParticleUploadHost::Stats() const noexcept {
    return stats;
}

VkDeviceSize ParticleUploadHost::NextPow2(VkDeviceSize value_) noexcept {
    if (value_ <= 1U) {
        return 1U;
    }

    --value_;
    value_ |= (value_ >> 1U);
    value_ |= (value_ >> 2U);
    value_ |= (value_ >> 4U);
    value_ |= (value_ >> 8U);
    value_ |= (value_ >> 16U);
    value_ |= (value_ >> 32U);
    return value_ + 1U;
}

void ParticleUploadHost::HashCombineRevision(std::uint64_t& hash_,
                                             std::uint64_t value_) noexcept {
    hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
}

void ParticleUploadHost::DestroyStreamBuffer(VulkanContext& context_,
                                             StreamBuffer& stream_) {
    if (stream_.buffer.buffer != VK_NULL_HANDLE) {
        resource::BufferHost::DestroyBuffer(context_, stream_.buffer);
    }
    stream_ = {};
}

void ParticleUploadHost::RetireStreamBuffer(StreamBuffer& stream_,
                                            std::uint64_t retire_value_) {
    if (stream_.buffer.buffer == VK_NULL_HANDLE) {
        stream_ = {};
        return;
    }
    retired_buffers.Retire(std::move(stream_.buffer), retire_value_);
    stream_ = {};
}

void ParticleUploadHost::CollectRetiredBuffers(VulkanContext& context_,
                                               std::uint64_t completed_submit_value_) {
    (void)retired_buffers.Collect(completed_submit_value_, [&](resource::BufferResource& buffer_) {
        resource::BufferHost::DestroyBuffer(context_, buffer_);
    });
}

void ParticleUploadHost::DestroyRetiredBuffers(VulkanContext& context_) noexcept {
    (void)retired_buffers.Flush([&](resource::BufferResource& buffer_) {
        resource::BufferHost::DestroyBuffer(context_, buffer_);
    });
}

std::uint64_t ParticleUploadHost::ComputeRetireValue() const noexcept {
    return std::max(last_submitted_value_seen, completed_submit_value_seen + 1U);
}

void ParticleUploadHost::EnsureStreamCapacity(VulkanContext& context_,
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
        throw std::runtime_error("ParticleUploadHost stream capacity exceeded while growth is disabled");
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

ParticleUploadRange ParticleUploadHost::UploadFullBuffer(VulkanContext& context_,
                                                         render::UploadHost& upload_host_,
                                                         std::uint32_t frame_index_,
                                                         StreamBuffer& stream_,
                                                         const void* src_data_,
                                                         VkDeviceSize size_bytes_,
                                                         std::uint32_t element_count_,
                                                         std::uint64_t revision_,
                                                         VkDeviceSize minimum_capacity_bytes_) {
    ParticleUploadRange range{};
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
        throw std::invalid_argument("ParticleUploadHost::UploadFullBuffer received null src_data for non-empty upload");
    }

    EnsureStreamCapacity(context_,
                         stream_,
                         size_bytes_,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         minimum_capacity_bytes_);

    if (stream_.uploaded_revision == revision_ &&
        stream_.uploaded_size_bytes == size_bytes_ &&
        stream_.buffer.buffer != VK_NULL_HANDLE) {
        range.buffer = stream_.buffer.buffer;
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
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                               VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                                VK_ACCESS_2_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = stream_.buffer.buffer;
        barrier.offset = 0U;
        barrier.size = size_bytes_;
        upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        ++stats.barrier_count;
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

ParticleUploadHost::FrameState& ParticleUploadHost::FrameAt(std::uint32_t frame_index_) {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("ParticleUploadHost frame index out of range");
    }
    return frames[frame_index_];
}

const ParticleUploadHost::FrameState& ParticleUploadHost::FrameAt(std::uint32_t frame_index_) const {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("ParticleUploadHost frame index out of range");
    }
    return frames[frame_index_];
}

} // namespace vr::particle
