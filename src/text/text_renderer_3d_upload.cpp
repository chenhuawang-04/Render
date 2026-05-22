#include "vr/text/text_renderer_3d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/text/text_runtime_contract.hpp"
#include "vr/text/generated/text_3d_frag_spv.hpp"
#include "vr/text/generated/text_3d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vr::text {

VkDeviceSize TextRenderer3D::NextPow2(VkDeviceSize value_) noexcept {
    if (value_ <= 1U) {
        return 1U;
    }

    VkDeviceSize result = 1U;
    while (result < value_) {
        if (result > (std::numeric_limits<VkDeviceSize>::max() >> 1U)) {
            return std::numeric_limits<VkDeviceSize>::max();
        }
        result <<= 1U;
    }
    return result;
}

void TextRenderer3D::ResetPerFrameDrawState(std::uint32_t frame_index_,
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

void TextRenderer3D::EnsureGpuResourcesForFrame(VulkanContext& context_,
                                                const render::TextRenderer3DPrepareView& prepare_view_,
                                                std::uint32_t frame_index_,
                                                VkDeviceSize required_bytes_) {
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

void TextRenderer3D::EnsureGraphUploadStagingForFrame(
    VulkanContext& context_,
    const render::TextRenderer3DPrepareView& prepare_view_,
    std::uint32_t frame_index_,
    VkDeviceSize required_bytes_) {
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

void TextRenderer3D::ScheduleGraphInstanceUpload(render_graph::RenderGraphBuilder& builder_,
                                                 const render_graph::PassHandle pass_) {
    if (context == nullptr || gpu_memory_host == nullptr) {
        return;
    }
    if (active_frame_index >= frame_states.size() || render_scratch.instances.empty()) {
        return;
    }

    PerFrameState& frame_state = frame_states[active_frame_index];
    const VkDeviceSize required_bytes =
        static_cast<VkDeviceSize>(frame_state.instance_count) * sizeof(ecs::Text3DGpuInstance);
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
                render_scratch.instances.data(),
                static_cast<std::size_t>(required_bytes));

    frame_state.graph_vertex_buffer = builder_.CreateBuffer(
        "text_3d_instances",
        render_graph::BufferDesc{
            .size_bytes = required_bytes,
            .usage = render_graph::buffer_usage_vertex_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::transient);
    const auto upload_pass = builder_.AddPass("text_3d_upload_instances",
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
        [this,
         frame_index = active_frame_index,
         target = frame_state.graph_vertex_buffer,
         size = required_bytes](render_graph::GraphCommandContext& context_) {
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
                    "TextRenderer3D graph upload pass could not resolve target vertex buffer");
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

} // namespace vr::text
