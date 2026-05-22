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

void TextRenderer2D::PrepareFrame(const render::TextRenderer2DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer2D::PrepareFrame called before Initialize");
    }
    ValidateTextRuntimePrepareView(prepare_view_, "TextRenderer2D::PrepareFrame");

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    bindless_resources = prepare_view_.bindless;
    gpu_memory_host = &prepare_view_.gpu_memory;
    freetype_host = &prepare_view_.freetype;
    glyph_atlas_host = &prepare_view_.glyph_atlas;
    glyph_upload_host = &prepare_view_.glyph_upload;
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (glyph_upload_host != nullptr &&
        glyph_upload_host->IsInitialized() &&
        (!glyph_upload_host->BindlessConfig().Enabled() ||
         glyph_upload_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureGlyphUploadHost(*glyph_upload_host);
    }
    active_frame_index = prepare_view_.frame.frame_index;

    stats = {};
    stats.component_count = component_count;
    stats.draw_call_count = 0U;
    stats.descriptor_set_bind_count = 0U;
    stats.descriptor_set_update_count = 0U;
    stats.skipped_draw_batch_count = 0U;
    stats.uploaded_bytes = 0U;

    if (components == nullptr || component_count == 0U) {
        runtime_scratch.glyph_quads.clear();
        runtime_scratch.draw_batches.clear();
        gpu_instances.clear();
        cached_build_stats = {};
        cached_components_ptr = nullptr;
        cached_component_count = 0U;
        runtime_geometry_valid = false;
        ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());
        return;
    }

    const bool rebuild_required = !runtime_geometry_valid ||
                                  cached_components_ptr != components ||
                                  cached_component_count != component_count ||
                                  AnyComponentDirty(components, component_count);

    if (rebuild_required) {
        cached_build_stats = ecs::TextRuntimeSystem<ecs::Dim2>::Build(components,
                                                                       component_count,
                                                                       *glyph_atlas_host,
                                                                       *freetype_host,
                                                                       runtime_scratch,
                                                                       create_info_cache.runtime_build);
        BuildGpuInstancesFromScratch();
        cached_components_ptr = components;
        cached_component_count = component_count;
        runtime_geometry_valid = true;

        if (runtime_geometry_revision == std::numeric_limits<std::uint64_t>::max()) {
            runtime_geometry_revision = 1U;
        } else {
            ++runtime_geometry_revision;
            if (runtime_geometry_revision == 0U) {
                runtime_geometry_revision = 1U;
            }
        }
    }

    stats.visible_component_count = cached_build_stats.visible_component_count;
    stats.built_component_count = cached_build_stats.built_component_count;
    stats.glyph_quad_count = static_cast<std::uint32_t>(gpu_instances.size());
    stats.draw_batch_count = static_cast<std::uint32_t>(runtime_scratch.draw_batches.size());

    const VkDeviceSize required_bytes = static_cast<VkDeviceSize>(gpu_instances.size()) * sizeof(GpuTextInstance);

    EnsureGpuResourcesForFrame(*context, prepare_view_, active_frame_index, required_bytes);
    ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());

    PerFrameState& frame_state = frame_states[active_frame_index];
    frame_state.instance_count = static_cast<std::uint32_t>(gpu_instances.size());

    if (required_bytes == 0U) {
        return;
    }

    const bool needs_upload = frame_state.uploaded_revision != runtime_geometry_revision;
    if (!needs_upload) {
        return;
    }

    if (prepare_view_.render_graph_upload_active) {
        return;
    }

    upload_host->StageAndRecordCopyBuffer(active_frame_index,
                                          frame_state.vertex_buffer.buffer,
                                          0U,
                                          gpu_instances.data(),
                                          required_bytes,
                                          16U);

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = frame_state.vertex_buffer.buffer;
    barrier.offset = 0U;
    barrier.size = required_bytes;
    upload_host->RecordBufferBarrier2(active_frame_index, barrier);

    frame_state.uploaded_revision = runtime_geometry_revision;
    stats.uploaded_bytes = required_bytes;
}

} // namespace vr::text
