#pragma once

#include "vr/render/render_target_types.hpp"

namespace vr::render {

struct RenderTargetDesc final {
    const char* debug_name = nullptr;
    RenderTargetDimension dimension = RenderTargetDimension::image_2d;
    RenderTargetLifetime lifetime = RenderTargetLifetime::persistent;
    RenderTargetScaleMode scale_mode = RenderTargetScaleMode::absolute;
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
    std::uint32_t depth = 1U;
    float width_scale = 1.0F;
    float height_scale = 1.0F;
    VkImageCreateFlags flags = 0U;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0U;
    VkImageAspectFlags aspect = 0U;
    std::uint32_t mip_levels = 1U;
    std::uint32_t array_layers = 1U;
    RenderTargetColorEncoding color_encoding = RenderTargetColorEncoding::linear;
    RenderTargetMemoryPolicy memory_policy = RenderTargetMemoryPolicy::auto_select;
    bool allow_uav = false;
    bool allow_alias = false;
    bool allow_history = false;
};

struct ImportedRenderTargetDesc final {
    const char* debug_name = nullptr;
    RenderTargetOwnership ownership = RenderTargetOwnership::imported_image_imported_view;
    RenderTargetDimension dimension = RenderTargetDimension::image_2d;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{1U, 1U, 1U};
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0U;
    VkImageAspectFlags aspect = 0U;
    std::uint32_t mip_levels = 1U;
    std::uint32_t array_layers = 1U;
    RenderTargetColorEncoding color_encoding = RenderTargetColorEncoding::linear;
    RenderTargetStateKind initial_state = RenderTargetStateKind::undefined;
};

} // namespace vr::render

