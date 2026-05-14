#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "Center/Memory/Vulkan/Types.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vr::resource {

class GpuMemoryHost;

template<typename T>
using ImageMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ImageCreateInfo {
    VkImageCreateFlags flags = 0U;
    VkImageType image_type = VK_IMAGE_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{1U, 1U, 1U};
    uint32_t mip_levels = 1U;
    uint32_t array_layers = 1U;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0U;
    VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    ImageMcVector<uint32_t> queue_family_indices{};
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    bool create_default_view = false;
    VkImageViewCreateFlags default_view_flags = 0U;
    VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    VkImageAspectFlags default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t default_base_mip_level = 0U;
    uint32_t default_level_count = VK_REMAINING_MIP_LEVELS;
    uint32_t default_base_array_layer = 0U;
    uint32_t default_layer_count = VK_REMAINING_ARRAY_LAYERS;
};

struct ImageResource {
    VkImage image = VK_NULL_HANDLE;
    Center::Memory::Vulkan::Slice allocation_slice{};
    GpuMemoryHost* memory_host = nullptr;
    VkImageView default_view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    uint32_t mip_levels = 1U;
    uint32_t array_layers = 1U;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0U;
    VkMemoryPropertyFlags memory_properties = 0U;
    uint32_t memory_type_index = 0U;
};

class ImageHost final {
public:
    ImageHost() = delete;

    [[nodiscard]] static ImageResource CreateImage(VulkanContext& context_,
                                                   const ImageCreateInfo& create_info_,
                                                   GpuMemoryHost& gpu_memory_host_);

    static void DestroyImage(VulkanContext& context_,
                             ImageResource& resource_);

    [[nodiscard]] static VkImageView CreateView(VulkanContext& context_,
                                                VkImage image_,
                                                const VkImageViewCreateInfo& create_info_);

    static void DestroyView(VulkanContext& context_,
                            VkImageView& view_);
};

} // namespace vr::resource

