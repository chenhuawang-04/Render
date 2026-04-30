#pragma once

#include "vr/render/render_target_types.hpp"

namespace vr::render {

struct RenderTargetViewDesc final {
    VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
    VkImageAspectFlags aspect = 0U;
    std::uint32_t base_mip_level = 0U;
    std::uint32_t level_count = 1U;
    std::uint32_t base_array_layer = 0U;
    std::uint32_t layer_count = 1U;

    [[nodiscard]] constexpr RenderTargetSubresourceRange SubresourceRange() const noexcept {
        return RenderTargetSubresourceRange{
            .aspect = aspect,
            .base_mip_level = base_mip_level,
            .level_count = level_count,
            .base_array_layer = base_array_layer,
            .layer_count = layer_count,
        };
    }
};

struct RenderTargetDescriptorKey final {
    RenderTargetHandle target{};
    RenderTargetViewDesc view{};
    RenderTargetStateKind expected_state = RenderTargetStateKind::shader_read;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    RenderTargetDescriptorUsage descriptor_usage = RenderTargetDescriptorUsage::sampled_image;
};

} // namespace vr::render
