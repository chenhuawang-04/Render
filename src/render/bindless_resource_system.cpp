#include "vr/render/bindless_resource_system.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <stdexcept>

namespace vr::render {

namespace {

struct ResolvedBindlessTableConfig final {
    std::uint32_t capacity = 1U;
    bool enable_update_after_bind = false;
};

[[nodiscard]] bool SupportsUpdateAfterBind(const DescriptorIndexingCaps& caps_,
                                           VkDescriptorType descriptor_type_) noexcept {
    switch (descriptor_type_) {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return caps_.sampled_image_update_after_bind;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        return caps_.sampler_update_after_bind;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return caps_.sampled_image_update_after_bind &&
               caps_.sampler_update_after_bind;
    default:
        return false;
    }
}

[[nodiscard]] std::uint32_t StageDescriptorLimit(const DescriptorIndexingCaps& caps_,
                                                 VkDescriptorType descriptor_type_,
                                                 bool update_after_bind_) noexcept {
    switch (descriptor_type_) {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return update_after_bind_
            ? caps_.max_update_after_bind_sampled_images
            : caps_.max_sampled_image_slots;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        return update_after_bind_
            ? caps_.max_update_after_bind_samplers
            : caps_.max_sampler_slots;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return update_after_bind_
            ? std::min(caps_.max_update_after_bind_sampled_images,
                       caps_.max_update_after_bind_samplers)
            : std::min(caps_.max_sampled_image_slots,
                       caps_.max_sampler_slots);
    default:
        return 0U;
    }
}

[[nodiscard]] std::uint32_t StageResourceLimit(const DescriptorIndexingCaps& caps_,
                                               bool update_after_bind_) noexcept {
    return update_after_bind_
        ? caps_.max_update_after_bind_resources
        : caps_.max_per_stage_resources;
}

[[nodiscard]] bool ShouldEnableUpdateAfterBind(const DescriptorIndexingCaps& caps_,
                                               VkDescriptorType descriptor_type_,
                                               BindlessUpdateAfterBindPolicy policy_) {
    switch (policy_) {
    case BindlessUpdateAfterBindPolicy::disabled:
        return false;
    case BindlessUpdateAfterBindPolicy::required:
        if (!SupportsUpdateAfterBind(caps_, descriptor_type_)) {
            throw std::runtime_error(
                "BindlessResourceSystem requires update-after-bind for the requested descriptor type");
        }
        return true;
    case BindlessUpdateAfterBindPolicy::auto_if_supported:
    default:
        return SupportsUpdateAfterBind(caps_, descriptor_type_);
    }
}

[[nodiscard]] ResolvedBindlessTableConfig ResolveBindlessTableConfig(
    const DescriptorIndexingCaps& caps_,
    VkDescriptorType descriptor_type_,
    std::uint32_t requested_capacity_,
    BindlessUpdateAfterBindPolicy update_after_bind_policy_) {
    ResolvedBindlessTableConfig resolved{};
    resolved.enable_update_after_bind =
        ShouldEnableUpdateAfterBind(caps_,
                                    descriptor_type_,
                                    update_after_bind_policy_);

    const std::uint32_t type_limit =
        StageDescriptorLimit(caps_,
                             descriptor_type_,
                             resolved.enable_update_after_bind);
    const std::uint32_t resource_limit =
        StageResourceLimit(caps_, resolved.enable_update_after_bind);

    std::uint32_t capacity = std::max(1U, requested_capacity_);
    if (type_limit > 0U) {
        capacity = std::min(capacity, type_limit);
    }
    if (resource_limit > 0U) {
        capacity = std::min(capacity, resource_limit);
    }

    resolved.capacity = std::max(1U, capacity);
    return resolved;
}

void ClampSharedStageResourceBudget(const DescriptorIndexingCaps& caps_,
                                    VkShaderStageFlags sampled_image_stage_flags_,
                                    VkShaderStageFlags sampler_stage_flags_,
                                    ResolvedBindlessTableConfig& sampled_images_,
                                    ResolvedBindlessTableConfig& samplers_) noexcept {
    if ((sampled_image_stage_flags_ & sampler_stage_flags_) == 0U) {
        return;
    }
    if (sampled_images_.enable_update_after_bind != samplers_.enable_update_after_bind) {
        return;
    }

    const std::uint32_t resource_limit =
        StageResourceLimit(caps_, sampled_images_.enable_update_after_bind);
    if (resource_limit == 0U) {
        return;
    }

    std::uint32_t sampler_capacity = std::max(1U, samplers_.capacity);
    if (sampler_capacity >= resource_limit) {
        sampler_capacity = std::max(1U, resource_limit - 1U);
    }

    const std::uint32_t remaining_for_images =
        resource_limit > sampler_capacity
            ? (resource_limit - sampler_capacity)
            : 1U;
    sampled_images_.capacity = std::max(1U, std::min(sampled_images_.capacity, remaining_for_images));
    samplers_.capacity = sampler_capacity;
}

void TransitionImage(VkCommandBuffer command_buffer_,
                     VkImage image_,
                     VkImageLayout old_layout_,
                     VkImageLayout new_layout_,
                     VkPipelineStageFlags src_stage_mask_,
                     VkAccessFlags src_access_mask_,
                     VkPipelineStageFlags dst_stage_mask_,
                     VkAccessFlags dst_access_mask_) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access_mask_;
    barrier.dstAccessMask = dst_access_mask_;
    barrier.oldLayout = old_layout_;
    barrier.newLayout = new_layout_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;
    vkCmdPipelineBarrier(command_buffer_,
                         src_stage_mask_,
                         dst_stage_mask_,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

resource::ImageResource CreatePlaceholderImage(VulkanContext& context_,
                                               resource::GpuMemoryHost& gpu_memory_host_) {
    resource::ImageCreateInfo image_create_info{};
    image_create_info.image_type = VK_IMAGE_TYPE_2D;
    image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_create_info.extent = VkExtent3D{1U, 1U, 1U};
    image_create_info.mip_levels = 1U;
    image_create_info.array_layers = 1U;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    image_create_info.create_default_view = true;
    image_create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    image_create_info.default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    image_create_info.default_base_mip_level = 0U;
    image_create_info.default_level_count = 1U;
    image_create_info.default_base_array_layer = 0U;
    image_create_info.default_layer_count = 1U;

    resource::ImageResource image =
        resource::ImageHost::CreateImage(context_, image_create_info, gpu_memory_host_);

    const VkCommandBuffer command_buffer = context_.BeginSingleTimeCommands();
    TransitionImage(command_buffer,
                    image.image,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    0U,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT);

    VkClearColorValue white{};
    white.float32[0] = 1.0F;
    white.float32[1] = 1.0F;
    white.float32[2] = 1.0F;
    white.float32[3] = 1.0F;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0U;
    range.levelCount = 1U;
    range.baseArrayLayer = 0U;
    range.layerCount = 1U;
    vkCmdClearColorImage(command_buffer,
                         image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &white,
                         1U,
                         &range);

    TransitionImage(command_buffer,
                    image.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
    context_.EndSingleTimeCommands(command_buffer);
    return image;
}

VkSampler CreateDefaultSampler(VulkanContext& context_,
                               resource::SamplerHost& sampler_host_) {
    resource::SamplerDesc sampler_desc{};
    sampler_desc.mag_filter = VK_FILTER_LINEAR;
    sampler_desc.min_filter = VK_FILTER_LINEAR;
    sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_desc.max_lod = 0.0F;
    return sampler_host_.AcquireSampler(context_, sampler_desc);
}

} // namespace

void BindlessResourceSystem::Initialize(VulkanContext& context_,
                                        resource::GpuMemoryHost& gpu_memory_host_,
                                        DescriptorHost& descriptor_host,
                                        resource::SamplerHost& sampler_host_,
                                        const BindlessResourceSystemCreateInfo& create_info_) {
    Shutdown(context_);

    if (!context_.DescriptorIndexingCapsInfo().enabled) {
        throw std::runtime_error(
            "BindlessResourceSystem::Initialize requires enabled descriptor indexing bindless features");
    }

    try {
        create_info_cache = create_info_;
        descriptor_host_ = &descriptor_host;
        placeholder_image_ = CreatePlaceholderImage(context_, gpu_memory_host_);
        placeholder_image_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        default_sampler_ = CreateDefaultSampler(context_, sampler_host_);

        const DescriptorIndexingCaps& descriptor_indexing_caps =
            context_.DescriptorIndexingCapsInfo();
        ResolvedBindlessTableConfig sampled_image_config =
            ResolveBindlessTableConfig(descriptor_indexing_caps,
                                       VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                       create_info_cache.sampled_image_capacity,
                                       create_info_cache.update_after_bind_policy);
        ResolvedBindlessTableConfig sampler_config =
            ResolveBindlessTableConfig(descriptor_indexing_caps,
                                       VK_DESCRIPTOR_TYPE_SAMPLER,
                                       create_info_cache.sampler_capacity,
                                       create_info_cache.update_after_bind_policy);
        ClampSharedStageResourceBudget(descriptor_indexing_caps,
                                       create_info_cache.sampled_image_stage_flags,
                                       create_info_cache.sampler_stage_flags,
                                       sampled_image_config,
                                       sampler_config);

        BindlessTableDesc image_table_desc{};
        image_table_desc.debug_name = "GlobalSampledImageTable";
        image_table_desc.descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        image_table_desc.requested_capacity = sampled_image_config.capacity;
        image_table_desc.stage_flags = create_info_cache.sampled_image_stage_flags;
        image_table_desc.enable_partially_bound = true;
        image_table_desc.enable_variable_descriptor_count = true;
        image_table_desc.enable_update_after_bind = sampled_image_config.enable_update_after_bind;
        image_table_desc.placeholder_image_info.imageView = placeholder_image_.default_view;
        image_table_desc.placeholder_image_info.imageLayout = placeholder_image_layout_;
        sampled_image_table_ = descriptor_host.CreateBindlessTable(context_, image_table_desc);

        BindlessTableDesc sampler_table_desc{};
        sampler_table_desc.debug_name = "GlobalSamplerTable";
        sampler_table_desc.descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLER;
        sampler_table_desc.requested_capacity = sampler_config.capacity;
        sampler_table_desc.stage_flags = create_info_cache.sampler_stage_flags;
        sampler_table_desc.enable_partially_bound = true;
        sampler_table_desc.enable_variable_descriptor_count = true;
        sampler_table_desc.enable_update_after_bind = sampler_config.enable_update_after_bind;
        sampler_table_desc.placeholder_image_info.sampler = default_sampler_;
        sampler_table_ = descriptor_host.CreateBindlessTable(context_, sampler_table_desc);

        placeholder_image_slot_ = BindlessSlot{.index = 0U, .generation = 1U};
        default_sampler_slot_ = BindlessSlot{.index = 0U, .generation = 1U};

        initialized_ = true;
        RefreshStats(&descriptor_host);
    } catch (...) {
        Shutdown(context_);
        throw;
    }
}

void BindlessResourceSystem::Shutdown(VulkanContext& context_) noexcept {
    if (placeholder_image_.image != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyImage(context_, placeholder_image_);
    }
    placeholder_image_ = {};

    create_info_cache = {};
    descriptor_host_ = nullptr;
    placeholder_image_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sampled_image_table_ = {};
    sampler_table_ = {};
    placeholder_image_slot_ = BindlessSlot{.index = 0U, .generation = 1U};
    default_sampler_slot_ = BindlessSlot{.index = 0U, .generation = 1U};
    default_sampler_ = VK_NULL_HANDLE;
    stats_ = {};
    initialized_ = false;
}

void BindlessResourceSystem::ConfigureTextureHost(asset::TextureHost& texture_host_) const noexcept {
    if (!initialized_) {
        texture_host_.ConfigureBindless({});
        return;
    }
    texture_host_.ConfigureBindless({
        .descriptor_host = descriptor_host_,
        .image_table = sampled_image_table_,
        .default_sampler_slot = default_sampler_slot_,
    });
}

void BindlessResourceSystem::ConfigureSurfaceImageHost(
    surface::SurfaceImageHost& surface_image_host_) const noexcept {
    if (!initialized_) {
        surface_image_host_.ConfigureBindless({});
        return;
    }
    surface_image_host_.ConfigureBindless({
        .descriptor_host = descriptor_host_,
        .image_table = sampled_image_table_,
    });
}

BindlessSlot BindlessResourceSystem::ResolveTextureImageSlot(const asset::TextureHost& texture_host_,
                                                             asset::TextureId texture_id_) const noexcept {
    if (!initialized_) {
        return {};
    }
    const BindlessSlot resolved = texture_host_.ResolveBindlessImageSlot(texture_id_);
    return resolved.IsValid() ? resolved : placeholder_image_slot_;
}

BindlessSlot BindlessResourceSystem::ResolveTextureSamplerSlot(const asset::TextureHost& texture_host_,
                                                               asset::TextureId texture_id_) const noexcept {
    if (!initialized_) {
        return {};
    }
    const BindlessSlot resolved = texture_host_.ResolveBindlessSamplerSlot(texture_id_);
    return resolved.IsValid() ? resolved : default_sampler_slot_;
}

VkDescriptorSet BindlessResourceSystem::SampledImageSet() const noexcept {
    return descriptor_host_ != nullptr && sampled_image_table_.IsValid()
        ? descriptor_host_->GetBindlessDescriptorSet(sampled_image_table_)
        : VK_NULL_HANDLE;
}

VkDescriptorSet BindlessResourceSystem::SamplerSet() const noexcept {
    return descriptor_host_ != nullptr && sampler_table_.IsValid()
        ? descriptor_host_->GetBindlessDescriptorSet(sampler_table_)
        : VK_NULL_HANDLE;
}

VkDescriptorSetLayout BindlessResourceSystem::SampledImageLayout() const noexcept {
    return descriptor_host_ != nullptr && sampled_image_table_.IsValid()
        ? descriptor_host_->GetBindlessLayout(sampled_image_table_)
        : VK_NULL_HANDLE;
}

VkDescriptorSetLayout BindlessResourceSystem::SamplerLayout() const noexcept {
    return descriptor_host_ != nullptr && sampler_table_.IsValid()
        ? descriptor_host_->GetBindlessLayout(sampler_table_)
        : VK_NULL_HANDLE;
}

BindlessTableId BindlessResourceSystem::SampledImageTable() const noexcept {
    return sampled_image_table_;
}

BindlessTableId BindlessResourceSystem::SamplerTable() const noexcept {
    return sampler_table_;
}

BindlessSlot BindlessResourceSystem::PlaceholderImageSlot() const noexcept {
    return placeholder_image_slot_;
}

BindlessSlot BindlessResourceSystem::DefaultSamplerSlot() const noexcept {
    return default_sampler_slot_;
}

VkImageLayout BindlessResourceSystem::PlaceholderImageLayout() const noexcept {
    return placeholder_image_layout_;
}

VkSampler BindlessResourceSystem::DefaultSampler() const noexcept {
    return default_sampler_;
}

const resource::ImageResource& BindlessResourceSystem::PlaceholderImage() const noexcept {
    return placeholder_image_;
}

const BindlessResourceSystemStats& BindlessResourceSystem::Stats() const noexcept {
    if (descriptor_host_ != nullptr) {
        const_cast<BindlessResourceSystem*>(this)->RefreshStats(descriptor_host_);
    }
    return stats_;
}

bool BindlessResourceSystem::IsInitialized() const noexcept {
    return initialized_;
}

void BindlessResourceSystem::RefreshStats(DescriptorHost* descriptor_host_) noexcept {
    stats_ = {};
    stats_.initialized = initialized_;
    stats_.sampled_image_table = sampled_image_table_;
    stats_.sampler_table = sampler_table_;
    stats_.placeholder_image_slot = placeholder_image_slot_;
    stats_.default_sampler_slot = default_sampler_slot_;
    if (descriptor_host_ != nullptr) {
        if (sampled_image_table_.IsValid()) {
            stats_.sampled_image = descriptor_host_->GetBindlessTableStats(sampled_image_table_);
        }
        if (sampler_table_.IsValid()) {
            stats_.sampler = descriptor_host_->GetBindlessTableStats(sampler_table_);
        }
    }
}

} // namespace vr::render
