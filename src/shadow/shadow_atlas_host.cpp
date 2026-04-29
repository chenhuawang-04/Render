#include "vr/shadow/shadow_atlas_host.hpp"

#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace vr::shadow {

void ShadowAtlasHost::Initialize(VulkanContext& context_,
                                 resource::GpuMemoryHost& gpu_memory_host_,
                                 const ShadowAtlasHostCreateInfo& create_info_) {
    Shutdown(context_);

    gpu_memory_host = &gpu_memory_host_;
    create_info_cache = create_info_;
    if (create_info_cache.reserve_atlas_count > 0U) {
        atlases.reserve(create_info_cache.reserve_atlas_count);
    }
    if (create_info_cache.reserve_retired_count > 0U) {
        retired_atlases.reserve(create_info_cache.reserve_retired_count);
    }

    stats = {};
    initialized = true;
}

void ShadowAtlasHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    for (auto& atlas : atlases) {
        DestroyAtlasRecord(context_, atlas);
    }
    atlases.clear();

    DestroyRetiredAtlases(context_);

    gpu_memory_host = nullptr;
    create_info_cache = {};
    stats = {};
    initialized = false;
}

void ShadowAtlasHost::BeginFrame(VulkanContext& context_,
                                 std::uint64_t completed_submit_value_) {
    if (!initialized) {
        return;
    }
    CollectRetiredAtlases(context_, completed_submit_value_);
}

void ShadowAtlasHost::EnsureAtlases(VulkanContext& context_,
                                    std::uint64_t last_submitted_value_,
                                    std::uint64_t completed_submit_value_,
                                    const ShadowAtlasRequest* requests_,
                                    std::uint32_t request_count_) {
    if (!initialized || gpu_memory_host == nullptr) {
        return;
    }
    CollectRetiredAtlases(context_, completed_submit_value_);

    if (requests_ == nullptr || request_count_ == 0U) {
        return;
    }

    for (std::uint32_t request_index = 0U; request_index < request_count_; ++request_index) {
        const ShadowAtlasRequest& request = requests_[request_index];
        if (request.namespace_id == 0U) {
            continue;
        }
        const std::uint16_t width = std::max<std::uint16_t>(request.width, 1U);
        const std::uint16_t height = std::max<std::uint16_t>(request.height, 1U);
        const std::uint16_t layer_count = std::max<std::uint16_t>(request.layer_count, 1U);

        AtlasRecord* existing = FindAtlas(request.namespace_id);
        if (existing == nullptr) {
            AtlasRecord atlas = CreateAtlasRecord(context_,
                                                  ShadowAtlasRequest{
                                                      .namespace_id = request.namespace_id,
                                                      .width = width,
                                                      .height = height,
                                                      .layer_count = layer_count,
                                                  });
            if (atlas.resource.image == VK_NULL_HANDLE) {
                continue;
            }
            const std::size_t insert_index = LowerBoundAtlasIndex(request.namespace_id);
            const std::size_t old_size = atlases.size();
            atlases.resize(old_size + 1U);
            for (std::size_t move_index = old_size; move_index > insert_index; --move_index) {
                atlases[move_index] = std::move(atlases[move_index - 1U]);
            }
            atlases[insert_index] = std::move(atlas);
            ++stats.created_atlas_count;
            ++stats.revision;
            continue;
        }

        const bool layout_changed = existing->current_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                                    existing->current_layout != VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL &&
                                    existing->current_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
                                    existing->current_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
                                    existing->current_layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL &&
                                    existing->current_layout != VK_IMAGE_LAYOUT_UNDEFINED;
        const bool incompatible = existing->width != width ||
                                  existing->height != height ||
                                  existing->layer_count != layer_count ||
                                  existing->format != create_info_cache.depth_format ||
                                  layout_changed;
        if (!incompatible) {
            continue;
        }

        RetireAtlas(*existing, last_submitted_value_);

        AtlasRecord replacement = CreateAtlasRecord(context_,
                                                    ShadowAtlasRequest{
                                                        .namespace_id = request.namespace_id,
                                                        .width = width,
                                                        .height = height,
                                                        .layer_count = layer_count,
                                                    });
        if (replacement.resource.image == VK_NULL_HANDLE) {
            *existing = {};
            continue;
        }
        *existing = replacement;
        ++stats.resized_atlas_count;
        ++stats.revision;
    }

    stats.atlas_count = static_cast<std::uint32_t>(atlases.size());
}

const ShadowAtlasHost::AtlasRecord* ShadowAtlasHost::FindAtlas(std::uint32_t namespace_id_) const noexcept {
    const std::size_t index = LowerBoundAtlasIndex(namespace_id_);
    if (index < atlases.size() && atlases[index].namespace_id == namespace_id_) {
        return &atlases[index];
    }
    return nullptr;
}

ShadowAtlasHost::AtlasRecord* ShadowAtlasHost::FindAtlas(std::uint32_t namespace_id_) noexcept {
    const std::size_t index = LowerBoundAtlasIndex(namespace_id_);
    if (index < atlases.size() && atlases[index].namespace_id == namespace_id_) {
        return &atlases[index];
    }
    return nullptr;
}

bool ShadowAtlasHost::IsInitialized() const noexcept {
    return initialized;
}

const ShadowAtlasHostStats& ShadowAtlasHost::Stats() const noexcept {
    return stats;
}

VkFormat ShadowAtlasHost::DepthFormat() const noexcept {
    return create_info_cache.depth_format;
}

std::size_t ShadowAtlasHost::LowerBoundAtlasIndex(std::uint32_t namespace_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = atlases.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (atlases[it].namespace_id < namespace_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void ShadowAtlasHost::DestroyAtlasViews(VulkanContext& context_,
                                        ShadowAtlasMcVector<VkImageView>& layer_views_) noexcept {
    for (auto& view : layer_views_) {
        resource::ImageHost::DestroyView(context_, view);
    }
    layer_views_.clear();
}

void ShadowAtlasHost::DestroyAtlasRecord(VulkanContext& context_,
                                         AtlasRecord& record_) noexcept {
    resource::ImageHost::DestroyView(context_, record_.array_view);
    DestroyAtlasViews(context_, record_.layer_views);
    resource::ImageHost::DestroyImage(context_, record_.resource);
    record_ = {};
}

void ShadowAtlasHost::RetireAtlas(AtlasRecord& record_,
                                  std::uint64_t retire_value_) {
    if (record_.resource.image == VK_NULL_HANDLE) {
        record_ = {};
        return;
    }

    RetiredAtlas retired{};
    retired.resource = record_.resource;
    retired.array_view = record_.array_view;
    retired.layer_views = std::move(record_.layer_views);
    retired.retire_value = retire_value_;
    retired_atlases.push_back(std::move(retired));
    ++stats.retired_atlas_count;
    ++stats.revision;

    record_ = {};
}

void ShadowAtlasHost::CollectRetiredAtlases(VulkanContext& context_,
                                            std::uint64_t completed_submit_value_) {
    if (retired_atlases.empty()) {
        return;
    }

    std::size_t write_index = 0U;
    for (std::size_t read_index = 0U; read_index < retired_atlases.size(); ++read_index) {
        RetiredAtlas& retired = retired_atlases[read_index];
        if (retired.retire_value <= completed_submit_value_) {
            DestroyAtlasViews(context_, retired.layer_views);
            resource::ImageHost::DestroyView(context_, retired.array_view);
            resource::ImageHost::DestroyImage(context_, retired.resource);
            ++stats.destroyed_atlas_count;
            continue;
        }
        if (write_index != read_index) {
            retired_atlases[write_index] = std::move(retired);
        }
        ++write_index;
    }
    retired_atlases.resize(write_index);
}

void ShadowAtlasHost::DestroyRetiredAtlases(VulkanContext& context_) noexcept {
    for (auto& retired : retired_atlases) {
        DestroyAtlasViews(context_, retired.layer_views);
        resource::ImageHost::DestroyView(context_, retired.array_view);
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_atlases.clear();
}

ShadowAtlasHost::AtlasRecord ShadowAtlasHost::CreateAtlasRecord(
    VulkanContext& context_,
    const ShadowAtlasRequest& request_) const {
    AtlasRecord record{};
    if (gpu_memory_host == nullptr) {
        return record;
    }

    resource::ImageCreateInfo image_create{};
    image_create.flags = 0U;
    image_create.image_type = VK_IMAGE_TYPE_2D;
    image_create.format = create_info_cache.depth_format;
    image_create.extent = VkExtent3D{
        .width = std::max<std::uint32_t>(request_.width, 1U),
        .height = std::max<std::uint32_t>(request_.height, 1U),
        .depth = 1U,
    };
    image_create.mip_levels = 1U;
    image_create.array_layers = std::max<std::uint32_t>(request_.layer_count, 1U);
    image_create.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create.usage = create_info_cache.usage;
    image_create.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    image_create.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create.memory_properties = create_info_cache.memory_properties;
    image_create.create_default_view = false;

    record.resource = resource::ImageHost::CreateImage(context_, image_create, *gpu_memory_host);
    if (record.resource.image == VK_NULL_HANDLE) {
        return {};
    }

    record.namespace_id = request_.namespace_id;
    record.width = request_.width;
    record.height = request_.height;
    record.layer_count = request_.layer_count;
    record.format = create_info_cache.depth_format;
    record.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    record.revision = 1U;
    record.layer_views.resize(record.layer_count);

    VkImageViewCreateInfo array_view_create{};
    array_view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    array_view_create.image = record.resource.image;
    array_view_create.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    array_view_create.format = record.format;
    array_view_create.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    array_view_create.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    array_view_create.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    array_view_create.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    array_view_create.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    array_view_create.subresourceRange.baseMipLevel = 0U;
    array_view_create.subresourceRange.levelCount = 1U;
    array_view_create.subresourceRange.baseArrayLayer = 0U;
    array_view_create.subresourceRange.layerCount = record.layer_count;
    record.array_view = resource::ImageHost::CreateView(context_,
                                                        record.resource.image,
                                                        array_view_create);

    for (std::uint16_t layer_index = 0U; layer_index < record.layer_count; ++layer_index) {
        VkImageViewCreateInfo view_create{};
        view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create.image = record.resource.image;
        view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create.format = record.format;
        view_create.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_create.subresourceRange.baseMipLevel = 0U;
        view_create.subresourceRange.levelCount = 1U;
        view_create.subresourceRange.baseArrayLayer = layer_index;
        view_create.subresourceRange.layerCount = 1U;
        record.layer_views[layer_index] = resource::ImageHost::CreateView(context_,
                                                                           record.resource.image,
                                                                           view_create);
    }

    return record;
}

} // namespace vr::shadow
