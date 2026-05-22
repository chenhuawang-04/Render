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

void TextRenderer2D::ResetPerFrameDrawState(std::uint32_t frame_index_,
                                            std::uint32_t atlas_page_count_) {
    (void)atlas_page_count_;
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    frame_state.instance_count = 0U;
    frame_state.graph_vertex_buffer = render_graph::invalid_resource_handle;
    frame_state.graph_vertex_version = render_graph::invalid_resource_version;
    frame_state.graph_vertex_size_bytes = 0U;
}

void TextRenderer2D::BuildGpuInstancesFromScratch() {
    gpu_instances.clear();
    if (runtime_scratch.glyph_quads.empty()) {
        return;
    }

    const bool pixel_snap_enabled = create_info_cache.enable_pixel_snap;
    const float pixel_snap_step = create_info_cache.pixel_snap_step;

    gpu_instances.resize(runtime_scratch.glyph_quads.size());
    for (std::size_t i = 0; i < runtime_scratch.glyph_quads.size(); ++i) {
        const ecs::TextGlyphQuad& quad = runtime_scratch.glyph_quads[i];
        GpuTextInstance instance{};
        instance.rect_x0 = quad.x0;
        instance.rect_y0 = quad.y0;
        instance.rect_x1 = quad.x1;
        instance.rect_y1 = quad.y1;
        if (pixel_snap_enabled) {
            instance.rect_x0 = QuantizeToStep(instance.rect_x0, pixel_snap_step);
            instance.rect_y0 = QuantizeToStep(instance.rect_y0, pixel_snap_step);
            instance.rect_x1 = QuantizeToStep(instance.rect_x1, pixel_snap_step);
            instance.rect_y1 = QuantizeToStep(instance.rect_y1, pixel_snap_step);
        }
        instance.uv_u0 = quad.u0;
        instance.uv_v0 = quad.v0;
        instance.uv_u1 = quad.u1;
        instance.uv_v1 = quad.v1;
        instance.color_rgba8 = PackRgba8(quad.color);
        instance.outline_color_rgba8 = PackRgba8(quad.outline_color);
        instance.params = PackParams(quad);
        gpu_instances[i] = instance;
    }
}

void TextRenderer2D::EnsureGpuResourcesForFrame(VulkanContext& context_,
                                                const render::TextRenderer2DPrepareView& prepare_view_,
                                                std::uint32_t frame_index_,
                                                VkDeviceSize required_bytes_) {
    EnsureGraphUploadStagingForFrame(context_,
                                     prepare_view_,
                                     frame_index_,
                                     required_bytes_);

    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    const VkDeviceSize required_capacity = (required_bytes_ > 0U)
        ? std::max(create_info_cache.initial_vertex_buffer_bytes,
                   NextPow2(required_bytes_))
        : frame_state.vertex_buffer_capacity_bytes;

    if (required_bytes_ == 0U &&
        frame_state.vertex_buffer.buffer == VK_NULL_HANDLE &&
        frame_state.vertex_buffer_capacity_bytes == 0U) {
        return;
    }
    if (frame_state.vertex_buffer.buffer != VK_NULL_HANDLE &&
        frame_state.vertex_buffer_capacity_bytes >= required_bytes_) {
        return;
    }

    resource::BufferHost::DestroyBuffer(context_, frame_state.vertex_buffer);
    frame_state.vertex_buffer_capacity_bytes = 0U;
    frame_state.uploaded_revision = 0U;

    if (required_capacity == 0U) {
        return;
    }

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = required_capacity;
    buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_create_info.persistently_mapped = false;

    const std::uint32_t upload_queue_family_index = prepare_view_.upload.QueueFamilyIndex();
    const std::uint32_t graphics_queue_family_index = context_.QueueFamilies().graphics.value();
    if (upload_queue_family_index != graphics_queue_family_index) {
        buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        buffer_create_info.queue_family_indices.push_back(upload_queue_family_index);
        buffer_create_info.queue_family_indices.push_back(graphics_queue_family_index);
    }

    frame_state.vertex_buffer = resource::BufferHost::CreateBuffer(context_,
                                                                   buffer_create_info,
                                                                   *gpu_memory_host);
    frame_state.vertex_buffer_capacity_bytes = required_capacity;
    frame_state.uploaded_revision = 0U;
}

void TextRenderer2D::EnsureGraphUploadStagingForFrame(
    VulkanContext& context_,
    const render::TextRenderer2DPrepareView& prepare_view_,
    std::uint32_t frame_index_,
    const VkDeviceSize required_bytes_) {
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    const VkDeviceSize required_capacity = (required_bytes_ > 0U)
        ? std::max(create_info_cache.initial_vertex_buffer_bytes,
                   NextPow2(required_bytes_))
        : frame_state.graph_staging_buffer_capacity_bytes;
    if (required_capacity == 0U) {
        return;
    }
    if (frame_state.graph_staging_buffer.buffer != VK_NULL_HANDLE &&
        frame_state.graph_staging_buffer_capacity_bytes >= required_bytes_) {
        return;
    }

    resource::BufferHost::DestroyBuffer(context_, frame_state.graph_staging_buffer);
    frame_state.graph_staging_buffer_capacity_bytes = 0U;

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = required_capacity;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_create_info.memory_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    buffer_create_info.persistently_mapped = true;

    if (context_.QueueFamilies().graphics.has_value() &&
        context_.QueueFamilies().transfer.has_value() &&
        context_.QueueFamilies().graphics.value() != context_.QueueFamilies().transfer.value()) {
        buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        buffer_create_info.queue_family_indices.push_back(context_.QueueFamilies().graphics.value());
        buffer_create_info.queue_family_indices.push_back(context_.QueueFamilies().transfer.value());
    } else {
        const std::uint32_t upload_queue_family_index = prepare_view_.upload.QueueFamilyIndex();
        const std::uint32_t graphics_queue_family_index = context_.QueueFamilies().graphics.value();
        if (upload_queue_family_index != graphics_queue_family_index) {
            buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
            buffer_create_info.queue_family_indices.push_back(upload_queue_family_index);
            buffer_create_info.queue_family_indices.push_back(graphics_queue_family_index);
        }
    }

    frame_state.graph_staging_buffer = resource::BufferHost::CreateBuffer(context_,
                                                                          buffer_create_info,
                                                                          *gpu_memory_host);
    frame_state.graph_staging_buffer_capacity_bytes = required_capacity;
}

void TextRenderer2D::ScheduleGraphInstanceUpload(render_graph::RenderGraphBuilder& builder_,
                                                 const render_graph::PassHandle pass_) {
    if (context == nullptr || gpu_memory_host == nullptr) {
        return;
    }
    if (active_frame_index >= frame_states.size() || gpu_instances.empty()) {
        return;
    }

    PerFrameState& frame_state = frame_states[active_frame_index];
    const VkDeviceSize required_bytes =
        static_cast<VkDeviceSize>(frame_state.instance_count) * sizeof(GpuTextInstance);
    if (frame_state.instance_count == 0U || required_bytes == 0U) {
        return;
    }
    if (render_graph::IsValidResourceVersionHandle(frame_state.graph_vertex_version) &&
        render_graph::IsValidResourceHandle(frame_state.graph_vertex_buffer) &&
        frame_state.graph_vertex_size_bytes == required_bytes) {
        (void)builder_.Read(
            pass_,
            frame_state.graph_vertex_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::vertex_buffer_read,
                .buffer_range = {.offset_bytes = 0U, .size_bytes = required_bytes},
            });
        return;
    }
    if (frame_state.graph_staging_buffer.buffer == VK_NULL_HANDLE ||
        frame_state.graph_staging_buffer.mapped_ptr == nullptr ||
        frame_state.graph_staging_buffer_capacity_bytes < required_bytes) {
        return;
    }

    std::memcpy(frame_state.graph_staging_buffer.mapped_ptr,
                gpu_instances.data(),
                static_cast<std::size_t>(required_bytes));

    frame_state.graph_vertex_buffer = builder_.CreateBuffer(
        "text_2d_instances",
        render_graph::BufferDesc{
            .size_bytes = required_bytes,
            .usage = render_graph::buffer_usage_vertex_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::transient);
    const auto upload_pass = builder_.AddPass("text_2d_upload_instances",
                                              false,
                                              render_graph::QueueClass::transfer);
    frame_state.graph_vertex_version = builder_.Write(
        upload_pass,
        frame_state.graph_vertex_buffer,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::transfer_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = required_bytes},
        });
    frame_state.graph_vertex_size_bytes = required_bytes;
    stats.uploaded_bytes = required_bytes;

    builder_.SetExecuteCallback(
        upload_pass,
        [this, frame_index = active_frame_index, target = frame_state.graph_vertex_buffer, size = required_bytes](
            render_graph::GraphCommandContext& context_) {
            if (frame_index >= frame_states.size()) {
                return;
            }

            const PerFrameState& frame_state_ref = frame_states[frame_index];
            if (frame_state_ref.graph_staging_buffer.buffer == VK_NULL_HANDLE) {
                return;
            }

            const auto* target_record = context_.FindBuffer(target);
            if (target_record == nullptr || target_record->owned_resource.buffer == VK_NULL_HANDLE) {
                throw std::runtime_error(
                    "TextRenderer2D graph upload pass could not resolve target vertex buffer");
            }

            VkBufferCopy copy_region{};
            copy_region.srcOffset = 0U;
            copy_region.dstOffset = 0U;
            copy_region.size = size;
            vkCmdCopyBuffer(context_.CommandBuffer(),
                            frame_state_ref.graph_staging_buffer.buffer,
                            target_record->owned_resource.buffer,
                            1U,
                            &copy_region);
        });
    (void)builder_.Read(
        pass_,
        frame_state.graph_vertex_version,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::vertex_buffer_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = required_bytes},
        });
}

void TextRenderer2D::EnsurePipelineObjects(VulkanContext& context_,
                                           render::DescriptorHost& descriptor_host_,
                                           render::PipelineHost& pipeline_host_,
                                           VkFormat color_format_) {
    (void)descriptor_host_;
    RequireTextRuntimeFeatures(context_, "TextRenderer2D::EnsurePipelineObjects");

    const render::PipelineHostStats& pipeline_stats = pipeline_host_.Stats();
    if (shader_vertex_id.IsValid() && pipeline_stats.shader_module_count < shader_vertex_id.value) {
        shader_vertex_id = {};
    }
    if (shader_fragment_id.IsValid() && pipeline_stats.shader_module_count < shader_fragment_id.value) {
        shader_fragment_id = {};
    }
    if (pipeline_layout_id.IsValid() && pipeline_stats.pipeline_layout_count < pipeline_layout_id.value) {
        pipeline_layout_id = {};
    }
    if (graphics_pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < graphics_pipeline_id.value) {
        graphics_pipeline_id = {};
        pipeline_color_format = VK_FORMAT_UNDEFINED;
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_text_2d_vert_spv;
        shader_create_info.word_count = generated::k_text_2d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_text_2d_frag_spv;
        shader_create_info.word_count = generated::k_text_2d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        if (bindless_resources == nullptr ||
            bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
            bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
            throw std::runtime_error("TextRenderer2D::EnsurePipelineObjects requires bindless resource layouts");
        }
        render::PipelineLayoutDesc pipeline_layout_desc{};
        pipeline_layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        pipeline_layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        pipeline_layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, pipeline_layout_desc);
    }

    if (graphics_pipeline_id.IsValid() && pipeline_color_format == color_format_) {
        return;
    }

    render::GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    pipeline_desc.use_dynamic_rendering = true;
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);

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

    pipeline_desc.vertex_input.bindings.push_back({
        .binding = 0U,
        .stride = static_cast<std::uint32_t>(sizeof(GpuTextInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 0U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = 0U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 1U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = 16U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 2U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = 32U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = 36U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = 40U
    });

    pipeline_desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipeline_desc.input_assembly.primitive_restart_enable = false;

    pipeline_desc.viewport.viewport_count = 1U;
    pipeline_desc.viewport.scissor_count = 1U;
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    pipeline_desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
    pipeline_desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    pipeline_desc.rasterization.line_width = 1.0F;

    pipeline_desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;

    const VkPipelineColorBlendAttachmentState blend_attachment =
        render::BuildColorBlendAttachment(render::ColorBlendPreset::alpha);
    pipeline_desc.color_blend.attachments.push_back(blend_attachment);

    graphics_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
}

} // namespace vr::text
