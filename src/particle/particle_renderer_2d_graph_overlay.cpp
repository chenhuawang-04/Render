#include "vr/particle/particle_renderer_2d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_2d_frag_spv.hpp"
#include "vr/particle/generated/particle_2d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::particle {

namespace {

constexpr VkBufferUsageFlags k_graph_particle_draw_instances_imported_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VkBufferUsageFlags k_graph_particle_indirect_commands_imported_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

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

void ParticleRenderer2D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "ParticleRenderer2D::BuildDirectRuntimeGraph called before Initialize");
    }

    const auto pass = graph_view_.builder.AddPass("particle_renderer_2d_direct");
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

void ParticleRenderer2D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                         const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "ParticleRenderer2D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const_cast<ParticleRenderer2D*>(this)->ScheduleGraphComputeBuild(builder_, pass_);

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("particle_2d.frag"));
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

void ParticleRenderer2D::RegisterGraphImportedResources(
    runtime::services::RenderGraphRuntimeService& graph_runtime_service_) const {
    if (!graph_compute_pass_owned ||
        !render_graph::IsValidResourceHandle(graph_draw_instances_resource) ||
        !render_graph::IsValidResourceHandle(graph_indirect_commands_resource)) {
        return;
    }

    if (last_gpu_build_result.resources.draw_instances.buffer != VK_NULL_HANDLE &&
        last_gpu_build_result.resources.draw_instances.size_bytes != 0U) {
        graph_runtime_service_.RegisterDirectImportedBuffer(
            graph_draw_instances_resource,
            render_graph::ImportedBufferBinding{
                .buffer = last_gpu_build_result.resources.draw_instances.buffer,
                .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
                .usage = k_graph_particle_draw_instances_imported_usage,
            });
    }
    if (last_gpu_build_result.resources.indirect_commands.buffer != VK_NULL_HANDLE &&
        last_gpu_build_result.resources.indirect_commands.size_bytes != 0U) {
        graph_runtime_service_.RegisterDirectImportedBuffer(
            graph_indirect_commands_resource,
            render_graph::ImportedBufferBinding{
                .buffer = last_gpu_build_result.resources.indirect_commands.buffer,
                .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
                .usage = k_graph_particle_indirect_commands_imported_usage,
            });
    }
}

void ParticleRenderer2D::ScheduleGraphComputeBuild(render_graph::RenderGraphBuilder& builder_,
                                                   const render_graph::PassHandle pass_) {
    if (!graph_compute_pass_owned) {
        return;
    }
    const auto append_overlay_reads = [&](const VkDeviceSize draw_instances_size_,
                                          const VkDeviceSize indirect_commands_size_) {
        (void)builder_.Read(
            pass_,
            graph_draw_instances_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::vertex_buffer_read,
                .buffer_range = {
                    .offset_bytes = 0U,
                    .size_bytes = draw_instances_size_,
                },
            });
        (void)builder_.Read(
            pass_,
            graph_indirect_commands_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::indirect_command_read,
                .buffer_range = {
                    .offset_bytes = 0U,
                    .size_bytes = indirect_commands_size_,
                },
            });
    };
    if (graph_compute_pass_scheduled) {
        if (render_graph::IsValidResourceVersionHandle(graph_draw_instances_version) &&
            render_graph::IsValidResourceVersionHandle(graph_indirect_commands_version)) {
            append_overlay_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
                                 last_gpu_build_result.resources.indirect_commands.size_bytes);
        }
        return;
    }
    if (last_gpu_build_result.resources.draw_instances.buffer == VK_NULL_HANDLE ||
        last_gpu_build_result.resources.indirect_commands.buffer == VK_NULL_HANDLE ||
        last_gpu_build_result.resources.draw_instances.size_bytes == 0U ||
        last_gpu_build_result.resources.indirect_commands.size_bytes == 0U ||
        particle_simulation_host == nullptr) {
        graph_compute_pass_owned = false;
        return;
    }

    graph_draw_instances_resource = builder_.CreateBuffer(
        "particle_2d_draw_instances",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_vertex_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);
    graph_indirect_commands_resource = builder_.CreateBuffer(
        "particle_2d_indirect_commands",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_indirect_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);

    const auto compute_pass = builder_.AddPass("particle_2d_gpu_build",
                                               false,
                                               render_graph::QueueClass::compute);
    graph_draw_instances_version = builder_.Write(
        compute_pass,
        graph_draw_instances_resource,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::shader_storage_write,
            .buffer_range = {
                .offset_bytes = 0U,
                .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
            },
        });
    graph_indirect_commands_version = builder_.Write(
        compute_pass,
        graph_indirect_commands_resource,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::shader_storage_write,
            .buffer_range = {
                .offset_bytes = 0U,
                .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
            },
        });
    builder_.SetExecuteCallback(
        compute_pass,
        [this, frame_index = active_frame_index](render_graph::GraphCommandContext& context_) {
            if (particle_simulation_host == nullptr) {
                return;
            }
            particle_simulation_host->RecordBuild2D(*context,
                                                    *pipeline_host,
                                                    frame_index,
                                                    context_.CommandBuffer());
        });
    append_overlay_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
                         last_gpu_build_result.resources.indirect_commands.size_bytes);
    graph_compute_pass_scheduled = true;
}

void ParticleRenderer2D::RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                                            render_graph::ResourceHandle color_target_) {
    RecordGraphInternal(context_, color_target_);
}

void ParticleRenderer2D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                             render_graph::ResourceHandle color_target_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer2D::RecordGraphOverlay requires initialized BindlessResourceSystem");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context,
                          *bindless_resources,
                          *pipeline_host,
                          resolved_color.format);

    if (gpu_build_active &&
        particle_simulation_host != nullptr &&
        !graph_compute_pass_owned) {
        particle_simulation_host->RecordBuild2D(*context,
                                                *pipeline_host,
                                                active_frame_index,
                                                context_.CommandBuffer());
    }

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

void ParticleRenderer2D::RecordDrawBatches(VkCommandBuffer command_buffer_,
                                           VkExtent2D render_extent_,
                                           VkFormat color_format_,
                                           const render_graph::GraphCommandContext* graph_context_) {
    if (((gpu_build_active &&
          last_gpu_build_result.resources.draw_instances.buffer != VK_NULL_HANDLE) ||
         (last_upload_result.upload.buffer != VK_NULL_HANDLE)) &&
        !runtime_scratch.draw_batches.empty()) {
        const VkBuffer vertex_buffer = gpu_build_active
            ? last_gpu_build_result.resources.draw_instances.buffer
            : last_upload_result.upload.buffer;
        const VkDeviceSize vertex_offset = gpu_build_active
            ? 0U
            : last_upload_result.upload.offset;
        vkCmdBindVertexBuffers(command_buffer_,
                               0U,
                               1U,
                               &vertex_buffer,
                               &vertex_offset);

        PushConstants push_constants{};
        push_constants.viewport_width = static_cast<float>(render_extent_.width);
        push_constants.viewport_height = static_cast<float>(render_extent_.height);
        push_constants.inv_viewport_width_2x =
            (render_extent_.width > 0U)
                ? (2.0F / static_cast<float>(render_extent_.width))
                : 0.0F;
        push_constants.inv_viewport_height_2x =
            (render_extent_.height > 0U)
                ? (2.0F / static_cast<float>(render_extent_.height))
                : 0.0F;
        push_constants.params = 0U;
        push_constants.params |= create_info_cache.input_positions_pixel_space ? 0x1U : 0U;
        push_constants.params |= create_info_cache.pixel_space_origin_top_left ? 0x2U : 0U;
        push_constants.reserved0 = 0U;
        push_constants.reserved1 = 0U;
        push_constants.reserved2 = 0U;

        const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
        if (graph_context_ != nullptr) {
            graph_context_->BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                          pipeline_layout,
                                                          0U,
                                                          2U);
        } else {
            const std::array<VkDescriptorSet, 2U> bindless_sets{
                bindless_resources->SampledImageSet(),
                bindless_resources->SamplerSet()
            };
            if (bindless_sets[0U] == VK_NULL_HANDLE || bindless_sets[1U] == VK_NULL_HANDLE) {
                throw std::runtime_error(
                    "ParticleRenderer2D::RecordDrawBatches requires valid bindless descriptor sets");
            }
            vkCmdBindDescriptorSets(command_buffer_,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0U,
                                    static_cast<std::uint32_t>(bindless_sets.size()),
                                    bindless_sets.data(),
                                    0U,
                                    nullptr);
        }
        ++stats.descriptor_set_bind_count;

        render::GraphicsPipelineId bound_pipeline{};
        std::uint32_t batch_index = 0U;
        for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
            if (batch.instance_count == 0U) {
                ++batch_index;
                continue;
            }

            const BlendModeKind blend_mode = DecodeBlendModeKind(batch.pipeline_state);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForBlendMode(
                *context,
                *pipeline_host,
                color_format_,
                blend_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(command_buffer_,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                vkCmdPushConstants(command_buffer_,
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                bound_pipeline = pipeline_id;
            }

            if (gpu_build_active) {
                const VkDeviceSize indirect_offset =
                    static_cast<VkDeviceSize>(batch_index) * sizeof(ParticleGpuIndirectCommand);
                vkCmdDrawIndirect(command_buffer_,
                                  last_gpu_build_result.resources.indirect_commands.buffer,
                                  indirect_offset,
                                  1U,
                                  sizeof(ParticleGpuIndirectCommand));
                ++stats.indirect_draw_count;
            } else {
                vkCmdDraw(command_buffer_,
                          6U,
                          batch.instance_count,
                          0U,
                          batch.instance_begin);
            }
            ++stats.draw_call_count;
            ++batch_index;
        }
    }
}

} // namespace vr::particle
