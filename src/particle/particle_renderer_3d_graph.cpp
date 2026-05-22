#include "vr/particle/particle_renderer_3d.hpp"

#include "vr/ecs/system/particle_system.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_3d_frag_spv.hpp"
#include "vr/particle/generated/particle_3d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
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

[[nodiscard]] float ResolveDepthClearValue(bool reverse_z_,
                                           float configured_value_) noexcept {
    return reverse_z_ ? 0.0F : configured_value_;
}

} // namespace

void ParticleRenderer3D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "ParticleRenderer3D::BuildDirectRuntimeGraph called before Initialize");
    }

    render_graph::ResourceHandle depth_target = render_graph::invalid_resource_handle;
    if (create_info_cache.enable_depth) {
        const render_graph::Extent3D depth_extent{
            .width = graph_view_.reference_extent.width != 0U ? graph_view_.reference_extent.width : 1U,
            .height = graph_view_.reference_extent.height != 0U ? graph_view_.reference_extent.height : 1U,
            .depth = graph_view_.reference_extent.depth != 0U ? graph_view_.reference_extent.depth : 1U,
        };
        render_graph::TextureDesc depth_desc{
            .dimension = render_graph::TextureDimension::image_2d,
            .format = render_graph::TextureFormat::d32_sfloat,
            .extent = depth_extent,
            .usage = render_graph::texture_usage_depth_stencil_attachment_flag,
            .mip_level_count = 1U,
            .array_layer_count = 1U,
            .sample_count = render_graph::SampleCount::x1,
            .prefer_lazy_memory = create_info_cache.clear_depth,
        };
        if (!create_info_cache.clear_depth &&
            descriptor_host != nullptr &&
            descriptor_host->FramesInFlight() > 1U) {
            const std::uint32_t frames_in_flight =
                (std::max)(descriptor_host->FramesInFlight(), 1U);
            const std::uint32_t selected_frame_slot = active_frame_index % frames_in_flight;
            for (std::uint32_t frame_slot = 0U; frame_slot < frames_in_flight; ++frame_slot) {
                char debug_name[68]{};
                std::snprintf(debug_name,
                              sizeof(debug_name),
                              "particle_renderer_3d_depth_slot_%u",
                              frame_slot);
                const auto candidate = graph_view_.builder.CreateTexture(
                    debug_name,
                    depth_desc,
                    render_graph::ResourceLifetime::persistent);
                if (frame_slot == selected_frame_slot) {
                    depth_target = candidate;
                }
            }
        } else {
            depth_target = graph_view_.builder.CreateTexture(
                "particle_renderer_3d_depth",
                depth_desc,
                create_info_cache.clear_depth
                    ? render_graph::ResourceLifetime::transient
                    : render_graph::ResourceLifetime::persistent);
        }
    }

    const float depth_clear_value =
        ResolveDepthClearValue(active_camera_reverse_z, create_info_cache.clear_depth_value);
    render_graph::ResourceVersionHandle color_version =
        render_graph::invalid_resource_version;
    render_graph::ResourceVersionHandle depth_version =
        render_graph::invalid_resource_version;
    auto append_stage_pass = [&](const render::SceneRenderStage stage_,
                                 const char* debug_name_,
                                 const bool clear_color_,
                                 const bool clear_depth_) {
        const auto pass = graph_view_.builder.AddPass(debug_name_);
        if (render_graph::IsValidResourceVersionHandle(color_version)) {
            (void)graph_view_.builder.Read(
                pass,
                color_version,
                render_graph::AccessDesc{
                    .access = render_graph::AccessKind::color_attachment_read,
                });
        }
        color_version = graph_view_.builder.Write(
            pass,
            graph_view_.present_target,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::color_attachment_write,
            });

        render_graph::RasterPassDesc raster_pass_desc{
            .color_attachments = {
                render_graph::RasterColorAttachmentDesc{
                    .target = graph_view_.present_target,
                    .load_op = clear_color_
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
        };

        if (render_graph::IsValidResourceHandle(depth_target)) {
            if (render_graph::IsValidResourceVersionHandle(depth_version)) {
                (void)graph_view_.builder.Read(
                    pass,
                    depth_version,
                    render_graph::AccessDesc{
                        .access = render_graph::AccessKind::depth_stencil_read,
                    });
            }
            depth_version = graph_view_.builder.Write(
                pass,
                depth_target,
                render_graph::AccessDesc{
                    .access = render_graph::AccessKind::depth_stencil_write,
                });
            raster_pass_desc.has_depth_attachment = true;
            raster_pass_desc.depth_attachment = render_graph::RasterDepthAttachmentDesc{
                .target = depth_target,
                .load_op = clear_depth_
                    ? render_graph::AttachmentLoadOp::clear
                    : render_graph::AttachmentLoadOp::load,
                .store_op = render_graph::AttachmentStoreOp::store,
                .stencil_load_op = clear_depth_
                    ? render_graph::AttachmentLoadOp::clear
                    : render_graph::AttachmentLoadOp::load,
                .stencil_store_op = render_graph::AttachmentStoreOp::store,
                .clear_value = {
                    .depth = depth_clear_value,
                    .stencil = create_info_cache.clear_stencil_value,
                },
            };
        }

        graph_view_.builder.SetRasterPassDesc(pass, raster_pass_desc);
        DescribeGraphDescriptorBindings(graph_view_.builder, pass);
        graph_view_.builder.SetExecuteCallback(
            pass,
            [this,
             stage_,
             color_target = graph_view_.present_target,
             depth_target](render_graph::GraphCommandContext& context_) {
                RecordGraphSceneStage(context_, stage_, color_target, depth_target);
            });
    };

    append_stage_pass(render::SceneRenderStage::opaque,
                      "particle_renderer_3d_direct_opaque",
                      create_info_cache.clear_swapchain,
                      create_info_cache.clear_depth);
    append_stage_pass(render::SceneRenderStage::transparent,
                      "particle_renderer_3d_direct_transparent",
                      false,
                      false);
    graph_view_.present_ready_version = color_version;
}

void ParticleRenderer3D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                         const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "ParticleRenderer3D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const_cast<ParticleRenderer3D*>(this)->ScheduleGraphComputeBuild(builder_, pass_);

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("particle_3d.frag"));
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

void ParticleRenderer3D::RegisterGraphImportedResources(
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

void ParticleRenderer3D::ScheduleGraphComputeBuild(render_graph::RenderGraphBuilder& builder_,
                                                   const render_graph::PassHandle pass_) {
    if (!graph_compute_pass_owned) {
        return;
    }
    const auto append_scene_reads = [&](const VkDeviceSize draw_instances_size_,
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
            append_scene_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
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
        "particle_3d_draw_instances",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_vertex_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);
    graph_indirect_commands_resource = builder_.CreateBuffer(
        "particle_3d_indirect_commands",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_indirect_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);

    const auto compute_pass = builder_.AddPass("particle_3d_gpu_build",
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
            particle_simulation_host->RecordBuild3D(*context,
                                                    *pipeline_host,
                                                    frame_index,
                                                    ResolveCameraPosition(),
                                                    ResolveCameraForward(),
                                                    context_.CommandBuffer());
        });
    append_scene_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
                       last_gpu_build_result.resources.indirect_commands.size_bytes);
    graph_compute_pass_scheduled = true;
}

void ParticleRenderer3D::RecordGraphSceneStage(render_graph::GraphCommandContext& context_,
                                               render::SceneRenderStage stage_,
                                               render_graph::ResourceHandle color_target_,
                                               render_graph::ResourceHandle depth_target_) {
    RecordGraphInternal(context_,
                        render::SceneRenderStagePassHintValue(stage_),
                        true,
                        color_target_,
                        depth_target_);
}

void ParticleRenderer3D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                             std::uint32_t pass_bucket_,
                                             bool filter_by_pass_bucket_,
                                             render_graph::ResourceHandle color_target_,
                                             render_graph::ResourceHandle depth_target_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage missing initialized BindlessResourceSystem");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage resolved zero-sized render extent");
    }

    const bool use_depth_attachment = render_graph::IsValidResourceHandle(depth_target_);
    const VkFormat active_depth_format = use_depth_attachment
        ? context_.ResolveTextureView(depth_target_).format
        : VK_FORMAT_UNDEFINED;

    EnsurePipelineObjects(*context,
                          *bindless_resources,
                          *pipeline_host,
                          resolved_color.format,
                          active_depth_format);

    if (gpu_build_active &&
        particle_simulation_host != nullptr &&
        !graph_compute_pass_owned) {
        particle_simulation_host->RecordBuild3D(*context,
                                                *pipeline_host,
                                                active_frame_index,
                                                ResolveCameraPosition(),
                                                ResolveCameraForward(),
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
        vkCmdBindVertexBuffers(context_.CommandBuffer(),
                               0U,
                               1U,
                               &vertex_buffer,
                               &vertex_offset);
        const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);

        PushConstants push_constants{};
        if (camera_component != nullptr) {
            push_constants.view_projection = camera_component->runtime.view_projection_matrix;
        } else {
            push_constants.view_projection = ecs::spatial_math::IdentityMatrix4x4();
        }
        const ecs::Float3 camera_right = ResolveCameraRight();
        const ecs::Float3 camera_up = ResolveCameraUp();
        const ecs::Float3 camera_forward = ResolveCameraForward();
        push_constants.camera_right = ecs::Float4{.x = camera_right.x, .y = camera_right.y, .z = camera_right.z, .w = 0.0F};
        push_constants.camera_up = ecs::Float4{.x = camera_up.x, .y = camera_up.y, .z = camera_up.z, .w = 0.0F};
        push_constants.camera_forward = ecs::Float4{.x = camera_forward.x, .y = camera_forward.y, .z = camera_forward.z, .w = 0.0F};
        push_constants.params = 0U;
        push_constants.reserved0 = 0U;
        push_constants.reserved1 = 0U;
        push_constants.reserved2 = 0U;

        context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               pipeline_layout,
                                               0U,
                                               2U);
        ++stats.descriptor_set_bind_count;

        render::GraphicsPipelineId bound_pipeline{};
        std::uint32_t bound_push_params = (std::numeric_limits<std::uint32_t>::max)();
        std::uint32_t stage_draw_call_count = 0U;
        std::uint32_t stage_filtered_batch_count = 0U;

        std::uint32_t batch_index = 0U;
        for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
            if (filter_by_pass_bucket_ &&
                ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                ++stage_filtered_batch_count;
                ++batch_index;
                continue;
            }
            if (batch.instance_count == 0U) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const ecs::ParticleRenderMode render_mode = DecodeRenderMode(batch.pipeline_state);
            if (render_mode == ecs::ParticleRenderMode::mesh ||
                render_mode == ecs::ParticleRenderMode::trail) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const BlendModeKind blend_mode = DecodeBlendModeKind(batch.pipeline_state);
            const DepthPipelineMode depth_mode = ResolveDepthPipelineMode(batch.pipeline_state,
                                                                          active_depth_format != VK_FORMAT_UNDEFINED,
                                                                          active_camera_reverse_z);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                 *pipeline_host,
                                                                                 resolved_color.format,
                                                                                 active_depth_format,
                                                                                 blend_mode,
                                                                                 depth_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const std::uint32_t push_params =
                (static_cast<std::uint32_t>(render_mode) & 0xFFU) |
                ((static_cast<std::uint32_t>(DecodeFacingMode(batch.pipeline_state)) & 0xFFU) << 8U);

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(context_.CommandBuffer(),
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                bound_pipeline = pipeline_id;
                bound_push_params = (std::numeric_limits<std::uint32_t>::max)();
            }

            if (bound_push_params != push_params) {
                push_constants.params = push_params;
                vkCmdPushConstants(context_.CommandBuffer(),
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                bound_push_params = push_params;
            }

            if (gpu_build_active) {
                const VkDeviceSize indirect_offset =
                    static_cast<VkDeviceSize>(batch_index) * sizeof(ParticleGpuIndirectCommand);
                vkCmdDrawIndirect(context_.CommandBuffer(),
                                  last_gpu_build_result.resources.indirect_commands.buffer,
                                  indirect_offset,
                                  1U,
                                  sizeof(ParticleGpuIndirectCommand));
                ++stats.indirect_draw_count;
            } else {
                vkCmdDraw(context_.CommandBuffer(),
                          6U,
                          batch.instance_count,
                          0U,
                          batch.instance_begin);
            }
            ++stats.draw_call_count;
            ++stage_draw_call_count;
            ++batch_index;
        }

        if (filter_by_pass_bucket_) {
            stats.stage_filtered_batch_count += stage_filtered_batch_count;
            if (stage_draw_call_count == 0U) {
                ++stats.empty_stage_pass_count;
            }
            if (pass_bucket_ == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::opaque)) {
                stats.opaque_draw_call_count += stage_draw_call_count;
            } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent)) {
                stats.transparent_draw_call_count += stage_draw_call_count;
            }
        } else {
            for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
                if (batch.instance_count == 0U) {
                    continue;
                }
                const std::uint32_t bucket =
                    ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key);
                if (bucket == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::opaque)) {
                    ++stats.opaque_draw_call_count;
                } else if (bucket == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent)) {
                    ++stats.transparent_draw_call_count;
                }
            }
        }
    }
}

} // namespace vr::particle
