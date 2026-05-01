#pragma once

#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/render_target_format_utils.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_target_pool.hpp"
#include "vr/render/runtime_prepare_context.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

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
    RenderTargetStateKind color_final_state = RenderTargetStateKind::shader_read;
    RenderTargetStateKind depth_final_state = RenderTargetStateKind::depth_attachment;
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

class SceneRenderTargetSet final {
public:
    SceneRenderTargetSet() = default;
    ~SceneRenderTargetSet() = default;

    SceneRenderTargetSet(const SceneRenderTargetSet&) = delete;
    SceneRenderTargetSet& operator=(const SceneRenderTargetSet&) = delete;

    SceneRenderTargetSet(SceneRenderTargetSet&&) = delete;
    SceneRenderTargetSet& operator=(SceneRenderTargetSet&&) = delete;

    void Initialize(const SceneRenderTargetSetCreateInfo& create_info_ = {}) noexcept;
    void Reset() noexcept;

    void EnsureForSwapchain(VulkanContext& context_,
                            RenderTargetHost& render_target_host_,
                            VkExtent2D swapchain_extent_,
                            std::uint64_t last_submitted_value_,
                            std::uint64_t completed_submit_value_);
    void PrepareFrame(const RuntimePrepareContext& prepare_context_);
    void OnSwapchainRecreated(VulkanContext& context_,
                              RenderTargetHost& render_target_host_,
                              RenderTargetPool* render_target_pool_,
                              VkExtent2D swapchain_extent_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] RenderTargetColorOutputConfig BuildColorOutputConfig(bool clear_target_,
                                                                       bool final_pass_) const;
    [[nodiscard]] RenderTargetDepthOutputConfig BuildDepthOutputConfig(bool clear_target_) const;

    template<typename RendererT>
    void ConfigureSceneRenderer(RendererT& renderer_,
                                SceneRenderPassRole pass_role_) const {
        const bool clear_target = pass_role_ == SceneRenderPassRole::single ||
                                  pass_role_ == SceneRenderPassRole::first;
        const bool final_pass = pass_role_ == SceneRenderPassRole::single ||
                                pass_role_ == SceneRenderPassRole::last;
        ConfigureSceneRenderer(renderer_, clear_target, final_pass);
    }

    template<typename RendererT>
    void ConfigureSceneRenderer(RendererT& renderer_,
                                bool clear_target_,
                                bool final_pass_) const {
        renderer_.SetOutputTargetConfig(BuildColorOutputConfig(clear_target_, final_pass_));
        if (create_info_cache.enable_depth) {
            if constexpr (supports_depth_target_config<RendererT>) {
                renderer_.SetDepthTargetConfig(BuildDepthOutputConfig(clear_target_));
            } else {
                throw std::runtime_error(
                    "SceneRenderTargetSet depth target requested for renderer without depth target support");
            }
        }
    }

    template<typename RendererT>
    void ResetSceneRenderer(RendererT& renderer_) const {
        renderer_.ResetOutputTargetConfig();
        if (create_info_cache.enable_depth) {
            if constexpr (supports_depth_target_config<RendererT>) {
                renderer_.ResetDepthTargetConfig();
            }
        }
    }

    void ConfigureCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept;
    void ResetCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool HasDepthTarget() const noexcept;
    [[nodiscard]] RenderTargetHandle ColorTarget() const noexcept;
    [[nodiscard]] RenderTargetHandle DepthTarget() const noexcept;
    [[nodiscard]] VkFormat ColorFormat() const noexcept;
    [[nodiscard]] VkFormat DepthFormat() const noexcept;
    [[nodiscard]] const SceneRenderTargetSetCreateInfo& CreateInfo() const noexcept;

private:
    template<typename RendererT>
    static constexpr bool supports_depth_target_config =
        requires(RendererT& renderer_,
                 const RenderTargetDepthOutputConfig& depth_output_config_) {
            renderer_.SetDepthTargetConfig(depth_output_config_);
            renderer_.ResetDepthTargetConfig();
        };

    [[nodiscard]] static VkFormat ResolveColorFormat(VulkanContext& context_,
                                                     VkFormat requested_format_);
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_,
                                                     VkFormat requested_format_);
    void AcquireTransientTargets(VulkanContext& context_,
                                 RenderTargetHost& render_target_host_,
                                 RenderTargetPool& render_target_pool_,
                                 VkExtent2D swapchain_extent_);

private:
    SceneRenderTargetSetCreateInfo create_info_cache{};
    RenderTargetHandle color_target{};
    RenderTargetHandle depth_target{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    bool initialized = false;
};

} // namespace vr::render
