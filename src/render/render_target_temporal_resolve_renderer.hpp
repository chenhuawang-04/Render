#pragma once

#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/scene_prepare_views.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/resource/sampler_host.hpp"

namespace vr {
class VulkanContext;
}

namespace vr::render_graph {
class RenderGraphBuilder;
}

namespace vr::render::detail {

class RenderTargetTemporalResolveRenderer final {
public:
    void Initialize() noexcept;
    void Shutdown(VulkanContext& context_) noexcept;

    void ResetPreparedRuntimeState() noexcept;
    void PrepareGraphFrame(const SceneRecorder3DPrepareView& prepare_view_);
    void DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                         render_graph::PassHandle pass_) const;
    void RecordGraphPass(render_graph::GraphCommandContext& context_,
                         render_graph::ResourceHandle current_source_,
                         render_graph::ResourceHandle previous_source_,
                         render_graph::ResourceHandle previous_depth_source_,
                         render_graph::ResourceHandle motion_source_,
                         render_graph::ResourceHandle output_target_,
                         float current_weight_,
                         float previous_weight_,
                         float motion_rejection_begin_pixels_,
                         float motion_rejection_end_pixels_,
                         float depth_rejection_begin_,
                         float depth_rejection_end_,
                         bool reproject_history_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool HasPreparedRuntimeState() const noexcept;

private:
    struct PushConstants final {
        float current_weight = 1.0F;
        float previous_weight = 0.0F;
        float motion_rejection_begin_pixels = 0.75F;
        float motion_rejection_end_pixels = 2.5F;
        float depth_rejection_begin = 0.0015F;
        float depth_rejection_end = 0.02F;
        std::uint32_t flags = 0U;
        std::uint32_t current_texture_slot = 0U;
        std::uint32_t previous_texture_slot = 0U;
        std::uint32_t previous_depth_texture_slot = 0U;
        std::uint32_t motion_texture_slot = 0U;
        std::uint32_t sampler_slot = 0U;
        std::uint32_t target_width = 0U;
        std::uint32_t target_height = 0U;
        std::uint32_t reserved0 = 0U;
        std::uint32_t reserved1 = 0U;
    };
    static_assert(sizeof(PushConstants) == 64U);

    void EnsurePipelineObjects(VulkanContext& context_,
                               PipelineHost& pipeline_host_,
                               VkFormat output_format_);

private:
    VulkanContext* context = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    BindlessResourceSystem* bindless_resources = nullptr;

    PipelineLayoutId pipeline_layout_id{};
    ShaderModuleId fullscreen_vertex_shader_id{};
    ShaderModuleId fragment_shader_id{};
    GraphicsPipelineId pipeline_id{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    bool initialized = false;
};

} // namespace vr::render::detail
