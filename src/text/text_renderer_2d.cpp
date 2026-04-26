#include "vr/text/text_renderer_2d.hpp"

#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_context.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/text/generated/text_2d_frag_spv.hpp"
#include "vr/text/generated/text_2d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace vr::text {

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

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_glyph_count > 0U) {
        ecs::TextRuntimeSystem<ecs::Dim2>::Reserve(runtime_scratch,
                                                   create_info_cache.reserve_component_count,
                                                   create_info_cache.reserve_glyph_count);
    }

    if (create_info_cache.reserve_glyph_count > 0U) {
        gpu_instances.reserve(create_info_cache.reserve_glyph_count);
    }

    descriptor_image_write_scratch.reserve(1U);
    descriptor_buffer_write_scratch.reserve(0U);
    descriptor_texel_write_scratch.reserve(0U);

    stats = {};
    initialized = true;
}

void TextRenderer2D::Shutdown(VulkanContext& context_) {
    for (auto& frame_state : frame_states) {
        resource::BufferHost::DestroyBuffer(context_, frame_state.vertex_buffer);
        frame_state.vertex_buffer_capacity_bytes = 0U;
        frame_state.instance_count = 0U;
        frame_state.page_sets.clear();
        frame_state.page_set_epochs.clear();
        frame_state.page_set_epoch = 1U;
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

    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    graphics_pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();

    components = nullptr;
    component_count = 0U;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    freetype_host = nullptr;
    glyph_atlas_host = nullptr;
    glyph_upload_host = nullptr;

    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    stats = {};
    initialized = false;
}

void TextRenderer2D::SetComponents(ecs::Text<ecs::Dim2>* components_,
                                   std::uint32_t component_count_) noexcept {
    components = components_;
    component_count = component_count_;
}

void TextRenderer2D::PrepareFrame(const render::RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer2D::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr ||
        prepare_context_.upload_host == nullptr ||
        prepare_context_.descriptor_host == nullptr ||
        prepare_context_.pipeline_host == nullptr ||
        prepare_context_.gpu_memory_host == nullptr ||
        prepare_context_.freetype_host == nullptr ||
        prepare_context_.glyph_atlas_host == nullptr ||
        prepare_context_.glyph_upload_host == nullptr) {
        throw std::runtime_error("TextRenderer2D::PrepareFrame requires all runtime text modules enabled");
    }

    context = prepare_context_.context;
    upload_host = prepare_context_.upload_host;
    descriptor_host = prepare_context_.descriptor_host;
    pipeline_host = prepare_context_.pipeline_host;
    gpu_memory_host = prepare_context_.gpu_memory_host;
    freetype_host = prepare_context_.freetype_host;
    glyph_atlas_host = prepare_context_.glyph_atlas_host;
    glyph_upload_host = prepare_context_.glyph_upload_host;
    active_frame_index = prepare_context_.frame_index;

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
        ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());
        return;
    }

    const ecs::TextRuntimeBuildStats build_stats =
        ecs::TextRuntimeSystem<ecs::Dim2>::Build(components,
                                                 component_count,
                                                 *glyph_atlas_host,
                                                 *freetype_host,
                                                 runtime_scratch,
                                                 create_info_cache.runtime_build);

    stats.visible_component_count = build_stats.visible_component_count;
    stats.built_component_count = build_stats.built_component_count;
    stats.glyph_quad_count = build_stats.emitted_glyph_quad_count;
    stats.draw_batch_count = build_stats.emitted_batch_count;

    BuildGpuInstancesFromScratch();
    const VkDeviceSize required_bytes = static_cast<VkDeviceSize>(gpu_instances.size()) * sizeof(GpuTextInstance);

    EnsureGpuResourcesForFrame(*context, prepare_context_, active_frame_index, required_bytes);
    ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());

    PerFrameState& frame_state = frame_states[active_frame_index];
    frame_state.instance_count = static_cast<std::uint32_t>(gpu_instances.size());

    if (required_bytes == 0U) {
        return;
    }

    upload_host->StageAndRecordCopyBuffer(active_frame_index,
                                          frame_state.vertex_buffer.buffer,
                                          0U,
                                          gpu_instances.data(),
                                          required_bytes,
                                          16U);

    if (context->EnabledVulkan13Features().synchronization2 == VK_TRUE) {
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
    }

    stats.uploaded_bytes = required_bytes;
}

void TextRenderer2D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer2D::Record called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr || glyph_upload_host == nullptr) {
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

    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          record_context_.format);

    if (record_context_.image_index >= image_initialized.size()) {
        const std::size_t previous_size = image_initialized.size();
        image_initialized.resize(record_context_.image_index + 1U);
        for (std::size_t i = previous_size; i < image_initialized.size(); ++i) {
            image_initialized[i] = 0U;
        }
    }
    const bool has_previous_content = image_initialized[record_context_.image_index] != 0U;

    RecordImageTransitionToColorAttachment(record_context_, has_previous_content);

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
    rendering_info.renderArea.offset = {0, 0};
    rendering_info.renderArea.extent = record_context_.extent;
    rendering_info.layerCount = 1U;
    rendering_info.viewMask = 0U;
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
    scissor.offset = {0, 0};
    scissor.extent = record_context_.extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    const VkPipeline pipeline_handle = pipeline_host->GetGraphicsPipeline(graphics_pipeline_id);
    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    vkCmdBindPipeline(record_context_.command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_handle);

    PushConstants push_constants{};
    push_constants.inv_viewport_x = 1.0F / static_cast<float>(record_context_.extent.width);
    push_constants.inv_viewport_y = 1.0F / static_cast<float>(record_context_.extent.height);
    push_constants.depth = create_info_cache.depth;
    push_constants.sdf_smooth = create_info_cache.sdf_smooth;
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    const std::uint32_t frame_index = record_context_.frame_index;
    if (frame_index < frame_states.size()) {
        PreparePageDescriptorSetsForFrame(frame_index);

        const PerFrameState& frame_state = frame_states[frame_index];
        if (frame_state.instance_count > 0U && frame_state.vertex_buffer.buffer != VK_NULL_HANDLE) {
            const VkBuffer vertex_buffer = frame_state.vertex_buffer.buffer;
            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(record_context_.command_buffer, 0U, 1U, &vertex_buffer, &vertex_offset);

            VkDescriptorSet bound_set = VK_NULL_HANDLE;
            for (const auto& batch : runtime_scratch.draw_batches) {
                if (batch.glyph_count == 0U) {
                    continue;
                }
                if (batch.atlas_page_id >= glyph_upload_host->PageCount()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                if (batch.atlas_page_id >= frame_state.page_sets.size() ||
                    batch.atlas_page_id >= frame_state.page_set_epochs.size() ||
                    frame_state.page_set_epochs[batch.atlas_page_id] != frame_state.page_set_epoch) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const VkDescriptorSet descriptor_set = frame_state.page_sets[batch.atlas_page_id];
                if (descriptor_set == VK_NULL_HANDLE) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                if (descriptor_set != bound_set) {
                    vkCmdBindDescriptorSets(record_context_.command_buffer,
                                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipeline_layout,
                                            0U,
                                            1U,
                                            &descriptor_set,
                                            0U,
                                            nullptr);
                    bound_set = descriptor_set;
                    ++stats.descriptor_set_bind_count;
                }

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
    RecordImageTransitionToPresent(record_context_);
    image_initialized[record_context_.image_index] = 1U;
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

    for (auto& frame_state : frame_states) {
        frame_state.page_sets.clear();
        frame_state.page_set_epochs.clear();
        frame_state.page_set_epoch = 1U;
    }
}

bool TextRenderer2D::IsInitialized() const noexcept {
    return initialized;
}

const TextRenderer2DStats& TextRenderer2D::Stats() const noexcept {
    return stats;
}

void TextRenderer2D::ResetPerFrameDrawState(std::uint32_t frame_index_,
                                            std::uint32_t atlas_page_count_) {
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    frame_state.instance_count = 0U;

    if (frame_state.page_set_epoch == std::numeric_limits<std::uint32_t>::max()) {
        for (auto& page_epoch : frame_state.page_set_epochs) {
            page_epoch = 0U;
        }
        frame_state.page_set_epoch = 1U;
    } else {
        ++frame_state.page_set_epoch;
        if (frame_state.page_set_epoch == 0U) {
            frame_state.page_set_epoch = 1U;
        }
    }

    const std::size_t previous_set_size = frame_state.page_sets.size();
    frame_state.page_sets.resize(atlas_page_count_);
    for (std::size_t i = previous_set_size; i < frame_state.page_sets.size(); ++i) {
        frame_state.page_sets[i] = VK_NULL_HANDLE;
    }

    const std::size_t previous_epoch_size = frame_state.page_set_epochs.size();
    frame_state.page_set_epochs.resize(atlas_page_count_);
    for (std::size_t i = previous_epoch_size; i < frame_state.page_set_epochs.size(); ++i) {
        frame_state.page_set_epochs[i] = 0U;
    }
}

void TextRenderer2D::BuildGpuInstancesFromScratch() {
    gpu_instances.clear();
    if (runtime_scratch.glyph_quads.empty()) {
        return;
    }

    gpu_instances.resize(runtime_scratch.glyph_quads.size());
    for (std::size_t i = 0; i < runtime_scratch.glyph_quads.size(); ++i) {
        const ecs::TextGlyphQuad& quad = runtime_scratch.glyph_quads[i];
        GpuTextInstance instance{};
        instance.rect_x0 = quad.x0;
        instance.rect_y0 = quad.y0;
        instance.rect_x1 = quad.x1;
        instance.rect_y1 = quad.y1;
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
                                                const render::RuntimePrepareContext& prepare_context_,
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

    if (required_capacity == 0U) {
        return;
    }

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = required_capacity;
    buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_create_info.persistently_mapped = false;

    const std::uint32_t upload_queue_family_index = prepare_context_.upload_host->QueueFamilyIndex();
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
}

void TextRenderer2D::PreparePageDescriptorSetsForFrame(std::uint32_t frame_index_) {
    if (context == nullptr || descriptor_host == nullptr || glyph_upload_host == nullptr) {
        throw std::runtime_error(
            "TextRenderer2D::PreparePageDescriptorSetsForFrame requires prepared runtime hosts");
    }
    if (!descriptor_layout_id.IsValid()) {
        return;
    }
    if (frame_index_ >= frame_states.size()) {
        return;
    }
    if (runtime_scratch.draw_batches.empty()) {
        return;
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    const std::uint32_t atlas_page_count = glyph_upload_host->PageCount();

    if (frame_state.page_sets.size() < atlas_page_count) {
        const std::size_t previous_set_size = frame_state.page_sets.size();
        frame_state.page_sets.resize(atlas_page_count);
        for (std::size_t i = previous_set_size; i < frame_state.page_sets.size(); ++i) {
            frame_state.page_sets[i] = VK_NULL_HANDLE;
        }
    }
    if (frame_state.page_set_epochs.size() < atlas_page_count) {
        const std::size_t previous_epoch_size = frame_state.page_set_epochs.size();
        frame_state.page_set_epochs.resize(atlas_page_count);
        for (std::size_t i = previous_epoch_size; i < frame_state.page_set_epochs.size(); ++i) {
            frame_state.page_set_epochs[i] = 0U;
        }
    }

    for (const auto& batch : runtime_scratch.draw_batches) {
        if (batch.glyph_count == 0U) {
            continue;
        }
        if (batch.atlas_page_id >= atlas_page_count) {
            continue;
        }
        (void)EnsurePageDescriptorSet(*context,
                                      *descriptor_host,
                                      frame_index_,
                                      batch.atlas_page_id);
    }
}

void TextRenderer2D::EnsurePipelineObjects(VulkanContext& context_,
                                           render::DescriptorHost& descriptor_host_,
                                           render::PipelineHost& pipeline_host_,
                                           VkFormat color_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        throw std::runtime_error(
            "TextRenderer2D requires Vulkan 1.3 dynamicRendering feature (required_vulkan13_features.dynamicRendering)");
    }

    if (descriptor_layout_id.IsValid() &&
        descriptor_host_.CachedLayoutCount() < descriptor_layout_id.value) {
        descriptor_layout_id = {};
    }

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

    if (!descriptor_layout_id.IsValid()) {
        render::DescriptorSetLayoutDesc layout_desc{};
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0U;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1U;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = nullptr;
        layout_desc.bindings.push_back(binding);
        descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
    }

    if (!pipeline_layout_id.IsValid()) {
        render::PipelineLayoutDesc pipeline_layout_desc{};
        pipeline_layout_desc.set_layouts.push_back(descriptor_host_.GetLayout(descriptor_layout_id));
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

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                      VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT;
    pipeline_desc.color_blend.attachments.push_back(blend_attachment);

    graphics_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
}

VkDescriptorSet TextRenderer2D::EnsurePageDescriptorSet(VulkanContext& context_,
                                                        render::DescriptorHost& descriptor_host_,
                                                        std::uint32_t frame_index_,
                                                        std::uint32_t page_index_) {
    if (frame_index_ >= frame_states.size()) {
        throw std::out_of_range("TextRenderer2D::EnsurePageDescriptorSet frame index out of range");
    }
    if (glyph_upload_host == nullptr) {
        throw std::runtime_error("TextRenderer2D::EnsurePageDescriptorSet missing GlyphUploadHost");
    }
    if (page_index_ >= glyph_upload_host->PageCount()) {
        throw std::out_of_range("TextRenderer2D::EnsurePageDescriptorSet page index out of range");
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    if (page_index_ >= frame_state.page_sets.size()) {
        const std::size_t previous_size = frame_state.page_sets.size();
        frame_state.page_sets.resize(page_index_ + 1U);
        for (std::size_t i = previous_size; i < frame_state.page_sets.size(); ++i) {
            frame_state.page_sets[i] = VK_NULL_HANDLE;
        }
    }
    if (page_index_ >= frame_state.page_set_epochs.size()) {
        const std::size_t previous_size = frame_state.page_set_epochs.size();
        frame_state.page_set_epochs.resize(page_index_ + 1U);
        for (std::size_t i = previous_size; i < frame_state.page_set_epochs.size(); ++i) {
            frame_state.page_set_epochs[i] = 0U;
        }
    }

    if (frame_state.page_set_epochs[page_index_] == frame_state.page_set_epoch &&
        frame_state.page_sets[page_index_] != VK_NULL_HANDLE) {
        return frame_state.page_sets[page_index_];
    }

    const VkDescriptorSet descriptor_set =
        descriptor_host_.AllocateSet(context_, frame_index_, descriptor_layout_id);

    descriptor_image_write_scratch.clear();
    descriptor_image_write_scratch.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = glyph_upload_host->Sampler(),
        .image_view = glyph_upload_host->PageImageView(page_index_),
        .image_layout = glyph_upload_host->PageShaderLayout(page_index_)
    });
    descriptor_host_.UpdateSet(context_,
                               descriptor_set,
                               descriptor_buffer_write_scratch,
                               descriptor_image_write_scratch,
                               descriptor_texel_write_scratch);

    frame_state.page_sets[page_index_] = descriptor_set;
    frame_state.page_set_epochs[page_index_] = frame_state.page_set_epoch;
    ++stats.descriptor_set_update_count;
    return descriptor_set;
}

void TextRenderer2D::RecordImageTransitionToColorAttachment(
    const render::FrameRecordContext& record_context_,
    bool has_previous_content_) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = has_previous_content_ ? VK_ACCESS_MEMORY_READ_BIT : 0U;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = has_previous_content_
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_UNDEFINED;
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

void TextRenderer2D::RecordImageTransitionToPresent(
    const render::FrameRecordContext& record_context_) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0U;
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

} // namespace vr::text
