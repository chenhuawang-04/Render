#include "vr/surface/surface_renderer_3d.hpp"

#include "vr/ecs/system/spatial_math.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_context.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/surface/generated/surface_3d_frag_spv.hpp"
#include "vr/surface/generated/surface_3d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace vr::surface {

bool SurfaceRenderer3D::IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool SurfaceRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags SurfaceRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags;
}

VkFormat SurfaceRenderer3D::ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };
    for (VkFormat format : fallback_formats) {
        if (IsDepthFormatSupported(context_, format)) {
            return format;
        }
    }
    throw std::runtime_error("SurfaceRenderer3D failed to resolve usable depth format");
}

std::size_t SurfaceRenderer3D::PipelineModeIndex(PipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t SurfaceRenderer3D::CullModeIndex(CullMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t SurfaceRenderer3D::LowerBoundTextureSetIndex(
    const SurfaceRenderer3DMcVector<TextureSetEntry>& entries_,
    std::uint64_t binding_key_) noexcept {
    std::size_t first = 0U;
    std::size_t count = entries_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (entries_[it].binding_key < binding_key_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

SurfaceRenderer3D::PipelineMode SurfaceRenderer3D::ResolvePipelineMode(std::uint32_t batch_params_,
                                                                       bool use_depth_) noexcept {
    if (!use_depth_ || (batch_params_ & 0x1U) == 0U) {
        return PipelineMode::no_depth;
    }
    if ((batch_params_ & 0x2U) != 0U) {
        return PipelineMode::depth_read_write;
    }
    return PipelineMode::depth_read;
}

SurfaceRenderer3D::CullMode SurfaceRenderer3D::ResolveCullMode(std::uint32_t batch_params_) noexcept {
    const bool double_sided = (batch_params_ & 0x4U) != 0U;
    return double_sided ? CullMode::none : CullMode::back;
}

void SurfaceRenderer3D::Initialize(const SurfaceRenderer3DCreateInfo& create_info_) {
    create_info_cache = create_info_;

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::SurfaceRuntimeSystem<ecs::Dim3>::Reserve(
            runtime_scratch,
            create_info_cache.reserve_component_count,
            create_info_cache.reserve_instance_count);
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch,
                                               create_info_cache.reserve_component_count);
    }
    if (create_info_cache.reserve_dirty_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::SurfaceUploadPlanSystem<ecs::Dim3>::Reserve(
            plan_scratch,
            create_info_cache.reserve_dirty_component_count,
            create_info_cache.reserve_instance_count);
    }

    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& per_mode : pipeline_ids) {
        for (auto& pipeline_id : per_mode) {
            pipeline_id = {};
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;

    depth_format = VK_FORMAT_UNDEFINED;
    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    frame_texture_sets.clear();
    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    fallback_texture = {};
    fallback_sampler_id = {};
    fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_initialized.clear();

    surface_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    surface_upload_host = nullptr;
    surface_image_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;

    last_upload_result = {};
    culling_stats = {};
    stats = {};
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;

    initialized = true;
}

void SurfaceRenderer3D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);
    if (context_.Device() != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyImage(context_, fallback_texture);
    } else {
        fallback_texture = {};
    }
    fallback_sampler_id = {};

    surface_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    surface_upload_host = nullptr;
    surface_image_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;

    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& per_mode : pipeline_ids) {
        for (auto& pipeline_id : per_mode) {
            pipeline_id = {};
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.cache = {};
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_bits.clear();
    culling_stats = {};
    plan_scratch.instance_indices.clear();
    plan_scratch.ranges.clear();
    plan_scratch.dense_marks.clear();
    frame_texture_sets.clear();
    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_initialized.clear();
    last_upload_result = {};
    stats = {};

    depth_format = VK_FORMAT_UNDEFINED;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
    initialized = false;
}

void SurfaceRenderer3D::SetHost(SurfaceUploadHost* upload_host_) noexcept {
    surface_upload_host = upload_host_;
}

void SurfaceRenderer3D::SetImageHost(SurfaceImageHost* image_host_) noexcept {
    surface_image_host = image_host_;
}

void SurfaceRenderer3D::SetHosts(SurfaceUploadHost* upload_host_,
                                 SurfaceImageHost* image_host_) noexcept {
    surface_upload_host = upload_host_;
    surface_image_host = image_host_;
}

void SurfaceRenderer3D::SetSceneData(ecs::Surface<ecs::Dim3>* surface_components_,
                                     ecs::Transform<ecs::Dim3>* transforms_,
                                     std::uint32_t component_count_,
                                     ecs::Camera<ecs::Dim3>* camera_component_,
                                     ecs::Transform<ecs::Dim3>* camera_transform_,
                                     ecs::Bounds<ecs::Dim3>* bounds_components_) noexcept {
    surface_components = surface_components_;
    transforms = transforms_;
    component_count = component_count_;
    camera_component = camera_component_;
    camera_transform = camera_transform_;
    bounds_components = bounds_components_;
    if (component_count_ > 0U) {
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch, component_count_);
    }
}

void SurfaceRenderer3D::SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                                              std::uint32_t dirty_component_count_) noexcept {
    pending_dirty_component_indices = dirty_component_indices_;
    pending_dirty_component_count = dirty_component_count_;
}

void SurfaceRenderer3D::PrepareFrame(const render::RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr ||
        prepare_context_.upload_host == nullptr ||
        prepare_context_.descriptor_host == nullptr ||
        prepare_context_.pipeline_host == nullptr ||
        prepare_context_.gpu_memory_host == nullptr ||
        prepare_context_.sampler_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame missing runtime dependencies");
    }
    if (surface_upload_host == nullptr || !surface_upload_host->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame requires initialized SurfaceUploadHost");
    }

    context = prepare_context_.context;
    upload_host = prepare_context_.upload_host;
    descriptor_host = prepare_context_.descriptor_host;
    pipeline_host = prepare_context_.pipeline_host;
    gpu_memory_host = prepare_context_.gpu_memory_host;
    sampler_host = prepare_context_.sampler_host;
    active_frame_index = prepare_context_.frame_index;
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_context_.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen,
                                           prepare_context_.completed_submit_value);

    surface_upload_host->BeginFrame(*context, active_frame_index);
    if (surface_image_host != nullptr && surface_image_host->IsInitialized()) {
        surface_image_host->BeginFrame(*context, completed_submit_value_seen);
    }
    EnsureFallbackTexture(*context, *upload_host, active_frame_index);

    if (active_frame_index >= frame_texture_sets.size()) {
        frame_texture_sets.resize(active_frame_index + 1U);
    }
    frame_texture_sets[active_frame_index].clear();

    stats = {};
    stats.component_count = component_count;
    last_upload_result = {};
    culling_stats = {};

    if (surface_components == nullptr || component_count == 0U) {
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_bits.clear();
        pending_dirty_component_indices = nullptr;
        pending_dirty_component_count = 0U;
        return;
    }

    ecs::Surface3DRuntimeBuildHint runtime_build_hint{};
    runtime_build_hint.transform_dirty_component_indices = pending_dirty_component_indices;
    runtime_build_hint.transform_dirty_component_count = pending_dirty_component_count;
    if (bounds_components != nullptr && camera_component != nullptr) {
        const ecs::CullingBuildOptions culling_options{
            .enable_culling_mask_filter = true,
            .enable_frustum_culling = true,
            .enable_aabb_refine = true,
            .write_visibility_bits = false
        };
        culling_stats = ecs::CullingSystem<ecs::Dim3>::BuildVisibleSet(bounds_components,
                                                                        component_count,
                                                                        camera_component,
                                                                        culling_scratch,
                                                                        culling_options);
        runtime_build_hint.visible_component_indices = culling_scratch.visible_indices.data();
        runtime_build_hint.visible_component_count = culling_stats.visible_count;
        runtime_build_hint.use_visible_component_indices = 1U;
        runtime_build_hint.external_visible_set_signature = culling_stats.visible_set_signature;
        runtime_build_hint.use_external_visible_set_signature = 1U;

        stats.used_bounds_culling = true;
        stats.culling_input_count = culling_stats.input_count;
        stats.culling_visible_count = culling_stats.visible_count;
        stats.culling_culled_count = culling_stats.culled_by_mask_count +
                                     culling_stats.culled_by_frustum_count +
                                     culling_stats.culled_by_invalid_bounds_count;
        stats.culling_mask_reject_count = culling_stats.culled_by_mask_count;
        stats.culling_frustum_reject_count = culling_stats.culled_by_frustum_count;
        stats.culling_invalid_bounds_count = culling_stats.culled_by_invalid_bounds_count;
        stats.culling_plane_test_count = culling_stats.plane_test_count;
    } else {
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_bits.clear();
    }

    last_upload_result = surface_upload_host->PrepareRuntimeAndUpload3D(
        *context,
        *upload_host,
        active_frame_index,
        surface_components,
        transforms,
        component_count,
        runtime_scratch,
        plan_scratch,
        runtime_build_hint,
        create_info_cache.runtime_upload_options);

    stats.visible_component_count = last_upload_result.runtime.batch.visible_count;
    stats.instance_count = last_upload_result.runtime.emitted_instance_count;
    stats.draw_batch_count = last_upload_result.runtime.emitted_batch_count;
    stats.depth_test_batch_count = last_upload_result.runtime.depth_test_batch_count;
    stats.depth_write_batch_count = last_upload_result.runtime.depth_write_batch_count;
    stats.uploaded_instance_count = last_upload_result.upload.element_count;
    stats.uploaded_patch_count = last_upload_result.upload.patch_count;
    stats.uploaded_bytes = last_upload_result.upload.size_bytes;
    stats.cache_reused = last_upload_result.runtime.cache_reused;
    stats.transform_only_update = last_upload_result.runtime.transform_only_update;
    stats.used_partial_upload = last_upload_result.used_partial_upload;
    stats.skipped_upload = last_upload_result.skipped_upload;

    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
}

void SurfaceRenderer3D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer3D::Record called before Initialize");
    }
    if (context == nullptr ||
        descriptor_host == nullptr ||
        pipeline_host == nullptr ||
        sampler_host == nullptr ||
        gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::Record called before PrepareFrame");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE ||
        record_context_.image == VK_NULL_HANDLE ||
        record_context_.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer3D::Record requires valid frame context image handles");
    }
    if (record_context_.extent.width == 0U || record_context_.extent.height == 0U) {
        throw std::runtime_error("SurfaceRenderer3D::Record received zero-sized swapchain extent");
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

    const resource::ImageResource* depth_resource = nullptr;
    bool depth_initialized = false;
    VkFormat active_depth_format = VK_FORMAT_UNDEFINED;
    if (create_info_cache.enable_depth) {
        const std::uint32_t required_image_count = static_cast<std::uint32_t>(std::max<std::size_t>(
            image_initialized.size(),
            static_cast<std::size_t>(record_context_.image_index + 1U)));
        EnsureDepthResources(*context, required_image_count, record_context_.extent);
        if (record_context_.image_index >= depth_images.size()) {
            throw std::runtime_error("SurfaceRenderer3D::Record depth image index out of range");
        }
        depth_resource = &depth_images[record_context_.image_index];
        if (depth_resource->image == VK_NULL_HANDLE || depth_resource->default_view == VK_NULL_HANDLE) {
            throw std::runtime_error("SurfaceRenderer3D::Record depth resource is invalid");
        }
        depth_initialized = depth_image_initialized[record_context_.image_index] != 0U;
        RecordDepthTransitionToAttachment(record_context_.command_buffer,
                                          *depth_resource,
                                          depth_initialized);
        active_depth_format = depth_format;
    }

    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          record_context_.format,
                          active_depth_format);

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
    if (depth_resource != nullptr) {
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = depth_resource->default_view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment.resolveImageView = VK_NULL_HANDLE;
        depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.loadOp = (create_info_cache.clear_depth || !depth_initialized)
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
    rendering_info.pDepthAttachment = (depth_resource != nullptr) ? &depth_attachment : nullptr;
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

    if (last_upload_result.upload.buffer != VK_NULL_HANDLE &&
        !runtime_scratch.draw_batches.empty()) {
        const VkBuffer vertex_buffer = last_upload_result.upload.buffer;
        const VkDeviceSize vertex_offset = last_upload_result.upload.offset;
        vkCmdBindVertexBuffers(record_context_.command_buffer,
                               0U,
                               1U,
                               &vertex_buffer,
                               &vertex_offset);

        PushConstants push_constants{};
        if (camera_component != nullptr) {
            push_constants.view_projection = camera_component->runtime.view_projection_matrix;
        } else {
            push_constants.view_projection = ecs::spatial_math::IdentityMatrix4x4();
        }
        push_constants.params = 0U;
        push_constants.reserved0 = 0U;
        push_constants.reserved1 = 0U;
        push_constants.reserved2 = 0U;

        render::GraphicsPipelineId bound_pipeline{};
        VkDescriptorSet bound_descriptor_set = VK_NULL_HANDLE;
        for (const ecs::Surface3DDrawBatch& batch : runtime_scratch.draw_batches) {
            if (batch.instance_count == 0U) {
                ++stats.skipped_batch_count;
                continue;
            }

            const PipelineMode mode = ResolvePipelineMode(batch.params, create_info_cache.enable_depth);
            const CullMode cull_mode = ResolveCullMode(batch.params);
            const render::GraphicsPipelineId pipeline_id =
                pipeline_ids[PipelineModeIndex(mode)][CullModeIndex(cull_mode)];
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                continue;
            }

            const VkDescriptorSet descriptor_set = AcquireTextureDescriptorSet(active_frame_index,
                                                                               batch.texture_id,
                                                                               batch.sampler_id);
            if (descriptor_set == VK_NULL_HANDLE) {
                ++stats.skipped_batch_count;
                continue;
            }

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(record_context_.command_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                vkCmdPushConstants(record_context_.command_buffer,
                                   pipeline_host->GetPipelineLayout(pipeline_layout_id),
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                bound_pipeline = pipeline_id;
                bound_descriptor_set = VK_NULL_HANDLE;
            }

            if (bound_descriptor_set != descriptor_set) {
                vkCmdBindDescriptorSets(record_context_.command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_host->GetPipelineLayout(pipeline_layout_id),
                                        0U,
                                        1U,
                                        &descriptor_set,
                                        0U,
                                        nullptr);
                bound_descriptor_set = descriptor_set;
                ++stats.descriptor_set_bind_count;
            }

            vkCmdDraw(record_context_.command_buffer,
                      6U,
                      batch.instance_count,
                      0U,
                      batch.instance_begin);
            ++stats.draw_call_count;
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    RecordImageTransitionToPresent(record_context_);

    image_initialized[record_context_.image_index] = 1U;
    if (create_info_cache.enable_depth && record_context_.image_index < depth_image_initialized.size()) {
        depth_image_initialized[record_context_.image_index] = 1U;
    }
}

void SurfaceRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                             VkExtent2D extent_,
                                             VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void SurfaceRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
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

    if (create_info_cache.enable_depth) {
        depth_image_initialized.resize(image_count_);
        for (auto& value : depth_image_initialized) {
            value = 0U;
        }
    }

    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool SurfaceRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const SurfaceRenderer3DStats& SurfaceRenderer3D::Stats() const noexcept {
    return stats;
}

void SurfaceRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                              render::DescriptorHost& descriptor_host_,
                                              render::PipelineHost& pipeline_host_,
                                              VkFormat color_format_,
                                              VkFormat depth_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        throw std::runtime_error("SurfaceRenderer3D requires Vulkan 1.3 dynamicRendering");
    }
    if (!descriptor_layout_id.IsValid()) {
        render::DescriptorSetLayoutDesc layout_desc{};
        layout_desc.bindings.push_back({
            .binding = 0U,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        });
        descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_surface_3d_vert_spv;
        shader_info.word_count = generated::k_surface_3d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_surface_3d_frag_spv;
        shader_info.word_count = generated::k_surface_3d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!pipeline_layout_id.IsValid()) {
        render::PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(descriptor_host_.GetLayout(descriptor_layout_id));
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
        for (auto& per_mode : pipeline_ids) {
            for (auto& pipeline_id : per_mode) {
                pipeline_id = {};
            }
        }
    }

    for (std::size_t mode_index = 0U; mode_index < static_cast<std::size_t>(PipelineMode::count); ++mode_index) {
        for (std::size_t cull_index = 0U; cull_index < static_cast<std::size_t>(CullMode::count); ++cull_index) {
            if (pipeline_ids[mode_index][cull_index].IsValid()) {
                continue;
            }
            pipeline_ids[mode_index][cull_index] = EnsurePipelineForMode(
                context_,
                pipeline_host_,
                color_format_,
                depth_format_,
                static_cast<PipelineMode>(mode_index),
                static_cast<CullMode>(cull_index));
        }
    }
}

render::GraphicsPipelineId SurfaceRenderer3D::EnsurePipelineForMode(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat color_format_,
    VkFormat depth_format_,
    PipelineMode mode_,
    CullMode cull_mode_) {
    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    desc.use_dynamic_rendering = true;
    desc.rendering.color_attachment_formats.push_back(color_format_);
    desc.rendering.depth_attachment_format = depth_format_;

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
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Surface3DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    desc.vertex_input.attributes.push_back({.location = 0U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0U});
    desc.vertex_input.attributes.push_back({.location = 1U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 16U});
    desc.vertex_input.attributes.push_back({.location = 2U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 32U});
    desc.vertex_input.attributes.push_back({.location = 3U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 48U});
    desc.vertex_input.attributes.push_back({.location = 4U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 64U});
    desc.vertex_input.attributes.push_back({.location = 5U, .binding = 0U, .format = VK_FORMAT_R32_SFLOAT, .offset = 80U});
    desc.vertex_input.attributes.push_back({.location = 6U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 84U});
    desc.vertex_input.attributes.push_back({.location = 7U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 88U});
    desc.vertex_input.attributes.push_back({.location = 8U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 92U});
    desc.vertex_input.attributes.push_back({.location = 9U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 96U});
    desc.vertex_input.attributes.push_back({.location = 10U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 100U});
    desc.vertex_input.attributes.push_back({.location = 11U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 104U});
    desc.vertex_input.attributes.push_back({.location = 12U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 108U});
    desc.vertex_input.attributes.push_back({.location = 13U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 112U});
    desc.vertex_input.attributes.push_back({.location = 14U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 116U});

    desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    desc.rasterization.cull_mode = (cull_mode_ == CullMode::none)
        ? VK_CULL_MODE_NONE
        : VK_CULL_MODE_BACK_BIT;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    switch (mode_) {
    case PipelineMode::no_depth:
        desc.depth_stencil.depth_test_enable = false;
        desc.depth_stencil.depth_write_enable = false;
        desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    case PipelineMode::depth_read:
        desc.depth_stencil.depth_test_enable = true;
        desc.depth_stencil.depth_write_enable = false;
        desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    case PipelineMode::depth_read_write:
        desc.depth_stencil.depth_test_enable = true;
        desc.depth_stencil.depth_write_enable = true;
        desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    default:
        break;
    }

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

    return pipeline_host_.RegisterGraphicsPipeline(context_, desc);
}

void SurfaceRenderer3D::EnsureFallbackTexture(VulkanContext& context_,
                                              render::UploadHost& upload_host_,
                                              std::uint32_t frame_index_) {
    if (sampler_host == nullptr || gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::EnsureFallbackTexture missing runtime hosts");
    }
    if (context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("SurfaceRenderer3D::EnsureFallbackTexture requires synchronization2");
    }

    if (!fallback_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_LINEAR;
        sampler_desc.min_filter = VK_FILTER_LINEAR;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.min_lod = 0.0F;
        sampler_desc.max_lod = 0.0F;
        fallback_sampler_id = sampler_host->RegisterSampler(context_, sampler_desc);
    }

    if (fallback_texture.image != VK_NULL_HANDLE &&
        fallback_texture.default_view != VK_NULL_HANDLE &&
        fallback_texture_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }

    if (fallback_texture.image == VK_NULL_HANDLE ||
        fallback_texture.default_view == VK_NULL_HANDLE) {
        resource::ImageCreateInfo create_info{};
        create_info.image_type = VK_IMAGE_TYPE_2D;
        create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        create_info.extent = VkExtent3D{1U, 1U, 1U};
        create_info.mip_levels = 1U;
        create_info.array_layers = 1U;
        create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        create_info.create_default_view = true;
        create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        create_info.default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.default_base_mip_level = 0U;
        create_info.default_level_count = 1U;
        create_info.default_base_array_layer = 0U;
        create_info.default_layer_count = 1U;
        fallback_texture = resource::ImageHost::CreateImage(context_, create_info, *gpu_memory_host);
        fallback_texture_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (fallback_texture_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }

    VkImageMemoryBarrier2 to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    to_transfer.srcAccessMask = 0U;
    to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = fallback_texture.image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.baseMipLevel = 0U;
    to_transfer.subresourceRange.levelCount = 1U;
    to_transfer.subresourceRange.baseArrayLayer = 0U;
    to_transfer.subresourceRange.layerCount = 1U;
    upload_host_.RecordImageBarrier2(frame_index_, to_transfer);

    constexpr std::uint32_t white_pixel = 0xFFFFFFFFU;
    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0U;
    copy_region.bufferRowLength = 0U;
    copy_region.bufferImageHeight = 0U;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0U;
    copy_region.imageSubresource.baseArrayLayer = 0U;
    copy_region.imageSubresource.layerCount = 1U;
    copy_region.imageOffset = VkOffset3D{0, 0, 0};
    copy_region.imageExtent = VkExtent3D{1U, 1U, 1U};
    upload_host_.StageAndRecordCopyImage(frame_index_,
                                         fallback_texture.image,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         copy_region,
                                         &white_pixel,
                                         sizeof(white_pixel),
                                         4U);

    VkImageMemoryBarrier2 to_shader_read{};
    to_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_shader_read.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_shader_read.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_shader_read.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    to_shader_read.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.image = fallback_texture.image;
    to_shader_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_shader_read.subresourceRange.baseMipLevel = 0U;
    to_shader_read.subresourceRange.levelCount = 1U;
    to_shader_read.subresourceRange.baseArrayLayer = 0U;
    to_shader_read.subresourceRange.layerCount = 1U;
    upload_host_.RecordImageBarrier2(frame_index_, to_shader_read);

    fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

VkDescriptorSet SurfaceRenderer3D::AcquireTextureDescriptorSet(std::uint32_t frame_index_,
                                                               std::uint32_t texture_id_,
                                                               std::uint32_t sampler_id_) {
    if (context == nullptr || descriptor_host == nullptr || sampler_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::AcquireTextureDescriptorSet missing runtime hosts");
    }
    if (!descriptor_layout_id.IsValid()) {
        return VK_NULL_HANDLE;
    }
    if (frame_index_ >= frame_texture_sets.size()) {
        throw std::out_of_range("SurfaceRenderer3D::AcquireTextureDescriptorSet frame index out of range");
    }

    VkSampler sampler = VK_NULL_HANDLE;
    bool explicit_sampler_valid = false;
    if (sampler_id_ != 0U) {
        const resource::SamplerHostStats sampler_stats = sampler_host->Stats();
        if (sampler_id_ <= sampler_stats.sampler_count) {
            sampler = sampler_host->GetSampler(resource::SamplerId{sampler_id_});
            explicit_sampler_valid = sampler != VK_NULL_HANDLE;
        }
    }
    if (sampler == VK_NULL_HANDLE && fallback_sampler_id.IsValid()) {
        sampler = sampler_host->GetSampler(fallback_sampler_id);
    }
    const std::uint32_t effective_sampler_id = explicit_sampler_valid ? sampler_id_ : 0U;

    VkImageView image_view = fallback_texture.default_view;
    VkImageLayout image_layout = fallback_texture_layout;
    std::uint32_t effective_texture_id = 0U;
    if (texture_id_ != 0U && surface_image_host != nullptr && surface_image_host->IsInitialized()) {
        const SurfaceImageHost::ImageRecord* image_record = surface_image_host->FindImage(texture_id_);
        if (image_record != nullptr &&
            image_record->resource.default_view != VK_NULL_HANDLE &&
            image_record->current_layout != VK_IMAGE_LAYOUT_UNDEFINED) {
            image_view = image_record->resource.default_view;
            image_layout = image_record->current_layout;
            effective_texture_id = texture_id_;
        }
    }

    const std::uint64_t binding_key =
        (static_cast<std::uint64_t>(effective_texture_id) << 32U) |
        static_cast<std::uint64_t>(effective_sampler_id);

    SurfaceRenderer3DMcVector<TextureSetEntry>& entries = frame_texture_sets[frame_index_];
    const std::size_t lower_bound_index = LowerBoundTextureSetIndex(entries, binding_key);
    if (lower_bound_index < entries.size() &&
        entries[lower_bound_index].binding_key == binding_key) {
        return entries[lower_bound_index].descriptor_set;
    }

    if (sampler == VK_NULL_HANDLE ||
        image_view == VK_NULL_HANDLE ||
        image_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        return VK_NULL_HANDLE;
    }

    const VkDescriptorSet descriptor_set =
        descriptor_host->AllocateSet(*context, frame_index_, descriptor_layout_id);

    descriptor_buffer_write_scratch.clear();
    descriptor_image_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    descriptor_image_write_scratch.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = sampler,
        .image_view = image_view,
        .image_layout = image_layout
    });
    descriptor_host->UpdateSet(*context,
                               descriptor_set,
                               descriptor_buffer_write_scratch,
                               descriptor_image_write_scratch,
                               descriptor_texel_write_scratch);
    ++stats.descriptor_set_update_count;

    const std::size_t old_size = entries.size();
    entries.resize(old_size + 1U);
    if (lower_bound_index < old_size) {
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            entries[index] = std::move(entries[index - 1U]);
        }
    }
    entries[lower_bound_index] = TextureSetEntry{
        .binding_key = binding_key,
        .descriptor_set = descriptor_set
    };
    return descriptor_set;
}

void SurfaceRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                             std::uint32_t image_count_,
                                             VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::EnsureDepthResources missing GpuMemoryHost");
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
    for (auto& value : depth_image_initialized) {
        value = 0U;
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

void SurfaceRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
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

void SurfaceRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
                                                     std::uint64_t completed_value_) {
    if (retired_depth_images.empty() || context_.Device() == VK_NULL_HANDLE) {
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

void SurfaceRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_image : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_image);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void SurfaceRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

void SurfaceRenderer3D::RecordImageTransitionToColorAttachment(
    const render::FrameRecordContext& record_context_,
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

void SurfaceRenderer3D::RecordImageTransitionToPresent(
    const render::FrameRecordContext& record_context_) const {
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

void SurfaceRenderer3D::RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
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

} // namespace vr::surface
