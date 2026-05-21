#include "vr/render/render_target_bloom_renderer.hpp"

#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/generated/render_target_bloom_blur_frag_spv.hpp"
#include "vr/render/generated/render_target_bloom_combine_frag_spv.hpp"
#include "vr/render/generated/render_target_bloom_prefilter_frag_spv.hpp"
#include "vr/render/generated/render_target_composite_vert_spv.hpp"
#include "vr/render/render_target_format_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace vr::render {

namespace {

constexpr float k_min_downsample_scale = 0.125F;
constexpr float k_max_downsample_scale = 1.0F;

[[nodiscard]] float ClampPositive(float value_,
                                  float minimum_,
                                  float maximum_) noexcept {
    if (!std::isfinite(value_)) {
        return minimum_;
    }
    return std::clamp(value_, minimum_, maximum_);
}

[[nodiscard]] VkExtent2D ResolveDownsampleExtent(const VkExtent3D& source_extent_,
                                                 float downsample_scale_) noexcept {
    const float clamped_scale = ClampPositive(downsample_scale_,
                                              k_min_downsample_scale,
                                              k_max_downsample_scale);
    const std::uint32_t width = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(source_extent_.width) * clamped_scale));
    const std::uint32_t height = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(source_extent_.height) * clamped_scale));
    return VkExtent2D{width, height};
}

[[nodiscard]] BindlessTableId ResolveSampledImageTableId(
    const BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SampledImageTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return BindlessResourceSystem::SampledImageTableContractId();
}

[[nodiscard]] BindlessTableId ResolveSamplerTableId(
    const BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SamplerTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return BindlessResourceSystem::SamplerTableContractId();
}

void BindFullscreenViewportAndScissor(VkCommandBuffer command_buffer_,
                                      VkExtent2D extent_) noexcept {
    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(command_buffer_, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = extent_;
    vkCmdSetScissor(command_buffer_, 0U, 1U, &scissor);
}

} // namespace

void RenderTargetBloomRenderer::Initialize(const RenderTargetBloomRendererCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    sampler_host = nullptr;
    bindless_resources = nullptr;
    single_source_pipeline_layout_id = {};
    dual_source_pipeline_layout_id = {};
    fullscreen_vertex_shader_id = {};
    prefilter_fragment_shader_id = {};
    blur_fragment_shader_id = {};
    combine_fragment_shader_id = {};
    prefilter_pipeline_id = {};
    blur_pipeline_id = {};
    combine_pipeline_id = {};
    intermediate_pipeline_format = VK_FORMAT_UNDEFINED;
    final_pipeline_color_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void RenderTargetBloomRenderer::Shutdown(VulkanContext& context_) {
    (void)context_;
    if (!initialized) {
        return;
    }

    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    sampler_host = nullptr;
    bindless_resources = nullptr;
    single_source_pipeline_layout_id = {};
    dual_source_pipeline_layout_id = {};
    fullscreen_vertex_shader_id = {};
    prefilter_fragment_shader_id = {};
    blur_fragment_shader_id = {};
    combine_fragment_shader_id = {};
    prefilter_pipeline_id = {};
    blur_pipeline_id = {};
    combine_pipeline_id = {};
    intermediate_pipeline_format = VK_FORMAT_UNDEFINED;
    final_pipeline_color_format = VK_FORMAT_UNDEFINED;
    initialized = false;
}

void RenderTargetBloomRenderer::ResetPreparedFrameState() noexcept {
    stats = {};
}

void RenderTargetBloomRenderer::BindPreparedFrameServices(
    VulkanContext& context_,
    DescriptorHost& descriptor_host_,
    PipelineHost& pipeline_host_,
    RenderTargetHost& render_target_host_,
    resource::SamplerHost& sampler_host_,
    BindlessResourceSystem* bindless_) noexcept {
    context = &context_;
    descriptor_host = &descriptor_host_;
    pipeline_host = &pipeline_host_;
    render_target_host = &render_target_host_;
    sampler_host = &sampler_host_;
    bindless_resources = bindless_;
}

void RenderTargetBloomRenderer::PrepareGraphFrame(const SceneRecorder3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetBloomRenderer::PrepareGraphFrame called before Initialize");
    }
    if (prepare_view_.descriptor == nullptr ||
        prepare_view_.pipeline == nullptr ||
        prepare_view_.sampler == nullptr) {
        throw std::runtime_error(
            "RenderTargetBloomRenderer::PrepareGraphFrame requires descriptor, pipeline, and sampler services");
    }

    BindPreparedFrameServices(prepare_view_.device,
                              *prepare_view_.descriptor,
                              *prepare_view_.pipeline,
                              prepare_view_.render_target,
                              *prepare_view_.sampler,
                              prepare_view_.bindless);
    ResetPreparedFrameState();
}

void RenderTargetBloomRenderer::DescribeGraphSingleSourceBindings(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetBloomRenderer::DescribeGraphSingleSourceBindings called before Initialize");
    }
    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("render_target_bloom_single_source.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     0U,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     1U,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);
}

void RenderTargetBloomRenderer::DescribeGraphDualSourceBindings(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::PassHandle pass_) const {
    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("render_target_bloom_dual_source.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     0U,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     1U,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);
}

void RenderTargetBloomRenderer::RecordGraphPrefilterPass(render_graph::GraphCommandContext& context_,
                                                     render_graph::ResourceHandle scene_source_,
                                                     render_graph::ResourceHandle bloom_target_) {
    if (!initialized || context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        render_target_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("RenderTargetBloomRenderer::RecordGraphPrefilterPass requires prepared runtime state");
    }
    const auto source_target = context_.ResolveTextureTarget(scene_source_);
    const auto bloom_target = context_.ResolveTextureTarget(bloom_target_);
    const auto source_view = context_.ResolveTextureView(scene_source_);
    const auto bloom_view = context_.ResolveTextureView(bloom_target_);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          bloom_view.format,
                          bloom_view.format);

    const auto scene_slot = render_target_host->EnsureBindlessImageSlot(source_target);
    const auto sampler = bindless_resources->DefaultSamplerSlot();
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(prefilter_pipeline_id));
    context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                                           0U,
                                           2U);
    ++stats.descriptor_set_bind_count;
    BindFullscreenViewportAndScissor(context_.CommandBuffer(), VkExtent2D{bloom_view.extent.width, bloom_view.extent.height});

    const PrefilterPushConstants push_constants{
        .threshold = std::max(create_info_cache.bloom_threshold, 0.0F),
        .knee = std::max(create_info_cache.bloom_knee, 0.0001F),
        .texture_slot = scene_slot.index,
        .sampler_slot = sampler.index,
        .reserved0 = 0.0F,
        .reserved1 = 0U,
    };
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PrefilterPushConstants),
                       &push_constants);
    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
    ++stats.prefilter_draw_call_count;
    ++stats.pass_count;
}

void RenderTargetBloomRenderer::RecordGraphBlurPass(render_graph::GraphCommandContext& context_,
                                                    render_graph::ResourceHandle input_target_,
                                                    render_graph::ResourceHandle output_target_) {
    if (!initialized || context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        render_target_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("RenderTargetBloomRenderer::RecordGraphBlurPass requires prepared runtime state");
    }
    const auto input_target = context_.ResolveTextureTarget(input_target_);
    const auto input_view = context_.ResolveTextureView(input_target_);
    const auto output_view = context_.ResolveTextureView(output_target_);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          output_view.format,
                          output_view.format);

    const auto input_slot = render_target_host->EnsureBindlessImageSlot(input_target);
    const auto sampler = bindless_resources->DefaultSamplerSlot();
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(blur_pipeline_id));
    context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                                           0U,
                                           2U);
    ++stats.descriptor_set_bind_count;
    BindFullscreenViewportAndScissor(context_.CommandBuffer(), VkExtent2D{output_view.extent.width, output_view.extent.height});

    const BlurPushConstants push_constants{
        .texel_offset_x = (input_view.extent.width > 0U)
            ? (1.0F / static_cast<float>(input_view.extent.width))
            : 0.0F,
        .texel_offset_y = (input_view.extent.height > 0U)
            ? (1.0F / static_cast<float>(input_view.extent.height))
            : 0.0F,
        .filter_scale = std::max(create_info_cache.blur_filter_scale, 0.0F),
        .texture_slot = input_slot.index,
        .sampler_slot = sampler.index,
        .reserved0 = 0U,
    };
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(BlurPushConstants),
                       &push_constants);
    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
    ++stats.blur_draw_call_count;
    ++stats.pass_count;
}

void RenderTargetBloomRenderer::RecordGraphCombinePass(render_graph::GraphCommandContext& context_,
                                                       render_graph::ResourceHandle scene_source_,
                                                       render_graph::ResourceHandle bloom_target_,
                                                       render_graph::ResourceHandle output_target_) {
    if (!initialized || context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        render_target_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("RenderTargetBloomRenderer::RecordGraphCombinePass requires prepared runtime state");
    }
    const auto scene_target = context_.ResolveTextureTarget(scene_source_);
    const auto bloom_target = context_.ResolveTextureTarget(bloom_target_);
    const auto output_view = context_.ResolveTextureView(output_target_);
    const auto scene_view = context_.ResolveTextureView(scene_source_);
    const auto bloom_view = context_.ResolveTextureView(bloom_target_);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          bloom_view.format,
                          output_view.format);

    const auto scene_slot = render_target_host->EnsureBindlessImageSlot(scene_target);
    const auto bloom_slot = render_target_host->EnsureBindlessImageSlot(bloom_target);
    const auto sampler = bindless_resources->DefaultSamplerSlot();
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(combine_pipeline_id));
    context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           pipeline_host->GetPipelineLayout(dual_source_pipeline_layout_id),
                                           0U,
                                           2U);
    ++stats.descriptor_set_bind_count;
    BindFullscreenViewportAndScissor(context_.CommandBuffer(), VkExtent2D{output_view.extent.width, output_view.extent.height});

    CombinePushConstants push_constants{};
    push_constants.exposure = std::max(create_info_cache.exposure, 0.0F);
    push_constants.inv_gamma = SafeInvGamma(create_info_cache.output_gamma);
    push_constants.bloom_intensity = std::max(create_info_cache.bloom_intensity, 0.0F);
    push_constants.flags = 0U;
    push_constants.flags |= create_info_cache.enable_reinhard_tonemap ? 0x1U : 0U;
    push_constants.flags |= create_info_cache.apply_manual_gamma ? 0x2U : 0U;
    push_constants.flags |= (scene_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x4U : 0U;
    push_constants.flags |= (bloom_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x8U : 0U;
    push_constants.scene_texture_slot = scene_slot.index;
    push_constants.bloom_texture_slot = bloom_slot.index;
    push_constants.sampler_slot = sampler.index;
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(dual_source_pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(CombinePushConstants),
                       &push_constants);
    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
    ++stats.combine_draw_call_count;
    ++stats.pass_count;
}

void RenderTargetBloomRenderer::OnSwapchainRecreated(std::uint32_t image_count_,
                                                     VkExtent2D extent_,
                                                     VkFormat format_) {
    (void)image_count_;
    (void)extent_;
    (void)format_;
}

bool RenderTargetBloomRenderer::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetBloomRendererStats& RenderTargetBloomRenderer::Stats() const noexcept {
    return stats;
}

const RenderTargetBloomRendererCreateInfo& RenderTargetBloomRenderer::CreateInfo() const noexcept {
    return create_info_cache;
}

void RenderTargetBloomRenderer::EnsurePipelineObjects(VulkanContext& context_,
                                                      DescriptorHost& descriptor_host_,
                                                      PipelineHost& pipeline_host_,
                                                      VkFormat intermediate_format_,
                                                      VkFormat final_color_format_) {
    (void)descriptor_host_;
    if (bindless_resources == nullptr ||
        bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
        bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderTargetBloomRenderer::EnsurePipelineObjects requires bindless resource layouts");
    }

    if (!fullscreen_vertex_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        fullscreen_vertex_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!prefilter_fragment_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_bloom_prefilter_frag_spv;
        shader_create_info.word_count = generated::k_render_target_bloom_prefilter_frag_spv_word_count;
        prefilter_fragment_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!blur_fragment_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_bloom_blur_frag_spv;
        shader_create_info.word_count = generated::k_render_target_bloom_blur_frag_spv_word_count;
        blur_fragment_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!combine_fragment_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_bloom_combine_frag_spv;
        shader_create_info.word_count = generated::k_render_target_bloom_combine_frag_spv_word_count;
        combine_fragment_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!single_source_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PrefilterPushConstants),
        });
        single_source_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }
    if (!dual_source_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(CombinePushConstants),
        });
        dual_source_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    const auto build_fullscreen_pipeline_desc = [&](PipelineLayoutId pipeline_layout_id_,
                                                    ShaderModuleId fragment_shader_id_,
                                                    VkFormat color_format_) {
        GraphicsPipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id_);
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = pipeline_host_.GetShaderModule(fullscreen_vertex_shader_id),
            .entry_name = "main",
            .flags = 0U,
            .specialization = {}
        });
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = pipeline_host_.GetShaderModule(fragment_shader_id_),
            .entry_name = "main",
            .flags = 0U,
            .specialization = {}
        });
        pipeline_desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipeline_desc.viewport.viewport_count = 1U;
        pipeline_desc.viewport.scissor_count = 1U;
        pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);
        pipeline_desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
        pipeline_desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pipeline_desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
        pipeline_desc.rasterization.line_width = 1.0F;
        pipeline_desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
        pipeline_desc.depth_stencil.depth_test_enable = false;
        pipeline_desc.depth_stencil.depth_write_enable = false;
        VkPipelineColorBlendAttachmentState blend_state{};
        blend_state.blendEnable = VK_FALSE;
        blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
        pipeline_desc.color_blend.attachments.push_back(blend_state);
        pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);
        return pipeline_desc;
    };

    if (!prefilter_pipeline_id.IsValid() || intermediate_pipeline_format != intermediate_format_) {
        GraphicsPipelineDesc pipeline_desc = build_fullscreen_pipeline_desc(single_source_pipeline_layout_id,
                                                                           prefilter_fragment_shader_id,
                                                                           intermediate_format_);
        prefilter_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    if (!blur_pipeline_id.IsValid() || intermediate_pipeline_format != intermediate_format_) {
        GraphicsPipelineDesc pipeline_desc = build_fullscreen_pipeline_desc(single_source_pipeline_layout_id,
                                                                           blur_fragment_shader_id,
                                                                           intermediate_format_);
        blur_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    if (!combine_pipeline_id.IsValid() || final_pipeline_color_format != final_color_format_) {
        GraphicsPipelineDesc pipeline_desc = build_fullscreen_pipeline_desc(dual_source_pipeline_layout_id,
                                                                           combine_fragment_shader_id,
                                                                           final_color_format_);
        combine_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    intermediate_pipeline_format = intermediate_format_;
    final_pipeline_color_format = final_color_format_;
}

VkFormat RenderTargetBloomRenderer::ResolveIntermediateFormat(VulkanContext& context_,
                                                              VkFormat requested_format_,
                                                              VkFormat source_format_) {
    if (requested_format_ != VK_FORMAT_UNDEFINED) {
        if (!IsColorAttachmentSampledFormatSupported(context_, requested_format_)) {
            throw std::runtime_error("RenderTargetBloomRenderer requested intermediate format is unsupported");
        }
        return requested_format_;
    }

    if (source_format_ != VK_FORMAT_UNDEFINED &&
        IsColorAttachmentSampledFormatSupported(context_, source_format_)) {
        return source_format_;
    }

    constexpr std::array<VkFormat, 4U> defaults{
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM
    };
    return ResolveFirstSupportedColorAttachmentSampledFormat(context_, defaults);
}

float RenderTargetBloomRenderer::SafeInvGamma(float gamma_) noexcept {
    if (!(gamma_ > 0.0F) || !std::isfinite(gamma_)) {
        return 1.0F;
    }
    return 1.0F / gamma_;
}

} // namespace vr::render
