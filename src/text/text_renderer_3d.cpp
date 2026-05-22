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

bool TextRenderer3D::IsDepthFormatSupported(VulkanContext& context_,
                                            VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }

    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool TextRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags TextRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags mask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return mask;
}

VkFormat TextRenderer3D::ResolveDepthFormat(VulkanContext& context_,
                                            VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (const VkFormat candidate : fallback_formats) {
        if (IsDepthFormatSupported(context_, candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error("TextRenderer3D failed to resolve depth format for depth attachment");
}

std::size_t TextRenderer3D::PipelineModeIndex(DepthPipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

TextRenderer3D::DepthPipelineMode TextRenderer3D::ResolveDepthPipelineMode(
    const ecs::Text3DDrawBatch& batch_,
    bool use_depth_,
    bool reverse_z_) noexcept {
    if (!use_depth_ || (batch_.depth_flags & 0x1U) == 0U) {
        return DepthPipelineMode::no_depth;
    }

    const bool depth_write = (batch_.depth_flags & 0x2U) != 0U;
    if (reverse_z_) {
        return depth_write
            ? DepthPipelineMode::depth_test_write_reverse_z
            : DepthPipelineMode::depth_test_reverse_z;
    }
    return depth_write
        ? DepthPipelineMode::depth_test_write
        : DepthPipelineMode::depth_test;
}

void TextRenderer3D::Initialize(const TextRenderer3DCreateInfo& create_info_) {
    if (initialized) {
        if (context != nullptr) {
            Shutdown(*context);
        } else {
            throw std::runtime_error(
                "TextRenderer3D::Initialize called while already initialized without valid VulkanContext");
        }
    }

    create_info_cache = create_info_;
    if (create_info_cache.initial_vertex_buffer_bytes == 0U) {
        create_info_cache.initial_vertex_buffer_bytes = 1U;
    }
    if (!(create_info_cache.sdf_smooth > 0.0F) || !std::isfinite(create_info_cache.sdf_smooth)) {
        create_info_cache.sdf_smooth = 1.0F;
    }
    if (!(create_info_cache.bitmap_gamma > 0.0F) || !std::isfinite(create_info_cache.bitmap_gamma)) {
        create_info_cache.bitmap_gamma = 1.0F;
    }
    if (!(create_info_cache.bitmap_edge_sharpness > 0.0F) ||
        !std::isfinite(create_info_cache.bitmap_edge_sharpness)) {
        create_info_cache.bitmap_edge_sharpness = 1.0F;
    }
    if (!std::isfinite(create_info_cache.clear_depth_value)) {
        create_info_cache.clear_depth_value = 1.0F;
    }
    create_info_cache.clear_depth_value = std::clamp(create_info_cache.clear_depth_value, 0.0F, 1.0F);

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_glyph_count > 0U) {
        ecs::TextRender3DSystem::Reserve(render_scratch,
                                         create_info_cache.reserve_component_count,
                                         create_info_cache.reserve_glyph_count);
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch,
                                               create_info_cache.reserve_component_count);
    }

    cached_runtime_stats = {};
    cached_render_stats = {};
    frame_data_cache = {};
    cached_components_ptr = nullptr;
    cached_transforms_ptr = nullptr;
    cached_camera_component_ptr = nullptr;
    cached_camera_transform_ptr = nullptr;
    cached_component_count = 0U;
    cached_transform_signature = 0U;
    cached_camera_world_revision = 0U;
    bounds_components = nullptr;
    culling_stats = {};
    runtime_geometry_revision = 1U;
    runtime_geometry_valid = false;
    instance_geometry_valid = false;
    contains_billboard_instances = false;
    active_camera_reverse_z = false;
    stats = {};

    for (auto& pipeline_id : graphics_pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = true;
}

void TextRenderer3D::Shutdown(VulkanContext& context_) {
    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    for (auto& frame_state : frame_states) {
        resource::BufferHost::DestroyBuffer(context_, frame_state.vertex_buffer);
        frame_state.vertex_buffer_capacity_bytes = 0U;
        resource::BufferHost::DestroyBuffer(context_, frame_state.graph_staging_buffer);
        frame_state.graph_staging_buffer_capacity_bytes = 0U;
        frame_state.instance_count = 0U;
        frame_state.uploaded_revision = 0U;
        frame_state.graph_vertex_buffer = render_graph::invalid_resource_handle;
        frame_state.graph_vertex_version = render_graph::invalid_resource_version;
        frame_state.graph_vertex_size_bytes = 0U;
    }
    frame_states.clear();

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);
    image_initialized.clear();

    render_scratch.instances.clear();
    render_scratch.draw_batches.clear();
    render_scratch.runtime_scratch.glyph_quads.clear();
    render_scratch.runtime_scratch.draw_batches.clear();
    render_scratch.runtime_scratch.batch_scratch.visible_items.clear();
    render_scratch.runtime_scratch.batch_scratch.radix_scratch.clear();
    render_scratch.runtime_scratch.batch_scratch.ordered_indices.clear();
    render_scratch.runtime_scratch.utf32_codepoints.clear();
    render_scratch.runtime_scratch.line_widths.clear();
    render_scratch.runtime_scratch.line_x_offsets.clear();
    render_scratch.runtime_scratch.run_glyphs.clear();
    render_scratch.runtime_scratch.face_variants.clear();
    render_scratch.runtime_scratch.glyph_resolve_cache.clear();
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    culling_stats = {};

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : graphics_pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    text_components = nullptr;
    text_transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    bindless_resources = nullptr;
    gpu_memory_host = nullptr;
    freetype_host = nullptr;
    glyph_atlas_host = nullptr;
    glyph_upload_host = nullptr;

    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;

    cached_runtime_stats = {};
    cached_render_stats = {};
    frame_data_cache = {};
    cached_components_ptr = nullptr;
    cached_transforms_ptr = nullptr;
    cached_camera_component_ptr = nullptr;
    cached_camera_transform_ptr = nullptr;
    cached_component_count = 0U;
    cached_transform_signature = 0U;
    cached_camera_world_revision = 0U;

    runtime_geometry_revision = 1U;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    runtime_geometry_valid = false;
    instance_geometry_valid = false;
    contains_billboard_instances = false;
    active_camera_reverse_z = false;
    stats = {};
    initialized = false;
}

void TextRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                          VkExtent2D extent_,
                                          VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void TextRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
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
    for (auto& initialized_flag : image_initialized) {
        initialized_flag = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;

}

bool TextRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const TextRenderer3DStats& TextRenderer3D::Stats() const noexcept {
    return stats;
}

} // namespace vr::text

