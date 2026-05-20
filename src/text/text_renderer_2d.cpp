#include "vr/text/text_renderer_2d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/runtime_prepare_views.hpp"
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
    output_target_config = {};
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

void TextRenderer2D::SetOutputTargetConfig(
    const render::RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void TextRenderer2D::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

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

    if (prepare_view_.prefer_render_graph_upload_path) {
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

void TextRenderer2D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                     const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "TextRenderer2D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const_cast<TextRenderer2D*>(this)->ScheduleGraphInstanceUpload(builder_, pass_);

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("text_2d.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     0U,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     1U,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);
}

void TextRenderer2D::RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                                       render_graph::ResourceHandle color_target_) {
    RecordGraphInternal(context_, color_target_);
}

void TextRenderer2D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer2D::Record called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        glyph_upload_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("TextRenderer2D::Record called before PrepareFrame");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE ||
        record_context_.image == VK_NULL_HANDLE ||
        record_context_.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("TextRenderer2D::Record requires valid frame context image handles");
    }
    if (record_context_.extent.width == 0U || record_context_.extent.height == 0U) {
        throw std::runtime_error("TextRenderer2D::Record received zero-sized swapchain extent");
    }

    if (record_context_.image_index >= image_initialized.size()) {
        const std::size_t previous_size = image_initialized.size();
        image_initialized.resize(record_context_.image_index + 1U);
        for (std::size_t i = previous_size; i < image_initialized.size(); ++i) {
            image_initialized[i] = 0U;
        }
    }
    const bool has_previous_content = image_initialized[record_context_.image_index] != 0U;

    const render::ResolvedColorRenderPass color_pass = render::BuildColorRenderPass(
        record_context_,
        output_target_config,
        create_info_cache.clear_swapchain,
        create_info_cache.clear_color,
        has_previous_content);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          color_pass.target.format);

    vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(color_pass.target.extent.width);
    viewport.height = static_cast<float>(color_pass.target.extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = color_pass.target.extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    const VkPipeline pipeline_handle = pipeline_host->GetGraphicsPipeline(graphics_pipeline_id);
    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    vkCmdBindPipeline(record_context_.command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_handle);

    PushConstants push_constants{};
    push_constants.inv_viewport_x = 1.0F / static_cast<float>(color_pass.target.extent.width);
    push_constants.inv_viewport_y = 1.0F / static_cast<float>(color_pass.target.extent.height);
    push_constants.depth = create_info_cache.depth;
    push_constants.sdf_smooth = create_info_cache.sdf_smooth;
    push_constants.bitmap_gamma = create_info_cache.bitmap_gamma;
    push_constants.bitmap_edge_sharpness = create_info_cache.bitmap_edge_sharpness;
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    const std::uint32_t frame_index = record_context_.frame_index;
    if (frame_index < frame_states.size()) {
        const PerFrameState& frame_state = frame_states[frame_index];
        if (frame_state.instance_count > 0U && frame_state.vertex_buffer.buffer != VK_NULL_HANDLE) {
            const VkDescriptorSet bindless_sets[] = {
                bindless_resources->SampledImageSet(),
                bindless_resources->SamplerSet()
            };
            vkCmdBindDescriptorSets(record_context_.command_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0U,
                                    2U,
                                    bindless_sets,
                                    0U,
                                    nullptr);
            stats.descriptor_set_bind_count += 2U;

            const VkBuffer vertex_buffer = frame_state.vertex_buffer.buffer;
            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(record_context_.command_buffer, 0U, 1U, &vertex_buffer, &vertex_offset);

            for (const auto& batch : runtime_scratch.draw_batches) {
                if (batch.glyph_count == 0U) {
                    continue;
                }
                if (batch.atlas_page_id >= glyph_upload_host->PageCount()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const render::BindlessSlot texture_slot =
                    glyph_upload_host->ResolveBindlessImageSlot(batch.atlas_page_id);
                const render::BindlessSlot sampler_slot =
                    glyph_upload_host->BindlessConfig().sampler_slot;
                if (!texture_slot.IsValid() || !sampler_slot.IsValid()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                push_constants.texture_slot = texture_slot.index;
                push_constants.sampler_slot = sampler_slot.index;
                vkCmdPushConstants(record_context_.command_buffer,
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);

                vkCmdDraw(record_context_.command_buffer,
                          4U,
                          batch.glyph_count,
                          0U,
                          batch.glyph_begin);
                ++stats.draw_call_count;
            }
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    render::RecordEndColorPass(record_context_, output_target_config);
    image_initialized[record_context_.image_index] = 1U;
}

void TextRenderer2D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                       render_graph::ResourceHandle color_target_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        glyph_upload_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay called before PrepareFrame");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("TextRenderer2D::RecordGraphOverlay resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          resolved_color.format);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(context_.CommandBuffer(), 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

    const VkPipeline pipeline_handle = pipeline_host->GetGraphicsPipeline(graphics_pipeline_id);
    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_handle);

    PushConstants push_constants{};
    push_constants.inv_viewport_x = 1.0F / static_cast<float>(render_extent.width);
    push_constants.inv_viewport_y = 1.0F / static_cast<float>(render_extent.height);
    push_constants.depth = create_info_cache.depth;
    push_constants.sdf_smooth = create_info_cache.sdf_smooth;
    push_constants.bitmap_gamma = create_info_cache.bitmap_gamma;
    push_constants.bitmap_edge_sharpness = create_info_cache.bitmap_edge_sharpness;
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    const std::uint32_t frame_index = active_frame_index;
    if (frame_index < frame_states.size()) {
        const PerFrameState& frame_state = frame_states[frame_index];
        VkBuffer active_vertex_buffer = frame_state.vertex_buffer.buffer;
        if (frame_state.graph_vertex_size_bytes > 0U &&
            render_graph::IsValidResourceHandle(frame_state.graph_vertex_buffer)) {
            if (const auto* graph_vertex_record = context_.FindBuffer(frame_state.graph_vertex_buffer);
                graph_vertex_record != nullptr) {
                if (graph_vertex_record->owned_resource.buffer != VK_NULL_HANDLE) {
                    active_vertex_buffer = graph_vertex_record->owned_resource.buffer;
                } else if (graph_vertex_record->imported_buffer.buffer != VK_NULL_HANDLE) {
                    active_vertex_buffer = graph_vertex_record->imported_buffer.buffer;
                }
            }
        }

        if (frame_state.instance_count > 0U && active_vertex_buffer != VK_NULL_HANDLE) {
            context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   pipeline_layout,
                                                   0U,
                                                   2U);
            stats.descriptor_set_bind_count += 2U;

            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(context_.CommandBuffer(),
                                   0U,
                                   1U,
                                   &active_vertex_buffer,
                                   &vertex_offset);

            for (const auto& batch : runtime_scratch.draw_batches) {
                if (batch.glyph_count == 0U) {
                    continue;
                }
                if (batch.atlas_page_id >= glyph_upload_host->PageCount()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const render::BindlessSlot texture_slot =
                    glyph_upload_host->ResolveBindlessImageSlot(batch.atlas_page_id);
                const render::BindlessSlot sampler_slot =
                    glyph_upload_host->BindlessConfig().sampler_slot;
                if (!texture_slot.IsValid() || !sampler_slot.IsValid()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                push_constants.texture_slot = texture_slot.index;
                push_constants.sampler_slot = sampler_slot.index;
                vkCmdPushConstants(context_.CommandBuffer(),
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);

                vkCmdDraw(context_.CommandBuffer(),
                          4U,
                          batch.glyph_count,
                          0U,
                          batch.glyph_begin);
                ++stats.draw_call_count;
            }
        }
    }
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

