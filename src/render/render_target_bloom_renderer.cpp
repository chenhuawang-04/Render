#include "vr/render/render_target_bloom_renderer.hpp"

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
    single_source_layout_id = {};
    dual_source_layout_id = {};
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
    linear_clamp_sampler_id = {};
    frame_descriptor_cache.clear();
    scene_source_target = {};
    scene_source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    bloom_target_a = {};
    bloom_target_b = {};
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
    single_source_layout_id = {};
    dual_source_layout_id = {};
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
    linear_clamp_sampler_id = {};
    frame_descriptor_cache.clear();
    scene_source_target = {};
    scene_source_expected_state = RenderTargetStateKind::shader_read;
    output_target_config = {};
    bloom_target_a = {};
    bloom_target_b = {};
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
    stats = {};
    bloom_target_a = {};
    bloom_target_b = {};
    active_intermediate_format = VK_FORMAT_UNDEFINED;
    frame_ready = false;

    if (prepare_view_.frame.frame_index >= frame_descriptor_cache.size()) {
        frame_descriptor_cache.resize(prepare_view_.frame.frame_index + 1U);
    }
    frame_descriptor_cache[prepare_view_.frame.frame_index] = {};

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

    frame_ready = IsValidRenderTargetHandle(bloom_target_a) &&
                  IsValidRenderTargetHandle(bloom_target_b);
}

void RenderTargetBloomRenderer::Record(const FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("RenderTargetBloomRenderer::Record called before Initialize");
    }
    if (context == nullptr ||
        descriptor_host == nullptr ||
        pipeline_host == nullptr ||
        render_target_host == nullptr ||
        sampler_host == nullptr) {
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
        throw std::runtime_error("RenderTargetBloomRenderer output target must differ from source/intermediate targets");
    }

    const RenderTargetResolvedView bloom_view_a = render_target_host->ResolveView(bloom_target_a);
    const ResolvedColorRenderTarget final_target = ResolveColorRenderTarget(record_context_,
                                                                            output_target_config);
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          bloom_view_a.format,
                          final_target.format);

    if (record_context_.frame_index >= frame_descriptor_cache.size()) {
        frame_descriptor_cache.resize(record_context_.frame_index + 1U);
    }
    FrameDescriptorCache& descriptor_cache = frame_descriptor_cache[record_context_.frame_index];

    const std::uint32_t blur_pass_pair_count = std::max<std::uint32_t>(1U,
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
        const VkDescriptorSet descriptor_set = AcquireSingleSourceDescriptorSet(
            record_context_.frame_index,
            descriptor_cache.prefilter,
            single_source_layout_id,
            scene_source_target,
            scene_source_expected_state);
        if (descriptor_set == VK_NULL_HANDLE) {
            ++stats.skipped_draw_count;
            record_fallback_output_pass();
            return;
        }

        vkCmdBeginRendering(record_context_.command_buffer, prefilter_pass.rendering_info.VkInfoPtr());
        BindFullscreenViewportAndScissor(record_context_.command_buffer, prefilter_pass.target.extent);
        vkCmdBindPipeline(record_context_.command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_host->GetGraphicsPipeline(prefilter_pipeline_id));
        vkCmdBindDescriptorSets(record_context_.command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                                0U,
                                1U,
                                &descriptor_set,
                                0U,
                                nullptr);
        ++stats.descriptor_set_bind_count;

        const PrefilterPushConstants push_constants{
            .threshold = std::max(create_info_cache.bloom_threshold, 0.0F),
            .knee = std::max(create_info_cache.bloom_knee, 0.0001F),
            .reserved0 = 0.0F,
            .reserved1 = 0.0F
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
            const VkDescriptorSet descriptor_set = AcquireSingleSourceDescriptorSet(
                record_context_.frame_index,
                descriptor_cache.blur_horizontal,
                single_source_layout_id,
                bloom_target_a,
                RenderTargetStateKind::shader_read);
            if (descriptor_set == VK_NULL_HANDLE) {
                ++stats.skipped_draw_count;
                record_fallback_output_pass();
                return;
            }

            vkCmdBeginRendering(record_context_.command_buffer,
                                blur_horizontal_pass.rendering_info.VkInfoPtr());
            BindFullscreenViewportAndScissor(record_context_.command_buffer,
                                             blur_horizontal_pass.target.extent);
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(blur_pipeline_id));
            vkCmdBindDescriptorSets(record_context_.command_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                                    0U,
                                    1U,
                                    &descriptor_set,
                                    0U,
                                    nullptr);
            ++stats.descriptor_set_bind_count;

            const BlurPushConstants push_constants{
                .texel_offset_x = (input_view_horizontal.extent.width > 0U)
                    ? (1.0F / static_cast<float>(input_view_horizontal.extent.width))
                    : 0.0F,
                .texel_offset_y = 0.0F,
                .filter_scale = std::max(create_info_cache.blur_filter_scale, 0.0F),
                .reserved0 = 0.0F
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
            const VkDescriptorSet descriptor_set = AcquireSingleSourceDescriptorSet(
                record_context_.frame_index,
                descriptor_cache.blur_vertical,
                single_source_layout_id,
                bloom_target_b,
                RenderTargetStateKind::shader_read);
            if (descriptor_set == VK_NULL_HANDLE) {
                ++stats.skipped_draw_count;
                record_fallback_output_pass();
                return;
            }

            vkCmdBeginRendering(record_context_.command_buffer,
                                blur_vertical_pass.rendering_info.VkInfoPtr());
            BindFullscreenViewportAndScissor(record_context_.command_buffer,
                                             blur_vertical_pass.target.extent);
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(blur_pipeline_id));
            vkCmdBindDescriptorSets(record_context_.command_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_host->GetPipelineLayout(single_source_pipeline_layout_id),
                                    0U,
                                    1U,
                                    &descriptor_set,
                                    0U,
                                    nullptr);
            ++stats.descriptor_set_bind_count;

            const BlurPushConstants push_constants{
                .texel_offset_x = 0.0F,
                .texel_offset_y = (input_view_vertical.extent.height > 0U)
                    ? (1.0F / static_cast<float>(input_view_vertical.extent.height))
                    : 0.0F,
                .filter_scale = std::max(create_info_cache.blur_filter_scale, 0.0F),
                .reserved0 = 0.0F
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

    const VkDescriptorSet combine_descriptor_set = AcquireDualSourceDescriptorSet(
        record_context_.frame_index,
        descriptor_cache.combine,
        dual_source_layout_id,
        scene_source_target,
        scene_source_expected_state,
        bloom_target_a,
        RenderTargetStateKind::shader_read);
    if (combine_descriptor_set == VK_NULL_HANDLE) {
        ++stats.skipped_draw_count;
        record_fallback_output_pass();
        return;
    }

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
    vkCmdBindDescriptorSets(record_context_.command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_host->GetPipelineLayout(dual_source_pipeline_layout_id),
                            0U,
                            1U,
                            &combine_descriptor_set,
                            0U,
                            nullptr);
    ++stats.descriptor_set_bind_count;

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

void RenderTargetBloomRenderer::OnSwapchainRecreated(std::uint32_t image_count_,
                                                     VkExtent2D extent_,
                                                     VkFormat format_) {
    (void)extent_;
    (void)format_;
    frame_descriptor_cache.resize(image_count_);
    for (auto& entry : frame_descriptor_cache) {
        entry = {};
    }
}

bool RenderTargetBloomRenderer::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetBloomRendererStats& RenderTargetBloomRenderer::Stats() const noexcept {
    return stats;
}

void RenderTargetBloomRenderer::EnsurePipelineObjects(VulkanContext& context_,
                                                      DescriptorHost& descriptor_host_,
                                                      PipelineHost& pipeline_host_,
                                                      VkFormat intermediate_format_,
                                                      VkFormat final_color_format_) {
    if (!single_source_layout_id.IsValid()) {
        DescriptorSetLayoutDesc layout_desc{};
        layout_desc.bindings.push_back({
            .binding = 0U,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        });
        single_source_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
    }
    if (!dual_source_layout_id.IsValid()) {
        DescriptorSetLayoutDesc layout_desc{};
        layout_desc.bindings.push_back({
            .binding = 0U,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        });
        layout_desc.bindings.push_back({
            .binding = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        });
        dual_source_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
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
        layout_desc.set_layouts.push_back(descriptor_host_.GetLayout(single_source_layout_id));
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PrefilterPushConstants),
        });
        single_source_pipeline_layout_id =
            pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }
    if (!dual_source_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(descriptor_host_.GetLayout(dual_source_layout_id));
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(CombinePushConstants),
        });
        dual_source_pipeline_layout_id =
            pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (!prefilter_pipeline_id.IsValid() || intermediate_pipeline_format != intermediate_format_) {
        GraphicsPipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(single_source_pipeline_layout_id);
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = pipeline_host_.GetShaderModule(fullscreen_vertex_shader_id),
            .entry_name = "main",
            .flags = 0U,
            .specialization = {}
        });
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = pipeline_host_.GetShaderModule(prefilter_fragment_shader_id),
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
        pipeline_desc.rendering.color_attachment_formats.push_back(intermediate_format_);
        prefilter_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    if (!blur_pipeline_id.IsValid() || intermediate_pipeline_format != intermediate_format_) {
        GraphicsPipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(single_source_pipeline_layout_id);
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = pipeline_host_.GetShaderModule(fullscreen_vertex_shader_id),
            .entry_name = "main",
            .flags = 0U,
            .specialization = {}
        });
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = pipeline_host_.GetShaderModule(blur_fragment_shader_id),
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
        pipeline_desc.rendering.color_attachment_formats.push_back(intermediate_format_);
        blur_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    if (!combine_pipeline_id.IsValid() || final_pipeline_color_format != final_color_format_) {
        GraphicsPipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(dual_source_pipeline_layout_id);
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = pipeline_host_.GetShaderModule(fullscreen_vertex_shader_id),
            .entry_name = "main",
            .flags = 0U,
            .specialization = {}
        });
        pipeline_desc.shader_stages.push_back({
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = pipeline_host_.GetShaderModule(combine_fragment_shader_id),
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
        pipeline_desc.rendering.color_attachment_formats.push_back(final_color_format_);
        combine_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    }

    intermediate_pipeline_format = intermediate_format_;
    final_pipeline_color_format = final_color_format_;
}

VkDescriptorSet RenderTargetBloomRenderer::AcquireSingleSourceDescriptorSet(
    std::uint32_t frame_index_,
    SingleSourceDescriptorCacheEntry& cache_entry_,
    DescriptorSetLayoutId layout_id_,
    RenderTargetHandle source_target_,
    RenderTargetStateKind expected_source_state_) {
    const RenderTargetResolvedView resolved_view = render_target_host->ResolveView(source_target_);
    const RenderTargetDescriptorKey descriptor_key{
        .target = source_target_,
        .view = {},
        .expected_state = expected_source_state_,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptor_usage = RenderTargetDescriptorUsage::combined_image_sampler,
    };

    if (cache_entry_.valid &&
        cache_entry_.descriptor_set != VK_NULL_HANDLE &&
        cache_entry_.resource_revision == resolved_view.resource_revision &&
        SingleSourceDescriptorKeyEquals(cache_entry_.descriptor_key, descriptor_key)) {
        stats.reused_descriptor_set = true;
        return cache_entry_.descriptor_set;
    }

    if (!linear_clamp_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_LINEAR;
        sampler_desc.min_filter = VK_FILTER_LINEAR;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.min_lod = 0.0F;
        sampler_desc.max_lod = 0.0F;
        linear_clamp_sampler_id = sampler_host->RegisterSampler(*context, sampler_desc);
    }

    const VkDescriptorSet descriptor_set = descriptor_host->AllocateSet(*context,
                                                                        frame_index_,
                                                                        layout_id_);
    if (descriptor_set == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    DescriptorMcVector<DescriptorImageWrite> image_writes{};
    image_writes.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = sampler_host->GetSampler(linear_clamp_sampler_id),
        .image_view = resolved_view.image_view,
        .image_layout = RenderTargetHost::DescribeState(expected_source_state_, resolved_view.aspect).layout,
    });

    descriptor_host->UpdateSet(*context, descriptor_set, {}, image_writes, {});
    ++stats.descriptor_set_update_count;
    cache_entry_.descriptor_set = descriptor_set;
    cache_entry_.descriptor_key = descriptor_key;
    cache_entry_.resource_revision = resolved_view.resource_revision;
    cache_entry_.valid = true;
    return descriptor_set;
}

VkDescriptorSet RenderTargetBloomRenderer::AcquireDualSourceDescriptorSet(
    std::uint32_t frame_index_,
    DualSourceDescriptorCacheEntry& cache_entry_,
    DescriptorSetLayoutId layout_id_,
    RenderTargetHandle scene_target_,
    RenderTargetStateKind scene_expected_state_,
    RenderTargetHandle bloom_target_,
    RenderTargetStateKind bloom_expected_state_) {
    const RenderTargetResolvedView scene_view = render_target_host->ResolveView(scene_target_);
    const RenderTargetResolvedView bloom_view = render_target_host->ResolveView(bloom_target_);

    const RenderTargetDescriptorKey scene_descriptor_key{
        .target = scene_target_,
        .view = {},
        .expected_state = scene_expected_state_,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptor_usage = RenderTargetDescriptorUsage::combined_image_sampler,
    };
    const RenderTargetDescriptorKey bloom_descriptor_key{
        .target = bloom_target_,
        .view = {},
        .expected_state = bloom_expected_state_,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptor_usage = RenderTargetDescriptorUsage::combined_image_sampler,
    };

    if (cache_entry_.valid &&
        cache_entry_.descriptor_set != VK_NULL_HANDLE &&
        DualSourceDescriptorKeysEqual(cache_entry_,
                                      scene_descriptor_key,
                                      bloom_descriptor_key,
                                      scene_view.resource_revision,
                                      bloom_view.resource_revision)) {
        stats.reused_descriptor_set = true;
        return cache_entry_.descriptor_set;
    }

    if (!linear_clamp_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_LINEAR;
        sampler_desc.min_filter = VK_FILTER_LINEAR;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.min_lod = 0.0F;
        sampler_desc.max_lod = 0.0F;
        linear_clamp_sampler_id = sampler_host->RegisterSampler(*context, sampler_desc);
    }

    const VkDescriptorSet descriptor_set = descriptor_host->AllocateSet(*context,
                                                                        frame_index_,
                                                                        layout_id_);
    if (descriptor_set == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    const VkSampler sampler = sampler_host->GetSampler(linear_clamp_sampler_id);
    DescriptorMcVector<DescriptorImageWrite> image_writes{};
    image_writes.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = sampler,
        .image_view = scene_view.image_view,
        .image_layout = RenderTargetHost::DescribeState(scene_expected_state_, scene_view.aspect).layout,
    });
    image_writes.push_back({
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = sampler,
        .image_view = bloom_view.image_view,
        .image_layout = RenderTargetHost::DescribeState(bloom_expected_state_, bloom_view.aspect).layout,
    });

    descriptor_host->UpdateSet(*context, descriptor_set, {}, image_writes, {});
    ++stats.descriptor_set_update_count;

    cache_entry_.descriptor_set = descriptor_set;
    cache_entry_.scene_descriptor_key = scene_descriptor_key;
    cache_entry_.bloom_descriptor_key = bloom_descriptor_key;
    cache_entry_.scene_resource_revision = scene_view.resource_revision;
    cache_entry_.bloom_resource_revision = bloom_view.resource_revision;
    cache_entry_.valid = true;
    return descriptor_set;
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

bool RenderTargetBloomRenderer::SingleSourceDescriptorKeyEquals(
    const RenderTargetDescriptorKey& lhs_,
    const RenderTargetDescriptorKey& rhs_) noexcept {
    return DescriptorKeyEquals(lhs_, rhs_);
}

bool RenderTargetBloomRenderer::DualSourceDescriptorKeysEqual(
    const DualSourceDescriptorCacheEntry& cache_entry_,
    const RenderTargetDescriptorKey& scene_descriptor_key_,
    const RenderTargetDescriptorKey& bloom_descriptor_key_,
    std::uint32_t scene_resource_revision_,
    std::uint32_t bloom_resource_revision_) noexcept {
    return cache_entry_.scene_resource_revision == scene_resource_revision_ &&
           cache_entry_.bloom_resource_revision == bloom_resource_revision_ &&
           DescriptorKeyEquals(cache_entry_.scene_descriptor_key, scene_descriptor_key_) &&
           DescriptorKeyEquals(cache_entry_.bloom_descriptor_key, bloom_descriptor_key_);
}

} // namespace vr::render
