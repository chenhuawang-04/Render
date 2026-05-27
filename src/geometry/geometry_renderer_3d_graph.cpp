#include "vr/geometry/geometry_renderer_3d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/geometry/geometry_appearance_resolver.hpp"
#include "vr/geometry/generated/geometry_3d_frag_spv.hpp"
#include "vr/geometry/generated/geometry_3d_vert_spv.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/scene_3d_descriptor_contract.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::geometry {

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

void GeometryRenderer3D::SetFrameViewProjectionOverride(
    const ecs::Matrix4x4& view_projection_) noexcept {
    frame_view_projection_override = view_projection_;
    frame_view_projection_override_active = true;
}

void GeometryRenderer3D::ClearFrameViewProjectionOverride() noexcept {
    frame_view_projection_override_active = false;
}

void GeometryRenderer3D::SetFrameTemporalMotionProducerState(
    const render::SceneTemporalMotionProducerState& state_) noexcept {
    temporal_motion_producer_state = state_;
    temporal_motion_producer_state_active = true;
}

void GeometryRenderer3D::ClearFrameTemporalMotionProducerState() noexcept {
    temporal_motion_producer_state = {};
    temporal_motion_producer_state_active = false;
}

void GeometryRenderer3D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "GeometryRenderer3D::BuildDirectRuntimeGraph called before Initialize");
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
                              "geometry_renderer_3d_depth_slot_%u",
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
                "geometry_renderer_3d_depth",
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
                      "geometry_renderer_3d_direct_opaque",
                      create_info_cache.clear_swapchain,
                      create_info_cache.clear_depth);
    append_stage_pass(render::SceneRenderStage::transparent,
                      "geometry_renderer_3d_direct_transparent",
                      false,
                      false);
    graph_view_.present_ready_version = color_version;
}

void GeometryRenderer3D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                         const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "GeometryRenderer3D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render::BuildSharedScene3DShaderContract("geometry_3d.frag"));
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

    const std::uint32_t light_records_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveLightRecordsExternalBufferBinding,
            .debug_name = "geometry_3d.light_records",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_light_records_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      light_records_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t cluster_headers_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveClusterHeadersExternalBufferBinding,
            .debug_name = "geometry_3d.cluster_headers",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_cluster_headers_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_headers_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t cluster_indices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveClusterIndicesExternalBufferBinding,
            .debug_name = "geometry_3d.cluster_indices",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_cluster_indices_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_indices_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t shadow_views_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveShadowViewsExternalBufferBinding,
            .debug_name = "geometry_3d.shadow_views",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_shadow_views_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      shadow_views_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t lighting_uniform_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveLightingUniformExternalBufferBinding,
            .debug_name = "geometry_3d.lighting_uniform",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_lighting_uniform_binding,
                                      render_graph::DescriptorBindingKind::uniform_buffer,
                                      lighting_uniform_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t skeletal_components_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveSkeletalComponentsExternalBufferBinding,
            .debug_name = "geometry_3d.skeletal_components",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_skeletal_components_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      skeletal_components_resolver_id,
                                      render_graph::shader_stage_vertex_flag);

    const std::uint32_t skeletal_matrices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveSkeletalMatricesExternalBufferBinding,
            .debug_name = "geometry_3d.skeletal_matrices",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_skeletal_matrices_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      skeletal_matrices_resolver_id,
                                      render_graph::shader_stage_vertex_flag);

    const std::uint32_t appearance_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveAppearanceExternalBufferBinding,
            .debug_name = "geometry_3d.appearance_records",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_geometry_appearance_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      appearance_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    if (!builder_.HasPassDescriptorBinding(pass_,
                                           render::scene_3d_ibl_set,
                                           render::scene_3d_ibl_params_binding)) {
        const std::uint32_t ibl_params_resolver_id =
            builder_.RegisterExternalBufferBindingResolver({
                .user_data = this,
                .resolve_fn = &GeometryRenderer3D::ResolveIblParamsExternalBufferBinding,
                .debug_name = "geometry_3d.ibl_params",
            });
        builder_.AddExternalBufferBinding(pass_,
                                          render::scene_3d_ibl_set,
                                          render::scene_3d_ibl_params_binding,
                                          render_graph::DescriptorBindingKind::uniform_buffer,
                                          ibl_params_resolver_id,
                                          render_graph::shader_stage_fragment_flag);
    }
}

void GeometryRenderer3D::DescribeGraphTemporalMotionBindings(
    render_graph::RenderGraphBuilder& builder_,
    const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "GeometryRenderer3D::DescribeGraphTemporalMotionBindings called before Initialize");
    }

    builder_.SetPassShaderContract(
        pass_,
        render::BuildScene3DTemporalMotionShaderContract(
            "geometry_3d_temporal_motion.vert"));

    const std::uint32_t current_skeletal_components_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::
                ResolveSkeletalComponentsExternalBufferBinding,
            .debug_name = "geometry_3d_temporal_motion.current_skeletal_components",
        });
    builder_.AddExternalBufferBinding(
        pass_,
        render::scene_3d_temporal_motion_buffer_set,
        render::scene_3d_temporal_current_skeletal_components_binding,
        render_graph::DescriptorBindingKind::storage_buffer,
        current_skeletal_components_resolver_id,
        render_graph::shader_stage_vertex_flag);

    const std::uint32_t current_skeletal_matrices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::
                ResolveSkeletalMatricesExternalBufferBinding,
            .debug_name = "geometry_3d_temporal_motion.current_skeletal_matrices",
        });
    builder_.AddExternalBufferBinding(
        pass_,
        render::scene_3d_temporal_motion_buffer_set,
        render::scene_3d_temporal_current_skeletal_matrices_binding,
        render_graph::DescriptorBindingKind::storage_buffer,
        current_skeletal_matrices_resolver_id,
        render_graph::shader_stage_vertex_flag);

    const std::uint32_t previous_skeletal_components_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::
                ResolvePreviousSkeletalComponentsExternalBufferBinding,
            .debug_name =
                "geometry_3d_temporal_motion.previous_skeletal_components",
        });
    builder_.AddExternalBufferBinding(
        pass_,
        render::scene_3d_temporal_motion_buffer_set,
        render::scene_3d_temporal_previous_skeletal_components_binding,
        render_graph::DescriptorBindingKind::storage_buffer,
        previous_skeletal_components_resolver_id,
        render_graph::shader_stage_vertex_flag);

    const std::uint32_t previous_skeletal_matrices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::
                ResolvePreviousSkeletalMatricesExternalBufferBinding,
            .debug_name =
                "geometry_3d_temporal_motion.previous_skeletal_matrices",
        });
    builder_.AddExternalBufferBinding(
        pass_,
        render::scene_3d_temporal_motion_buffer_set,
        render::scene_3d_temporal_previous_skeletal_matrices_binding,
        render_graph::DescriptorBindingKind::storage_buffer,
        previous_skeletal_matrices_resolver_id,
        render_graph::shader_stage_vertex_flag);
}

void GeometryRenderer3D::RecordGraphSceneStage(render_graph::GraphCommandContext& context_,
                                               render::SceneRenderStage stage_,
                                               render_graph::ResourceHandle color_target_,
                                               render_graph::ResourceHandle depth_target_) {
    RecordGraphInternal(context_,
                        render::SceneRenderStagePassHintValue(stage_),
                        true,
                        color_target_,
                        depth_target_);
}

void GeometryRenderer3D::RecordGraphTemporalMotion(
    render_graph::GraphCommandContext& context_,
    render_graph::ResourceHandle motion_target_,
    render_graph::ResourceHandle depth_target_) {
    if (!initialized) {
        throw std::runtime_error(
            "GeometryRenderer3D::RecordGraphTemporalMotion called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr ||
        geometry_resource_host == nullptr) {
        throw std::runtime_error(
            "GeometryRenderer3D::RecordGraphTemporalMotion called before PrepareFrame");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "GeometryRenderer3D::RecordGraphTemporalMotion requires valid command buffer");
    }
    const ActiveFrameRuntimeTruth& active_runtime_truth =
        active_frame_runtime_truth;
    const auto& draw_batches = active_prepared_frame_state.artifacts.draw_batches;
    if (!temporal_motion_producer_state_active ||
        !temporal_motion_producer_state.previous_available ||
        active_prepared_frame_state.artifacts.temporal_motion_build_stats
                .previous_match_count == 0U ||
        active_runtime_truth.instance_upload_range.buffer == VK_NULL_HANDLE ||
        active_runtime_truth.temporal_motion_instance_range.buffer ==
            VK_NULL_HANDLE ||
        draw_batches.empty()) {
        return;
    }

    const auto resolved_motion = context_.ResolveTextureView(motion_target_);
    const VkExtent2D render_extent{
        resolved_motion.extent.width,
        resolved_motion.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D::RecordGraphTemporalMotion resolved zero-sized render extent");
    }

    const bool use_depth_attachment =
        render_graph::IsValidResourceHandle(depth_target_);
    const VkFormat active_depth_format = use_depth_attachment
        ? context_.ResolveTextureView(depth_target_).format
        : VK_FORMAT_UNDEFINED;
    EnsureTemporalMotionPipelineObjects(*context,
                                        *pipeline_host,
                                        resolved_motion.format,
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

    const VkBuffer instance_buffers[2] = {
        active_runtime_truth.instance_upload_range.buffer,
        active_runtime_truth.temporal_motion_instance_range.buffer,
    };
    const VkDeviceSize instance_offsets[2] = {
        active_runtime_truth.instance_upload_range.offset,
        active_runtime_truth.temporal_motion_instance_range.offset,
    };
    vkCmdBindVertexBuffers(context_.CommandBuffer(),
                           1U,
                           2U,
                           instance_buffers,
                           instance_offsets);

    TemporalMotionPushConstants push_constants{};
    push_constants.current_view_projection =
        camera_component != nullptr
            ? camera_component->runtime.view_projection_matrix
            : ecs::spatial_math::IdentityMatrix4x4();
    push_constants.current_clip_to_previous_clip =
        temporal_motion_producer_state.current_clip_to_previous_clip;
    push_constants.current_jitter_uv_x =
        temporal_motion_producer_state.current_jitter_uv_x;
    push_constants.current_jitter_uv_y =
        temporal_motion_producer_state.current_jitter_uv_y;
    push_constants.previous_jitter_uv_x =
        temporal_motion_producer_state.previous_jitter_uv_x;
    push_constants.previous_jitter_uv_y =
        temporal_motion_producer_state.previous_jitter_uv_y;
    push_constants.flags = 0U;

    render::GraphicsPipelineId active_pipeline_id{};
    VkBuffer active_vertex_buffer = VK_NULL_HANDLE;
    VkBuffer active_index_buffer = VK_NULL_HANDLE;
    bool shared_state_bound = false;

    for (const ecs::Geometry3DDrawBatch& batch : draw_batches) {
        if (batch.instance_count == 0U ||
            (batch.params & 0x1U) == 0U ||
            (batch.params & 0x2U) == 0U) {
            continue;
        }

        const auto* mesh = geometry_resource_host->FindMesh(batch.geometry_id);
        if (mesh == nullptr || mesh->vertex_buffer.buffer == VK_NULL_HANDLE ||
            mesh->index_buffer.buffer == VK_NULL_HANDLE ||
            mesh->submeshes.empty()) {
            continue;
        }

        const std::uint32_t submesh_index =
            std::min(batch.submesh_index,
                     static_cast<std::uint32_t>(mesh->submeshes.size() - 1U));
        const GeometrySubmeshRange& submesh = mesh->submeshes[submesh_index];
        if (submesh.index_count == 0U) {
            continue;
        }

        const render::GraphicsPipelineId pipeline_id =
            EnsureTemporalMotionPipelineForMode(
                *context,
                *pipeline_host,
                resolved_motion.format,
                active_depth_format,
                use_depth_attachment ? TemporalMotionDepthMode::depth_test
                                     : TemporalMotionDepthMode::no_depth,
                ResolveTopologyMode(mesh->topology, batch),
                ResolveCullMode(batch));
        if (!pipeline_id.IsValid()) {
            continue;
        }

        if (active_pipeline_id.value != pipeline_id.value) {
            vkCmdBindPipeline(context_.CommandBuffer(),
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));
            active_pipeline_id = pipeline_id;
            if (!shared_state_bound) {
                context_.BindCurrentPassDescriptorSets(
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_host->GetPipelineLayout(
                        temporal_motion_pipeline_layout_id),
                    0U,
                    1U);
                shared_state_bound = true;
            }
            vkCmdPushConstants(
                context_.CommandBuffer(),
                pipeline_host->GetPipelineLayout(
                    temporal_motion_pipeline_layout_id),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0U,
                sizeof(TemporalMotionPushConstants),
                &push_constants);
        }

        if (active_vertex_buffer != mesh->vertex_buffer.buffer) {
            const VkBuffer vertex_buffer = mesh->vertex_buffer.buffer;
            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(context_.CommandBuffer(),
                                   0U,
                                   1U,
                                   &vertex_buffer,
                                   &vertex_offset);
            active_vertex_buffer = mesh->vertex_buffer.buffer;
        }

        if (active_index_buffer != mesh->index_buffer.buffer) {
            vkCmdBindIndexBuffer(context_.CommandBuffer(),
                                 mesh->index_buffer.buffer,
                                 0U,
                                 VK_INDEX_TYPE_UINT32);
            active_index_buffer = mesh->index_buffer.buffer;
        }

        vkCmdDrawIndexed(context_.CommandBuffer(),
                         submesh.index_count,
                         batch.instance_count,
                         submesh.first_index,
                         submesh.vertex_offset,
                         batch.instance_begin);
        ++stats.temporal_motion_draw_call_count;
    }
}

void GeometryRenderer3D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                             std::uint32_t pass_bucket_,
                                             bool filter_by_pass_bucket_,
                                             render_graph::ResourceHandle color_target_,
                                             render_graph::ResourceHandle depth_target_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr || geometry_resource_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage called before PrepareFrame");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage requires valid command buffer");
    }

    const ActiveFrameRuntimeTruth& active_runtime_truth =
        active_frame_runtime_truth;
    const auto& draw_batches = active_prepared_frame_state.artifacts.draw_batches;
    if (active_runtime_truth.instance_upload_range.buffer == VK_NULL_HANDLE ||
        draw_batches.empty()) {
        return;
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage resolved zero-sized render extent");
    }

    const bool use_depth_attachment = render_graph::IsValidResourceHandle(depth_target_);
    const VkFormat active_depth_format = use_depth_attachment
        ? context_.ResolveTextureView(depth_target_).format
        : VK_FORMAT_UNDEFINED;

    EnsurePipelineObjects(*context,
                          *pipeline_host,
                          resolved_color.format,
                          use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED);

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

    FramePushConstants frame_push_constants{};
    if (frame_view_projection_override_active) {
        frame_push_constants.view_projection = frame_view_projection_override;
    } else if (camera_component != nullptr) {
        frame_push_constants.view_projection = camera_component->runtime.view_projection_matrix;
    } else {
        frame_push_constants.view_projection = ecs::spatial_math::IdentityMatrix4x4();
    }
    frame_push_constants.directional_light_x = create_info_cache.directional_light_x;
    frame_push_constants.directional_light_y = create_info_cache.directional_light_y;
    frame_push_constants.directional_light_z = create_info_cache.directional_light_z;
    frame_push_constants.directional_light_intensity = std::max(0.0F, create_info_cache.directional_light_intensity);

    const VkPipelineLayout pipeline_layout = pipeline_layout_id.IsValid()
        ? pipeline_host->GetPipelineLayout(pipeline_layout_id)
        : VK_NULL_HANDLE;
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkCmdPushConstants(context_.CommandBuffer(),
                           pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0U,
                           sizeof(FramePushConstants),
                           &frame_push_constants);
    }

    render::GraphicsPipelineId active_pipeline_id{};
    VkBuffer active_vertex_buffer = VK_NULL_HANDLE;
    VkBuffer active_index_buffer = VK_NULL_HANDLE;
    std::uint32_t active_effective_visual_resource_id =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t cached_effective_visual_resource_id =
        std::numeric_limits<std::uint32_t>::max();
    AppearancePushConstants cached_sampling_push_constants{};
    std::uint32_t cached_geometry_id = 0U;
    const GeometryResourceHost::MeshRecord* cached_mesh = nullptr;
    bool shared_state_bound = false;

    std::uint32_t stage_draw_call_count = 0U;
    std::uint32_t stage_filtered_batch_count = 0U;
    const VkBuffer instance_vertex_buffer =
        active_runtime_truth.instance_upload_range.buffer;
    const VkDeviceSize instance_vertex_offset =
        active_runtime_truth.instance_upload_range.offset;
    vkCmdBindVertexBuffers(context_.CommandBuffer(),
                           1U,
                           1U,
                           &instance_vertex_buffer,
                           &instance_vertex_offset);

    for (const ecs::Geometry3DDrawBatch& batch : draw_batches) {
        if (filter_by_pass_bucket_ &&
            ecs::GeometrySystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
            ++stage_filtered_batch_count;
            continue;
        }
        if (batch.instance_count == 0U) {
            continue;
        }

        const GeometryResourceHost::MeshRecord* mesh = nullptr;
        if (cached_mesh != nullptr && cached_geometry_id == batch.geometry_id) {
            mesh = cached_mesh;
        } else {
            mesh = geometry_resource_host->FindMesh(batch.geometry_id);
            cached_mesh = mesh;
            cached_geometry_id = batch.geometry_id;
        }

        if (mesh == nullptr || mesh->index_buffer.buffer == VK_NULL_HANDLE ||
            mesh->vertex_buffer.buffer == VK_NULL_HANDLE || mesh->submeshes.empty()) {
            ++stats.skipped_batch_count;
            continue;
        }

        const std::uint32_t submesh_index = std::min(batch.submesh_index,
                                                     static_cast<std::uint32_t>(mesh->submeshes.size() - 1U));
        const GeometrySubmeshRange& submesh = mesh->submeshes[submesh_index];
        if (submesh.index_count == 0U) {
            ++stats.skipped_batch_count;
            continue;
        }

        const BlendMode blend_mode = ResolveBlendMode(batch);
        const PipelineMode mode = ResolvePipelineMode(batch, use_depth_attachment);
        const TopologyMode topology_mode = ResolveTopologyMode(mesh->topology, batch);
        const CullMode cull_mode = ResolveCullMode(batch);
        const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                             *pipeline_host,
                                                                             resolved_color.format,
                                                                             use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED,
                                                                             blend_mode,
                                                                             mode,
                                                                             topology_mode,
                                                                             cull_mode);
        if (!pipeline_id.IsValid()) {
            ++stats.skipped_batch_count;
            continue;
        }

        AppearancePushConstants sampling_push_constants{};
        if (batch.effective_visual_resource_id == cached_effective_visual_resource_id) {
            sampling_push_constants = cached_sampling_push_constants;
        } else if (!ResolveAppearancePushConstants(batch.effective_visual_resource_id,
                                                   sampling_push_constants)) {
            ++stats.skipped_batch_count;
            continue;
        } else {
            cached_effective_visual_resource_id = batch.effective_visual_resource_id;
            cached_sampling_push_constants = sampling_push_constants;
        }

        if (active_pipeline_id.value != pipeline_id.value) {
            vkCmdBindPipeline(context_.CommandBuffer(),
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));
            active_pipeline_id = pipeline_id;
        }

        if (!shared_state_bound) {
            if (pipeline_layout == VK_NULL_HANDLE) {
                throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage requires valid pipeline layout");
            }
            context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   pipeline_layout,
                                                   0U,
                                                   4U);
            shared_state_bound = true;
            ++stats.descriptor_set_bind_count;
            ++stats.light_descriptor_set_bind_count;
            ++stats.ibl_descriptor_set_bind_count;
        }

        if (pipeline_layout != VK_NULL_HANDLE &&
            active_effective_visual_resource_id != batch.effective_visual_resource_id) {
            vkCmdPushConstants(context_.CommandBuffer(),
                               pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               static_cast<std::uint32_t>(offsetof(PushConstants, appearance)),
                               sizeof(AppearancePushConstants),
                               &sampling_push_constants);
            active_effective_visual_resource_id = batch.effective_visual_resource_id;
            ++stats.appearance_push_constant_update_count;
        }

        if (active_vertex_buffer != mesh->vertex_buffer.buffer) {
            const VkBuffer vertex_buffer = mesh->vertex_buffer.buffer;
            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(context_.CommandBuffer(),
                                   0U,
                                   1U,
                                   &vertex_buffer,
                                   &vertex_offset);
            active_vertex_buffer = mesh->vertex_buffer.buffer;
        }

        if (active_index_buffer != mesh->index_buffer.buffer) {
            vkCmdBindIndexBuffer(context_.CommandBuffer(),
                                 mesh->index_buffer.buffer,
                                 0U,
                                 VK_INDEX_TYPE_UINT32);
            active_index_buffer = mesh->index_buffer.buffer;
        }

        vkCmdDrawIndexed(context_.CommandBuffer(),
                         submesh.index_count,
                         batch.instance_count,
                         submesh.first_index,
                         submesh.vertex_offset,
                         batch.instance_begin);
        ++stats.draw_call_count;
        ++stage_draw_call_count;
    }

    if (filter_by_pass_bucket_) {
        stats.stage_filtered_batch_count += stage_filtered_batch_count;
        if (stage_draw_call_count == 0U) {
            ++stats.empty_stage_pass_count;
        }
        if (pass_bucket_ == static_cast<std::uint32_t>(ecs::GeometryRenderPassHint::opaque)) {
            stats.opaque_draw_call_count += stage_draw_call_count;
        } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::GeometryRenderPassHint::transparent)) {
            stats.transparent_draw_call_count += stage_draw_call_count;
        }
    }

    stats.appearance_resolve_cache_entry_count = static_cast<std::uint32_t>(resolved_appearances.size());
}

} // namespace vr::geometry
