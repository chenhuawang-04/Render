#include "vr/geometry/geometry_renderer_2d.hpp"

#include "vr/geometry/generated/geometry_2d_frag_spv.hpp"
#include "vr/geometry/generated/geometry_2d_vert_spv.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_context.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace vr::geometry {

void GeometryRenderer2D::Initialize(const GeometryRenderer2DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_primitive_count > 0U) {
        ecs::GeometryRuntimeSystem<ecs::Dim2>::Reserve(runtime_scratch,
                                                       create_info_cache.reserve_component_count,
                                                       create_info_cache.reserve_primitive_count);
    }

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    image_initialized.clear();
    primitive_range = {};
    runtime_stats = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = true;
}

void GeometryRenderer2D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    (void)context_;

    geometry_components = nullptr;
    component_count = 0U;
    geometry_upload_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    pipeline_host = nullptr;

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    image_initialized.clear();
    primitive_range = {};
    runtime_scratch.primitives.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.cache = {};
    runtime_stats = {};
    stats = {};

    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = false;
}

void GeometryRenderer2D::SetHost(GeometryUploadHost* upload_host_) noexcept {
    geometry_upload_host = upload_host_;
}

void GeometryRenderer2D::SetSceneData(ecs::Geometry<ecs::Dim2>* geometry_components_,
                                      std::uint32_t component_count_) noexcept {
    geometry_components = geometry_components_;
    component_count = component_count_;
}

void GeometryRenderer2D::PrepareFrame(const render::RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer2D::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr ||
        prepare_context_.upload_host == nullptr ||
        prepare_context_.pipeline_host == nullptr) {
        throw std::runtime_error("GeometryRenderer2D::PrepareFrame missing runtime dependencies");
    }
    if (geometry_upload_host == nullptr || !geometry_upload_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer2D::PrepareFrame requires initialized GeometryUploadHost");
    }

    context = prepare_context_.context;
    upload_host = prepare_context_.upload_host;
    pipeline_host = prepare_context_.pipeline_host;
    active_frame_index = prepare_context_.frame_index;
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_context_.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen, prepare_context_.completed_submit_value);

    geometry_upload_host->BeginFrame(*context, active_frame_index);

    stats = {};
    stats.component_count = component_count;
    primitive_range = {};
    runtime_stats = {};

    if (geometry_components == nullptr || component_count == 0U) {
        runtime_scratch.primitives.clear();
        runtime_scratch.draw_batches.clear();
        return;
    }

    runtime_stats = ecs::GeometryRuntimeSystem<ecs::Dim2>::Build(geometry_components,
                                                                  component_count,
                                                                  runtime_scratch,
                                                                  create_info_cache.runtime_build);
    stats.visible_component_count = runtime_stats.batch.visible_count;
    stats.primitive_count = runtime_stats.emitted_primitive_count;
    stats.draw_batch_count = runtime_stats.emitted_batch_count;
    stats.cache_reused = runtime_stats.cache_reused;

    if (runtime_scratch.primitives.empty()) {
        return;
    }

    primitive_range = geometry_upload_host->Upload2DPrimitives(*context,
                                                                *upload_host,
                                                                active_frame_index,
                                                                runtime_scratch.primitives.data(),
                                                                static_cast<std::uint32_t>(runtime_scratch.primitives.size()),
                                                                runtime_stats.build_signature);
    if (primitive_range.uploaded) {
        stats.uploaded_primitive_count = primitive_range.element_count;
        stats.uploaded_bytes = primitive_range.size_bytes;
    }
}

void GeometryRenderer2D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer2D::Record called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("GeometryRenderer2D::Record called before PrepareFrame");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE ||
        record_context_.image == VK_NULL_HANDLE ||
        record_context_.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("GeometryRenderer2D::Record requires valid frame context image handles");
    }
    if (record_context_.extent.width == 0U || record_context_.extent.height == 0U) {
        throw std::runtime_error("GeometryRenderer2D::Record received zero-sized swapchain extent");
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
    EnsurePipelineObjects(*context, *pipeline_host, record_context_.format);

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

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = VkOffset2D{0, 0};
    rendering_info.renderArea.extent = record_context_.extent;
    rendering_info.layerCount = 1U;
    rendering_info.colorAttachmentCount = 1U;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = nullptr;
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

    if (pipeline_id.IsValid()) {
        vkCmdBindPipeline(record_context_.command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_host->GetGraphicsPipeline(pipeline_id));

        PushConstants push_constants{};
        push_constants.viewport_width = static_cast<float>(record_context_.extent.width);
        push_constants.viewport_height = static_cast<float>(record_context_.extent.height);
        push_constants.inv_viewport_width_2x = (record_context_.extent.width > 0U)
            ? (2.0F / static_cast<float>(record_context_.extent.width))
            : 0.0F;
        push_constants.inv_viewport_height_2x = (record_context_.extent.height > 0U)
            ? (2.0F / static_cast<float>(record_context_.extent.height))
            : 0.0F;
        push_constants.params = 0U;
        push_constants.params |= create_info_cache.input_positions_pixel_space ? 0x1U : 0U;
        push_constants.params |= create_info_cache.pixel_space_origin_top_left ? 0x2U : 0U;
        push_constants.reserved0 = 0U;
        push_constants.reserved1 = 0U;
        push_constants.reserved2 = 0U;
        vkCmdPushConstants(record_context_.command_buffer,
                           pipeline_host->GetPipelineLayout(pipeline_layout_id),
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0U,
                           sizeof(PushConstants),
                           &push_constants);
    }

    if (pipeline_id.IsValid() &&
        primitive_range.buffer != VK_NULL_HANDLE &&
        !runtime_scratch.draw_batches.empty()) {
        vkCmdBindVertexBuffers(record_context_.command_buffer,
                               0U,
                               1U,
                               &primitive_range.buffer,
                               &primitive_range.offset);

        for (const ecs::Geometry2DDrawBatch& batch : runtime_scratch.draw_batches) {
            if (batch.primitive_count == 0U) {
                ++stats.skipped_batch_count;
                continue;
            }

            vkCmdDraw(record_context_.command_buffer,
                      6U,
                      batch.primitive_count,
                      0U,
                      batch.primitive_begin);
            ++stats.draw_call_count;
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    RecordImageTransitionToPresent(record_context_);
    image_initialized[record_context_.image_index] = 1U;
}

void GeometryRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void GeometryRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_,
                                              std::uint64_t last_submitted_value_,
                                              std::uint64_t completed_submit_value_) {
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);

    image_initialized.resize(image_count_);
    for (auto& value : image_initialized) {
        value = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool GeometryRenderer2D::IsInitialized() const noexcept {
    return initialized;
}

const GeometryRenderer2DStats& GeometryRenderer2D::Stats() const noexcept {
    return stats;
}

void GeometryRenderer2D::EnsurePipelineObjects(VulkanContext& context_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        throw std::runtime_error("GeometryRenderer2D requires Vulkan 1.3 dynamicRendering");
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_geometry_2d_vert_spv;
        shader_info.word_count = generated::k_geometry_2d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_geometry_2d_frag_spv;
        shader_info.word_count = generated::k_geometry_2d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!pipeline_layout_id.IsValid()) {
        render::PipelineLayoutDesc layout_desc{};
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_color_format != color_format_) {
        pipeline_color_format = color_format_;
        pipeline_id = {};
    }
    if (pipeline_id.IsValid()) {
        return;
    }

    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    desc.use_dynamic_rendering = true;
    desc.rendering.color_attachment_formats.push_back(color_format_);
    desc.rendering.depth_attachment_format = VK_FORMAT_UNDEFINED;

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
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Geometry2DPathPrimitive)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    desc.vertex_input.attributes.push_back({.location = 0U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0U});
    desc.vertex_input.attributes.push_back({.location = 1U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 16U});
    desc.vertex_input.attributes.push_back({.location = 2U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 20U});
    desc.vertex_input.attributes.push_back({.location = 3U, .binding = 0U, .format = VK_FORMAT_R32_SFLOAT, .offset = 24U});
    desc.vertex_input.attributes.push_back({.location = 4U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 28U});
    desc.vertex_input.attributes.push_back({.location = 5U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 32U});
    desc.vertex_input.attributes.push_back({.location = 6U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = 36U});

    desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    desc.depth_stencil.depth_test_enable = false;
    desc.depth_stencil.depth_write_enable = false;

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

    pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, desc);
}

void GeometryRenderer2D::RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_,
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

void GeometryRenderer2D::RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const {
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

} // namespace vr::geometry
