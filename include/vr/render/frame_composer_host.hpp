#pragma once

#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <cstdint>
#include <functional>

namespace vr {
class VulkanContext;
}

namespace vr::render {

class RenderTargetHost;

struct FrameComposerHostCreateInfo final {
    const char* color_debug_name = "FrameComposerHdrColor";
    const char* depth_debug_name = "FrameComposerDepth";
    VkFormat hdr_color_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    RenderTargetLifetime color_lifetime = RenderTargetLifetime::persistent;
    RenderTargetLifetime depth_lifetime = RenderTargetLifetime::persistent;
    VkClearColorValue clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    bool clear_swapchain = true;
    bool enable_reinhard_tonemap = true;
    float exposure = 1.0F;
    float output_gamma = 2.2F;
    bool apply_manual_gamma = false;
};

struct FrameComposerHostStats final {
    std::uint32_t prepared_frame_count = 0U;
    std::uint32_t ready_frame_count = 0U;
    std::uint32_t swapchain_recreate_count = 0U;
    std::uint32_t tonemap_record_count = 0U;
    std::uint32_t tonemap_skipped_count = 0U;
    std::uint32_t revision = 0U;
};

class FrameComposerHost final {
public:
    using ImportedTextureRegisterFn = std::function<void(
        render_graph::ResourceHandle,
        RenderTargetHandle)>;

    void Initialize(const FrameComposerHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    [[nodiscard]] bool PrepareFrame(const FrameComposerPrepareView& prepare_view_);
    [[nodiscard]] bool OnSwapchainRecreated(VulkanContext& context_,
                                            RenderTargetHost& render_target_host_,
                                            VkExtent2D swapchain_extent_,
                                            std::uint64_t last_submitted_value_,
                                            std::uint64_t completed_submit_value_);

    void BuildRenderGraph(render_graph::RenderGraphBuilder& builder_,
                          render_graph::ResourceHandle present_target_,
                          const render_graph::Extent3D& reference_extent_,
                          render_graph::ResourceVersionHandle& present_ready_version_,
                          const ImportedTextureRegisterFn& register_imported_texture_);
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const FrameComposerHostStats& Stats() const noexcept;
    [[nodiscard]] const FrameComposerHostCreateInfo& CreateInfo() const noexcept;

private:
    [[nodiscard]] static bool HasNonZeroExtent(VkExtent2D extent_) noexcept;
    void AccumulateTonemapStats(std::uint32_t previous_draw_call_count_,
                                std::uint32_t previous_skipped_draw_count_) noexcept;

private:
    FrameComposerHostCreateInfo create_info_cache{};
    FrameComposerHostStats stats{};
    RenderTargetCompositeRenderer tonemap_renderer{};
    std::uint32_t prepared_frame_slot_count = 0U;
    bool initialized = false;
};

} // namespace vr::render
