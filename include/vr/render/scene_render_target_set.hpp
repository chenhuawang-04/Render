#pragma once

#include "vr/render/render_target_types.hpp"

#include <cstdint>

namespace vr::render {

struct SceneRenderTargetSetCreateInfo final {
    const char* color_debug_name = "SceneColor";
    const char* depth_debug_name = "SceneDepth";
    RenderTargetScaleMode scale_mode = RenderTargetScaleMode::swapchain_relative;
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
    std::uint32_t depth = 1U;
    float width_scale = 1.0F;
    float height_scale = 1.0F;
    bool enable_depth = true;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags additional_color_usage = 0U;
    VkImageUsageFlags additional_depth_usage = 0U;
    RenderTargetLifetime color_lifetime = RenderTargetLifetime::persistent;
    RenderTargetLifetime depth_lifetime = RenderTargetLifetime::persistent;
    RenderTargetColorEncoding color_encoding = RenderTargetColorEncoding::linear;
    VkClearColorValue clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
};

enum class SceneRenderPassRole : std::uint8_t {
    single = 0U,
    first = 1U,
    middle = 2U,
    last = 3U
};

template<typename RendererT>
struct SceneRendererBinding final {
    RendererT* renderer = nullptr;
    SceneRenderPassRole pass_role = SceneRenderPassRole::single;
};

template<typename RendererT>
[[nodiscard]] constexpr SceneRendererBinding<RendererT> BindSceneRenderer(
    RendererT& renderer_,
    SceneRenderPassRole pass_role_) noexcept {
    return SceneRendererBinding<RendererT>{.renderer = &renderer_, .pass_role = pass_role_};
}

} // namespace vr::render
