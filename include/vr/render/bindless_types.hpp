#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vr::render {

struct BindlessSlot {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return generation != 0U;
    }
};

struct BindlessTableId {
    std::uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct BindlessTableDesc {
    const char* debug_name = nullptr;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    std::uint32_t requested_capacity = 0U;
    VkShaderStageFlags stage_flags = VK_SHADER_STAGE_ALL;
    bool enable_partially_bound = true;
    bool enable_variable_descriptor_count = true;
    bool enable_update_after_bind = false;
    VkDescriptorImageInfo placeholder_image_info{};

    [[nodiscard]] bool HasPlaceholder() const noexcept {
        switch (descriptor_type) {
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return placeholder_image_info.imageView != VK_NULL_HANDLE;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            return placeholder_image_info.sampler != VK_NULL_HANDLE;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return placeholder_image_info.sampler != VK_NULL_HANDLE &&
                   placeholder_image_info.imageView != VK_NULL_HANDLE;
        default:
            break;
        }
        return false;
    }
};

struct BindlessTableStats {
    std::uint32_t capacity = 0U;
    std::uint32_t live_count = 0U;
    std::uint32_t pending_write_count = 0U;
    std::uint32_t deferred_free_count = 0U;
    bool has_placeholder = false;
};

} // namespace vr::render
