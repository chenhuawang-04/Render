#pragma once

#include "vr/vulkan_context.hpp"

#include <array>
#include <stdexcept>

namespace vr::render {

[[nodiscard]] inline bool IsColorAttachmentSampledFormatSupported(VulkanContext& context_,
                                                                  VkFormat format_) noexcept {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (properties.optimalTilingFeatures & required) == required;
}

[[nodiscard]] inline bool IsDepthStencilAttachmentFormatSupported(VulkanContext& context_,
                                                                  VkFormat format_) noexcept {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

[[nodiscard]] inline VkImageAspectFlags DepthStencilAspectMask(VkFormat format_) noexcept {
    switch (format_) {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
}

template<std::size_t candidate_count_v>
[[nodiscard]] inline VkFormat ResolveFirstSupportedColorAttachmentSampledFormat(
    VulkanContext& context_,
    const std::array<VkFormat, candidate_count_v>& candidates_) {
    for (VkFormat candidate : candidates_) {
        if (IsColorAttachmentSampledFormatSupported(context_, candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("No supported sampled color-attachment format found");
}

template<std::size_t candidate_count_v>
[[nodiscard]] inline VkFormat ResolveFirstSupportedDepthStencilFormat(
    VulkanContext& context_,
    const std::array<VkFormat, candidate_count_v>& candidates_) {
    for (VkFormat candidate : candidates_) {
        if (IsDepthStencilAttachmentFormatSupported(context_, candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("No supported depth-stencil attachment format found");
}

} // namespace vr::render
