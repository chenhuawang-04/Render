#include "render_target_temporal_resolve_renderer.hpp"

#include "vr/render/generated/render_target_composite_vert_spv.hpp"
#include "vr/render/generated/render_target_temporal_resolve_frag_spv.hpp"
#include "vr/render_graph/render_graph_builder.hpp"

#include <algorithm>
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

void RenderTargetTemporalResolveRenderer::Initialize() noexcept {
    ResetPreparedRuntimeState();
    pipeline_layout_id = {};
    fullscreen_vertex_shader_id = {};
    fragment_shader_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void RenderTargetTemporalResolveRenderer::Shutdown(VulkanContext& context_) noexcept {
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

void RenderTargetTemporalResolveRenderer::ResetPreparedRuntimeState() noexcept {
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    sampler_host = nullptr;
    bindless_resources = nullptr;
}

void RenderTargetTemporalResolveRenderer::PrepareGraphFrame(
    const SceneRecorder3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::PrepareGraphFrame called before Initialize");
    }
    if (prepare_view_.descriptor == nullptr ||
        prepare_view_.pipeline == nullptr ||
        prepare_view_.sampler == nullptr) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::PrepareGraphFrame requires descriptor, pipeline, and sampler services");
    }

    context = &prepare_view_.device;
    descriptor_host = prepare_view_.descriptor;
    pipeline_host = prepare_view_.pipeline;
    render_target_host = &prepare_view_.render_target;
    sampler_host = prepare_view_.sampler;
    bindless_resources = prepare_view_.bindless;
}

void RenderTargetTemporalResolveRenderer::DescribeGraphDescriptorBindings(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::DescribeGraphDescriptorBindings called before Initialize");
    }

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract(
            "render_target_temporal_resolve.frag"));
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

void RenderTargetTemporalResolveRenderer::RecordGraphPass(
    render_graph::GraphCommandContext& context_,
    const render_graph::ResourceHandle current_source_,
    const render_graph::ResourceHandle previous_source_,
    const render_graph::ResourceHandle previous_depth_source_,
    const render_graph::ResourceHandle motion_source_,
    const render_graph::ResourceHandle output_target_,
    const float current_weight_,
    const float previous_weight_,
    const float motion_rejection_begin_pixels_,
    const float motion_rejection_end_pixels_,
    const float depth_rejection_begin_,
    const float depth_rejection_end_,
    const bool reproject_history_) {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass called before Initialize");
    }
    if (!HasPreparedRuntimeState()) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass requires prepared runtime state");
    }

    const auto current_target = context_.ResolveTextureTarget(current_source_);
    const auto previous_target = context_.ResolveTextureTarget(previous_source_);
    const auto previous_depth_target =
        context_.ResolveTextureTarget(previous_depth_source_);
    const auto motion_target = context_.ResolveTextureTarget(motion_source_);
    const auto output_target = context_.ResolveTextureTarget(output_target_);
    if (!IsValidRenderTargetHandle(current_target) || !render_target_host->IsValid(current_target)) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass requires a valid current source target");
    }
    if (!IsValidRenderTargetHandle(previous_target) || !render_target_host->IsValid(previous_target)) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass requires a valid previous source target");
    }
    if (!IsValidRenderTargetHandle(previous_depth_target) ||
        !render_target_host->IsValid(previous_depth_target)) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass requires a valid previous depth target");
    }
    if (!IsValidRenderTargetHandle(motion_target) || !render_target_host->IsValid(motion_target)) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass requires a valid motion source target");
    }
    if (!IsValidRenderTargetHandle(output_target) || !render_target_host->IsValid(output_target)) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass requires a valid output target");
    }

    const auto current_view = context_.ResolveTextureView(current_source_);
    const auto previous_view = context_.ResolveTextureView(previous_source_);
    const auto output_view = context_.ResolveTextureView(output_target_);
    if (output_view.extent.width == 0U || output_view.extent.height == 0U) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context, *pipeline_host, output_view.format);

    const auto current_slot = render_target_host->EnsureBindlessImageSlot(current_target);
    const auto previous_slot = render_target_host->EnsureBindlessImageSlot(previous_target);
    const auto previous_depth_slot =
        render_target_host->EnsureBindlessImageSlot(previous_depth_target);
    const auto motion_slot = render_target_host->EnsureBindlessImageSlot(motion_target);
    const auto sampler_slot = bindless_resources->DefaultSamplerSlot();
    if (!current_slot.IsValid() ||
        !previous_slot.IsValid() ||
        !previous_depth_slot.IsValid() ||
        !motion_slot.IsValid() ||
        !sampler_slot.IsValid()) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::RecordGraphPass requires valid bindless texture and sampler slots");
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
    push_constants.current_weight = (std::max)(current_weight_, 0.0F);
    push_constants.previous_weight = (std::max)(previous_weight_, 0.0F);
    push_constants.motion_rejection_begin_pixels =
        (std::max)(motion_rejection_begin_pixels_, 0.0F);
    push_constants.motion_rejection_end_pixels =
        (std::max)(motion_rejection_end_pixels_,
                   push_constants.motion_rejection_begin_pixels);
    push_constants.depth_rejection_begin =
        (std::max)(depth_rejection_begin_, 0.0F);
    push_constants.depth_rejection_end =
        (std::max)(depth_rejection_end_,
                   push_constants.depth_rejection_begin);
    push_constants.flags = 0U;
    push_constants.flags |=
        (current_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x1U : 0U;
    push_constants.flags |=
        (previous_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x2U : 0U;
    push_constants.flags |= reproject_history_ ? 0x4U : 0U;
    push_constants.current_texture_slot = current_slot.index;
    push_constants.previous_texture_slot = previous_slot.index;
    push_constants.previous_depth_texture_slot = previous_depth_slot.index;
    push_constants.motion_texture_slot = motion_slot.index;
    push_constants.sampler_slot = sampler_slot.index;
    push_constants.target_width = output_view.extent.width;
    push_constants.target_height = output_view.extent.height;
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
}

bool RenderTargetTemporalResolveRenderer::IsInitialized() const noexcept {
    return initialized;
}

bool RenderTargetTemporalResolveRenderer::HasPreparedRuntimeState() const noexcept {
    return context != nullptr &&
           descriptor_host != nullptr &&
           pipeline_host != nullptr &&
           render_target_host != nullptr &&
           sampler_host != nullptr &&
           bindless_resources != nullptr &&
           bindless_resources->IsInitialized();
}

void RenderTargetTemporalResolveRenderer::EnsurePipelineObjects(
    VulkanContext& context_,
    PipelineHost& pipeline_host_,
    const VkFormat output_format_) {
    if (bindless_resources == nullptr ||
        bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
        bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderTargetTemporalResolveRenderer::EnsurePipelineObjects requires bindless resource layouts");
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
        shader_create_info.code_words = generated::k_render_target_temporal_resolve_frag_spv;
        shader_create_info.word_count = generated::k_render_target_temporal_resolve_frag_spv_word_count;
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
