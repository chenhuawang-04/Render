#include "vr/render/environment/background_pass_2d.hpp"

#include "vr/render/generated/background_2d_frag_spv.hpp"
#include "vr/render/generated/render_target_composite_vert_spv.hpp"

#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] bool SupportsFullscreenBackground(scene::Background2DMode mode_) noexcept {
    return mode_ == scene::Background2DMode::solid_color ||
           mode_ == scene::Background2DMode::gradient;
}

} // namespace

void BackgroundPass2D::Initialize(const BackgroundPass2DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    context = nullptr;
    pipeline_host = nullptr;
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    output_target_config = {};
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void BackgroundPass2D::Shutdown(VulkanContext& context_) {
    (void)context_;
    if (!initialized) {
        return;
    }

    stats = {};
    context = nullptr;
    pipeline_host = nullptr;
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    output_target_config = {};
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = false;
}

void BackgroundPass2D::SetOutputTargetConfig(
    const RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void BackgroundPass2D::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void BackgroundPass2D::PrepareFrame(const BackgroundPass2DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("BackgroundPass2D::PrepareFrame called before Initialize");
    }

    context = &prepare_view_.device;
    pipeline_host = &prepare_view_.pipeline;
    stats.prepare_count += 1U;
}

void BackgroundPass2D::Record(const FrameRecordContext& record_context_,
                              const RenderView2D& view_,
                              const scene::Background2DRenderState& background_state_) {
    if (!initialized) {
        throw std::runtime_error("BackgroundPass2D::Record called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("BackgroundPass2D::Record called before PrepareFrame");
    }
    if (!SupportsFullscreenBackground(background_state_.mode)) {
        ++stats.skipped_draw_count;
        return;
    }

    const ResolvedColorRenderPass color_pass = BuildColorRenderPass(record_context_,
                                                                    output_target_config,
                                                                    true,
                                                                    create_info_cache.fallback_clear_color,
                                                                    false);
    EnsurePipelineObjects(*context, *pipeline_host, color_pass.target.format);

    vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

    const VkViewport viewport = ToVkViewport(view_.viewport);
    const VkRect2D scissor = ToVkRect2D(view_.scissor);
    vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    vkCmdBindPipeline(record_context_.command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(pipeline_id));

    const PushBlock push_block{
        .color0 = background_state_.color0,
        .color1 = background_state_.color1,
        .opacity = background_state_.opacity,
        .mode = static_cast<std::uint32_t>(background_state_.mode),
        .reserved0 = 0.0F,
        .reserved1 = 0.0F,
    };
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushBlock),
                       &push_block);

    vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
    ++stats.draw_call_count;

    vkCmdEndRendering(record_context_.command_buffer);
    RecordEndColorPass(record_context_, output_target_config);
}

void BackgroundPass2D::RecordGraphPass(render_graph::GraphCommandContext& context_,
                                       const RenderView2D& view_,
                                       const scene::Background2DRenderState& background_state_,
                                       render_graph::ResourceHandle color_target_) {
    if (!initialized) {
        throw std::runtime_error("BackgroundPass2D::RecordGraphPass called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("BackgroundPass2D::RecordGraphPass called before PrepareFrame");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("BackgroundPass2D::RecordGraphPass requires valid command buffer");
    }
    if (!SupportsFullscreenBackground(background_state_.mode)) {
        ++stats.skipped_draw_count;
        return;
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    if (resolved_color.extent.width == 0U || resolved_color.extent.height == 0U) {
        throw std::runtime_error("BackgroundPass2D::RecordGraphPass resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context, *pipeline_host, resolved_color.format);

    const VkViewport viewport = ToVkViewport(view_.viewport);
    const VkRect2D scissor = ToVkRect2D(view_.scissor);
    vkCmdSetViewport(context_.CommandBuffer(), 0U, 1U, &viewport);
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(pipeline_id));

    const PushBlock push_block{
        .color0 = background_state_.color0,
        .color1 = background_state_.color1,
        .opacity = background_state_.opacity,
        .mode = static_cast<std::uint32_t>(background_state_.mode),
        .reserved0 = 0.0F,
        .reserved1 = 0.0F,
    };
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushBlock),
                       &push_block);

    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
    ++stats.draw_call_count;
}

void BackgroundPass2D::OnSwapchainRecreated(std::uint32_t,
                                            VkExtent2D extent_,
                                            VkFormat format_,
                                            std::uint64_t,
                                            std::uint64_t) {
    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool BackgroundPass2D::IsInitialized() const noexcept {
    return initialized;
}

const BackgroundPass2DStats& BackgroundPass2D::Stats() const noexcept {
    return stats;
}

void BackgroundPass2D::EnsurePipelineObjects(VulkanContext& context_,
                                             PipelineHost& pipeline_host_,
                                             VkFormat color_format_) {
    if (!shader_vertex_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_background_2d_frag_spv;
        shader_create_info.word_count = generated::k_background_2d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushBlock),
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

