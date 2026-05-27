#pragma once

#include "vr/ecs/component/spatial_types.hpp"
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

class RenderTargetTemporalMotionRenderer final {
public:
    void Initialize() noexcept;
    void Shutdown(VulkanContext& context_) noexcept;

    void ResetPreparedRuntimeState() noexcept;
    void PrepareGraphFrame(const SceneRecorder3DPrepareView& prepare_view_);
    void DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                         render_graph::PassHandle pass_) const;
    void RecordGraphPass(render_graph::GraphCommandContext& context_,
                         render_graph::ResourceHandle depth_source_,
                         render_graph::ResourceHandle output_target_,
                         const ecs::Matrix4x4& current_clip_to_previous_clip_,
                         float current_jitter_uv_x_,
                         float current_jitter_uv_y_,
                         float previous_jitter_uv_x_,
                         float previous_jitter_uv_y_,
                         bool has_previous_reprojection_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool HasPreparedRuntimeState() const noexcept;

private:
    struct PushConstants final {
        ecs::Matrix4x4 current_clip_to_previous_clip{};
        float current_jitter_uv_x = 0.0F;
        float current_jitter_uv_y = 0.0F;
        float previous_jitter_uv_x = 0.0F;
        float previous_jitter_uv_y = 0.0F;
        std::uint32_t flags = 0U;
        std::uint32_t depth_texture_slot = 0U;
        std::uint32_t sampler_slot = 0U;
        std::uint32_t reserved0 = 0U;
    };
    static_assert(sizeof(PushConstants) == 96U);

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
