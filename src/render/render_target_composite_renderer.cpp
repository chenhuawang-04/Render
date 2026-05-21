#include "vr/render/render_target_composite_renderer.hpp"

#include "vr/render/bindless_resource_system.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
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
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = false;
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
    stats = {};
}

void RenderTargetCompositeRenderer::DescribeGraphDescriptorBindings(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "RenderTargetCompositeRenderer::DescribeGraphDescriptorBindings called before Initialize");
    }
    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("render_target_composite.frag"));
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

render_graph::RasterColorAttachmentDesc RenderTargetCompositeRenderer::BuildGraphColorAttachmentDesc(
    render_graph::ResourceHandle output_target_,
    bool has_previous_content_) const noexcept {
    const render_graph::AttachmentLoadOp load_op =
        (create_info_cache.clear_swapchain || !has_previous_content_)
            ? render_graph::AttachmentLoadOp::clear
            : render_graph::AttachmentLoadOp::load;

    return render_graph::RasterColorAttachmentDesc{
        .target = output_target_,
        .load_op = load_op,
        .store_op = render_graph::AttachmentStoreOp::store,
        .clear_value = {
            .red = create_info_cache.clear_color.float32[0],
            .green = create_info_cache.clear_color.float32[1],
            .blue = create_info_cache.clear_color.float32[2],
            .alpha = create_info_cache.clear_color.float32[3],
        },
    };
}

void RenderTargetCompositeRenderer::RecordGraphPass(render_graph::GraphCommandContext& context_,
                                                    render_graph::ResourceHandle source_color_,
                                                    render_graph::ResourceHandle output_target_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetCompositeRenderer::RecordGraphPass called before Initialize");
    }
    if (context == nullptr ||
        descriptor_host == nullptr ||
        pipeline_host == nullptr ||
        render_target_host == nullptr ||
        bindless_resources == nullptr ||
        !bindless_resources->IsInitialized()) {
        throw std::runtime_error("RenderTargetCompositeRenderer::RecordGraphPass called before PrepareFrame");
    }

    const auto source_graph_target = context_.ResolveTextureTarget(source_color_);
    const auto output_graph_target = context_.ResolveTextureTarget(output_target_);
    if (!IsValidRenderTargetHandle(source_graph_target) || !render_target_host->IsValid(source_graph_target)) {
        ++stats.skipped_draw_count;
        return;
    }
    if (!IsValidRenderTargetHandle(output_graph_target) || !render_target_host->IsValid(output_graph_target)) {
        throw std::runtime_error("RenderTargetCompositeRenderer::RecordGraphPass requires a valid output target");
    }
    if (output_graph_target.index == source_graph_target.index &&
        output_graph_target.generation == source_graph_target.generation) {
        throw std::runtime_error(
            "RenderTargetCompositeRenderer::RecordGraphPass source target must differ from output target");
    }

    const auto source_view = context_.ResolveTextureView(source_color_);
    const auto output_view = context_.ResolveTextureView(output_target_);
    if (output_view.extent.width == 0U || output_view.extent.height == 0U) {
        throw std::runtime_error("RenderTargetCompositeRenderer::RecordGraphPass resolved zero-sized render extent");
    }

    const auto source_graph_slot = render_target_host->EnsureBindlessImageSlot(source_graph_target);
    const auto graph_sampler_slot = bindless_resources->DefaultSamplerSlot();
    if (!source_graph_slot.IsValid() || !graph_sampler_slot.IsValid()) {
        ++stats.skipped_draw_count;
        return;
    }

    BindAndDrawFullscreen(context_.CommandBuffer(),
                          source_view,
                          output_view.format,
                          VkExtent2D{output_view.extent.width, output_view.extent.height},
                          source_graph_slot,
                          graph_sampler_slot,
                          &context_);
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

void RenderTargetCompositeRenderer::BindAndDrawFullscreen(
    VkCommandBuffer command_buffer_,
    const RenderTargetResolvedView& source_view_,
    VkFormat output_format_,
    VkExtent2D output_extent_,
    BindlessSlot source_texture_slot_,
    BindlessSlot sampler_slot_,
    render_graph::GraphCommandContext* graph_context_) {
    EnsurePipelineObjects(*context, *descriptor_host, *pipeline_host, output_format_);
    BindFullscreenViewportAndScissor(command_buffer_, output_extent_);

    vkCmdBindPipeline(command_buffer_,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(pipeline_id));
    if (graph_context_ != nullptr) {
        graph_context_->BindCurrentPassDescriptorSets(
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_host->GetPipelineLayout(pipeline_layout_id),
            0U,
            2U);
    } else {
        const VkDescriptorSet bindless_sets[] = {
            bindless_resources->SampledImageSet(),
            bindless_resources->SamplerSet()
        };
        vkCmdBindDescriptorSets(command_buffer_,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_host->GetPipelineLayout(pipeline_layout_id),
                                0U,
                                2U,
                                bindless_sets,
                                0U,
                                nullptr);
    }
    stats.descriptor_set_bind_count += 2U;

    PushConstants push_constants{};
    push_constants.exposure = create_info_cache.exposure;
    push_constants.inv_gamma = SafeInvGamma(create_info_cache.output_gamma);
    push_constants.flags = 0U;
    push_constants.flags |= create_info_cache.enable_reinhard_tonemap ? 0x1U : 0U;
    push_constants.flags |= create_info_cache.apply_manual_gamma ? 0x2U : 0U;
    push_constants.flags |= (source_view_.color_encoding == RenderTargetColorEncoding::srgb) ? 0x4U : 0U;
    push_constants.texture_slot = source_texture_slot_.index;
    push_constants.sampler_slot = sampler_slot_.index;
    vkCmdPushConstants(command_buffer_,
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    vkCmdDraw(command_buffer_, 3U, 1U, 0U, 0U);
    ++stats.draw_call_count;
}

} // namespace vr::render
