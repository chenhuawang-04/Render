#include "vr/text/text_renderer_2d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/text/generated/text_2d_frag_spv.hpp"
#include "vr/text/text_runtime_contract.hpp"
#include "vr/text/generated/text_2d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vr::text {

namespace {

[[nodiscard]] render::BindlessTableId ResolveSampledImageTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SampledImageTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SampledImageTableContractId();
}

[[nodiscard]] render::BindlessTableId ResolveSamplerTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SamplerTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SamplerTableContractId();
}

} // namespace

void TextRenderer2D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "TextRenderer2D::BuildDirectRuntimeGraph called before Initialize");
    }

    const auto pass = graph_view_.builder.AddPass("text_renderer_2d_direct");
    graph_view_.present_ready_version = graph_view_.builder.Write(
        pass,
        graph_view_.present_target,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::color_attachment_write,
        });
    graph_view_.builder.SetRasterPassDesc(
        pass,
        render_graph::RasterPassDesc{
            .color_attachments = {
                render_graph::RasterColorAttachmentDesc{
                    .target = graph_view_.present_target,
                    .load_op = create_info_cache.clear_swapchain
                        ? render_graph::AttachmentLoadOp::clear
                        : render_graph::AttachmentLoadOp::load,
                    .store_op = render_graph::AttachmentStoreOp::store,
                    .clear_value = {
                        .red = create_info_cache.clear_color.float32[0],
                        .green = create_info_cache.clear_color.float32[1],
                        .blue = create_info_cache.clear_color.float32[2],
                        .alpha = create_info_cache.clear_color.float32[3],
                    },
                },
            },
        });
    DescribeGraphDescriptorBindings(graph_view_.builder, pass);
    graph_view_.builder.SetExecuteCallback(
        pass,
        [this, color_target = graph_view_.present_target](render_graph::GraphCommandContext& context_) {
            RecordGraphOverlay(context_, color_target);
        });
}

void TextRenderer2D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                     const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "TextRenderer2D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const_cast<TextRenderer2D*>(this)->ScheduleGraphInstanceUpload(builder_, pass_);

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("text_2d.frag"));
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

void TextRenderer2D::RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                                       render_graph::ResourceHandle color_target_) {
    RecordGraphInternal(context_, color_target_);
}

void TextRenderer2D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                       render_graph::ResourceHandle color_target_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        glyph_upload_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay called before PrepareFrame");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          resolved_color.format);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(context_.CommandBuffer(), 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

    const VkPipeline pipeline_handle = pipeline_host->GetGraphicsPipeline(graphics_pipeline_id);
    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_handle);

    PushConstants push_constants{};
    push_constants.inv_viewport_x = 1.0F / static_cast<float>(render_extent.width);
    push_constants.inv_viewport_y = 1.0F / static_cast<float>(render_extent.height);
    push_constants.depth = create_info_cache.depth;
    push_constants.sdf_smooth = create_info_cache.sdf_smooth;
    push_constants.bitmap_gamma = create_info_cache.bitmap_gamma;
    push_constants.bitmap_edge_sharpness = create_info_cache.bitmap_edge_sharpness;
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    const std::uint32_t frame_index = active_frame_index;
    if (frame_index < frame_states.size()) {
        const PerFrameState& frame_state = frame_states[frame_index];
        VkBuffer active_vertex_buffer = frame_state.vertex_buffer.buffer;
        if (frame_state.graph_vertex_size_bytes > 0U &&
            render_graph::IsValidResourceHandle(frame_state.graph_vertex_buffer)) {
            if (const auto* graph_vertex_record = context_.FindBuffer(frame_state.graph_vertex_buffer);
                graph_vertex_record != nullptr) {
                if (graph_vertex_record->owned_resource.buffer != VK_NULL_HANDLE) {
                    active_vertex_buffer = graph_vertex_record->owned_resource.buffer;
                } else if (graph_vertex_record->imported_buffer.buffer != VK_NULL_HANDLE) {
                    active_vertex_buffer = graph_vertex_record->imported_buffer.buffer;
                }
            }
        }

        if (frame_state.instance_count > 0U && active_vertex_buffer != VK_NULL_HANDLE) {
            context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   pipeline_layout,
                                                   0U,
                                                   2U);
            stats.descriptor_set_bind_count += 2U;

            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(context_.CommandBuffer(),
                                   0U,
                                   1U,
                                   &active_vertex_buffer,
                                   &vertex_offset);

            for (const auto& batch : runtime_scratch.draw_batches) {
                if (batch.glyph_count == 0U) {
                    continue;
                }
                if (batch.atlas_page_id >= glyph_upload_host->PageCount()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const render::BindlessSlot texture_slot =
                    glyph_upload_host->ResolveBindlessImageSlot(batch.atlas_page_id);
                const render::BindlessSlot sampler_slot =
                    glyph_upload_host->BindlessConfig().sampler_slot;
                if (!texture_slot.IsValid() || !sampler_slot.IsValid()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                push_constants.texture_slot = texture_slot.index;
                push_constants.sampler_slot = sampler_slot.index;
                vkCmdPushConstants(context_.CommandBuffer(),
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);

                vkCmdDraw(context_.CommandBuffer(),
                          4U,
                          batch.glyph_count,
                          0U,
                          batch.glyph_begin);
                ++stats.draw_call_count;
            }
        }
    }
}

} // namespace vr::text
