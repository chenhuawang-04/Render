#include "vr/render/render_target_composite_renderer.hpp"

#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/generated/render_target_composite_frag_spv.hpp"
#include "vr/render/generated/render_target_composite_vert_spv.hpp"

#include <cmath>
#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] float SafeInvGamma(float gamma_) noexcept {
    if (!(gamma_ > 0.0F) || !std::isfinite(gamma_)) {
        return 1.0F;
    }
    return 1.0F / gamma_;
}

} // namespace

void RenderTargetCompositeRenderer::Initialize(
    const RenderTargetCompositeRendererCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    sampler_host = nullptr;
    bindless_resources = nullptr;
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    source_target = {};
    source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    source_texture_slot = {};
    sampler_slot = {};
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void RenderTargetCompositeRenderer::Shutdown(VulkanContext& context_) {
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
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    source_target = {};
    source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    source_texture_slot = {};
    sampler_slot = {};
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = false;
}

void RenderTargetCompositeRenderer::SetSourceTarget(
    RenderTargetHandle source_target_,
    RenderTargetStateKind expected_source_state_) noexcept {
    source_target = source_target_;
    source_expected_state = expected_source_state_;
}

void RenderTargetCompositeRenderer::ClearSourceTarget() noexcept {
    source_target = {};
    source_expected_state = RenderTargetStateKind::shader_read;
}

void RenderTargetCompositeRenderer::SetOutputTargetConfig(
    const RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void RenderTargetCompositeRenderer::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void RenderTargetCompositeRenderer::PrepareFrame(
    const RenderTargetCompositeRendererPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetCompositeRenderer::PrepareFrame called before Initialize");
    }

    context = &prepare_view_.device;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    render_target_host = &prepare_view_.render_target;
    sampler_host = &prepare_view_.sampler;
    bindless_resources = prepare_view_.bindless;
    source_texture_slot = {};
    sampler_slot = {};
    stats = {};

    if (!IsValidRenderTargetHandle(source_target) || !render_target_host->IsValid(source_target)) {
        return;
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return;
    }

    source_texture_slot = render_target_host->EnsureBindlessImageSlot(source_target);
    sampler_slot = bindless_resources->DefaultSamplerSlot();
}

void RenderTargetCompositeRenderer::Record(const FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetCompositeRenderer::Record called before Initialize");
    }
    if (context == nullptr ||
        descriptor_host == nullptr ||
        pipeline_host == nullptr ||
        render_target_host == nullptr) {
        throw std::runtime_error("RenderTargetCompositeRenderer::Record called before PrepareFrame");
    }
    if (!IsValidRenderTargetHandle(source_target) || !render_target_host->IsValid(source_target)) {
        ++stats.skipped_draw_count;
        return;
    }
    if (IsValidRenderTargetHandle(output_target_config.color_target) &&
        output_target_config.color_target.index == source_target.index &&
        output_target_config.color_target.generation == source_target.generation) {
        throw std::runtime_error("RenderTargetCompositeRenderer source target must differ from output target");
    }

    render_target_host->RecordTransition(record_context_.command_buffer,
                                         source_target,
                                         source_expected_state);

    const ResolvedColorRenderPass color_pass = BuildColorRenderPass(record_context_,
                                                                    output_target_config,
                                                                    create_info_cache.clear_swapchain,
                                                                    create_info_cache.clear_color,
                                                                    false);
    EnsurePipelineObjects(*context, *descriptor_host, *pipeline_host, color_pass.target.format);

    if (!source_texture_slot.IsValid() || !sampler_slot.IsValid()) {
        ++stats.skipped_draw_count;
        return;
    }

    vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(color_pass.target.extent.width);
    viewport.height = static_cast<float>(color_pass.target.extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = color_pass.target.extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    vkCmdBindPipeline(record_context_.command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(pipeline_id));
    const VkDescriptorSet bindless_sets[] = {
        bindless_resources->SampledImageSet(),
        bindless_resources->SamplerSet()
    };
    vkCmdBindDescriptorSets(record_context_.command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_host->GetPipelineLayout(pipeline_layout_id),
                            0U,
                            2U,
                            bindless_sets,
                            0U,
                            nullptr);
    stats.descriptor_set_bind_count += 2U;

    const RenderTargetResolvedView source_view = render_target_host->ResolveView(source_target);
    PushConstants push_constants{};
    push_constants.exposure = create_info_cache.exposure;
    push_constants.inv_gamma = SafeInvGamma(create_info_cache.output_gamma);
    push_constants.flags = 0U;
    push_constants.flags |= create_info_cache.enable_reinhard_tonemap ? 0x1U : 0U;
    push_constants.flags |= create_info_cache.apply_manual_gamma ? 0x2U : 0U;
    push_constants.flags |= (source_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x4U : 0U;
    push_constants.texture_slot = source_texture_slot.index;
    push_constants.sampler_slot = sampler_slot.index;
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
    ++stats.draw_call_count;

    vkCmdEndRendering(record_context_.command_buffer);
    RecordEndColorPass(record_context_, output_target_config);
}

void RenderTargetCompositeRenderer::OnSwapchainRecreated(std::uint32_t image_count_,
                                                         VkExtent2D extent_,
                                                         VkFormat format_) {
    (void)image_count_;
    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool RenderTargetCompositeRenderer::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetCompositeRendererStats& RenderTargetCompositeRenderer::Stats() const noexcept {
    return stats;
}

void RenderTargetCompositeRenderer::EnsurePipelineObjects(VulkanContext& context_,
                                                          DescriptorHost& descriptor_host_,
                                                          PipelineHost& pipeline_host_,
                                                          VkFormat color_format_) {
    (void)descriptor_host_;
    if (!shader_vertex_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_frag_spv;
        shader_create_info.word_count = generated::k_render_target_composite_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        if (bindless_resources == nullptr ||
            bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
            bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "RenderTargetCompositeRenderer::EnsurePipelineObjects requires bindless resource layouts");
        }
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants),
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_id.IsValid() && pipeline_color_format == color_format_) {
        return;
    }

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(shader_fragment_id),
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

    pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
}

} // namespace vr::render
