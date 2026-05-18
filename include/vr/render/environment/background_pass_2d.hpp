#pragma once

#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/render_view.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/scene/background/sprite_background.hpp"

#include <cstdint>

namespace vr::render {

struct BackgroundPass2DCreateInfo final {
    VkClearColorValue fallback_clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
};

struct BackgroundPass2DStats final {
    std::uint32_t prepare_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_draw_count = 0U;
};

class BackgroundPass2D final {
public:
    void Initialize(const BackgroundPass2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetOutputTargetConfig(const RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;

    void PrepareFrame(const BackgroundPass2DPrepareView& prepare_view_);
    void Record(const FrameRecordContext& record_context_,
                const RenderView2D& view_,
                const scene::Background2DRenderState& background_state_);
    void RecordGraphPass(render_graph::GraphCommandContext& context_,
                         const RenderView2D& view_,
                         const scene::Background2DRenderState& background_state_,
                         render_graph::ResourceHandle color_target_);

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const BackgroundPass2DStats& Stats() const noexcept;

    struct PushBlock final {
        ecs::Float4 color0{};
        ecs::Float4 color1{};
        float opacity = 1.0F;
        std::uint32_t mode = 0U;
        float reserved0 = 0.0F;
        float reserved1 = 0.0F;
    };

    void EnsurePipelineObjects(VulkanContext& context_,
                               PipelineHost& pipeline_host_,
                               VkFormat color_format_);

private:
    BackgroundPass2DCreateInfo create_info_cache{};
    BackgroundPass2DStats stats{};

    VulkanContext* context = nullptr;
    PipelineHost* pipeline_host = nullptr;

    PipelineLayoutId pipeline_layout_id{};
    ShaderModuleId shader_vertex_id{};
    ShaderModuleId shader_fragment_id{};
    GraphicsPipelineId pipeline_id{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    RenderTargetColorOutputConfig output_target_config{};
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    bool initialized = false;
};

} // namespace vr::render

