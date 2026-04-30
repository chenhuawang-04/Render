#include "vr/render/render_target_composite_renderer.hpp"

#include "vr/render/generated/render_target_composite_frag_spv.hpp"
#include "vr/render/generated/render_target_composite_vert_spv.hpp"
#include "vr/render/runtime_prepare_context.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] bool DescriptorKeyEquals(const RenderTargetDescriptorKey& lhs_,
                                       const RenderTargetDescriptorKey& rhs_) noexcept {
    return lhs_.target.index == rhs_.target.index &&
           lhs_.target.generation == rhs_.target.generation &&
           lhs_.view.view_type == rhs_.view.view_type &&
           lhs_.view.aspect == rhs_.view.aspect &&
           lhs_.view.base_mip_level == rhs_.view.base_mip_level &&
           lhs_.view.level_count == rhs_.view.level_count &&
           lhs_.view.base_array_layer == rhs_.view.base_array_layer &&
           lhs_.view.layer_count == rhs_.view.layer_count &&
           lhs_.expected_state == rhs_.expected_state &&
           lhs_.descriptor_type == rhs_.descriptor_type &&
           lhs_.descriptor_usage == rhs_.descriptor_usage;
}

[[nodiscard]] float SafeInvGamma(float gamma_) noexcept {
    if (!(gamma_ > 0.0F) || !std::isfinite(gamma_)) {
        return 1.0F;
    }
    return 1.0F / gamma_;
}

} // namespace

void RenderTargetCompositeRenderer::Initialize(
    const RenderTargetCompositeRendererCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    sampler_host = nullptr;
    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    source_sampler_id = {};
    frame_descriptor_cache.clear();
    source_target = {};
    source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void RenderTargetCompositeRenderer::Shutdown(VulkanContext& context_) {
    (void)context_;
    if (!initialized) {
        return;
    }

    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    sampler_host = nullptr;
    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    source_sampler_id = {};
    frame_descriptor_cache.clear();
    source_target = {};
    source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = false;
}

void RenderTargetCompositeRenderer::SetSourceTarget(
    RenderTargetHandle source_target_,
    RenderTargetStateKind expected_source_state_) noexcept {
    source_target = source_target_;
    source_expected_state = expected_source_state_;
}

void RenderTargetCompositeRenderer::ClearSourceTarget() noexcept {
    source_target = {};
    source_expected_state = RenderTargetStateKind::shader_read;
}

void RenderTargetCompositeRenderer::SetOutputTargetConfig(
    const RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void RenderTargetCompositeRenderer::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void RenderTargetCompositeRenderer::PrepareFrame(const RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetCompositeRenderer::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr ||
        prepare_context_.descriptor_host == nullptr ||
        prepare_context_.pipeline_host == nullptr ||
        prepare_context_.render_target_host == nullptr ||
        prepare_context_.sampler_host == nullptr) {
        throw std::runtime_error("RenderTargetCompositeRenderer::PrepareFrame missing runtime dependencies");
    }

    context = prepare_context_.context;
    descriptor_host = prepare_context_.descriptor_host;
    pipeline_host = prepare_context_.pipeline_host;
    render_target_host = prepare_context_.render_target_host;
    sampler_host = prepare_context_.sampler_host;
    stats = {};

    if (prepare_context_.frame_index >= frame_descriptor_cache.size()) {
        frame_descriptor_cache.resize(prepare_context_.frame_index + 1U);
    }
    frame_descriptor_cache[prepare_context_.frame_index] = {};
}

void RenderTargetCompositeRenderer::Record(const FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetCompositeRenderer::Record called before Initialize");
    }
    if (context == nullptr ||
        descriptor_host == nullptr ||
        pipeline_host == nullptr ||
        render_target_host == nullptr ||
        sampler_host == nullptr) {
        throw std::runtime_error("RenderTargetCompositeRenderer::Record called before PrepareFrame");
    }
    if (!IsValidRenderTargetHandle(source_target)) {
        ++stats.skipped_draw_count;
        return;
    }
    if (!render_target_host->IsValid(source_target)) {
        ++stats.skipped_draw_count;
        return;
    }
    if (IsValidRenderTargetHandle(output_target_config.color_target) &&
        output_target_config.color_target.index == source_target.index &&
        output_target_config.color_target.generation == source_target.generation) {
        throw std::runtime_error("RenderTargetCompositeRenderer source target must differ from output target");
    }

    render_target_host->RecordTransition(record_context_.command_buffer,
                                         source_target,
                                         source_expected_state);

    const ResolvedColorRenderPass color_pass = BuildColorRenderPass(record_context_,
                                                                    output_target_config,
                                                                    create_info_cache.clear_swapchain,
                                                                    create_info_cache.clear_color,
                                                                    false);
    EnsurePipelineObjects(*context, *descriptor_host, *pipeline_host, color_pass.target.format);
    const VkDescriptorSet descriptor_set = AcquireSourceDescriptorSet(record_context_.frame_index,
                                                                      source_target,
                                                                      source_expected_state);
    if (descriptor_set == VK_NULL_HANDLE) {
        ++stats.skipped_draw_count;
        return;
    }

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
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = color_pass.target.extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    vkCmdBindPipeline(record_context_.command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(pipeline_id));
    vkCmdBindDescriptorSets(record_context_.command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_host->GetPipelineLayout(pipeline_layout_id),
                            0U,
                            1U,
                            &descriptor_set,
                            0U,
                            nullptr);
    ++stats.descriptor_set_bind_count;

    const RenderTargetResolvedView source_view = render_target_host->ResolveView(source_target);
    PushConstants push_constants{};
    push_constants.exposure = create_info_cache.exposure;
    push_constants.inv_gamma = SafeInvGamma(create_info_cache.output_gamma);
    push_constants.flags = 0U;
    push_constants.flags |= create_info_cache.enable_reinhard_tonemap ? 0x1U : 0U;
    push_constants.flags |= create_info_cache.apply_manual_gamma ? 0x2U : 0U;
    push_constants.flags |= (source_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x4U : 0U;
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
    ++stats.draw_call_count;

    vkCmdEndRendering(record_context_.command_buffer);
    RecordEndColorPass(record_context_, output_target_config);
}

void RenderTargetCompositeRenderer::OnSwapchainRecreated(std::uint32_t image_count_,
                                                         VkExtent2D extent_,
                                                         VkFormat format_) {
    swapchain_extent = extent_;
    swapchain_format = format_;
    frame_descriptor_cache.resize(image_count_);
    for (auto& entry : frame_descriptor_cache) {
        entry = {};
    }
}

bool RenderTargetCompositeRenderer::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetCompositeRendererStats& RenderTargetCompositeRenderer::Stats() const noexcept {
    return stats;
}

void RenderTargetCompositeRenderer::EnsurePipelineObjects(VulkanContext& context_,
                                                          DescriptorHost& descriptor_host_,
                                                          PipelineHost& pipeline_host_,
                                                          VkFormat color_format_) {
    if (!descriptor_layout_id.IsValid()) {
        DescriptorSetLayoutDesc layout_desc{};
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
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_frag_spv;
        shader_create_info.word_count = generated::k_render_target_composite_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(descriptor_host_.GetLayout(descriptor_layout_id));
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants),
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_id.IsValid() && pipeline_color_format == color_format_) {
        return;
    }

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
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
    pipeline_desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_desc.viewport.viewport_count = 1U;
    pipeline_desc.viewport.scissor_count = 1U;
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);
    pipeline_desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
    pipeline_desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    pipeline_desc.rasterization.line_width = 1.0F;
    pipeline_desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    pipeline_desc.depth_stencil.depth_test_enable = false;
    pipeline_desc.depth_stencil.depth_write_enable = false;
    VkPipelineColorBlendAttachmentState blend_state{};
    blend_state.blendEnable = VK_FALSE;
    blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;
    pipeline_desc.color_blend.attachments.push_back(blend_state);
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);

    pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
}

VkDescriptorSet RenderTargetCompositeRenderer::AcquireSourceDescriptorSet(
    std::uint32_t frame_index_,
    RenderTargetHandle source_target_,
    RenderTargetStateKind expected_source_state_) {
    if (frame_index_ >= frame_descriptor_cache.size()) {
        frame_descriptor_cache.resize(frame_index_ + 1U);
    }

    const RenderTargetResolvedView resolved_view = render_target_host->ResolveView(source_target_);
    const RenderTargetDescriptorKey descriptor_key{
        .target = source_target_,
        .view = {},
        .expected_state = expected_source_state_,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptor_usage = RenderTargetDescriptorUsage::combined_image_sampler,
    };

    DescriptorCacheEntry& entry = frame_descriptor_cache[frame_index_];
    if (entry.valid &&
        entry.descriptor_set != VK_NULL_HANDLE &&
        entry.resource_revision == resolved_view.resource_revision &&
        DescriptorKeyEquals(entry.descriptor_key, descriptor_key)) {
        stats.reused_descriptor_set = true;
        return entry.descriptor_set;
    }

    if (!source_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_LINEAR;
        sampler_desc.min_filter = VK_FILTER_LINEAR;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.min_lod = 0.0F;
        sampler_desc.max_lod = 0.0F;
        source_sampler_id = sampler_host->RegisterSampler(*context, sampler_desc);
    }

    const VkDescriptorSet descriptor_set = descriptor_host->AllocateSet(*context,
                                                                        frame_index_,
                                                                        descriptor_layout_id);
    if (descriptor_set == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    DescriptorMcVector<DescriptorImageWrite> image_writes{};
    image_writes.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = sampler_host->GetSampler(source_sampler_id),
        .image_view = resolved_view.image_view,
        .image_layout = RenderTargetHost::DescribeState(expected_source_state_, resolved_view.aspect).layout,
    });

    descriptor_host->UpdateSet(*context, descriptor_set, {}, image_writes, {});
    ++stats.descriptor_set_update_count;

    entry.descriptor_set = descriptor_set;
    entry.descriptor_key = descriptor_key;
    entry.resource_revision = resolved_view.resource_revision;
    entry.valid = true;
    return descriptor_set;
}

std::uint64_t RenderTargetCompositeRenderer::HashDescriptorKey(
    const RenderTargetDescriptorKey& descriptor_key_,
    std::uint32_t resource_revision_) noexcept {
    std::uint64_t hash = descriptor_key_.target.index;
    hash = (hash * 1315423911ULL) ^ descriptor_key_.target.generation;
    hash = (hash * 1315423911ULL) ^ descriptor_key_.view.aspect;
    hash = (hash * 1315423911ULL) ^ descriptor_key_.view.base_mip_level;
    hash = (hash * 1315423911ULL) ^ descriptor_key_.view.level_count;
    hash = (hash * 1315423911ULL) ^ descriptor_key_.view.base_array_layer;
    hash = (hash * 1315423911ULL) ^ descriptor_key_.view.layer_count;
    hash = (hash * 1315423911ULL) ^ static_cast<std::uint64_t>(descriptor_key_.expected_state);
    hash = (hash * 1315423911ULL) ^ static_cast<std::uint64_t>(descriptor_key_.descriptor_type);
    hash = (hash * 1315423911ULL) ^ static_cast<std::uint64_t>(descriptor_key_.descriptor_usage);
    hash = (hash * 1315423911ULL) ^ resource_revision_;
    return hash;
}

} // namespace vr::render
