#include "vr/surface/surface_upload_host.hpp"

#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vr::surface {

void SurfaceUploadHost::Initialize(VulkanContext& context_,
                                   resource::GpuMemoryHost& gpu_memory_host_,
                                   const SurfaceUploadHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    if (create_info_.frames_in_flight == 0U) {
        throw std::invalid_argument("SurfaceUploadHost::Initialize frames_in_flight must be > 0");
    }

    gpu_memory_host = &gpu_memory_host_;
    create_info_cache = create_info_;
    if (create_info_cache.patch_fallback_coverage_percent > 100U) {
        create_info_cache.patch_fallback_coverage_percent = 100U;
    }
    frames.clear();
    frames.resize(create_info_cache.frames_in_flight);
    patch_scratch.clear();
    merged_patch_scratch.clear();
    stats = {};
    initialized = true;
}

void SurfaceUploadHost::Shutdown(VulkanContext& context_) {
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

    gpu_memory_host = nullptr;
    create_info_cache = {};
    patch_scratch.clear();
    merged_patch_scratch.clear();
    stats = {};
    initialized = false;
}

void SurfaceUploadHost::BeginFrame(VulkanContext& context_,
                                   std::uint32_t frame_index_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceUploadHost::BeginFrame called before Initialize");
    }

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

SurfaceUploadRange SurfaceUploadHost::Upload2DInstances(VulkanContext& context_,
                                                        render::UploadHost& upload_host_,
                                                        std::uint32_t frame_index_,
                                                        const ecs::Surface2DGpuInstance* instances_,
                                                        std::uint32_t instance_count_,
                                                        std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(instance_count_) * sizeof(ecs::Surface2DGpuInstance);

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

SurfaceUploadRange SurfaceUploadHost::Upload3DInstances(VulkanContext& context_,
                                                        render::UploadHost& upload_host_,
                                                        std::uint32_t frame_index_,
                                                        const ecs::Surface3DGpuInstance* instances_,
                                                        std::uint32_t instance_count_,
                                                        std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(instance_count_) * sizeof(ecs::Surface3DGpuInstance);

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

SurfaceUploadRange SurfaceUploadHost::Upload2DInstancePatches(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::Surface2DGpuInstance* instances_,
    std::uint32_t instance_count_,
    const SurfaceUploadPatch* patches_,
    std::uint32_t patch_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    return UploadBufferPatches(context_,
                               upload_host_,
                               frame_index_,
                               frame.instances_2d,
                               instances_,
                               instance_count_,
                               sizeof(ecs::Surface2DGpuInstance),
                               patches_,
                               patch_count_,
                               revision_,
                               create_info_cache.initial_2d_instance_buffer_bytes);
}

SurfaceUploadRange SurfaceUploadHost::Upload3DInstancePatches(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::Surface3DGpuInstance* instances_,
    std::uint32_t instance_count_,
    const SurfaceUploadPatch* patches_,
    std::uint32_t patch_count_,
    std::uint64_t revision_) {
    FrameState& frame = FrameAt(frame_index_);
    return UploadBufferPatches(context_,
                               upload_host_,
                               frame_index_,
                               frame.instances_3d,
                               instances_,
                               instance_count_,
                               sizeof(ecs::Surface3DGpuInstance),
                               patches_,
                               patch_count_,
                               revision_,
                               create_info_cache.initial_3d_instance_buffer_bytes);
}

Surface2DRuntimeUploadResult SurfaceUploadHost::PrepareRuntimeAndUpload2D(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::Surface<ecs::Dim2>* components_,
    const ecs::Transform<ecs::Dim2>* transforms_,
    std::uint32_t component_count_,
    ecs::Surface2DRuntimeScratch& runtime_scratch_,
    ecs::SurfaceUploadPlanScratch<ecs::Dim2>& plan_scratch_,
    const ecs::Surface2DRuntimeBuildHint& runtime_build_hint_,
    const Surface2DRuntimeUploadOptions& options_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceUploadHost::PrepareRuntimeAndUpload2D called before Initialize");
    }

    BeginFrame(context_, frame_index_);

    Surface2DRuntimeUploadResult result{};
    result.runtime = ecs::SurfaceRuntimeSystem<ecs::Dim2>::Build(components_,
                                                                 transforms_,
                                                                 component_count_,
                                                                 runtime_scratch_,
                                                                 options_.runtime_build,
                                                                 runtime_build_hint_);

    if (runtime_scratch_.instances.empty() || result.runtime.emitted_instance_count == 0U) {
        result.skipped_upload = true;
        return result;
    }

    if (result.runtime.cache_status == ecs::SurfaceRuntimeCacheStatus::hit_reused) {
        result.skipped_upload = true;
        return result;
    }

    const std::uint64_t upload_revision = ComposeUploadRevision(result.runtime.surface_signature,
                                                                result.runtime.transform_signature);

    if (ShouldAttemptPartialUpload(result.runtime, runtime_build_hint_, options_)) {
        result.plan = ecs::SurfaceUploadPlanSystem<ecs::Dim2>::BuildRangesFromDirtyComponents(
            runtime_scratch_,
            runtime_build_hint_.transform_dirty_component_indices,
            runtime_build_hint_.transform_dirty_component_count,
            options_.plan_build,
            plan_scratch_);

        if (result.plan.range_count > 0U && result.plan.covered_instance_count > 0U) {
            static_assert(sizeof(ecs::SurfaceUploadPatchRange) == sizeof(SurfaceUploadPatch));
            static_assert(alignof(ecs::SurfaceUploadPatchRange) == alignof(SurfaceUploadPatch));
            const auto* patches = reinterpret_cast<const SurfaceUploadPatch*>(plan_scratch_.ranges.data());

            result.upload = Upload2DInstancePatches(context_,
                                                    upload_host_,
                                                    frame_index_,
                                                    runtime_scratch_.instances.data(),
                                                    static_cast<std::uint32_t>(runtime_scratch_.instances.size()),
                                                    patches,
                                                    result.plan.range_count,
                                                    upload_revision);
            result.used_partial_upload = result.upload.partial && result.upload.uploaded;
            result.skipped_upload = !result.upload.uploaded;
            return result;
        }

        if (result.runtime.transform_rewritten_instance_count == 0U) {
            result.skipped_upload = true;
            return result;
        }
    }

    result.upload = Upload2DInstances(context_,
                                      upload_host_,
                                      frame_index_,
                                      runtime_scratch_.instances.data(),
                                      static_cast<std::uint32_t>(runtime_scratch_.instances.size()),
                                      upload_revision);
    result.used_partial_upload = false;
    result.skipped_upload = !result.upload.uploaded;
    return result;
}

Surface3DRuntimeUploadResult SurfaceUploadHost::PrepareRuntimeAndUpload3D(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    const ecs::Surface<ecs::Dim3>* components_,
    const ecs::Transform<ecs::Dim3>* transforms_,
    std::uint32_t component_count_,
    ecs::Surface3DRuntimeScratch& runtime_scratch_,
    ecs::SurfaceUploadPlanScratch<ecs::Dim3>& plan_scratch_,
    const ecs::Surface3DRuntimeBuildHint& runtime_build_hint_,
    const Surface3DRuntimeUploadOptions& options_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceUploadHost::PrepareRuntimeAndUpload3D called before Initialize");
    }

    BeginFrame(context_, frame_index_);

    Surface3DRuntimeUploadResult result{};
    result.runtime = ecs::SurfaceRuntimeSystem<ecs::Dim3>::Build(components_,
                                                                 transforms_,
                                                                 component_count_,
                                                                 runtime_scratch_,
                                                                 options_.runtime_build,
                                                                 runtime_build_hint_);

    if (runtime_scratch_.instances.empty() || result.runtime.emitted_instance_count == 0U) {
        result.skipped_upload = true;
        return result;
    }

    if (result.runtime.cache_status == ecs::SurfaceRuntimeCacheStatus::hit_reused) {
        result.skipped_upload = true;
        return result;
    }

    const std::uint64_t upload_revision = ComposeUploadRevision(result.runtime.surface_signature,
                                                                result.runtime.transform_signature);

    if (ShouldAttemptPartialUpload(result.runtime, runtime_build_hint_, options_)) {
        result.plan = ecs::SurfaceUploadPlanSystem<ecs::Dim3>::BuildRangesFromDirtyComponents(
            runtime_scratch_,
            runtime_build_hint_.transform_dirty_component_indices,
            runtime_build_hint_.transform_dirty_component_count,
            options_.plan_build,
            plan_scratch_);

        if (result.plan.range_count > 0U && result.plan.covered_instance_count > 0U) {
            static_assert(sizeof(ecs::SurfaceUploadPatchRange) == sizeof(SurfaceUploadPatch));
            static_assert(alignof(ecs::SurfaceUploadPatchRange) == alignof(SurfaceUploadPatch));
            const auto* patches = reinterpret_cast<const SurfaceUploadPatch*>(plan_scratch_.ranges.data());

            result.upload = Upload3DInstancePatches(context_,
                                                    upload_host_,
                                                    frame_index_,
                                                    runtime_scratch_.instances.data(),
                                                    static_cast<std::uint32_t>(runtime_scratch_.instances.size()),
                                                    patches,
                                                    result.plan.range_count,
                                                    upload_revision);
            result.used_partial_upload = result.upload.partial && result.upload.uploaded;
            result.skipped_upload = !result.upload.uploaded;
            return result;
        }

        if (result.runtime.transform_rewritten_instance_count == 0U) {
            result.skipped_upload = true;
            return result;
        }
    }

    result.upload = Upload3DInstances(context_,
                                      upload_host_,
                                      frame_index_,
                                      runtime_scratch_.instances.data(),
                                      static_cast<std::uint32_t>(runtime_scratch_.instances.size()),
                                      upload_revision);
    result.used_partial_upload = false;
    result.skipped_upload = !result.upload.uploaded;
    return result;
}

bool SurfaceUploadHost::ShouldAttemptPartialUpload(
    const ecs::Surface2DRuntimeBuildStats& runtime_stats_,
    const ecs::Surface2DRuntimeBuildHint& runtime_build_hint_,
    const Surface2DRuntimeUploadOptions& options_) noexcept {
    if (!options_.enable_partial_upload) {
        return false;
    }
    if (runtime_stats_.cache_status != ecs::SurfaceRuntimeCacheStatus::hit_partial_update ||
        !runtime_stats_.transform_only_update) {
        return false;
    }
    if (runtime_build_hint_.transform_dirty_component_indices == nullptr) {
        return false;
    }
    const std::uint32_t minimum_dirty_count =
        options_.min_partial_dirty_component_count == 0U ? 1U : options_.min_partial_dirty_component_count;
    if (runtime_build_hint_.transform_dirty_component_count < minimum_dirty_count) {
        return false;
    }
    if (options_.require_dirty_hint_for_partial && !runtime_stats_.transform_update_from_dirty_hint) {
        return false;
    }
    return true;
}

bool SurfaceUploadHost::ShouldAttemptPartialUpload(
    const ecs::Surface3DRuntimeBuildStats& runtime_stats_,
    const ecs::Surface3DRuntimeBuildHint& runtime_build_hint_,
    const Surface3DRuntimeUploadOptions& options_) noexcept {
    if (!options_.enable_partial_upload) {
        return false;
    }
    if (runtime_stats_.cache_status != ecs::SurfaceRuntimeCacheStatus::hit_partial_update ||
        !runtime_stats_.transform_only_update) {
        return false;
    }
    if (runtime_build_hint_.transform_dirty_component_indices == nullptr) {
        return false;
    }
    const std::uint32_t minimum_dirty_count =
        options_.min_partial_dirty_component_count == 0U ? 1U : options_.min_partial_dirty_component_count;
    if (runtime_build_hint_.transform_dirty_component_count < minimum_dirty_count) {
        return false;
    }
    if (options_.require_dirty_hint_for_partial && !runtime_stats_.transform_update_from_dirty_hint) {
        return false;
    }
    return true;
}

bool SurfaceUploadHost::IsInitialized() const noexcept {
    return initialized;
}

std::uint32_t SurfaceUploadHost::FramesInFlight() const noexcept {
    return static_cast<std::uint32_t>(frames.size());
}

const SurfaceUploadHostStats& SurfaceUploadHost::Stats() const noexcept {
    return stats;
}

VkDeviceSize SurfaceUploadHost::NextPow2(VkDeviceSize value_) noexcept {
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

void SurfaceUploadHost::DestroyStreamBuffer(VulkanContext& context_,
                                            StreamBuffer& stream_) {
    resource::BufferHost::DestroyBuffer(context_, stream_.buffer);
    stream_.capacity_bytes = 0U;
    stream_.uploaded_size_bytes = 0U;
    stream_.element_count = 0U;
    stream_.uploaded_revision = 0U;
}

void SurfaceUploadHost::EnsureStreamCapacity(VulkanContext& context_,
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
        throw std::runtime_error("SurfaceUploadHost stream capacity exceeded while growth is disabled");
    }

    const VkDeviceSize target_capacity = NextPow2(std::max(required_bytes_, minimum_capacity_bytes_));
    DestroyStreamBuffer(context_, stream_);

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = target_capacity;
    buffer_create_info.usage = usage_;
    buffer_create_info.memory_properties = create_info_cache.memory_properties;
    stream_.buffer = resource::BufferHost::CreateBuffer(context_,
                                                        buffer_create_info,
                                                        *gpu_memory_host);
    stream_.capacity_bytes = target_capacity;
    ++stats.resized_buffer_count;
}

SurfaceUploadRange SurfaceUploadHost::UploadFullBuffer(VulkanContext& context_,
                                                       render::UploadHost& upload_host_,
                                                       std::uint32_t frame_index_,
                                                       StreamBuffer& stream_,
                                                       const void* src_data_,
                                                       VkDeviceSize size_bytes_,
                                                       std::uint32_t element_count_,
                                                       std::uint64_t revision_,
                                                       VkDeviceSize minimum_capacity_bytes_) {
    SurfaceUploadRange range{};
    range.element_count = element_count_;
    range.size_bytes = size_bytes_;
    range.uploaded_revision = revision_;
    range.patch_count = (size_bytes_ > 0U) ? 1U : 0U;
    range.partial = false;

    if (size_bytes_ == 0U || element_count_ == 0U) {
        stream_.uploaded_size_bytes = 0U;
        stream_.element_count = 0U;
        stream_.uploaded_revision = revision_;
        return range;
    }
    if (src_data_ == nullptr) {
        throw std::invalid_argument("SurfaceUploadHost::UploadFullBuffer received null src_data for non-empty upload");
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

SurfaceUploadRange SurfaceUploadHost::UploadBufferPatches(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    std::uint32_t frame_index_,
    StreamBuffer& stream_,
    const void* src_data_,
    std::uint32_t element_count_,
    std::size_t element_size_bytes_,
    const SurfaceUploadPatch* patches_,
    std::uint32_t patch_count_,
    std::uint64_t revision_,
    VkDeviceSize minimum_capacity_bytes_) {
    const VkDeviceSize total_size_bytes = static_cast<VkDeviceSize>(element_count_) *
                                          static_cast<VkDeviceSize>(element_size_bytes_);

    SurfaceUploadRange range{};
    range.element_count = element_count_;
    range.size_bytes = total_size_bytes;
    range.uploaded_revision = revision_;
    range.partial = true;
    range.patch_count = 0U;

    if (total_size_bytes == 0U || element_count_ == 0U) {
        stream_.uploaded_size_bytes = 0U;
        stream_.element_count = 0U;
        stream_.uploaded_revision = revision_;
        return range;
    }
    if (src_data_ == nullptr) {
        throw std::invalid_argument("SurfaceUploadHost::UploadBufferPatches received null src_data for non-empty upload");
    }

    EnsureStreamCapacity(context_,
                         stream_,
                         total_size_bytes,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         minimum_capacity_bytes_);

    const bool can_patch =
        (stream_.buffer.buffer != VK_NULL_HANDLE) &&
        (stream_.uploaded_size_bytes == total_size_bytes) &&
        (stream_.element_count == element_count_) &&
        (patches_ != nullptr) &&
        (patch_count_ > 0U);

    if (!can_patch) {
        return UploadFullBuffer(context_,
                                upload_host_,
                                frame_index_,
                                stream_,
                                src_data_,
                                total_size_bytes,
                                element_count_,
                                revision_,
                                minimum_capacity_bytes_);
    }

    stats.patch_input_count += patch_count_;

    std::uint32_t dropped_patch_count = 0U;
    std::uint64_t merged_patch_bytes = 0U;
    BuildMergedByteRanges(element_count_,
                          element_size_bytes_,
                          patches_,
                          patch_count_,
                          dropped_patch_count,
                          merged_patch_bytes);
    stats.patch_dropped_count += dropped_patch_count;

    const std::uint32_t merged_patch_count =
        static_cast<std::uint32_t>(merged_patch_scratch.size());
    range.patch_count = merged_patch_count;

    if (merged_patch_count == 0U ||
        ShouldFallbackToFullUpload(total_size_bytes,
                                   static_cast<VkDeviceSize>(merged_patch_bytes),
                                   merged_patch_count)) {
        ++stats.patch_fallback_full_upload_count;
        return UploadFullBuffer(context_,
                                upload_host_,
                                frame_index_,
                                stream_,
                                src_data_,
                                total_size_bytes,
                                element_count_,
                                revision_,
                                minimum_capacity_bytes_);
    }

    const auto* bytes = static_cast<const std::byte*>(src_data_);
    VkDeviceSize min_offset = merged_patch_scratch[0U].dst_offset;
    VkDeviceSize max_end = 0U;
    for (const BytePatchRange& patch_range : merged_patch_scratch) {
        const void* src_ptr = bytes + patch_range.dst_offset;

        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              stream_.buffer.buffer,
                                              patch_range.dst_offset,
                                              src_ptr,
                                              patch_range.size_bytes,
                                              16U);
        max_end = std::max(max_end, patch_range.dst_offset + patch_range.size_bytes);
    }

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
        barrier.offset = min_offset;
        barrier.size = max_end - min_offset;
        upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        ++stats.barrier_count;
    }

    stream_.uploaded_revision = revision_;
    stream_.uploaded_size_bytes = total_size_bytes;
    stream_.element_count = element_count_;

    range.buffer = stream_.buffer.buffer;
    range.uploaded = true;
    range.partial = true;
    range.patch_count = merged_patch_count;
    ++stats.upload_count;
    ++stats.partial_upload_count;
    stats.patch_merged_count += merged_patch_count;
    stats.patch_copy_count += merged_patch_count;
    stats.uploaded_bytes += merged_patch_bytes;
    return range;
}

bool SurfaceUploadHost::ShouldFallbackToFullUpload(
    VkDeviceSize full_size_bytes_,
    VkDeviceSize merged_patch_bytes_,
    std::uint32_t merged_patch_count_) const noexcept {
    if (full_size_bytes_ == 0U) {
        return false;
    }
    if (merged_patch_count_ == 0U) {
        return true;
    }
    if (merged_patch_bytes_ >= full_size_bytes_) {
        return true;
    }
    if (create_info_cache.patch_fallback_copy_count > 0U &&
        merged_patch_count_ >= create_info_cache.patch_fallback_copy_count) {
        return true;
    }
    if (create_info_cache.patch_fallback_coverage_percent > 0U) {
        const std::uint64_t scaled_patch_bytes =
            static_cast<std::uint64_t>(merged_patch_bytes_) * 100ULL;
        const std::uint64_t scaled_full_bytes =
            static_cast<std::uint64_t>(full_size_bytes_) *
            static_cast<std::uint64_t>(create_info_cache.patch_fallback_coverage_percent);
        if (scaled_patch_bytes >= scaled_full_bytes) {
            return true;
        }
    }
    return false;
}

std::uint64_t SurfaceUploadHost::ComposeUploadRevision(
    std::uint64_t surface_signature_,
    std::uint64_t transform_signature_) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    HashCombineRevision(hash, surface_signature_);
    HashCombineRevision(hash, transform_signature_);
    return hash;
}

void SurfaceUploadHost::HashCombineRevision(std::uint64_t& hash_,
                                            std::uint64_t value_) noexcept {
    hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
}

void SurfaceUploadHost::BuildMergedByteRanges(
    std::uint32_t element_count_,
    std::size_t element_size_bytes_,
    const SurfaceUploadPatch* patches_,
    std::uint32_t patch_count_,
    std::uint32_t& out_dropped_patch_count_,
    std::uint64_t& out_merged_patch_bytes_) {
    patch_scratch.clear();
    merged_patch_scratch.clear();
    out_dropped_patch_count_ = 0U;
    out_merged_patch_bytes_ = 0U;

    if (patches_ == nullptr || patch_count_ == 0U ||
        element_count_ == 0U || element_size_bytes_ == 0U) {
        return;
    }

    if (patch_scratch.capacity() < patch_count_) {
        patch_scratch.reserve(patch_count_);
    }

    for (std::uint32_t i = 0U; i < patch_count_; ++i) {
        const SurfaceUploadPatch patch = patches_[i];
        if (patch.instance_count == 0U || patch.instance_begin >= element_count_) {
            ++out_dropped_patch_count_;
            continue;
        }

        const std::uint32_t max_count = element_count_ - patch.instance_begin;
        const std::uint32_t clamped_count = std::min(patch.instance_count, max_count);
        if (clamped_count == 0U) {
            ++out_dropped_patch_count_;
            continue;
        }

        patch_scratch.push_back(SurfaceUploadPatch{
            .instance_begin = patch.instance_begin,
            .instance_count = clamped_count
        });
    }

    if (patch_scratch.empty()) {
        return;
    }

    std::sort(patch_scratch.begin(),
              patch_scratch.end(),
              [](const SurfaceUploadPatch& lhs_,
                 const SurfaceUploadPatch& rhs_) {
                  if (lhs_.instance_begin != rhs_.instance_begin) {
                      return lhs_.instance_begin < rhs_.instance_begin;
                  }
                  return lhs_.instance_count < rhs_.instance_count;
              });

    if (merged_patch_scratch.capacity() < patch_scratch.size()) {
        merged_patch_scratch.reserve(patch_scratch.size());
    }

    const VkDeviceSize merge_gap_bytes =
        static_cast<VkDeviceSize>(create_info_cache.patch_merge_gap_bytes);
    for (const SurfaceUploadPatch& patch : patch_scratch) {
        const VkDeviceSize begin_offset =
            static_cast<VkDeviceSize>(patch.instance_begin) *
            static_cast<VkDeviceSize>(element_size_bytes_);
        const VkDeviceSize end_offset = begin_offset +
                                        static_cast<VkDeviceSize>(patch.instance_count) *
                                            static_cast<VkDeviceSize>(element_size_bytes_);

        if (merged_patch_scratch.empty()) {
            merged_patch_scratch.push_back(BytePatchRange{
                .dst_offset = begin_offset,
                .size_bytes = end_offset - begin_offset
            });
            continue;
        }

        BytePatchRange& last = merged_patch_scratch.back();
        const VkDeviceSize last_end = last.dst_offset + last.size_bytes;
        const VkDeviceSize merge_limit = last_end + merge_gap_bytes;
        if (begin_offset <= merge_limit) {
            const VkDeviceSize merged_end = (end_offset > last_end) ? end_offset : last_end;
            last.size_bytes = merged_end - last.dst_offset;
            continue;
        }

        merged_patch_scratch.push_back(BytePatchRange{
            .dst_offset = begin_offset,
            .size_bytes = end_offset - begin_offset
        });
    }

    for (const BytePatchRange& patch : merged_patch_scratch) {
        out_merged_patch_bytes_ += static_cast<std::uint64_t>(patch.size_bytes);
    }
}

SurfaceUploadHost::FrameState& SurfaceUploadHost::FrameAt(std::uint32_t frame_index_) {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("SurfaceUploadHost frame_index out of range");
    }
    return frames[frame_index_];
}

const SurfaceUploadHost::FrameState& SurfaceUploadHost::FrameAt(std::uint32_t frame_index_) const {
    if (frame_index_ >= frames.size()) {
        throw std::out_of_range("SurfaceUploadHost frame_index out of range");
    }
    return frames[frame_index_];
}

} // namespace vr::surface
