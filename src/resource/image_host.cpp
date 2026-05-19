#include "vr/resource/image_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <sstream>
#include <stdexcept>

namespace vr::resource {

namespace {

[[nodiscard]] const char* VkResultName(VkResult result_) noexcept {
    switch (result_) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        default: return "VK_ERROR_UNKNOWN";
    }
}

void ThrowVk(const char* stage_, VkResult result_) {
    std::ostringstream oss;
    oss << stage_ << " failed: " << VkResultName(result_) << " (" << static_cast<int>(result_) << ")";
    throw std::runtime_error(oss.str());
}

void CheckVk(const char* stage_, VkResult result_) {
    if (result_ != VK_SUCCESS) {
        ThrowVk(stage_, result_);
    }
}

} // namespace

ImageResource ImageHost::CreateImageObject(VulkanContext& context_,
                                           const ImageCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("ImageHost::CreateImageObject requires initialized Vulkan device");
    }
    if (create_info_.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("ImageHost::CreateImageObject requires valid format");
    }
    if (create_info_.extent.width == 0U ||
        create_info_.extent.height == 0U ||
        create_info_.extent.depth == 0U) {
        throw std::runtime_error("ImageHost::CreateImageObject extent dimensions must be > 0");
    }
    if (create_info_.usage == 0U) {
        throw std::runtime_error("ImageHost::CreateImageObject requires non-zero usage flags");
    }
    if (create_info_.mip_levels == 0U || create_info_.array_layers == 0U) {
        throw std::runtime_error("ImageHost::CreateImageObject mip_levels/array_layers must be > 0");
    }

    ImageResource resource{};

    VkImageCreateInfo vk_create_info{};
    vk_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    vk_create_info.flags = create_info_.flags;
    vk_create_info.imageType = create_info_.image_type;
    vk_create_info.format = create_info_.format;
    vk_create_info.extent = create_info_.extent;
    vk_create_info.mipLevels = create_info_.mip_levels;
    vk_create_info.arrayLayers = create_info_.array_layers;
    vk_create_info.samples = create_info_.samples;
    vk_create_info.tiling = create_info_.tiling;
    vk_create_info.usage = create_info_.usage;
    vk_create_info.sharingMode = create_info_.sharing_mode;
    if (create_info_.sharing_mode == VK_SHARING_MODE_CONCURRENT) {
        if (create_info_.queue_family_indices.empty()) {
            throw std::runtime_error(
                "ImageHost::CreateImageObject concurrent sharing requires queue_family_indices");
        }
        vk_create_info.queueFamilyIndexCount = static_cast<uint32_t>(create_info_.queue_family_indices.size());
        vk_create_info.pQueueFamilyIndices = create_info_.queue_family_indices.data();
    } else {
        vk_create_info.queueFamilyIndexCount = 0U;
        vk_create_info.pQueueFamilyIndices = nullptr;
    }
    vk_create_info.initialLayout = create_info_.initial_layout;

    CheckVk("vkCreateImage",
            vkCreateImage(context_.Device(), &vk_create_info, nullptr, &resource.image));
    resource.format = create_info_.format;
    resource.extent = create_info_.extent;
    resource.mip_levels = create_info_.mip_levels;
    resource.array_layers = create_info_.array_layers;
    resource.samples = create_info_.samples;
    resource.usage = create_info_.usage;
    resource.tiling = create_info_.tiling;
    return resource;
}

ImageResource ImageHost::CreateImage(VulkanContext& context_,
                                     const ImageCreateInfo& create_info_,
                                     GpuMemoryHost& gpu_memory_host_) {
    if (!gpu_memory_host_.IsInitialized()) {
        throw std::runtime_error("ImageHost::CreateImage requires initialized GpuMemoryHost");
    }
    ImageResource resource = CreateImageObject(context_, create_info_);

    try {
        VkMemoryRequirements memory_requirements{};
        vkGetImageMemoryRequirements(context_.Device(), resource.image, &memory_requirements);

        const bool host_visible_requested =
            (create_info_.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
        const Center::Memory::Vulkan::HostAccess host_access = host_visible_requested
            ? Center::Memory::Vulkan::HostAccess::random_read_write
            : Center::Memory::Vulkan::HostAccess::none;

        const auto allocation_slice = gpu_memory_host_.AllocateImageMemory(
            memory_requirements,
            create_info_.tiling,
            create_info_.memory_properties,
            create_info_.memory_properties,
            host_visible_requested,
            Center::Memory::Vulkan::LifetimeHint::long_lived,
            host_access,
            false,
            false,
            resource.image);
        BindAllocation(resource, gpu_memory_host_, allocation_slice, true, 0U);

        if (create_info_.create_default_view) {
            VkImageViewCreateInfo default_view_info{};
            default_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            default_view_info.flags = create_info_.default_view_flags;
            default_view_info.image = resource.image;
            default_view_info.viewType = create_info_.default_view_type;
            default_view_info.format = create_info_.format;
            default_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            default_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            default_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            default_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            default_view_info.subresourceRange.aspectMask = create_info_.default_view_aspect;
            default_view_info.subresourceRange.baseMipLevel = create_info_.default_base_mip_level;
            default_view_info.subresourceRange.levelCount = create_info_.default_level_count;
            default_view_info.subresourceRange.baseArrayLayer = create_info_.default_base_array_layer;
            default_view_info.subresourceRange.layerCount = create_info_.default_layer_count;

            resource.default_view = CreateView(context_, resource.image, default_view_info);
        }
    } catch (...) {
        DestroyImage(context_, resource);
        throw;
    }

    return resource;
}

void ImageHost::BindAllocation(ImageResource& resource_,
                               GpuMemoryHost& gpu_memory_host_,
                               const Center::Memory::Vulkan::Slice& allocation_slice_,
                               const bool owns_allocation_,
                               const VkDeviceSize resource_offset_) {
    if (resource_.image == VK_NULL_HANDLE) {
        throw std::runtime_error("ImageHost::BindAllocation requires a created VkImage");
    }
    if (!gpu_memory_host_.IsInitialized()) {
        throw std::runtime_error("ImageHost::BindAllocation requires initialized GpuMemoryHost");
    }
    if (!allocation_slice_.valid()) {
        throw std::runtime_error("ImageHost::BindAllocation requires a valid allocation slice");
    }
    if (!gpu_memory_host_.BindImageMemory(resource_.image,
                                          allocation_slice_,
                                          resource_.tiling,
                                          resource_offset_)) {
        throw std::runtime_error("ImageHost::BindAllocation bind failed");
    }

    resource_.allocation_slice = allocation_slice_;
    resource_.memory_host = &gpu_memory_host_;
    resource_.memory_type_index = allocation_slice_.memory_type_index;
    resource_.memory_properties = gpu_memory_host_.QueryMemoryProperties(resource_.memory_type_index);
    resource_.owns_allocation = owns_allocation_;
}

void ImageHost::DestroyImage(VulkanContext& context_,
                             ImageResource& resource_) {
    const VkDevice device = context_.Device();
    if (device != VK_NULL_HANDLE) {
        if (resource_.default_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, resource_.default_view, nullptr);
            resource_.default_view = VK_NULL_HANDLE;
        }
        if (resource_.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, resource_.image, nullptr);
            resource_.image = VK_NULL_HANDLE;
        }
    }

    if (resource_.owns_allocation &&
        resource_.memory_host != nullptr &&
        resource_.allocation_slice.valid()) {
        resource_.memory_host->Deallocate(resource_.allocation_slice);
    }

    resource_.allocation_slice = {};
    resource_.memory_host = nullptr;
    resource_.format = VK_FORMAT_UNDEFINED;
    resource_.extent = {};
    resource_.mip_levels = 1U;
    resource_.array_layers = 1U;
    resource_.samples = VK_SAMPLE_COUNT_1_BIT;
    resource_.usage = 0U;
    resource_.tiling = VK_IMAGE_TILING_OPTIMAL;
    resource_.memory_properties = 0U;
    resource_.memory_type_index = 0U;
    resource_.owns_allocation = false;
}

VkImageView ImageHost::CreateView(VulkanContext& context_,
                                  VkImage image_,
                                  const VkImageViewCreateInfo& create_info_) {
    if (!context_.IsDeviceInitialized()) {
        throw std::runtime_error("ImageHost::CreateView requires initialized Vulkan device");
    }
    if (image_ == VK_NULL_HANDLE) {
        throw std::runtime_error("ImageHost::CreateView requires valid image handle");
    }

    VkImageViewCreateInfo view_info = create_info_;
    view_info.image = image_;
    if (view_info.sType == 0U) {
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    }

    VkImageView view = VK_NULL_HANDLE;
    CheckVk("vkCreateImageView",
            vkCreateImageView(context_.Device(), &view_info, nullptr, &view));
    return view;
}

void ImageHost::DestroyView(VulkanContext& context_,
                            VkImageView& view_) {
    if (view_ == VK_NULL_HANDLE) {
        return;
    }
    if (context_.Device() != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.Device(), view_, nullptr);
    }
    view_ = VK_NULL_HANDLE;
}

} // namespace vr::resource

