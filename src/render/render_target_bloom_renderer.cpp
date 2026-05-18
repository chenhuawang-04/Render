#include "vr/render/render_target_bloom_renderer.hpp"

#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render/generated/render_target_bloom_blur_frag_spv.hpp"
#include "vr/render/generated/render_target_bloom_combine_frag_spv.hpp"
#include "vr/render/generated/render_target_bloom_prefilter_frag_spv.hpp"
#include "vr/render/generated/render_target_composite_vert_spv.hpp"
#include "vr/render/render_target_format_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace vr::render {

namespace {

constexpr float k_min_downsample_scale = 0.125F;
constexpr float k_max_downsample_scale = 1.0F;

[[nodiscard]] float ClampPositive(float value_,
                                  float minimum_,
                                  float maximum_) noexcept {
    if (!std::isfinite(value_)) {
        return minimum_;
    }
    return std::clamp(value_, minimum_, maximum_);
}

[[nodiscard]] VkExtent2D ResolveDownsampleExtent(const VkExtent3D& source_extent_,
                                                 float downsample_scale_) noexcept {
    const float clamped_scale = ClampPositive(downsample_scale_,
                                              k_min_downsample_scale,
                                              k_max_downsample_scale);
    const std::uint32_t width = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(source_extent_.width) * clamped_scale));
    const std::uint32_t height = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(static_cast<float>(source_extent_.height) * clamped_scale));
    return VkExtent2D{width, height};
}

void BindFullscreenViewportAndScissor(VkCommandBuffer command_buffer_,
                                      VkExtent2D extent_) noexcept {
    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(command_buffer_, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = extent_;
    vkCmdSetScissor(command_buffer_, 0U, 1U, &scissor);
}

} // namespace

void RenderTargetBloomRenderer::Initialize(const RenderTargetBloomRendererCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
    sampler_host = nullptr;
    bindless_resources = nullptr;
    single_source_pipeline_layout_id = {};
    dual_source_pipeline_layout_id = {};
    fullscreen_vertex_shader_id = {};
    prefilter_fragment_shader_id = {};
    blur_fragment_shader_id = {};
    combine_fragment_shader_id = {};
    prefilter_pipeline_id = {};
    blur_pipeline_id = {};
    combine_pipeline_id = {};
    intermediate_pipeline_format = VK_FORMAT_UNDEFINED;
    final_pipeline_color_format = VK_FORMAT_UNDEFINED;
    scene_source_target = {};
    scene_source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    bloom_target_a = {};
    bloom_target_b = {};
    scene_texture_slot = {};
    bloom_texture_slot_a = {};
    bloom_texture_slot_b = {};
    sampler_slot = {};
    active_intermediate_format = VK_FORMAT_UNDEFINED;
    frame_ready = false;
    initialized = true;
}

void RenderTargetBloomRenderer::Shutdown(VulkanContext& context_) {
    (void)context_;
    if (!initialized) {
        return;
    }

    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    render_target_host = nullptr;
    render_target_pool = nullptr;
    sampler_host = nullptr;
    bindless_resources = nullptr;
    single_source_pipeline_layout_id = {};
    dual_source_pipeline_layout_id = {};
    fullscreen_vertex_shader_id = {};
    prefilter_fragment_shader_id = {};
    blur_fragment_shader_id = {};
    combine_fragment_shader_id = {};
    prefilter_pipeline_id = {};
    blur_pipeline_id = {};
    combine_pipeline_id = {};
    intermediate_pipeline_format = VK_FORMAT_UNDEFINED;
    final_pipeline_color_format = VK_FORMAT_UNDEFINED;
    scene_source_target = {};
    scene_source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    bloom_target_a = {};
    bloom_target_b = {};
    scene_texture_slot = {};
    bloom_texture_slot_a = {};
    bloom_texture_slot_b = {};
    sampler_slot = {};
    active_intermediate_format = VK_FORMAT_UNDEFINED;
    frame_ready = false;
    initialized = false;
}

void RenderTargetBloomRenderer::SetSceneSourceTarget(
    RenderTargetHandle source_target_,
    RenderTargetStateKind expected_source_state_) noexcept {
    scene_source_target = source_target_;
    scene_source_expected_state = expected_source_state_;
}

void RenderTargetBloomRenderer::ClearSceneSourceTarget() noexcept {
    scene_source_target = {};
    scene_source_expected_state = RenderTargetStateKind::shader_read;
}

void RenderTargetBloomRenderer::SetOutputTargetConfig(
    const RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void RenderTargetBloomRenderer::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void RenderTargetBloomRenderer::PrepareFrame(const RenderTargetBloomRendererPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetBloomRenderer::PrepareFrame called before Initialize");
    }

    context = &prepare_view_.device;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    render_target_host = &prepare_view_.render_target;
    render_target_pool = &prepare_view_.render_target_pool;
    sampler_host = &prepare_view_.sampler;
    bindless_resources = prepare_view_.bindless;
    stats = {};
    bloom_target_a = {};
    bloom_target_b = {};
    scene_texture_slot = {};
    bloom_texture_slot_a = {};
    bloom_texture_slot_b = {};
    sampler_slot = {};
    active_intermediate_format = VK_FORMAT_UNDEFINED;
    frame_ready = false;

    if (!IsValidRenderTargetHandle(scene_source_target) ||
        !render_target_host->IsValid(scene_source_target)) {
        return;
    }

    const RenderTargetResolvedView source_view = render_target_host->ResolveView(scene_source_target);
    if (source_view.image_view == VK_NULL_HANDLE ||
        source_view.extent.width == 0U ||
        source_view.extent.height == 0U) {
        return;
    }

    active_intermediate_format = ResolveIntermediateFormat(*context,
                                                           create_info_cache.intermediate_format,
                                                           source_view.format);
    const RenderTargetDesc bloom_target_desc = BuildIntermediateTargetDesc(source_view);

    const auto bloom_a_result = render_target_pool->AcquireTransientTarget(
        *context,
        *render_target_host,
        bloom_target_desc);
    const auto bloom_b_result = render_target_pool->AcquireTransientTarget(
        *context,
        *render_target_host,
        bloom_target_desc);

    bloom_target_a = bloom_a_result.handle;
    bloom_target_b = bloom_b_result.handle;
    stats.transient_target_count = 2U;
    stats.transient_reuse_count =
        static_cast<std::uint32_t>(bloom_a_result.reused) +
        static_cast<std::uint32_t>(bloom_b_result.reused);

    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return;
    }

    scene_texture_slot = render_target_host->EnsureBindlessImageSlot(scene_source_target);
    bloom_texture_slot_a = render_target_host->EnsureBindlessImageSlot(bloom_target_a);
    bloom_texture_slot_b = render_target_host->EnsureBindlessImageSlot(bloom_target_b);
    sampler_slot = bindless_resources->DefaultSamplerSlot();

    frame_ready = IsValidRenderTargetHandle(bloom_target_a) &&
                  IsValidRenderTargetHandle(bloom_target_b) &&
                  scene_texture_slot.IsValid() &&
                  bloom_texture_slot_a.IsValid() &&
                  bloom_texture_slot_b.IsValid() &&
                  sampler_slot.IsValid();
}

void RenderTargetBloomRenderer::Record(const FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetBloomRenderer::Record called before Initialize");
    }
    if (context == nullptr ||
        descriptor_host == nullptr ||
        pipeline_host == nullptr ||
        render_target_host == nullptr) {
        throw std::runtime_error("RenderTargetBloomRenderer::Record called before PrepareFrame");
    }

    const auto record_fallback_output_pass = [&]() {
        const ResolvedColorRenderPass fallback_pass = BuildColorRenderPass(record_context_,
                                                                           output_target_config,
                                                                           create_info_cache.clear_swapchain,
                                                                           create_info_cache.clear_color,
                                                                           false);
        vkCmdBeginRendering(record_context_.command_buffer, fallback_pass.rendering_info.VkInfoPtr());
        BindFullscreenViewportAndScissor(record_context_.command_buffer, fallback_pass.target.extent);
        vkCmdEndRendering(record_context_.command_buffer);
        RecordEndColorPass(record_context_, output_target_config);
        ++stats.pass_count;
    };

    if (!frame_ready ||
        !IsValidRenderTargetHandle(scene_source_target) ||
        !render_target_host->IsValid(scene_source_target) ||
        !IsValidRenderTargetHandle(bloom_target_a) ||
        !render_target_host->IsValid(bloom_target_a) ||
        !IsValidRenderTargetHandle(bloom_target_b) ||
        !render_target_host->IsValid(bloom_target_b)) {
        ++stats.skipped_draw_count;
        record_fallback_output_pass();
        return;
    }
    if (IsValidRenderTargetHandle(output_target_config.color_target) &&
        ((output_target_config.color_target.index == scene_source_target.index &&
          output_target_config.color_target.generation == scene_source_target.generation) ||
         (output_target_config.color_target.index == bloom_target_a.index &&
          output_target_config.color_target.generation == bloom_target_a.generation) ||
         (output_target_config.color_target.index == bloom_target_b.index &&
          output_target_config.color_target.generation == bloom_target_b.generation))) {
        throw std::runtime_error(
            "RenderTargetBloomRenderer output target must differ from source/intermediate targets");
    }

    const RenderTargetResolvedView bloom_view_a = render_target_host->ResolveView(bloom_target_a);
    const ResolvedColorRenderTarget final_target = ResolveColorRenderTarget(record_context_,
                                                                            output_target_config);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          bloom_view_a.format,
                          final_target.format);

    const VkDescriptorSet bindless_sets[] = {
        bindless_resources->SampledImageSet(),
        bindless_resources->SamplerSet(),
    };
    const auto bind_bindless_sets = [&](PipelineLayoutId pipeline_layout_id_) {
        vkCmdBindDescriptorSets(record_context_.command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_host->GetPipelineLayout(pipeline_layout_id_),
                                0U,
                                2U,
                                bindless_sets,
                                0U,
                                nullptr);
        stats.descriptor_set_bind_count += 2U;
    };

    const std::uint32_t blur_pass_pair_count = std::max<std::uint32_t>(
        1U,
        create_info_cache.blur_pass_pair_count);

    const auto build_intermediate_output = [](RenderTargetHandle target_handle_) {
        RenderTargetColorOutputConfig output{};
        output.color_target = target_handle_;
        output.final_state = RenderTargetStateKind::shader_read;
        output.use_explicit_load_op = true;
        output.load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
        return output;
    };

    const RenderTargetColorOutputConfig bloom_output_a = build_intermediate_output(bloom_target_a);
    const RenderTargetColorOutputConfig bloom_output_b = build_intermediate_output(bloom_target_b);

    {
        render_target_host->RecordTransition(record_context_.command_buffer,
                                             scene_source_target,
                                             scene_source_expected_state);
        const ResolvedColorRenderPass prefilter_pass = BuildColorRenderPass(record_context_,
                                                                            bloom_output_a,
                                                                            false,
                                                                            {},
                                                                            false);

        vkCmdBeginRendering(record_context_.command_buffer, prefilter_pass.rendering_info.VkInfoPtr());
        BindFullscreenViewportAndScissor(record_context_.command_buffer, prefilter_pass.target.extent);
        vkCmdBindPipeline(record_context_.command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_host->GetGraphicsPipeline(prefilter_pipeline_id));
        bind_bindless_sets(single_source_pipeline_layout_id);

        const PrefilterPushConstants push_constants{
            .threshold = std::max(create_info_cache.bloom_threshold, 0.0F),
            .knee = std::max(create_info_cache.bloom_knee, 0.0001F),
            .texture_slot = scene_texture_slot.index,
            .sampler_slot = sampler_slot.index,
            .reserved0 = 0.0F,
            .reserved1 = 0U,
        };
        vkCmdPushConstants(record_context_.command_buffer,
                           pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                           0U,
                           sizeof(PrefilterPushConstants),
                           &push_constants);
        vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
        vkCmdEndRendering(record_context_.command_buffer);
        RecordEndColorPass(record_context_, bloom_output_a);
        ++stats.prefilter_draw_call_count;
        ++stats.pass_count;
    }

    for (std::uint32_t pass_pair_index = 0U;
         pass_pair_index < blur_pass_pair_count;
         ++pass_pair_index) {
        const RenderTargetResolvedView input_view_horizontal = render_target_host->ResolveView(bloom_target_a);
        const RenderTargetResolvedView input_view_vertical = render_target_host->ResolveView(bloom_target_b);

        {
            render_target_host->RecordTransition(record_context_.command_buffer,
                                                 bloom_target_a,
                                                 RenderTargetStateKind::shader_read);
            const ResolvedColorRenderPass blur_horizontal_pass = BuildColorRenderPass(record_context_,
                                                                                      bloom_output_b,
                                                                                      false,
                                                                                      {},
                                                                                      false);

            vkCmdBeginRendering(record_context_.command_buffer,
                                blur_horizontal_pass.rendering_info.VkInfoPtr());
            BindFullscreenViewportAndScissor(record_context_.command_buffer,
                                             blur_horizontal_pass.target.extent);
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(blur_pipeline_id));
            bind_bindless_sets(single_source_pipeline_layout_id);

            const BlurPushConstants push_constants{
                .texel_offset_x = (input_view_horizontal.extent.width > 0U)
                    ? (1.0F / static_cast<float>(input_view_horizontal.extent.width))
                    : 0.0F,
                .texel_offset_y = 0.0F,
                .filter_scale = std::max(create_info_cache.blur_filter_scale, 0.0F),
                .texture_slot = bloom_texture_slot_a.index,
                .sampler_slot = sampler_slot.index,
                .reserved0 = 0U,
            };
            vkCmdPushConstants(record_context_.command_buffer,
                               pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(BlurPushConstants),
                               &push_constants);
            vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
            vkCmdEndRendering(record_context_.command_buffer);
            RecordEndColorPass(record_context_, bloom_output_b);
            ++stats.blur_draw_call_count;
            ++stats.pass_count;
        }

        {
            render_target_host->RecordTransition(record_context_.command_buffer,
                                                 bloom_target_b,
                                                 RenderTargetStateKind::shader_read);
            const ResolvedColorRenderPass blur_vertical_pass = BuildColorRenderPass(record_context_,
                                                                                    bloom_output_a,
                                                                                    false,
                                                                                    {},
                                                                                    false);

            vkCmdBeginRendering(record_context_.command_buffer,
                                blur_vertical_pass.rendering_info.VkInfoPtr());
            BindFullscreenViewportAndScissor(record_context_.command_buffer,
                                             blur_vertical_pass.target.extent);
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(blur_pipeline_id));
            bind_bindless_sets(single_source_pipeline_layout_id);

            const BlurPushConstants push_constants{
                .texel_offset_x = 0.0F,
                .texel_offset_y = (input_view_vertical.extent.height > 0U)
                    ? (1.0F / static_cast<float>(input_view_vertical.extent.height))
                    : 0.0F,
                .filter_scale = std::max(create_info_cache.blur_filter_scale, 0.0F),
                .texture_slot = bloom_texture_slot_b.index,
                .sampler_slot = sampler_slot.index,
                .reserved0 = 0U,
            };
            vkCmdPushConstants(record_context_.command_buffer,
                               pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(BlurPushConstants),
                               &push_constants);
            vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
            vkCmdEndRendering(record_context_.command_buffer);
            RecordEndColorPass(record_context_, bloom_output_a);
            ++stats.blur_draw_call_count;
            ++stats.pass_count;
        }
    }

    render_target_host->RecordTransition(record_context_.command_buffer,
                                         scene_source_target,
                                         scene_source_expected_state);
    render_target_host->RecordTransition(record_context_.command_buffer,
                                         bloom_target_a,
                                         RenderTargetStateKind::shader_read);

    const ResolvedColorRenderPass final_pass = BuildColorRenderPass(record_context_,
                                                                    output_target_config,
                                                                    create_info_cache.clear_swapchain,
                                                                    create_info_cache.clear_color,
                                                                    false);
    vkCmdBeginRendering(record_context_.command_buffer, final_pass.rendering_info.VkInfoPtr());
    BindFullscreenViewportAndScissor(record_context_.command_buffer, final_pass.target.extent);
    vkCmdBindPipeline(record_context_.command_buffer,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(combine_pipeline_id));
    bind_bindless_sets(dual_source_pipeline_layout_id);

    const RenderTargetResolvedView scene_view = render_target_host->ResolveView(scene_source_target);
    const RenderTargetResolvedView bloom_view = render_target_host->ResolveView(bloom_target_a);
    CombinePushConstants combine_push_constants{};
    combine_push_constants.exposure = std::max(create_info_cache.exposure, 0.0F);
    combine_push_constants.inv_gamma = SafeInvGamma(create_info_cache.output_gamma);
    combine_push_constants.bloom_intensity = std::max(create_info_cache.bloom_intensity, 0.0F);
    combine_push_constants.flags = 0U;
    combine_push_constants.flags |= create_info_cache.enable_reinhard_tonemap ? 0x1U : 0U;
    combine_push_constants.flags |= create_info_cache.apply_manual_gamma ? 0x2U : 0U;
    combine_push_constants.flags |= (scene_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x4U : 0U;
    combine_push_constants.flags |= (bloom_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x8U : 0U;
    combine_push_constants.scene_texture_slot = scene_texture_slot.index;
    combine_push_constants.bloom_texture_slot = bloom_texture_slot_a.index;
    combine_push_constants.sampler_slot = sampler_slot.index;
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_host->GetPipelineLayout(dual_source_pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(CombinePushConstants),
                       &combine_push_constants);
    vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
    vkCmdEndRendering(record_context_.command_buffer);
    RecordEndColorPass(record_context_, output_target_config);
    ++stats.combine_draw_call_count;
    ++stats.pass_count;
}

void RenderTargetBloomRenderer::RecordGraphPrefilterPass(render_graph::GraphCommandContext& context_,
                                                     render_graph::ResourceHandle scene_source_,
                                                     render_graph::ResourceHandle bloom_target_) {
    if (!initialized || context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        render_target_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("RenderTargetBloomRenderer::RecordGraphPrefilterPass requires prepared runtime state");
    }
    const auto source_target = context_.ResolveTextureTarget(scene_source_);
    const auto bloom_target = context_.ResolveTextureTarget(bloom_target_);
    const auto source_view = context_.ResolveTextureView(scene_source_);
    const auto bloom_view = context_.ResolveTextureView(bloom_target_);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          bloom_view.format,
                          bloom_view.format);

    const auto scene_slot = render_target_host->EnsureBindlessImageSlot(source_target);
    const auto sampler = bindless_resources->DefaultSamplerSlot();
    const VkDescriptorSet bindless_sets[] = {
        bindless_resources->SampledImageSet(),
        bindless_resources->SamplerSet(),
    };
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(prefilter_pipeline_id));
    vkCmdBindDescriptorSets(context_.CommandBuffer(),
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                            0U,
                            2U,
                            bindless_sets,
                            0U,
                            nullptr);
    ++stats.descriptor_set_bind_count;
    BindFullscreenViewportAndScissor(context_.CommandBuffer(), VkExtent2D{bloom_view.extent.width, bloom_view.extent.height});

    const PrefilterPushConstants push_constants{
        .threshold = std::max(create_info_cache.bloom_threshold, 0.0F),
        .knee = std::max(create_info_cache.bloom_knee, 0.0001F),
        .texture_slot = scene_slot.index,
        .sampler_slot = sampler.index,
        .reserved0 = 0.0F,
        .reserved1 = 0U,
    };
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PrefilterPushConstants),
                       &push_constants);
    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
    ++stats.prefilter_draw_call_count;
    ++stats.pass_count;
}

void RenderTargetBloomRenderer::RecordGraphBlurPass(render_graph::GraphCommandContext& context_,
                                                    render_graph::ResourceHandle input_target_,
                                                    render_graph::ResourceHandle output_target_) {
    if (!initialized || context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        render_target_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("RenderTargetBloomRenderer::RecordGraphBlurPass requires prepared runtime state");
    }
    const auto input_target = context_.ResolveTextureTarget(input_target_);
    const auto input_view = context_.ResolveTextureView(input_target_);
    const auto output_view = context_.ResolveTextureView(output_target_);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          output_view.format,
                          output_view.format);

    const auto input_slot = render_target_host->EnsureBindlessImageSlot(input_target);
    const auto sampler = bindless_resources->DefaultSamplerSlot();
    const VkDescriptorSet bindless_sets[] = {
        bindless_resources->SampledImageSet(),
        bindless_resources->SamplerSet(),
    };
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(blur_pipeline_id));
    vkCmdBindDescriptorSets(context_.CommandBuffer(),
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                            0U,
                            2U,
                            bindless_sets,
                            0U,
                            nullptr);
    ++stats.descriptor_set_bind_count;
    BindFullscreenViewportAndScissor(context_.CommandBuffer(), VkExtent2D{output_view.extent.width, output_view.extent.height});

    const BlurPushConstants push_constants{
        .texel_offset_x = (input_view.extent.width > 0U)
            ? (1.0F / static_cast<float>(input_view.extent.width))
            : 0.0F,
        .texel_offset_y = (input_view.extent.height > 0U)
            ? (1.0F / static_cast<float>(input_view.extent.height))
            : 0.0F,
        .filter_scale = std::max(create_info_cache.blur_filter_scale, 0.0F),
        .texture_slot = input_slot.index,
        .sampler_slot = sampler.index,
        .reserved0 = 0U,
    };
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(BlurPushConstants),
                       &push_constants);
    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
    ++stats.blur_draw_call_count;
    ++stats.pass_count;
}

void RenderTargetBloomRenderer::RecordGraphCombinePass(render_graph::GraphCommandContext& context_,
                                                       render_graph::ResourceHandle scene_source_,
                                                       render_graph::ResourceHandle bloom_target_,
                                                       render_graph::ResourceHandle output_target_) {
    if (!initialized || context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        render_target_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("RenderTargetBloomRenderer::RecordGraphCombinePass requires prepared runtime state");
    }
    const auto scene_target = context_.ResolveTextureTarget(scene_source_);
    const auto bloom_target = context_.ResolveTextureTarget(bloom_target_);
    const auto output_view = context_.ResolveTextureView(output_target_);
    const auto scene_view = context_.ResolveTextureView(scene_source_);
    const auto bloom_view = context_.ResolveTextureView(bloom_target_);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          bloom_view.format,
                          output_view.format);

    const auto scene_slot = render_target_host->EnsureBindlessImageSlot(scene_target);
    const auto bloom_slot = render_target_host->EnsureBindlessImageSlot(bloom_target);
    const auto sampler = bindless_resources->DefaultSamplerSlot();
    const VkDescriptorSet bindless_sets[] = {
        bindless_resources->SampledImageSet(),
        bindless_resources->SamplerSet(),
    };
    vkCmdBindPipeline(context_.CommandBuffer(),
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_host->GetGraphicsPipeline(combine_pipeline_id));
    vkCmdBindDescriptorSets(context_.CommandBuffer(),
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_host->GetPipelineLayout(dual_source_pipeline_layout_id),
                            0U,
                            2U,
                            bindless_sets,
                            0U,
                            nullptr);
    ++stats.descriptor_set_bind_count;
    BindFullscreenViewportAndScissor(context_.CommandBuffer(), VkExtent2D{output_view.extent.width, output_view.extent.height});

    CombinePushConstants push_constants{};
    push_constants.exposure = std::max(create_info_cache.exposure, 0.0F);
    push_constants.inv_gamma = SafeInvGamma(create_info_cache.output_gamma);
    push_constants.bloom_intensity = std::max(create_info_cache.bloom_intensity, 0.0F);
    push_constants.flags = 0U;
    push_constants.flags |= create_info_cache.enable_reinhard_tonemap ? 0x1U : 0U;
    push_constants.flags |= create_info_cache.apply_manual_gamma ? 0x2U : 0U;
    push_constants.flags |= (scene_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x4U : 0U;
    push_constants.flags |= (bloom_view.color_encoding == RenderTargetColorEncoding::srgb) ? 0x8U : 0U;
    push_constants.scene_texture_slot = scene_slot.index;
    push_constants.bloom_texture_slot = bloom_slot.index;
    push_constants.sampler_slot = sampler.index;
    vkCmdPushConstants(context_.CommandBuffer(),
                       pipeline_host->GetPipelineLayout(dual_source_pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(CombinePushConstants),
                       &push_constants);
    vkCmdDraw(context_.CommandBuffer(), 3U, 1U, 0U, 0U);
    ++stats.combine_draw_call_count;
    ++stats.pass_count;
}

void RenderTargetBloomRenderer::OnSwapchainRecreated(std::uint32_t image_count_,
                                                     VkExtent2D extent_,
                                                     VkFormat format_) {
    (void)image_count_;
    (void)extent_;
    (void)format_;
}

bool RenderTargetBloomRenderer::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetBloomRendererStats& RenderTargetBloomRenderer::Stats() const noexcept {
    return stats;
}

const RenderTargetBloomRendererCreateInfo& RenderTargetBloomRenderer::CreateInfo() const noexcept {
    return create_info_cache;
}

void RenderTargetBloomRenderer::EnsurePipelineObjects(VulkanContext& context_,
                                                      DescriptorHost& descriptor_host_,
                                                      PipelineHost& pipeline_host_,
                                                      VkFormat intermediate_format_,
                                                      VkFormat final_color_format_) {
    (void)descriptor_host_;
    if (bindless_resources == nullptr ||
        bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
        bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderTargetBloomRenderer::EnsurePipelineObjects requires bindless resource layouts");
    }

    if (!fullscreen_vertex_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        fullscreen_vertex_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!prefilter_fragment_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_bloom_prefilter_frag_spv;
        shader_create_info.word_count = generated::k_render_target_bloom_prefilter_frag_spv_word_count;
        prefilter_fragment_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!blur_fragment_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_bloom_blur_frag_spv;
        shader_create_info.word_count = generated::k_render_target_bloom_blur_frag_spv_word_count;
        blur_fragment_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!combine_fragment_shader_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_bloom_combine_frag_spv;
        shader_create_info.word_count = generated::k_render_target_bloom_combine_frag_spv_word_count;
        combine_fragment_shader_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!single_source_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PrefilterPushConstants),
        });
        single_source_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }
    if (!dual_source_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(bindless_resources->SampledImageLayout());
        layout_desc.set_layouts.push_back(bindless_resources->SamplerLayout());
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(CombinePushConstants),
        });
        dual_source_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    const auto build_fullscreen_pipeline_desc = [&](PipelineLayoutId pipeline_layout_id_,
                                                    ShaderModuleId fragment_shader_id_,
                                                    VkFormat color_format_) {
        GraphicsPipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id_);
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = pipeline_host_.GetShaderModule(fullscreen_vertex_shader_id),
            .entry_name = "main",
            .flags = 0U,
            .specialization = {}
        });
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = pipeline_host_.GetShaderModule(fragment_shader_id_),
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
        return pipeline_desc;
    };

    if (!prefilter_pipeline_id.IsValid() || intermediate_pipeline_format != intermediate_format_) {
        GraphicsPipelineDesc pipeline_desc = build_fullscreen_pipeline_desc(single_source_pipeline_layout_id,
                                                                           prefilter_fragment_shader_id,
                                                                           intermediate_format_);
        prefilter_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    if (!blur_pipeline_id.IsValid() || intermediate_pipeline_format != intermediate_format_) {
        GraphicsPipelineDesc pipeline_desc = build_fullscreen_pipeline_desc(single_source_pipeline_layout_id,
                                                                           blur_fragment_shader_id,
                                                                           intermediate_format_);
        blur_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    if (!combine_pipeline_id.IsValid() || final_pipeline_color_format != final_color_format_) {
        GraphicsPipelineDesc pipeline_desc = build_fullscreen_pipeline_desc(dual_source_pipeline_layout_id,
                                                                           combine_fragment_shader_id,
                                                                           final_color_format_);
        combine_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    intermediate_pipeline_format = intermediate_format_;
    final_pipeline_color_format = final_color_format_;
}

RenderTargetDesc RenderTargetBloomRenderer::BuildIntermediateTargetDesc(
    const RenderTargetResolvedView& source_view_) const {
    RenderTargetDesc desc{};
    desc.debug_name = "BloomIntermediate";
    desc.dimension = RenderTargetDimension::image_2d;
    desc.lifetime = RenderTargetLifetime::transient;
    desc.scale_mode = RenderTargetScaleMode::absolute;
    const VkExtent2D bloom_extent = ResolveDownsampleExtent(source_view_.extent,
                                                            create_info_cache.downsample_scale);
    desc.width = bloom_extent.width;
    desc.height = bloom_extent.height;
    desc.depth = 1U;
    desc.format = active_intermediate_format;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.color_encoding = create_info_cache.intermediate_color_encoding;
    return desc;
}

VkFormat RenderTargetBloomRenderer::ResolveIntermediateFormat(VulkanContext& context_,
                                                              VkFormat requested_format_,
                                                              VkFormat source_format_) {
    if (requested_format_ != VK_FORMAT_UNDEFINED) {
        if (!IsColorAttachmentSampledFormatSupported(context_, requested_format_)) {
            throw std::runtime_error("RenderTargetBloomRenderer requested intermediate format is unsupported");
        }
        return requested_format_;
    }

    if (source_format_ != VK_FORMAT_UNDEFINED &&
        IsColorAttachmentSampledFormatSupported(context_, source_format_)) {
        return source_format_;
    }

    constexpr std::array<VkFormat, 4U> defaults{
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM
    };
    return ResolveFirstSupportedColorAttachmentSampledFormat(context_, defaults);
}

float RenderTargetBloomRenderer::SafeInvGamma(float gamma_) noexcept {
    if (!(gamma_ > 0.0F) || !std::isfinite(gamma_)) {
        return 1.0F;
    }
    return 1.0F / gamma_;
}

} // namespace vr::render
