#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/scene_prepare_views.hpp"
#include "vr/resource/sampler_host.hpp"

#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render_graph {
class GraphCommandContext;
class RenderGraphBuilder;
}

namespace vr::render {

struct RenderTargetBloomRendererCreateInfo final {
    bool clear_swapchain = true;
    VkClearColorValue clear_color = {{0.02F, 0.02F, 0.03F, 1.0F}};
    float bloom_threshold = 1.0F;
    float bloom_knee = 0.5F;
    float bloom_intensity = 0.75F;
    float blur_filter_scale = 1.0F;
    float downsample_scale = 0.5F;
    std::uint32_t blur_pass_pair_count = 1U;
    bool enable_reinhard_tonemap = true;
    float exposure = 1.0F;
    float output_gamma = 2.2F;
    bool apply_manual_gamma = false;
    VkFormat intermediate_format = VK_FORMAT_UNDEFINED;
    RenderTargetColorEncoding intermediate_color_encoding = RenderTargetColorEncoding::linear;
};

struct RenderTargetBloomRendererStats final {
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t prefilter_draw_call_count = 0U;
    std::uint32_t blur_draw_call_count = 0U;
    std::uint32_t combine_draw_call_count = 0U;
    std::uint32_t skipped_draw_count = 0U;
    std::uint32_t pass_count = 0U;
    std::uint32_t transient_target_count = 0U;
    std::uint32_t transient_reuse_count = 0U;
};

class RenderTargetBloomRenderer final {
public:
    void Initialize(const RenderTargetBloomRendererCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void PrepareGraphFrame(const SceneRecorder3DPrepareView& prepare_view_);
    void DescribeGraphSingleSourceBindings(render_graph::RenderGraphBuilder& builder_,
                                           render_graph::PassHandle pass_) const;
    void DescribeGraphDualSourceBindings(render_graph::RenderGraphBuilder& builder_,
                                         render_graph::PassHandle pass_) const;
    void RecordGraphPrefilterPass(render_graph::GraphCommandContext& context_,
                                  render_graph::ResourceHandle scene_source_,
                                  render_graph::ResourceHandle bloom_target_);
    void RecordGraphBlurPass(render_graph::GraphCommandContext& context_,
                             render_graph::ResourceHandle input_target_,
                             render_graph::ResourceHandle output_target_);
    void RecordGraphCombinePass(render_graph::GraphCommandContext& context_,
                                render_graph::ResourceHandle scene_source_,
                                render_graph::ResourceHandle bloom_target_,
                                render_graph::ResourceHandle output_target_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RenderTargetBloomRendererStats& Stats() const noexcept;
    [[nodiscard]] const RenderTargetBloomRendererCreateInfo& CreateInfo() const noexcept;

private:
    void ResetPreparedFrameState() noexcept;
    void BindPreparedFrameServices(VulkanContext& context_,
                                   DescriptorHost& descriptor_host_,
                                   PipelineHost& pipeline_host_,
                                   RenderTargetHost& render_target_host_,
                                   resource::SamplerHost& sampler_host_,
                                   BindlessResourceSystem* bindless_) noexcept;
    struct PrefilterPushConstants final {
        float threshold = 1.0F;
        float knee = 0.5F;
        std::uint32_t texture_slot = 0U;
        std::uint32_t sampler_slot = 0U;
        float reserved0 = 0.0F;
        std::uint32_t reserved1 = 0U;
    };

    struct BlurPushConstants final {
        float texel_offset_x = 0.0F;
        float texel_offset_y = 0.0F;
        float filter_scale = 1.0F;
        std::uint32_t texture_slot = 0U;
        std::uint32_t sampler_slot = 0U;
        std::uint32_t reserved0 = 0U;
    };

    struct CombinePushConstants final {
        float exposure = 1.0F;
        float inv_gamma = 1.0F;
        float bloom_intensity = 0.75F;
        std::uint32_t flags = 0U;
        std::uint32_t scene_texture_slot = 0U;
        std::uint32_t bloom_texture_slot = 0U;
        std::uint32_t sampler_slot = 0U;
    };
    static_assert(sizeof(PrefilterPushConstants) == 24U);
    static_assert(sizeof(BlurPushConstants) == 24U);
    static_assert(sizeof(CombinePushConstants) == 28U);

    void EnsurePipelineObjects(VulkanContext& context_,
                               DescriptorHost& descriptor_host_,
                               PipelineHost& pipeline_host_,
                               VkFormat intermediate_format_,
                               VkFormat final_color_format_);
    [[nodiscard]] static VkFormat ResolveIntermediateFormat(VulkanContext& context_,
                                                            VkFormat requested_format_,
                                                            VkFormat source_format_);
    [[nodiscard]] static float SafeInvGamma(float gamma_) noexcept;

private:
    RenderTargetBloomRendererCreateInfo create_info_cache{};
    RenderTargetBloomRendererStats stats{};

    VulkanContext* context = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    BindlessResourceSystem* bindless_resources = nullptr;

    PipelineLayoutId single_source_pipeline_layout_id{};
    PipelineLayoutId dual_source_pipeline_layout_id{};
    ShaderModuleId fullscreen_vertex_shader_id{};
    ShaderModuleId prefilter_fragment_shader_id{};
    ShaderModuleId blur_fragment_shader_id{};
    ShaderModuleId combine_fragment_shader_id{};
    GraphicsPipelineId prefilter_pipeline_id{};
    GraphicsPipelineId blur_pipeline_id{};
    GraphicsPipelineId combine_pipeline_id{};
    VkFormat intermediate_pipeline_format = VK_FORMAT_UNDEFINED;
    VkFormat final_pipeline_color_format = VK_FORMAT_UNDEFINED;
    bool initialized = false;
};

} // namespace vr::render

