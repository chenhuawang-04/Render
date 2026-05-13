#include "vr/text/glyph_upload_host.hpp"

#include "vr/render/descriptor_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::text {

void GlyphUploadHost::Initialize(VulkanContext& context_,
                                 resource::GpuMemoryHost& gpu_memory_host_,
                                 resource::SamplerHost& sampler_host_,
                                 const GlyphUploadHostCreateInfo& create_info_) {
    Shutdown(context_);

    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("GlyphUploadHost::Initialize requires initialized Vulkan device");
    }
    if (!gpu_memory_host_.IsInitialized()) {
        throw std::runtime_error("GlyphUploadHost::Initialize requires initialized GpuMemoryHost");
    }
    if (!sampler_host_.IsInitialized()) {
        throw std::runtime_error("GlyphUploadHost::Initialize requires initialized SamplerHost");
    }
    if (create_info_.atlas_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("GlyphUploadHost::Initialize requires valid atlas format");
    }
    if (create_info_.image_usage == 0U) {
        throw std::runtime_error("GlyphUploadHost::Initialize requires non-zero image usage");
    }

    create_info_cache = create_info_;
    gpu_memory_host = &gpu_memory_host_;
    sampler_host = &sampler_host_;
    bindless_config = {};

    if (create_info_cache.reserve_page_count > 0U) {
        pages.reserve(create_info_cache.reserve_page_count);
        retired_pages.Reserve(create_info_cache.reserve_page_count);
    }

    resource::SamplerDesc sampler_desc{};
    sampler_desc.mag_filter = create_info_cache.use_linear_sampler ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler_desc.min_filter = create_info_cache.use_linear_sampler ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_desc.address_mode_u = create_info_cache.clamp_to_edge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_desc.address_mode_v = create_info_cache.clamp_to_edge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_desc.address_mode_w = sampler_desc.address_mode_u;
    sampler_desc.min_lod = 0.0F;
    sampler_desc.max_lod = 0.0F;
    sampler_desc.unnormalized_coordinates = false;
    sampler_id = sampler_host_.RegisterSampler(context_, sampler_desc);

    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = true;
}

void GlyphUploadHost::Shutdown(VulkanContext& context_) {
    if (!initialized && pages.empty()) {
        return;
    }

    DestroyPageResources(context_);
    DestroyRetiredPageResources(context_);
    pages.clear();
    retired_pages.Clear();
    rect_upload_scratch.clear();

    gpu_memory_host = nullptr;
    sampler_host = nullptr;
    bindless_config = {};
    sampler_id = {};
    create_info_cache = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = false;
}

void GlyphUploadHost::ConfigureBindless(const GlyphUploadHostBindlessConfig& bindless_config_) {
    if (bindless_config.SameBinding(bindless_config_)) {
        return;
    }
    InvalidateBindlessPageResources(bindless_config);
    bindless_config = bindless_config_;
    SyncBindlessPageResources();
}

void GlyphUploadHost::UploadDirtyPages(VulkanContext& context_,
                                       render::UploadHost& upload_host_,
                                       std::uint32_t frame_index_,
                                       GlyphAtlasHost& atlas_host_,
                                       std::uint64_t last_submitted_value_,
                                       std::uint64_t completed_submit_value_) {
    if (!initialized || gpu_memory_host == nullptr || sampler_host == nullptr) {
        throw std::runtime_error("GlyphUploadHost::UploadDirtyPages called before Initialize");
    }
    if (!upload_host_.IsInitialized()) {
        throw std::runtime_error("GlyphUploadHost::UploadDirtyPages requires initialized UploadHost");
    }
    if (!atlas_host_.IsInitialized()) {
        throw std::runtime_error("GlyphUploadHost::UploadDirtyPages requires initialized GlyphAtlasHost");
    }
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredPageResources(context_, completed_submit_value_seen);

    EnsurePageResources(context_, atlas_host_);

    const bool use_general_layout = ShouldUseGeneralLayout(context_);
    std::uint32_t uploaded_page_count = 0U;
    std::uint32_t uploaded_rect_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t skipped_clean_page_count = 0U;

    const std::uint32_t page_count = atlas_host_.PageCount();
    for (std::uint32_t page_index = 0U; page_index < page_count; ++page_index) {
        const auto& dirty_rects = atlas_host_.PageDirtyRects(page_index);
        if (dirty_rects.empty()) {
            ++skipped_clean_page_count;
            continue;
        }

        const GlyphAtlasPageView page_view = atlas_host_.Page(page_index);
        if (page_view.pixels == nullptr || page_view.width == 0U || page_view.height == 0U) {
            atlas_host_.ClearPageDirtyRects(page_index);
            ++skipped_clean_page_count;
            continue;
        }

        PageResource& page_resource = pages[page_index];
        const VkImageLayout copy_layout = use_general_layout
            ? VK_IMAGE_LAYOUT_GENERAL
            : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        if (!use_general_layout) {
            TransitionImageLayoutIfNeeded(upload_host_,
                                          frame_index_,
                                          page_resource,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          barrier_count);
        }

        std::uint32_t rect_uploaded_in_page = 0U;
        for (const GlyphRectU16& rect : dirty_rects) {
            if (rect.width == 0U || rect.height == 0U) {
                continue;
            }
            if (static_cast<std::uint32_t>(rect.x) + rect.width > page_view.width ||
                static_cast<std::uint32_t>(rect.y) + rect.height > page_view.height) {
                throw std::runtime_error("GlyphUploadHost dirty rect out of atlas page bounds");
            }

            const std::uint32_t rect_width = rect.width;
            const std::uint32_t rect_height = rect.height;
            const std::size_t packed_size = static_cast<std::size_t>(rect_width) * rect_height;
            rect_upload_scratch.resize(packed_size);

            for (std::uint32_t row = 0U; row < rect_height; ++row) {
                const std::uint8_t* src_row = page_view.pixels +
                    static_cast<std::size_t>(rect.y + row) * page_view.width + rect.x;
                std::uint8_t* dst_row = rect_upload_scratch.data() +
                    static_cast<std::size_t>(row) * rect_width;
                std::memcpy(dst_row, src_row, rect_width);
            }

            VkBufferImageCopy copy_region{};
            copy_region.bufferOffset = 0U;
            copy_region.bufferRowLength = rect_width;
            copy_region.bufferImageHeight = rect_height;
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel = 0U;
            copy_region.imageSubresource.baseArrayLayer = 0U;
            copy_region.imageSubresource.layerCount = 1U;
            copy_region.imageOffset = {
                static_cast<std::int32_t>(rect.x),
                static_cast<std::int32_t>(rect.y),
                0
            };
            copy_region.imageExtent = {rect_width, rect_height, 1U};

            upload_host_.StageAndRecordCopyImage(frame_index_,
                                                 page_resource.image.image,
                                                 copy_layout,
                                                 copy_region,
                                                 rect_upload_scratch.data(),
                                                 static_cast<VkDeviceSize>(packed_size),
                                                 4U);
            uploaded_bytes += packed_size;
            ++uploaded_rect_count;
            ++rect_uploaded_in_page;
        }

        if (rect_uploaded_in_page > 0U) {
            ++uploaded_page_count;
        }

        if (!use_general_layout) {
            TransitionImageLayoutIfNeeded(upload_host_,
                                          frame_index_,
                                          page_resource,
                                          create_info_cache.shader_read_layout,
                                          barrier_count);
        }

        atlas_host_.ClearPageDirtyRects(page_index);
    }

    stats.page_count = static_cast<std::uint32_t>(pages.size());
    stats.uploaded_page_count += uploaded_page_count;
    stats.uploaded_rect_count += uploaded_rect_count;
    stats.uploaded_bytes += uploaded_bytes;
    stats.barrier_count += barrier_count;
    stats.skipped_clean_page_count += skipped_clean_page_count;
}

VkImageView GlyphUploadHost::PageImageView(std::uint32_t page_index_) const {
    if (!initialized) {
        throw std::runtime_error("GlyphUploadHost::PageImageView called before Initialize");
    }
    if (page_index_ >= pages.size()) {
        throw std::out_of_range("GlyphUploadHost::PageImageView page index out of range");
    }
    return pages[page_index_].image.default_view;
}

VkImage GlyphUploadHost::PageImage(std::uint32_t page_index_) const {
    if (!initialized) {
        throw std::runtime_error("GlyphUploadHost::PageImage called before Initialize");
    }
    if (page_index_ >= pages.size()) {
        throw std::out_of_range("GlyphUploadHost::PageImage page index out of range");
    }
    return pages[page_index_].image.image;
}

VkImageLayout GlyphUploadHost::PageShaderLayout(std::uint32_t page_index_) const {
    if (!initialized) {
        throw std::runtime_error("GlyphUploadHost::PageShaderLayout called before Initialize");
    }
    if (page_index_ >= pages.size()) {
        throw std::out_of_range("GlyphUploadHost::PageShaderLayout page index out of range");
    }
    const VkImageLayout current_layout = pages[page_index_].current_layout;
    if (current_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        return create_info_cache.prefer_shader_read_layout
            ? create_info_cache.shader_read_layout
            : VK_IMAGE_LAYOUT_GENERAL;
    }
    return current_layout;
}

render::BindlessSlot GlyphUploadHost::ResolveBindlessImageSlot(std::uint32_t page_index_) const noexcept {
    if (!initialized || page_index_ >= pages.size()) {
        return {};
    }
    return pages[page_index_].image_slot;
}

VkSampler GlyphUploadHost::Sampler() const {
    if (!initialized || sampler_host == nullptr || !sampler_id.IsValid()) {
        throw std::runtime_error("GlyphUploadHost::Sampler requested before Initialize");
    }
    return sampler_host->GetSampler(sampler_id);
}

resource::SamplerId GlyphUploadHost::SamplerId() const noexcept {
    return sampler_id;
}

const GlyphUploadHostBindlessConfig& GlyphUploadHost::BindlessConfig() const noexcept {
    return bindless_config;
}

std::uint32_t GlyphUploadHost::PageCount() const noexcept {
    return static_cast<std::uint32_t>(pages.size());
}

bool GlyphUploadHost::IsInitialized() const noexcept {
    return initialized;
}

const GlyphUploadHostStats& GlyphUploadHost::Stats() const noexcept {
    return stats;
}

bool GlyphUploadHost::ShouldUseGeneralLayout(const VulkanContext& context_) const noexcept {
    const bool synchronization2_enabled =
        context_.EnabledVulkan13Features().synchronization2 == VK_TRUE;
    return create_info_cache.force_general_layout ||
           !create_info_cache.prefer_shader_read_layout ||
           !synchronization2_enabled;
}

void GlyphUploadHost::EnsurePageResources(VulkanContext& context_,
                                          const GlyphAtlasHost& atlas_host_) {
    const bool use_general_layout = ShouldUseGeneralLayout(context_);
    const std::uint32_t page_count = atlas_host_.PageCount();

    while (pages.size() > page_count) {
        RetirePageResource(pages.back(), ComputeRetireValue());
        pages.pop_back();
    }

    for (std::uint32_t page_index = 0U; page_index < page_count; ++page_index) {
        const GlyphAtlasPageView page_view = atlas_host_.Page(page_index);
        if (page_view.width == 0U || page_view.height == 0U) {
            throw std::runtime_error("GlyphUploadHost::EnsurePageResources encountered zero-sized atlas page");
        }

        auto create_page_resource = [&]() -> PageResource {
            resource::ImageCreateInfo image_create_info{};
            image_create_info.image_type = VK_IMAGE_TYPE_2D;
            image_create_info.format = create_info_cache.atlas_format;
            image_create_info.extent = {page_view.width, page_view.height, 1U};
            image_create_info.mip_levels = 1U;
            image_create_info.array_layers = 1U;
            image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_create_info.usage = create_info_cache.image_usage;
            const auto& families = context_.QueueFamilies();
            if (families.graphics.has_value() &&
                families.transfer.has_value() &&
                families.graphics.value() != families.transfer.value()) {
                image_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
                image_create_info.queue_family_indices.push_back(families.graphics.value());
                image_create_info.queue_family_indices.push_back(families.transfer.value());
            }
            image_create_info.initial_layout = use_general_layout
                ? VK_IMAGE_LAYOUT_GENERAL
                : VK_IMAGE_LAYOUT_UNDEFINED;
            image_create_info.memory_properties = create_info_cache.memory_properties;
            image_create_info.create_default_view = true;
            image_create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
            image_create_info.default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;

            PageResource resource{};
            resource.image = resource::ImageHost::CreateImage(context_,
                                                              image_create_info,
                                                              *gpu_memory_host);
            resource.current_layout = image_create_info.initial_layout;
            resource.generation = page_view.generation;
            return resource;
        };

        if (page_index >= pages.size()) {
            pages.push_back(create_page_resource());
            continue;
        }

        PageResource& existing = pages[page_index];
        const bool dimension_mismatch =
            existing.image.extent.width != page_view.width ||
            existing.image.extent.height != page_view.height;
        const bool generation_changed =
            existing.generation != page_view.generation;

        if (dimension_mismatch || generation_changed) {
            RetirePageResource(existing, ComputeRetireValue());
            existing = create_page_resource();
        }
    }

    SyncBindlessPageResources();
    stats.page_count = static_cast<std::uint32_t>(pages.size());
}

void GlyphUploadHost::RetirePageResource(PageResource& page_resource_,
                                         std::uint64_t retire_value_) {
    if (bindless_config.Enabled() && page_resource_.image_slot.IsValid()) {
        bindless_config.descriptor_host->QueueBindlessPlaceholderWrite(bindless_config.image_table,
                                                                       page_resource_.image_slot);
        bindless_config.descriptor_host->FreeBindlessSlotDeferred(bindless_config.image_table,
                                                                  page_resource_.image_slot,
                                                                  retire_value_);
        page_resource_.image_slot = {};
        page_resource_.bindless_image_revision_written = 0U;
    }
    if (page_resource_.image.image == VK_NULL_HANDLE) {
        page_resource_.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        page_resource_.generation = 0U;
        return;
    }
    retired_pages.Retire(std::move(page_resource_.image), retire_value_);
    page_resource_.image = {};
    page_resource_.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    page_resource_.generation = 0U;
}

void GlyphUploadHost::InvalidateBindlessPageResources(const GlyphUploadHostBindlessConfig& bindless_config_) {
    const std::uint64_t retire_value = std::max(last_submitted_value_seen, completed_submit_value_seen);
    for (PageResource& page : pages) {
        if (bindless_config_.Enabled() && page.image_slot.IsValid()) {
            bindless_config_.descriptor_host->QueueBindlessPlaceholderWrite(bindless_config_.image_table,
                                                                            page.image_slot);
            bindless_config_.descriptor_host->FreeBindlessSlotDeferred(bindless_config_.image_table,
                                                                       page.image_slot,
                                                                       retire_value);
        }
        page.image_slot = {};
        page.bindless_image_revision_written = 0U;
    }
}

void GlyphUploadHost::SyncBindlessPageResources() {
    if (!bindless_config.Enabled()) {
        return;
    }

    for (PageResource& page : pages) {
        if (page.image.default_view == VK_NULL_HANDLE) {
            continue;
        }

        if (!page.image_slot.IsValid()) {
            page.image_slot =
                bindless_config.descriptor_host->AllocateBindlessSlot(bindless_config.image_table);
            page.bindless_image_revision_written = 0U;
        }

        if (page.bindless_image_revision_written == page.generation) {
            continue;
        }

        const VkImageLayout shader_read_layout =
            page.current_layout == VK_IMAGE_LAYOUT_UNDEFINED
                ? create_info_cache.shader_read_layout
                : page.current_layout;
        bindless_config.descriptor_host->QueueBindlessImageWrite(bindless_config.image_table,
                                                                 page.image_slot,
                                                                 page.image.default_view,
                                                                 shader_read_layout);
        page.bindless_image_revision_written = page.generation;
    }
}

void GlyphUploadHost::CollectRetiredPageResources(VulkanContext& context_,
                                                  std::uint64_t completed_submit_value_) {
    if (retired_pages.Empty()) {
        return;
    }
    if (context_.Device() == VK_NULL_HANDLE) {
        return;
    }
    (void)retired_pages.Collect(completed_submit_value_,
                                [&](resource::ImageResource& image_) {
                                    resource::ImageHost::DestroyImage(context_, image_);
                                });
}

void GlyphUploadHost::DestroyRetiredPageResources(VulkanContext& context_) noexcept {
    (void)retired_pages.Flush([&](resource::ImageResource& image_) {
        resource::ImageHost::DestroyImage(context_, image_);
    });
}

std::uint64_t GlyphUploadHost::ComputeRetireValue() const noexcept {
    const std::uint64_t max_seen = std::max(last_submitted_value_seen, completed_submit_value_seen);
    if (max_seen == std::numeric_limits<std::uint64_t>::max()) {
        return max_seen;
    }
    return max_seen + 1U;
}

void GlyphUploadHost::DestroyPageResources(VulkanContext& context_) noexcept {
    for (auto& page : pages) {
        resource::ImageHost::DestroyImage(context_, page.image);
        page.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        page.generation = 0U;
        page.image_slot = {};
        page.bindless_image_revision_written = 0U;
    }
}

void GlyphUploadHost::TransitionImageLayoutIfNeeded(render::UploadHost& upload_host_,
                                                    std::uint32_t frame_index_,
                                                    PageResource& page_resource_,
                                                    VkImageLayout new_layout_,
                                                    std::uint32_t& barrier_count_) const {
    if (page_resource_.current_layout == new_layout_) {
        return;
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout = page_resource_.current_layout;
    barrier.newLayout = new_layout_;
    barrier.image = page_resource_.image.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    switch (page_resource_.current_layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0U;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        default:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
    }

    switch (new_layout_) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        default:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
    }

    upload_host_.RecordImageBarrier2(frame_index_, barrier);
    page_resource_.current_layout = new_layout_;
    ++barrier_count_;
}

} // namespace vr::text
