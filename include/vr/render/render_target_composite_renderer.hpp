#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/resource/sampler_host.hpp"

#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render_graph {
class RenderGraphBuilder;
}

namespace vr::render {

struct RenderTargetCompositeRendererCreateInfo final {
    bool clear_swapchain = true;
    VkClearColorValue clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
    bool enable_reinhard_tonemap = true;
    float exposure = 1.0F;
    float output_gamma = 2.2F;
    bool apply_manual_gamma = false;
};

struct RenderTargetCompositeRendererStats final {
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_draw_count = 0U;
};

class RenderTargetCompositeRenderer final {
public:
    void Initialize(const RenderTargetCompositeRendererCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetSourceTarget(RenderTargetHandle source_target_,
                         RenderTargetStateKind expected_source_state_ = RenderTargetStateKind::shader_read) noexcept;
    void ClearSourceTarget() noexcept;
    void SetOutputTargetConfig(const RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;

    void PrepareFrame(const RenderTargetCompositeRendererPrepareView& prepare_view_);
    void Record(const FrameRecordContext& record_context_);
    void DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                         render_graph::PassHandle pass_) const;
    [[nodiscard]] render_graph::RasterColorAttachmentDesc BuildGraphColorAttachmentDesc(
        render_graph::ResourceHandle output_target_,
        bool has_previous_content_) const noexcept;
    void RecordGraphPass(render_graph::GraphCommandContext& context_,
                         render_graph::ResourceHandle source_color_,
                         render_graph::ResourceHandle output_target_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RenderTargetCompositeRendererStats& Stats() const noexcept;

private:
    struct PushConstants final {
        float exposure = 1.0F;
        float inv_gamma = 1.0F;
        std::uint32_t flags = 0U;
        std::uint32_t texture_slot = 0U;
        std::uint32_t sampler_slot = 0U;
        std::uint32_t reserved0 = 0U;
    };
    static_assert(sizeof(PushConstants) == 24U);

    void EnsurePipelineObjects(VulkanContext& context_,
                               DescriptorHost& descriptor_host_,
                               PipelineHost& pipeline_host_,
                               VkFormat color_format_);
    void BindAndDrawFullscreen(VkCommandBuffer command_buffer_,
                               const RenderTargetResolvedView& source_view_,
                               VkFormat output_format_,
                               VkExtent2D output_extent_,
                               BindlessSlot source_texture_slot_,
                               BindlessSlot sampler_slot_,
                               render_graph::GraphCommandContext* graph_context_ = nullptr);

private:
    RenderTargetCompositeRendererCreateInfo create_info_cache{};
    RenderTargetCompositeRendererStats stats{};

    VulkanContext* context = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    BindlessResourceSystem* bindless_resources = nullptr;

    PipelineLayoutId pipeline_layout_id{};
    ShaderModuleId shader_vertex_id{};
    ShaderModuleId shader_fragment_id{};
    GraphicsPipelineId pipeline_id{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    RenderTargetHandle source_target{};
    RenderTargetStateKind source_expected_state = RenderTargetStateKind::shader_read;
    RenderTargetColorOutputConfig output_target_config{};
    BindlessSlot source_texture_slot{};
    BindlessSlot sampler_slot{};

    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    bool initialized = false;
};

} // namespace vr::render

