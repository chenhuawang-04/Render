#include "vr/surface/surface_renderer_3d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/scene_3d_descriptor_contract.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/surface/generated/surface_3d_frag_spv.hpp"
#include "vr/surface/generated/surface_3d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdint>
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

void SurfaceRenderer3D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "SurfaceRenderer3D::BuildDirectRuntimeGraph called before Initialize");
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
                char debug_name[67]{};
                std::snprintf(debug_name,
                              sizeof(debug_name),
                              "surface_renderer_3d_depth_slot_%u",
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
                "surface_renderer_3d_depth",
                depth_desc,
                create_info_cache.clear_depth
                    ? render_graph::ResourceLifetime::transient
                    : render_graph::ResourceLifetime::persistent);
        }
    }

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
                    .depth = create_info_cache.clear_depth_value,
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
                      "surface_renderer_3d_direct_opaque",
                      create_info_cache.clear_swapchain,
                      create_info_cache.clear_depth);
    append_stage_pass(render::SceneRenderStage::transparent,
                      "surface_renderer_3d_direct_transparent",
                      false,
                      false);
    graph_view_.present_ready_version = color_version;
}

void SurfaceRenderer3D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                        const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "SurfaceRenderer3D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render::BuildSharedScene3DShaderContract("surface_3d.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     render::scene_3d_sampled_image_set,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     render::scene_3d_sampler_set,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);
    const std::uint32_t appearance_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &SurfaceRenderer3D::ResolveAppearanceExternalBufferBinding,
            .debug_name = "surface_3d.appearance_records",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_surface_appearance_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      appearance_resolver_id,
                                      render_graph::shader_stage_fragment_flag);
    if (!builder_.HasPassDescriptorBinding(pass_,
                                           render::scene_3d_ibl_set,
                                           render::scene_3d_ibl_params_binding)) {
        const std::uint32_t ibl_params_resolver_id =
            builder_.RegisterExternalBufferBindingResolver({
                .user_data = this,
                .resolve_fn = &SurfaceRenderer3D::ResolveIblParamsExternalBufferBinding,
                .debug_name = "surface_3d.ibl_params",
            });
        builder_.AddExternalBufferBinding(pass_,
                                          render::scene_3d_ibl_set,
                                          render::scene_3d_ibl_params_binding,
                                          render_graph::DescriptorBindingKind::uniform_buffer,
                                          ibl_params_resolver_id,
                                          render_graph::shader_stage_fragment_flag);
    }
}

void SurfaceRenderer3D::RecordGraphSceneStage(render_graph::GraphCommandContext& context_,
                                              render::SceneRenderStage stage_,
                                              render_graph::ResourceHandle color_target_,
                                              render_graph::ResourceHandle depth_target_) {
    RecordGraphInternal(context_,
                        render::SceneRenderStagePassHintValue(stage_),
                        true,
                        color_target_,
                        depth_target_);
}

void SurfaceRenderer3D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                            std::uint32_t pass_bucket_,
                                            bool filter_by_pass_bucket_,
                                            render_graph::ResourceHandle color_target_,
                                            render_graph::ResourceHandle depth_target_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer3D::RecordGraphSceneStage called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr || gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::RecordGraphSceneStage called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer3D::RecordGraphSceneStage requires initialized BindlessResourceSystem");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer3D::RecordGraphSceneStage requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("SurfaceRenderer3D::RecordGraphSceneStage resolved zero-sized render extent");
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

    if (last_upload_result.upload.buffer != VK_NULL_HANDLE &&
        !runtime_scratch.draw_batches.empty()) {
        std::uint32_t stage_draw_call_count = 0U;
        std::uint32_t stage_filtered_batch_count = 0U;
        const VkBuffer vertex_buffer = last_upload_result.upload.buffer;
        const VkDeviceSize vertex_offset = last_upload_result.upload.offset;
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
        if (camera_transform != nullptr) {
            push_constants.camera_position = ecs::Float4{
                .x = camera_transform->runtime.world_matrix.m[12],
                .y = camera_transform->runtime.world_matrix.m[13],
                .z = camera_transform->runtime.world_matrix.m[14],
                .w = 1.0F
            };
        } else {
            push_constants.camera_position = ecs::Float4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        }
        push_constants.params = 0U;
        push_constants.ibl_specular_texture_slot = active_ibl_specular_texture_slot;
        push_constants.ibl_brdf_lut_texture_slot = active_ibl_brdf_lut_texture_slot;
        push_constants.ibl_sampler_slot = active_ibl_sampler_slot;

        render::GraphicsPipelineId bound_pipeline{};
        bool shared_state_bound = false;
        for (const ecs::Surface3DDrawBatch& batch : runtime_scratch.draw_batches) {
            if (filter_by_pass_bucket_ &&
                ecs::SurfaceSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                ++stage_filtered_batch_count;
                continue;
            }
            if (batch.instance_count == 0U) {
                ++stats.skipped_batch_count;
                continue;
            }

            const BlendMode blend_mode = ResolveBlendMode(batch.params);
            const PipelineMode mode = ResolvePipelineMode(batch.params, active_depth_format != VK_FORMAT_UNDEFINED);
            const CullMode cull_mode = ResolveCullMode(batch.params);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                 *pipeline_host,
                                                                                 resolved_color.format,
                                                                                 active_depth_format,
                                                                                 blend_mode,
                                                                                 mode,
                                                                                 cull_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                continue;
            }

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(context_.CommandBuffer(),
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                bound_pipeline = pipeline_id;
            }

            if (!shared_state_bound) {
                vkCmdPushConstants(context_.CommandBuffer(),
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                       pipeline_layout,
                                                       0U,
                                                       4U);
                shared_state_bound = true;
                ++stats.descriptor_set_bind_count;
                ++stats.ibl_descriptor_set_bind_count;
            }

            vkCmdDraw(context_.CommandBuffer(),
                      6U,
                      batch.instance_count,
                      0U,
                      batch.instance_begin);
            ++stats.draw_call_count;
            ++stage_draw_call_count;
        }

        if (filter_by_pass_bucket_) {
            stats.stage_filtered_batch_count += stage_filtered_batch_count;
            if (stage_draw_call_count == 0U) {
                ++stats.empty_stage_pass_count;
            }
            if (pass_bucket_ == static_cast<std::uint32_t>(ecs::SurfaceRenderPassHint::opaque)) {
                stats.opaque_draw_call_count += stage_draw_call_count;
            } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::SurfaceRenderPassHint::transparent)) {
                stats.transparent_draw_call_count += stage_draw_call_count;
            }
        }
    }
}

} // namespace vr::surface
