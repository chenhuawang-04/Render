#include "vr/surface/surface_image_host.hpp"

#include "vr/render/descriptor_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::surface {

void SurfaceImageHost::Initialize(VulkanContext& context_,
                                  resource::GpuMemoryHost& gpu_memory_host_,
                                  const SurfaceImageHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    gpu_memory_host = &gpu_memory_host_;
    bindless_config = {};
    create_info_cache = create_info_;
    images.clear();
    retired_images.Clear();
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;

    if (create_info_cache.reserve_image_count > 0U) {
        images.reserve(create_info_cache.reserve_image_count);
    }
    if (create_info_cache.reserve_retired_image_count > 0U) {
        retired_images.Reserve(create_info_cache.reserve_retired_image_count);
    }

    initialized = true;
}

void SurfaceImageHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    for (auto& image : images) {
        resource::ImageHost::DestroyImage(context_, image.resource);
    }
    images.clear();

    DestroyRetiredImages(context_);

    gpu_memory_host = nullptr;
    bindless_config = {};
    create_info_cache = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = false;
}

void SurfaceImageHost::BeginFrame(VulkanContext& context_,
                                  std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceImageHost::BeginFrame called before Initialize");
    }

    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredImages(context_, completed_submit_value_);
    SyncBindlessRecords();
    stats.image_count = static_cast<std::uint32_t>(images.size());
    stats.retired_image_count = retired_images.PendingCount();
}

void SurfaceImageHost::ConfigureBindless(const SurfaceImageHostBindlessConfig& bindless_config_) {
    if (bindless_config.SameBinding(bindless_config_)) {
        return;
    }
    InvalidateBindlessRecords(bindless_config);
    bindless_config = bindless_config_;
    SyncBindlessRecords();
    if (initialized) {
        ++stats.revision;
        stats.image_count = static_cast<std::uint32_t>(images.size());
        stats.retired_image_count = retired_images.PendingCount();
    }
}

void SurfaceImageHost::UploadImage(VulkanContext& context_,
                                   render::UploadHost& upload_host_,
                                   std::uint32_t frame_index_,
                                   std::uint64_t last_submitted_value_,
                                   std::uint64_t completed_submit_value_,
                                   const SurfaceImageUploadInfo& upload_info_) {
    if (!initialized || gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceImageHost::UploadImage called before Initialize");
    }
    if (upload_info_.image_id == 0U) {
        throw std::invalid_argument("SurfaceImageHost::UploadImage image_id must be non-zero");
    }
    if (upload_info_.pixels == nullptr) {
        throw std::invalid_argument("SurfaceImageHost::UploadImage pixels must be non-null");
    }
    if (upload_info_.width == 0U || upload_info_.height == 0U) {
        throw std::invalid_argument("SurfaceImageHost::UploadImage width/height must be > 0");
    }
    if (upload_info_.bytes_per_pixel == 0U) {
        throw std::invalid_argument("SurfaceImageHost::UploadImage bytes_per_pixel must be > 0");
    }
    if (context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("SurfaceImageHost::UploadImage requires Vulkan 1.3 synchronization2");
    }

    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredImages(context_, completed_submit_value_);
    const std::size_t lower_bound_index = LowerBoundImageIndex(upload_info_.image_id);

    const bool exists = lower_bound_index < images.size() &&
                        images[lower_bound_index].image_id == upload_info_.image_id;
    if (!exists) {
        const std::size_t old_size = images.size();
        images.resize(old_size + 1U);
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            images[index] = std::move(images[index - 1U]);
        }
        images[lower_bound_index] = {};
        images[lower_bound_index].image_id = upload_info_.image_id;
    }

    ImageRecord& record = images[lower_bound_index];
    const bool compatible_existing =
        record.resource.image != VK_NULL_HANDLE &&
        !upload_info_.force_recreate &&
        record.width == upload_info_.width &&
        record.height == upload_info_.height &&
        record.format == upload_info_.format &&
        record.usage == upload_info_.usage &&
        record.aspect_mask == upload_info_.aspect_mask &&
        record.shader_read_layout == upload_info_.shader_read_layout;

    if (!compatible_existing) {
        if (record.resource.image != VK_NULL_HANDLE) {
            RetireImage(record, last_submitted_value_);
            ++stats.updated_image_count;
        } else {
            ++stats.uploaded_image_count;
        }

        record.resource = CreateImageResource(context_,
                                              *gpu_memory_host,
                                              create_info_cache,
                                              upload_info_);
        record.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        record.width = upload_info_.width;
        record.height = upload_info_.height;
        record.format = upload_info_.format;
        record.usage = upload_info_.usage;
        record.shader_read_layout = upload_info_.shader_read_layout;
        record.aspect_mask = upload_info_.aspect_mask;
    } else {
        ++stats.updated_image_count;
    }

    const std::uint64_t upload_size_bytes =
        static_cast<std::uint64_t>(upload_info_.width) *
        static_cast<std::uint64_t>(upload_info_.height) *
        static_cast<std::uint64_t>(upload_info_.bytes_per_pixel);
    if (upload_size_bytes > static_cast<std::uint64_t>(std::numeric_limits<VkDeviceSize>::max())) {
        throw std::overflow_error("SurfaceImageHost::UploadImage upload size exceeds VkDeviceSize range");
    }

    const VkImageLayout old_layout = record.current_layout;
    if (old_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        RecordImageBarrier(upload_host_,
                           frame_index_,
                           record.resource.image,
                           old_layout,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           record.aspect_mask,
                           old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                               ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                               : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           0U,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT);
        ++stats.barrier_count;
    }

    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0U;
    copy_region.bufferRowLength = 0U;
    copy_region.bufferImageHeight = 0U;
    copy_region.imageSubresource.aspectMask = record.aspect_mask;
    copy_region.imageSubresource.mipLevel = 0U;
    copy_region.imageSubresource.baseArrayLayer = 0U;
    copy_region.imageSubresource.layerCount = 1U;
    copy_region.imageOffset = VkOffset3D{0, 0, 0};
    copy_region.imageExtent = VkExtent3D{upload_info_.width, upload_info_.height, 1U};

    upload_host_.StageAndRecordCopyImage(frame_index_,
                                         record.resource.image,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         copy_region,
                                         upload_info_.pixels,
                                         static_cast<VkDeviceSize>(upload_size_bytes),
                                         16U);

    if (record.shader_read_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        RecordImageBarrier(upload_host_,
                           frame_index_,
                           record.resource.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           record.shader_read_layout,
                           record.aspect_mask,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        ++stats.barrier_count;
    }

    record.current_layout = record.shader_read_layout;
    ++record.revision;
    SyncBindlessRecords();
    stats.uploaded_bytes += upload_size_bytes;
    stats.image_count = static_cast<std::uint32_t>(images.size());
    stats.retired_image_count = retired_images.PendingCount();
    ++stats.revision;
}

bool SurfaceImageHost::RemoveImage(VulkanContext& context_,
                                   std::uint32_t image_id_,
                                   std::uint64_t last_submitted_value_,
                                   std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceImageHost::RemoveImage called before Initialize");
    }
    if (image_id_ == 0U) {
        return false;
    }

    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredImages(context_, completed_submit_value_);
    const std::size_t lower_bound_index = LowerBoundImageIndex(image_id_);
    if (lower_bound_index >= images.size() ||
        images[lower_bound_index].image_id != image_id_) {
        return false;
    }

    ImageRecord& record = images[lower_bound_index];
    if (bindless_config.Enabled() && record.bindless.image_slot.IsValid()) {
        bindless_config.descriptor_host->QueueBindlessPlaceholderWrite(
            bindless_config.image_table,
            record.bindless.image_slot);
        bindless_config.descriptor_host->FreeBindlessSlotDeferred(
            bindless_config.image_table,
            record.bindless.image_slot,
            last_submitted_value_);
        record.bindless.retire_value = last_submitted_value_;
    }
    RetireImage(record, last_submitted_value_);
    images.erase(images.begin() + static_cast<std::ptrdiff_t>(lower_bound_index));

    ++stats.removed_image_count;
    stats.image_count = static_cast<std::uint32_t>(images.size());
    stats.retired_image_count = retired_images.PendingCount();
    ++stats.revision;
    return true;
}

const SurfaceImageHost::ImageRecord* SurfaceImageHost::FindImage(std::uint32_t image_id_) const noexcept {
    if (!initialized || image_id_ == 0U) {
        return nullptr;
    }

    const std::size_t lower_bound_index = LowerBoundImageIndex(image_id_);
    if (lower_bound_index >= images.size()) {
        return nullptr;
    }
    const ImageRecord& record = images[lower_bound_index];
    if (record.image_id != image_id_) {
        return nullptr;
    }
    return &record;
}

render::BindlessSlot SurfaceImageHost::ResolveBindlessImageSlot(std::uint32_t image_id_) const noexcept {
    const ImageRecord* record = FindImage(image_id_);
    return record != nullptr ? record->bindless.image_slot : render::BindlessSlot{};
}

bool SurfaceImageHost::IsInitialized() const noexcept {
    return initialized;
}

const SurfaceImageHostStats& SurfaceImageHost::Stats() const noexcept {
    return stats;
}

const SurfaceImageHostBindlessConfig& SurfaceImageHost::BindlessConfig() const noexcept {
    return bindless_config;
}

std::size_t SurfaceImageHost::LowerBoundImageIndex(std::uint32_t image_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = images.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (images[it].image_id < image_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void SurfaceImageHost::RetireImage(ImageRecord& record_,
                                   std::uint64_t retire_value_) {
    if (record_.resource.image == VK_NULL_HANDLE) {
        return;
    }

    retired_images.Retire(std::move(record_.resource), retire_value_);

    record_.resource = {};
    record_.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    record_.bindless.revision_written = 0U;
}

void SurfaceImageHost::CollectRetiredImages(VulkanContext& context_,
                                            std::uint64_t completed_submit_value_) {
    if (retired_images.Empty()) {
        return;
    }
    if (context_.Device() == VK_NULL_HANDLE) {
        return;
    }
    (void)retired_images.Collect(completed_submit_value_,
                                 [&](resource::ImageResource& resource_) {
                                     resource::ImageHost::DestroyImage(context_, resource_);
                                 });
}

void SurfaceImageHost::DestroyRetiredImages(VulkanContext& context_) noexcept {
    (void)retired_images.Flush([&](resource::ImageResource& resource_) {
        resource::ImageHost::DestroyImage(context_, resource_);
    });
}

void SurfaceImageHost::InvalidateBindlessRecords(const SurfaceImageHostBindlessConfig& bindless_config_) {
    const std::uint64_t retire_value = ComputeBindlessRetireValue();
    for (ImageRecord& record : images) {
        if (bindless_config_.Enabled() && record.bindless.image_slot.IsValid()) {
            bindless_config_.descriptor_host->QueueBindlessPlaceholderWrite(bindless_config_.image_table,
                                                                            record.bindless.image_slot);
            bindless_config_.descriptor_host->FreeBindlessSlotDeferred(bindless_config_.image_table,
                                                                       record.bindless.image_slot,
                                                                       retire_value);
            record.bindless.retire_value = retire_value;
        }
        record.bindless = {};
    }
}

void SurfaceImageHost::SyncBindlessRecords() {
    if (!bindless_config.Enabled()) {
        return;
    }

    for (ImageRecord& record : images) {
        if (record.resource.default_view == VK_NULL_HANDLE ||
            record.current_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            continue;
        }

        if (!record.bindless.image_slot.IsValid()) {
            record.bindless.image_slot =
                bindless_config.descriptor_host->AllocateBindlessSlot(bindless_config.image_table);
            record.bindless.revision_written = 0U;
        }

        if (record.bindless.revision_written == record.revision) {
            continue;
        }

        bindless_config.descriptor_host->QueueBindlessImageWrite(bindless_config.image_table,
                                                                 record.bindless.image_slot,
                                                                 record.resource.default_view,
                                                                 record.shader_read_layout);
        record.bindless.revision_written = record.revision;
    }
}

std::uint64_t SurfaceImageHost::ComputeBindlessRetireValue() const noexcept {
    return std::max(last_submitted_value_seen, completed_submit_value_seen);
}

resource::ImageResource SurfaceImageHost::CreateImageResource(
    VulkanContext& context_,
    resource::GpuMemoryHost& gpu_memory_host_,
    const SurfaceImageHostCreateInfo& create_info_,
    const SurfaceImageUploadInfo& upload_info_) {
    resource::ImageCreateInfo create_info{};
    create_info.image_type = VK_IMAGE_TYPE_2D;
    create_info.format = upload_info_.format;
    create_info.extent = VkExtent3D{upload_info_.width, upload_info_.height, 1U};
    create_info.mip_levels = 1U;
    create_info.array_layers = 1U;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = upload_info_.usage;
    create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    const auto& families = context_.QueueFamilies();
    if (families.graphics.has_value() &&
        families.transfer.has_value() &&
        families.graphics.value() != families.transfer.value()) {
        create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        create_info.queue_family_indices.push_back(families.graphics.value());
        create_info.queue_family_indices.push_back(families.transfer.value());
    }
    create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    create_info.memory_properties = create_info_.memory_properties;
    create_info.create_default_view = true;
    create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    create_info.default_view_aspect = upload_info_.aspect_mask;
    create_info.default_base_mip_level = 0U;
    create_info.default_level_count = 1U;
    create_info.default_base_array_layer = 0U;
    create_info.default_layer_count = 1U;
    return resource::ImageHost::CreateImage(context_, create_info, gpu_memory_host_);
}

void SurfaceImageHost::RecordImageBarrier(render::UploadHost& upload_host_,
                                          std::uint32_t frame_index_,
                                          VkImage image_,
                                          VkImageLayout old_layout_,
                                          VkImageLayout new_layout_,
                                          VkImageAspectFlags aspect_mask_,
                                          VkPipelineStageFlags2 src_stage_mask_,
                                          VkAccessFlags2 src_access_mask_,
                                          VkPipelineStageFlags2 dst_stage_mask_,
                                          VkAccessFlags2 dst_access_mask_) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage_mask_;
    barrier.srcAccessMask = src_access_mask_;
    barrier.dstStageMask = dst_stage_mask_;
    barrier.dstAccessMask = dst_access_mask_;
    barrier.oldLayout = old_layout_;
    barrier.newLayout = new_layout_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = aspect_mask_;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;
    upload_host_.RecordImageBarrier2(frame_index_, barrier);
}

} // namespace vr::surface
