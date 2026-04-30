#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>
#include <vulkan/vulkan.h>

namespace vr::render {

inline constexpr std::uint32_t invalid_render_target_index = (std::numeric_limits<std::uint32_t>::max)();
inline constexpr std::uint32_t k_max_render_target_color_attachments = 8U;

enum class RenderTargetLifetime : std::uint8_t {
    imported = 0U,
    persistent = 1U,
    history = 2U,
    transient = 3U,
};

enum class RenderTargetOwnership : std::uint8_t {
    owned = 0U,
    imported_image_owned_view = 1U,
    imported_image_imported_view = 2U,
};

enum class RenderTargetDimension : std::uint8_t {
    image_2d = 0U,
    image_2d_array = 1U,
    image_3d = 2U,
    cube = 3U,
    cube_array = 4U,
};

enum class RenderTargetScaleMode : std::uint8_t {
    absolute = 0U,
    swapchain_relative = 1U,
};

enum class RenderTargetColorEncoding : std::uint8_t {
    linear = 0U,
    srgb = 1U,
};

enum class RenderTargetMemoryPolicy : std::uint8_t {
    auto_select = 0U,
    device_local = 1U,
    transient_lazily_allocated = 2U,
};

enum class RenderTargetStateKind : std::uint8_t {
    undefined = 0U,
    color_attachment = 1U,
    depth_attachment = 2U,
    depth_read_only = 3U,
    shader_read = 4U,
    shader_write = 5U,
    transfer_src = 6U,
    transfer_dst = 7U,
    present_src = 8U,
};

enum class RenderTargetDescriptorUsage : std::uint8_t {
    sampled_image = 0U,
    combined_image_sampler = 1U,
    storage_image = 2U,
    input_attachment = 3U,
};

struct RenderTargetHandle final {
    std::uint32_t index = invalid_render_target_index;
    std::uint32_t generation = 0U;
};

inline constexpr RenderTargetHandle invalid_render_target_handle{};

struct RenderTargetSubresourceRange final {
    VkImageAspectFlags aspect = 0U;
    std::uint32_t base_mip_level = 0U;
    std::uint32_t level_count = 1U;
    std::uint32_t base_array_layer = 0U;
    std::uint32_t layer_count = 1U;
};

[[nodiscard]] constexpr bool IsValidRenderTargetHandle(RenderTargetHandle handle_) noexcept {
    return handle_.index != invalid_render_target_index &&
           handle_.generation != 0U;
}

[[nodiscard]] constexpr bool IsWholeImageDefaultRange(const RenderTargetSubresourceRange& range_) noexcept {
    return range_.base_mip_level == 0U &&
           range_.base_array_layer == 0U;
}

static_assert(std::is_standard_layout_v<RenderTargetHandle>);
static_assert(std::is_standard_layout_v<RenderTargetSubresourceRange>);

} // namespace vr::render
