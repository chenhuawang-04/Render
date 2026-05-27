#include "render_target_temporal_motion_renderer.hpp"

#include "vr/render/generated/render_target_composite_vert_spv.hpp"
#include "vr/render/generated/render_target_temporal_motion_frag_spv.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <stdexcept>

namespace vr::render::detail {

namespace {

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

void RenderTargetTemporalMotionRenderer::Initialize() noexcept {
    ResetPreparedRuntimeState();
    pipeline_layout_id = {};
    fullscreen_vertex_shader_id = {};
    fragment_shader_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void RenderTargetTemporalMotionRenderer::Shutdown(VulkanContext& context_) noexcept {
    (void)context_;
    if (!initialized) {
        return;
    }

    ResetPreparedRuntimeState();
    pipeline_layout_id = {};
    fullscreen_vertex_shader_id = {};
    fragment_shader_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    initialized = false;
}

void RenderTargetTemporalMotionRenderer::ResetPreparedRuntimeState() noexcept {
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    sampler_host = nullptr;
    bindless_resources = nullptr;
}

void RenderTargetTemporalMotionRenderer::PrepareGraphFrame(
    const SceneRecorder3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::PrepareGraphFrame called before Initialize");
    }
    if (prepare_view_.descriptor == nullptr ||
        prepare_view_.pipeline == nullptr ||
        prepare_view_.sampler == nullptr) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::PrepareGraphFrame requires descriptor, pipeline, and sampler services");
    }

    context = &prepare_view_.device;
    descriptor_host = prepare_view_.descriptor;
    pipeline_host = prepare_view_.pipeline;
    render_target_host = &prepare_view_.render_target;
    sampler_host = prepare_view_.sampler;
    bindless_resources = prepare_view_.bindless;
}

void RenderTargetTemporalMotionRenderer::DescribeGraphDescriptorBindings(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::DescribeGraphDescriptorBindings called before Initialize");
    }

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract(
            "render_target_temporal_motion.frag"));
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

void RenderTargetTemporalMotionRenderer::RecordGraphPass(
    render_graph::GraphCommandContext& context_,
    const render_graph::ResourceHandle depth_source_,
    const render_graph::ResourceHandle output_target_,
    const ecs::Matrix4x4& current_clip_to_previous_clip_,
    const float current_jitter_uv_x_,
    const float current_jitter_uv_y_,
    const float previous_jitter_uv_x_,
    const float previous_jitter_uv_y_,
    const bool has_previous_reprojection_) {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::RecordGraphPass called before Initialize");
    }
    if (!HasPreparedRuntimeState()) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::RecordGraphPass requires prepared runtime state");
    }

    const auto depth_target = context_.ResolveTextureTarget(depth_source_);
    const auto output_target = context_.ResolveTextureTarget(output_target_);
    if (!IsValidRenderTargetHandle(depth_target) || !render_target_host->IsValid(depth_target)) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::RecordGraphPass requires a valid depth source target");
    }
    if (!IsValidRenderTargetHandle(output_target) || !render_target_host->IsValid(output_target)) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::RecordGraphPass requires a valid output target");
    }

    const auto output_view = context_.ResolveTextureView(output_target_);
    if (output_view.extent.width == 0U || output_view.extent.height == 0U) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::RecordGraphPass resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context, *pipeline_host, output_view.format);

    const auto depth_slot = render_target_host->EnsureBindlessImageSlot(depth_target);
    const auto sampler_slot = bindless_resources->DefaultSamplerSlot();
    if (!depth_slot.IsValid() || !sampler_slot.IsValid()) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::RecordGraphPass requires valid bindless depth and sampler slots");
    }

    BindFullscreenViewportAndScissor(context_.CommandBuffer(),
                                     VkExtent2D{output_view.extent.width, output_view.extent.height});
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(pipeline_id));
    context_.BindCurrentPassDescriptorSets(
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_host->GetPipelineLayout(pipeline_layout_id),
        0U,
        2U);

    PushConstants push_constants{};
    push_constants.current_clip_to_previous_clip = current_clip_to_previous_clip_;
    push_constants.current_jitter_uv_x = current_jitter_uv_x_;
    push_constants.current_jitter_uv_y = current_jitter_uv_y_;
    push_constants.previous_jitter_uv_x = previous_jitter_uv_x_;
    push_constants.previous_jitter_uv_y = previous_jitter_uv_y_;
    push_constants.flags = has_previous_reprojection_ ? 0x1U : 0U;
    push_constants.depth_texture_slot = depth_slot.index;
    push_constants.sampler_slot = sampler_slot.index;
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
}

bool RenderTargetTemporalMotionRenderer::IsInitialized() const noexcept {
    return initialized;
}

bool RenderTargetTemporalMotionRenderer::HasPreparedRuntimeState() const noexcept {
    return context != nullptr &&
           descriptor_host != nullptr &&
           pipeline_host != nullptr &&
           render_target_host != nullptr &&
           sampler_host != nullptr &&
           bindless_resources != nullptr &&
           bindless_resources->IsInitialized();
}

void RenderTargetTemporalMotionRenderer::EnsurePipelineObjects(
    VulkanContext& context_,
    PipelineHost& pipeline_host_,
    const VkFormat output_format_) {
    if (bindless_resources == nullptr ||
        bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
        bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderTargetTemporalMotionRenderer::EnsurePipelineObjects requires bindless resource layouts");
    }

    if (!fullscreen_vertex_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        fullscreen_vertex_shader_id =
            pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!fragment_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_temporal_motion_frag_spv;
        shader_create_info.word_count = generated::k_render_target_temporal_motion_frag_spv_word_count;
        fragment_shader_id =
            pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants),
        });
        pipeline_layout_id =
            pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_id.IsValid() && pipeline_color_format == output_format_) {
        return;
    }

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(fullscreen_vertex_shader_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(fragment_shader_id),
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
    pipeline_desc.rendering.color_attachment_formats.push_back(output_format_);

    pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = output_format_;
}

} // namespace vr::render::detail
