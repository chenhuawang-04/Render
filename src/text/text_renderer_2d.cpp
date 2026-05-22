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

std::uint32_t TextRenderer2D::PackRgba8(const ecs::Rgba8& color_) noexcept {
    return static_cast<std::uint32_t>(color_.r) |
           (static_cast<std::uint32_t>(color_.g) << 8U) |
           (static_cast<std::uint32_t>(color_.b) << 16U) |
           (static_cast<std::uint32_t>(color_.a) << 24U);
}

std::uint32_t TextRenderer2D::PackParams(const ecs::TextGlyphQuad& quad_) noexcept {
    std::uint32_t packed = 0U;
    packed |= (quad_.sdf_enabled != 0U) ? 0x1U : 0U;
    packed |= (quad_.outline_enabled != 0U) ? 0x2U : 0U;
    packed |= (static_cast<std::uint32_t>(quad_.outline_width_px) << 8U);
    return packed;
}

VkDeviceSize TextRenderer2D::NextPow2(VkDeviceSize value_) noexcept {
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

float TextRenderer2D::QuantizeToStep(float value_, float step_) noexcept {
    if (!(step_ > 0.0F) || !std::isfinite(step_) || !std::isfinite(value_)) {
        return value_;
    }
    return std::round(value_ / step_) * step_;
}

bool TextRenderer2D::AnyComponentDirty(const ecs::Text<ecs::Dim2>* components_,
                                       std::uint32_t component_count_) noexcept {
    if (components_ == nullptr || component_count_ == 0U) {
        return false;
    }
    for (std::uint32_t i = 0U; i < component_count_; ++i) {
        if (components_[i].runtime.dirty_flags != 0U) {
            return true;
        }
    }
    return false;
}

void TextRenderer2D::Initialize(const TextRenderer2DCreateInfo& create_info_) {
    if (initialized) {
        if (context != nullptr) {
            Shutdown(*context);
        } else {
            throw std::runtime_error(
                "TextRenderer2D::Initialize called while already initialized without valid VulkanContext");
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
    if (!(create_info_cache.pixel_snap_step > 0.0F) || !std::isfinite(create_info_cache.pixel_snap_step)) {
        create_info_cache.pixel_snap_step = 1.0F;
    }

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_glyph_count > 0U) {
        ecs::TextRuntimeSystem<ecs::Dim2>::Reserve(runtime_scratch,
                                                   create_info_cache.reserve_component_count,
                                                   create_info_cache.reserve_glyph_count);
    }

    if (create_info_cache.reserve_glyph_count > 0U) {
        gpu_instances.reserve(create_info_cache.reserve_glyph_count);
    }

    cached_build_stats = {};
    cached_components_ptr = nullptr;
    cached_component_count = 0U;
    runtime_geometry_revision = 1U;
    runtime_geometry_valid = false;
    stats = {};
    initialized = true;
}

void TextRenderer2D::Shutdown(VulkanContext& context_) {
    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
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

    image_initialized.clear();
    gpu_instances.clear();

    runtime_scratch.glyph_quads.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.utf32_codepoints.clear();
    runtime_scratch.line_widths.clear();
    runtime_scratch.line_x_offsets.clear();
    runtime_scratch.run_glyphs.clear();
    runtime_scratch.face_variants.clear();
    runtime_scratch.glyph_resolve_cache.clear();

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    graphics_pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    components = nullptr;
    component_count = 0U;

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
    cached_build_stats = {};
    cached_components_ptr = nullptr;
    cached_component_count = 0U;
    runtime_geometry_revision = 1U;
    runtime_geometry_valid = false;
    stats = {};
    initialized = false;
}

void TextRenderer2D::SetComponents(ecs::Text<ecs::Dim2>* components_,
                                   std::uint32_t component_count_) noexcept {
    components = components_;
    component_count = component_count_;
}

void TextRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                          VkExtent2D extent_,
                                          VkFormat format_) {
    image_initialized.resize(image_count_);
    for (auto& initialized_flag : image_initialized) {
        initialized_flag = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool TextRenderer2D::IsInitialized() const noexcept {
    return initialized;
}

const TextRenderer2DStats& TextRenderer2D::Stats() const noexcept {
    return stats;
}

} // namespace vr::text

