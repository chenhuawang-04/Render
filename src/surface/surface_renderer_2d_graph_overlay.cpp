#include "vr/surface/surface_renderer_2d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/surface/generated/surface_2d_frag_spv.hpp"
#include "vr/surface/generated/surface_2d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::surface {

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

void SurfaceRenderer2D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "SurfaceRenderer2D::BuildDirectRuntimeGraph called before Initialize");
    }

    const auto pass = graph_view_.builder.AddPass("surface_renderer_2d_direct");
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

void SurfaceRenderer2D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                        const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "SurfaceRenderer2D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    auto shader_contract =
        render_graph::MakeSharedBindlessFragmentShaderContract("surface_2d.frag");
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 0U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 1U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 2U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 3U,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    shader_contract.bindings.push_back({
        .set = 2U,
        .binding = 4U,
        .kind = render_graph::DescriptorBindingKind::uniform_buffer,
        .stage_flags = render_graph::shader_stage_fragment_flag,
        .descriptor_count = 1U,
    });
    builder_.SetPassShaderContract(pass_, std::move(shader_contract));
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
    const std::uint32_t light_records_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveLightRecordsExternalBufferBinding,
            .debug_name = "surface_2d.light_records",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      0U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      light_records_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t cluster_headers_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveClusterHeadersExternalBufferBinding,
            .debug_name = "surface_2d.cluster_headers",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      1U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_headers_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t cluster_indices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveClusterIndicesExternalBufferBinding,
            .debug_name = "surface_2d.cluster_indices",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      2U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_indices_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t shadow_views_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveShadowViewsExternalBufferBinding,
            .debug_name = "surface_2d.shadow_views",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      3U,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      shadow_views_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    const std::uint32_t lighting_uniform_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer2D::ResolveLightingUniformExternalBufferBinding,
            .debug_name = "surface_2d.lighting_uniform",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      2U,
                                      4U,
                                      render_graph::DescriptorBindingKind::uniform_buffer,
                                      lighting_uniform_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
}

void SurfaceRenderer2D::RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                                           render_graph::ResourceHandle color_target_) {
    RecordGraphInternal(context_, color_target_);
}

void SurfaceRenderer2D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                            render_graph::ResourceHandle color_target_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay requires initialized BindlessResourceSystem");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("SurfaceRenderer2D::RecordGraphOverlay resolved zero-sized render extent");
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
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

    RecordDrawBatches(context_.CommandBuffer(),
                      render_extent,
                      resolved_color.format,
                      &context_);
}

void SurfaceRenderer2D::RecordDrawBatches(VkCommandBuffer command_buffer_,
                                          VkExtent2D render_extent_,
                                          VkFormat color_format_,
                                          const render_graph::GraphCommandContext* graph_context_) {
    if (last_upload_result.upload.buffer == VK_NULL_HANDLE ||
        runtime_scratch.draw_batches.empty()) {
        return;
    }

    const VkBuffer vertex_buffer = last_upload_result.upload.buffer;
    const VkDeviceSize vertex_offset = last_upload_result.upload.offset;
    vkCmdBindVertexBuffers(command_buffer_,
                           0U,
                           1U,
                           &vertex_buffer,
                           &vertex_offset);

    PushConstants push_constants{};
    push_constants.viewport_width = static_cast<float>(render_extent_.width);
    push_constants.viewport_height = static_cast<float>(render_extent_.height);
    push_constants.inv_viewport_width_2x = (render_extent_.width > 0U)
        ? (2.0F / static_cast<float>(render_extent_.width))
        : 0.0F;
    push_constants.inv_viewport_height_2x = (render_extent_.height > 0U)
        ? (2.0F / static_cast<float>(render_extent_.height))
        : 0.0F;
    push_constants.params = 0U;
    push_constants.params |= create_info_cache.input_positions_pixel_space ? 0x1U : 0U;
    push_constants.params |= create_info_cache.pixel_space_origin_top_left ? 0x2U : 0U;
    push_constants.reserved0 = 0U;
    push_constants.reserved1 = 0U;
    push_constants.reserved2 = 0U;

    if (graph_context_ == nullptr) {
        PrepareLightingDescriptorSetForFrame(active_frame_index);
    }
    VkDescriptorSet frame_lighting_descriptor_set = VK_NULL_HANDLE;
    if (active_frame_index < frame_lighting_resources.size()) {
        frame_lighting_descriptor_set = frame_lighting_resources[active_frame_index].descriptor_set;
    }

    const std::array<VkDescriptorSet, 2U> bindless_sets{
        bindless_resources->SampledImageSet(),
        bindless_resources->SamplerSet()
    };
    if (bindless_sets[0U] == VK_NULL_HANDLE || bindless_sets[1U] == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer2D::RecordDrawBatches requires valid bindless descriptor sets");
    }

    if (graph_context_ == nullptr && frame_lighting_descriptor_set == VK_NULL_HANDLE) {
        stats.skipped_batch_count += static_cast<std::uint32_t>(runtime_scratch.draw_batches.size());
        return;
    }
    const std::array<VkDescriptorSet, 3U> local_descriptor_sets{
        bindless_sets[0U],
        bindless_sets[1U],
        frame_lighting_descriptor_set,
    };

    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    render::GraphicsPipelineId bound_pipeline{};
    bool descriptor_sets_bound = false;
    for (const ecs::Surface2DDrawBatch& batch : runtime_scratch.draw_batches) {
        if (batch.instance_count == 0U) {
            ++stats.skipped_batch_count;
            continue;
        }

        const BlendModeKind blend_mode = ResolveBlendModeFromBatchParams(batch.params);
        const render::GraphicsPipelineId pipeline_id = EnsurePipelineForBlendMode(
            *context,
            *pipeline_host,
            color_format_,
            blend_mode);
        if (!pipeline_id.IsValid()) {
            ++stats.skipped_batch_count;
            continue;
        }

        if (bound_pipeline.value != pipeline_id.value) {
            vkCmdBindPipeline(command_buffer_,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));
            vkCmdPushConstants(command_buffer_,
                               pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(PushConstants),
                               &push_constants);
            bound_pipeline = pipeline_id;
            descriptor_sets_bound = false;
        }

        if (!descriptor_sets_bound) {
            if (graph_context_ != nullptr) {
                graph_context_->BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                              pipeline_layout,
                                                              0U,
                                                              3U);
            } else {
                vkCmdBindDescriptorSets(command_buffer_,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_layout,
                                        0U,
                                        static_cast<std::uint32_t>(local_descriptor_sets.size()),
                                        local_descriptor_sets.data(),
                                        0U,
                                        nullptr);
            }
            descriptor_sets_bound = true;
            ++stats.descriptor_set_bind_count;
            ++stats.light_descriptor_set_bind_count;
        }

        vkCmdDraw(command_buffer_,
                  6U,
                  batch.instance_count,
                  0U,
                  batch.instance_begin);
        ++stats.draw_call_count;
    }
}

} // namespace vr::surface
