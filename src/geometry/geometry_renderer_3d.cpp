#include "vr/geometry/geometry_renderer_3d.hpp"

#include "vr/geometry/generated/geometry_3d_frag_spv.hpp"
#include "vr/geometry/generated/geometry_3d_vert_spv.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_context.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vr::geometry {

bool GeometryRenderer3D::IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool GeometryRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags GeometryRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags;
}

VkFormat GeometryRenderer3D::ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (const VkFormat format : fallback_formats) {
        if (IsDepthFormatSupported(context_, format)) {
            return format;
        }
    }
    throw std::runtime_error("GeometryRenderer3D failed to resolve usable depth format");
}

std::size_t GeometryRenderer3D::PipelineModeIndex(PipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t GeometryRenderer3D::TopologyModeIndex(TopologyMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t GeometryRenderer3D::CullModeIndex(CullMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

GeometryRenderer3D::PipelineMode GeometryRenderer3D::ResolvePipelineMode(
    const ecs::Geometry3DDrawBatch& batch_,
    bool use_depth_) noexcept {
    if (!use_depth_ || (batch_.params & 0x1U) == 0U) {
        return PipelineMode::no_depth;
    }
    if ((batch_.params & 0x2U) != 0U) {
        return PipelineMode::depth_read_write;
    }
    return PipelineMode::depth_read;
}

GeometryRenderer3D::TopologyMode GeometryRenderer3D::ResolveTopologyMode(
    VkPrimitiveTopology mesh_topology_,
    const ecs::Geometry3DDrawBatch& batch_) noexcept {
    switch (mesh_topology_) {
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        return TopologyMode::triangles;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
        return TopologyMode::lines;
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
        return TopologyMode::points;
    default:
        break;
    }

    const std::uint32_t topology_bits = (batch_.params >> 5U) & 0x3U;
    if (topology_bits == 1U) {
        return TopologyMode::lines;
    }
    if (topology_bits == 2U) {
        return TopologyMode::points;
    }
    return TopologyMode::triangles;
}

GeometryRenderer3D::CullMode GeometryRenderer3D::ResolveCullMode(const ecs::Geometry3DDrawBatch& batch_) noexcept {
    const bool double_sided = (batch_.params & 0x4U) != 0U;
    return double_sided ? CullMode::none : CullMode::back;
}

void GeometryRenderer3D::Initialize(const GeometryRenderer3DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::GeometryRuntimeSystem<ecs::Dim3>::Reserve(runtime_scratch,
                                                       create_info_cache.reserve_component_count,
                                                       create_info_cache.reserve_instance_count);
    }

    if (!std::isfinite(create_info_cache.clear_depth_value)) {
        create_info_cache.clear_depth_value = 1.0F;
    }
    create_info_cache.clear_depth_value = std::clamp(create_info_cache.clear_depth_value, 0.0F, 1.0F);

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& mode_pipelines : pipeline_ids) {
        for (auto& topology_pipelines : mode_pipelines) {
            for (auto& pipeline_id : topology_pipelines) {
                pipeline_id = {};
            }
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    image_initialized.clear();
    runtime_stats = {};
    instance_range = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    stats = {};
    initialized = true;
}

void GeometryRenderer3D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);
    image_initialized.clear();

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& mode_pipelines : pipeline_ids) {
        for (auto& topology_pipelines : mode_pipelines) {
            for (auto& pipeline_id : topology_pipelines) {
                pipeline_id = {};
            }
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    geometry_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    geometry_resource_host = nullptr;
    geometry_upload_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    instance_range = {};

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_stats = {};
    stats = {};

    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = false;
}

void GeometryRenderer3D::SetHosts(GeometryResourceHost* resource_host_,
                                  GeometryUploadHost* upload_host_) noexcept {
    geometry_resource_host = resource_host_;
    geometry_upload_host = upload_host_;
}

void GeometryRenderer3D::SetSceneData(ecs::Geometry<ecs::Dim3>* geometry_components_,
                                      ecs::Transform<ecs::Dim3>* transforms_,
                                      std::uint32_t component_count_,
                                      ecs::Camera<ecs::Dim3>* camera_component_,
                                      ecs::Transform<ecs::Dim3>* camera_transform_) noexcept {
    geometry_components = geometry_components_;
    transforms = transforms_;
    component_count = component_count_;
    camera_component = camera_component_;
    camera_transform = camera_transform_;
}

void GeometryRenderer3D::PrepareFrame(const render::RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr ||
        prepare_context_.upload_host == nullptr ||
        prepare_context_.pipeline_host == nullptr ||
        prepare_context_.gpu_memory_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame missing runtime dependencies");
    }
    if (geometry_resource_host == nullptr || !geometry_resource_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryResourceHost");
    }
    if (geometry_upload_host == nullptr || !geometry_upload_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryUploadHost");
    }

    context = prepare_context_.context;
    upload_host = prepare_context_.upload_host;
    pipeline_host = prepare_context_.pipeline_host;
    gpu_memory_host = prepare_context_.gpu_memory_host;
    active_frame_index = prepare_context_.frame_index;
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_context_.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen, prepare_context_.completed_submit_value);
    CollectRetiredDepthResources(*context, completed_submit_value_seen);
    geometry_resource_host->BeginFrame(*context, completed_submit_value_seen);
    geometry_upload_host->BeginFrame(*context, active_frame_index);

    stats = {};
    stats.component_count = component_count;
    instance_range = {};

    if (geometry_components == nullptr ||
        component_count == 0U) {
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        runtime_stats = {};
        return;
    }

    runtime_stats = ecs::GeometryRuntimeSystem<ecs::Dim3>::Build(geometry_components,
                                                                  transforms,
                                                                  component_count,
                                                                  runtime_scratch,
                                                                  create_info_cache.runtime_build);
    stats.visible_component_count = runtime_stats.batch.visible_count;
    stats.instance_count = runtime_stats.emitted_instance_count;
    stats.draw_batch_count = runtime_stats.emitted_batch_count;
    stats.depth_test_batch_count = runtime_stats.depth_test_batch_count;
    stats.depth_write_batch_count = runtime_stats.depth_write_batch_count;
    stats.shadow_cast_batch_count = runtime_stats.shadow_cast_batch_count;
    stats.cache_reused = runtime_stats.cache_reused;
    stats.transform_only_update = runtime_stats.transform_only_update;

    if (runtime_scratch.instances.empty()) {
        return;
    }

    const std::uint64_t upload_revision =
        runtime_stats.geometry_signature ^ (runtime_stats.transform_signature * 0x9e3779b97f4a7c15ULL);
    instance_range = geometry_upload_host->Upload3DInstances(*context,
                                                             *upload_host,
                                                             active_frame_index,
                                                             runtime_scratch.instances.data(),
                                                             static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                                                             upload_revision);
    if (instance_range.uploaded) {
        stats.uploaded_instance_count = instance_range.element_count;
        stats.uploaded_bytes = instance_range.size_bytes;
    }
}

void GeometryRenderer3D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::Record called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr || geometry_resource_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::Record called before PrepareFrame");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE ||
        record_context_.image == VK_NULL_HANDLE ||
        record_context_.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("GeometryRenderer3D::Record requires valid frame context image handles");
    }
    if (record_context_.extent.width == 0U || record_context_.extent.height == 0U) {
        throw std::runtime_error("GeometryRenderer3D::Record received zero-sized swapchain extent");
    }

    if (record_context_.image_index >= image_initialized.size()) {
        const std::size_t previous_size = image_initialized.size();
        image_initialized.resize(record_context_.image_index + 1U);
        for (std::size_t i = previous_size; i < image_initialized.size(); ++i) {
            image_initialized[i] = 0U;
        }
    }
    const bool has_previous_content = image_initialized[record_context_.image_index] != 0U;
    RecordImageTransitionToColorAttachment(record_context_, has_previous_content);

    bool use_depth_attachment = false;
    if (create_info_cache.enable_depth) {
        if (depth_format == VK_FORMAT_UNDEFINED) {
            depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
        }
        EnsureDepthResources(*context,
                             static_cast<std::uint32_t>(image_initialized.size()),
                             record_context_.extent);
        use_depth_attachment = record_context_.image_index < depth_images.size() &&
                              depth_images[record_context_.image_index].default_view != VK_NULL_HANDLE;
    }

    EnsurePipelineObjects(*context,
                          *pipeline_host,
                          record_context_.format,
                          use_depth_attachment ? depth_format : VK_FORMAT_UNDEFINED);

    if (use_depth_attachment) {
        if (record_context_.image_index >= depth_image_initialized.size()) {
            const std::size_t old_size = depth_image_initialized.size();
            depth_image_initialized.resize(record_context_.image_index + 1U);
            for (std::size_t i = old_size; i < depth_image_initialized.size(); ++i) {
                depth_image_initialized[i] = 0U;
            }
        }
        RecordDepthTransitionToAttachment(record_context_.command_buffer,
                                          depth_images[record_context_.image_index],
                                          depth_image_initialized[record_context_.image_index] != 0U);
    }

    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = record_context_.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.resolveImageView = VK_NULL_HANDLE;
    color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.loadOp = (create_info_cache.clear_swapchain || !has_previous_content)
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = create_info_cache.clear_color;

    VkRenderingAttachmentInfo depth_attachment{};
    if (use_depth_attachment) {
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = depth_images[record_context_.image_index].default_view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment.resolveImageView = VK_NULL_HANDLE;
        depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.loadOp = create_info_cache.clear_depth
            ? VK_ATTACHMENT_LOAD_OP_CLEAR
            : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue.depthStencil.depth = create_info_cache.clear_depth_value;
        depth_attachment.clearValue.depthStencil.stencil = create_info_cache.clear_stencil_value;
    }

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = VkOffset2D{0, 0};
    rendering_info.renderArea.extent = record_context_.extent;
    rendering_info.layerCount = 1U;
    rendering_info.colorAttachmentCount = 1U;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = use_depth_attachment ? &depth_attachment : nullptr;
    rendering_info.pStencilAttachment = nullptr;
    vkCmdBeginRendering(record_context_.command_buffer, &rendering_info);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(record_context_.extent.width);
    viewport.height = static_cast<float>(record_context_.extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = record_context_.extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    PushConstants push_constants{};
    if (camera_component != nullptr) {
        push_constants.view_projection = camera_component->runtime.view_projection_matrix;
    } else {
        push_constants.view_projection = ecs::spatial_math::IdentityMatrix4x4();
    }
    push_constants.directional_light_x = create_info_cache.directional_light_x;
    push_constants.directional_light_y = create_info_cache.directional_light_y;
    push_constants.directional_light_z = create_info_cache.directional_light_z;
    push_constants.directional_light_intensity = std::max(0.0F, create_info_cache.directional_light_intensity);

    if (pipeline_layout_id.IsValid()) {
        vkCmdPushConstants(record_context_.command_buffer,
                           pipeline_host->GetPipelineLayout(pipeline_layout_id),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0U,
                           sizeof(PushConstants),
                           &push_constants);
    }

    render::GraphicsPipelineId active_pipeline_id{};
    VkBuffer active_vertex_buffer = VK_NULL_HANDLE;
    VkBuffer active_index_buffer = VK_NULL_HANDLE;
    std::uint32_t cached_geometry_id = 0U;
    const GeometryResourceHost::MeshRecord* cached_mesh = nullptr;
    if (instance_range.buffer != VK_NULL_HANDLE && !runtime_scratch.draw_batches.empty()) {
        const VkBuffer instance_vertex_buffer = instance_range.buffer;
        const VkDeviceSize instance_vertex_offset = instance_range.offset;
        vkCmdBindVertexBuffers(record_context_.command_buffer,
                               1U,
                               1U,
                               &instance_vertex_buffer,
                               &instance_vertex_offset);

        for (const ecs::Geometry3DDrawBatch& batch : runtime_scratch.draw_batches) {
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

            const PipelineMode mode = ResolvePipelineMode(batch, use_depth_attachment);
            const TopologyMode topology_mode = ResolveTopologyMode(mesh->topology, batch);
            const CullMode cull_mode = ResolveCullMode(batch);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                  *pipeline_host,
                                                                                  record_context_.format,
                                                                                  use_depth_attachment ? depth_format : VK_FORMAT_UNDEFINED,
                                                                                  mode,
                                                                                  topology_mode,
                                                                                  cull_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                continue;
            }

            if (active_pipeline_id.value != pipeline_id.value) {
                vkCmdBindPipeline(record_context_.command_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                active_pipeline_id = pipeline_id;
            }

            if (active_vertex_buffer != mesh->vertex_buffer.buffer) {
                const VkBuffer vertex_buffer = mesh->vertex_buffer.buffer;
                const VkDeviceSize vertex_offset = 0U;
                vkCmdBindVertexBuffers(record_context_.command_buffer,
                                       0U,
                                       1U,
                                       &vertex_buffer,
                                       &vertex_offset);
                active_vertex_buffer = mesh->vertex_buffer.buffer;
            }

            if (active_index_buffer != mesh->index_buffer.buffer) {
                vkCmdBindIndexBuffer(record_context_.command_buffer,
                                     mesh->index_buffer.buffer,
                                     0U,
                                     VK_INDEX_TYPE_UINT32);
                active_index_buffer = mesh->index_buffer.buffer;
            }

            vkCmdDrawIndexed(record_context_.command_buffer,
                             submesh.index_count,
                             batch.instance_count,
                             submesh.first_index,
                             submesh.vertex_offset,
                             batch.instance_begin);
            ++stats.draw_call_count;
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    RecordImageTransitionToPresent(record_context_);
    image_initialized[record_context_.image_index] = 1U;
    if (use_depth_attachment && record_context_.image_index < depth_image_initialized.size()) {
        depth_image_initialized[record_context_.image_index] = 1U;
    }
}

void GeometryRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void GeometryRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_,
                                              std::uint64_t last_submitted_value_,
                                              std::uint64_t completed_submit_value_) {
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);

    if (context != nullptr && create_info_cache.enable_depth) {
        RetireDepthResources(last_submitted_value_seen);
        CollectRetiredDepthResources(*context, completed_submit_value_seen);
    }

    image_initialized.resize(image_count_);
    for (auto& value : image_initialized) {
        value = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool GeometryRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const GeometryRenderer3DStats& GeometryRenderer3D::Stats() const noexcept {
    return stats;
}

void GeometryRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_,
                                               VkFormat depth_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        throw std::runtime_error("GeometryRenderer3D requires Vulkan 1.3 dynamicRendering");
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_geometry_3d_vert_spv;
        shader_info.word_count = generated::k_geometry_3d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_geometry_3d_frag_spv;
        shader_info.word_count = generated::k_geometry_3d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!pipeline_layout_id.IsValid()) {
        render::PipelineLayoutDesc layout_desc{};
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
        for (auto& mode_pipelines : pipeline_ids) {
            for (auto& topology_pipelines : mode_pipelines) {
                for (auto& pipeline_id : topology_pipelines) {
                    pipeline_id = {};
                }
            }
        }
    }
}

render::GraphicsPipelineId GeometryRenderer3D::EnsurePipelineForMode(VulkanContext& context_,
                                                                     render::PipelineHost& pipeline_host_,
                                                                     VkFormat color_format_,
                                                                     VkFormat depth_format_,
                                                                     PipelineMode mode_,
                                                                     TopologyMode topology_mode_,
                                                                     CullMode cull_mode_) {
    const std::size_t mode_index = PipelineModeIndex(mode_);
    const std::size_t topology_index = TopologyModeIndex(topology_mode_);
    const std::size_t cull_index = CullModeIndex(cull_mode_);
    if (mode_index >= pipeline_ids.size()) {
        throw std::out_of_range("GeometryRenderer3D pipeline mode out of range");
    }
    if (topology_index >= pipeline_ids[mode_index].size()) {
        throw std::out_of_range("GeometryRenderer3D topology mode out of range");
    }
    if (cull_index >= pipeline_ids[mode_index][topology_index].size()) {
        throw std::out_of_range("GeometryRenderer3D cull mode out of range");
    }
    if (pipeline_ids[mode_index][topology_index][cull_index].IsValid()) {
        return pipeline_ids[mode_index][topology_index][cull_index];
    }

    const bool depth_test = mode_ == PipelineMode::depth_read || mode_ == PipelineMode::depth_read_write;
    const bool depth_write = mode_ == PipelineMode::depth_read_write;
    const bool cull_back = cull_mode_ == CullMode::back;

    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    desc.use_dynamic_rendering = true;
    desc.rendering.color_attachment_formats.push_back(color_format_);
    desc.rendering.depth_attachment_format = depth_test ? depth_format_ : VK_FORMAT_UNDEFINED;

    desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(shader_fragment_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });

    desc.vertex_input.bindings.push_back({
        .binding = 0U,
        .stride = static_cast<std::uint32_t>(sizeof(GeometryMeshVertex)),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    });
    desc.vertex_input.bindings.push_back({
        .binding = 1U,
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Geometry3DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    desc.vertex_input.attributes.push_back({.location = 0U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0U});
    desc.vertex_input.attributes.push_back({.location = 1U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12U});
    desc.vertex_input.attributes.push_back({.location = 2U, .binding = 0U, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 24U});
    desc.vertex_input.attributes.push_back({.location = 3U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0U});
    desc.vertex_input.attributes.push_back({.location = 4U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 16U});
    desc.vertex_input.attributes.push_back({.location = 5U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 32U});
    desc.vertex_input.attributes.push_back({.location = 6U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 48U});
    desc.vertex_input.attributes.push_back({.location = 7U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 96U});
    desc.vertex_input.attributes.push_back({.location = 8U, .binding = 1U, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 112U});
    desc.vertex_input.attributes.push_back({.location = 9U, .binding = 1U, .format = VK_FORMAT_R32_UINT, .offset = 116U});

    switch (topology_mode_) {
    case TopologyMode::triangles:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    case TopologyMode::lines:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case TopologyMode::points:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        break;
    default:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    }
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    desc.rasterization.cull_mode = cull_back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    desc.depth_stencil.depth_test_enable = depth_test;
    desc.depth_stencil.depth_write_enable = depth_write;
    desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                           VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    desc.color_blend.attachments.push_back(blend);

    const render::GraphicsPipelineId pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, desc);
    pipeline_ids[mode_index][topology_index][cull_index] = pipeline_id;
    return pipeline_id;
}

void GeometryRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                              std::uint32_t image_count_,
                                              VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::EnsureDepthResources missing GpuMemoryHost");
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.preferred_depth_format);
    }

    const bool compatible = depth_images.size() == image_count_ &&
                            !depth_images.empty() &&
                            depth_images[0U].format == depth_format &&
                            depth_images[0U].extent.width == extent_.width &&
                            depth_images[0U].extent.height == extent_.height;
    if (compatible) {
        return;
    }

    RetireDepthResources(last_submitted_value_seen);
    CollectRetiredDepthResources(context_, completed_submit_value_seen);

    depth_images.resize(image_count_);
    depth_image_initialized.resize(image_count_);
    for (auto& flag : depth_image_initialized) {
        flag = 0U;
    }

    for (std::uint32_t i = 0U; i < image_count_; ++i) {
        resource::ImageCreateInfo create_info{};
        create_info.image_type = VK_IMAGE_TYPE_2D;
        create_info.format = depth_format;
        create_info.extent = VkExtent3D{extent_.width, extent_.height, 1U};
        create_info.mip_levels = 1U;
        create_info.array_layers = 1U;
        create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        create_info.create_default_view = true;
        create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        create_info.default_view_aspect = DepthImageAspectMask(depth_format);
        create_info.default_base_mip_level = 0U;
        create_info.default_level_count = 1U;
        create_info.default_base_array_layer = 0U;
        create_info.default_layer_count = 1U;
        depth_images[i] = resource::ImageHost::CreateImage(context_, create_info, *gpu_memory_host);
    }
}

void GeometryRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
    if (depth_images.empty()) {
        return;
    }
    retired_depth_images.reserve(retired_depth_images.size() + depth_images.size());
    for (auto& depth_image : depth_images) {
        if (depth_image.image == VK_NULL_HANDLE) {
            continue;
        }
        RetiredDepthImage retired{};
        retired.resource = depth_image;
        retired.retire_value = retire_value_;
        retired_depth_images.push_back(retired);
        depth_image = {};
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void GeometryRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
                                                      std::uint64_t completed_value_) {
    if (retired_depth_images.empty()) {
        return;
    }
    if (context_.Device() == VK_NULL_HANDLE) {
        return;
    }

    std::size_t write_index = 0U;
    for (std::size_t read_index = 0U; read_index < retired_depth_images.size(); ++read_index) {
        auto& retired = retired_depth_images[read_index];
        if (retired.retire_value <= completed_value_) {
            resource::ImageHost::DestroyImage(context_, retired.resource);
            continue;
        }
        if (write_index != read_index) {
            retired_depth_images[write_index] = retired;
        }
        ++write_index;
    }
    retired_depth_images.resize(write_index);
}

void GeometryRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_image : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_image);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void GeometryRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

void GeometryRenderer3D::RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_,
                                                                bool has_previous_content_) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = has_previous_content_ ? VK_ACCESS_MEMORY_READ_BIT : 0U;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = has_previous_content_ ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record_context_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(record_context_.command_buffer,
                         has_previous_content_
                             ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

void GeometryRenderer3D::RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record_context_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(record_context_.command_buffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

void GeometryRenderer3D::RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
                                                           const resource::ImageResource& depth_resource_,
                                                           bool initialized_) const {
    if (command_buffer_ == VK_NULL_HANDLE || depth_resource_.image == VK_NULL_HANDLE) {
        return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = initialized_
        ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
        : 0U;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = initialized_
        ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depth_resource_.image;
    barrier.subresourceRange.aspectMask = DepthImageAspectMask(depth_format);
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(command_buffer_,
                         initialized_
                             ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

} // namespace vr::geometry
