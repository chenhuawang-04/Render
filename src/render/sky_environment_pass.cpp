#include "vr/render/environment/sky_environment_pass.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/generated/render_target_composite_vert_spv.hpp"
#include "vr/render/generated/render_target_composite_far_vert_spv.hpp"
#include "vr/render/generated/sky_environment_atmosphere_frag_spv.hpp"
#include "vr/render/generated/sky_environment_frag_spv.hpp"
#include "vr/render/generated/sky_environment_equirect_frag_spv.hpp"
#include "vr/render/generated/sky_environment_image_frag_spv.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/resource/sampler_host.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] bool SupportsGradientMode(scene::SkyEnvironmentMode mode_) noexcept {
    return mode_ == scene::SkyEnvironmentMode::solid_color ||
           mode_ == scene::SkyEnvironmentMode::gradient;
}

[[nodiscard]] bool SupportsCubemapEnvironmentMode(scene::SkyEnvironmentMode mode_) noexcept {
    return mode_ == scene::SkyEnvironmentMode::cubemap;
}

[[nodiscard]] bool SupportsEquirectEnvironmentMode(scene::SkyEnvironmentMode mode_) noexcept {
    return mode_ == scene::SkyEnvironmentMode::equirectangular_hdr;
}

[[nodiscard]] bool SupportsProceduralAtmosphereMode(scene::SkyEnvironmentMode mode_) noexcept {
    return mode_ == scene::SkyEnvironmentMode::procedural_atmosphere;
}

[[nodiscard]] ecs::Float3 NormalizeOrFallback(const ecs::Float3& value_,
                                              const ecs::Float3& fallback_) noexcept {
    const float length_sq = value_.x * value_.x + value_.y * value_.y + value_.z * value_.z;
    if (!(length_sq > 1.0e-12F)) {
        return fallback_;
    }
    const float inv_length = 1.0F / std::sqrt(length_sq);
    return ecs::Float3{
        .x = value_.x * inv_length,
        .y = value_.y * inv_length,
        .z = value_.z * inv_length,
    };
}

[[nodiscard]] ecs::Float3 BuildWorldAxis(const ecs::Matrix4x4& matrix_,
                                         std::uint32_t column_index_,
                                         float sign_) noexcept {
    const std::uint32_t base = column_index_ * 4U;
    return ecs::Float3{
        .x = matrix_.m[base + 0U] * sign_,
        .y = matrix_.m[base + 1U] * sign_,
        .z = matrix_.m[base + 2U] * sign_,
    };
}

} // namespace

void SkyEnvironmentPass::Initialize(const SkyEnvironmentPassCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    context = nullptr;
    texture_host = nullptr;
    bindless_resources = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    ibl_host = nullptr;
    sampler_host = nullptr;
    active_environment_texture_slot = 0U;
    active_environment_sampler_slot = 0U;
    active_equirect_texture_slot = 0U;
    active_equirect_sampler_slot = 0U;
    has_active_environment_texture = false;
    has_active_equirect_texture = false;
    gradient_pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_vertex_far_id = {};
    gradient_shader_fragment_id = {};
    gradient_pipeline_id = {};
    gradient_pipeline_color_format = VK_FORMAT_UNDEFINED;
    gradient_depth_tested_pipeline_id = {};
    gradient_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    gradient_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    environment_image_pipeline_layout_id = {};
    environment_image_shader_fragment_id = {};
    environment_image_pipeline_id = {};
    environment_image_pipeline_color_format = VK_FORMAT_UNDEFINED;
    environment_image_depth_tested_pipeline_id = {};
    environment_image_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    environment_image_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    equirect_pipeline_layout_id = {};
    equirect_shader_fragment_id = {};
    equirect_pipeline_id = {};
    equirect_pipeline_color_format = VK_FORMAT_UNDEFINED;
    equirect_depth_tested_pipeline_id = {};
    equirect_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    equirect_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    atmosphere_pipeline_layout_id = {};
    atmosphere_shader_fragment_id = {};
    atmosphere_pipeline_id = {};
    atmosphere_pipeline_color_format = VK_FORMAT_UNDEFINED;
    atmosphere_depth_tested_pipeline_id = {};
    atmosphere_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    atmosphere_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    output_target_config = {};
    depth_output_target_config = {};
    has_depth_output_target_config = false;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void SkyEnvironmentPass::Shutdown(VulkanContext& context_) {
    (void)context_;
    if (!initialized) {
        return;
    }
    stats = {};
    context = nullptr;
    texture_host = nullptr;
    bindless_resources = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    ibl_host = nullptr;
    sampler_host = nullptr;
    active_environment_texture_slot = 0U;
    active_environment_sampler_slot = 0U;
    active_equirect_texture_slot = 0U;
    active_equirect_sampler_slot = 0U;
    has_active_environment_texture = false;
    has_active_equirect_texture = false;
    gradient_pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_vertex_far_id = {};
    gradient_shader_fragment_id = {};
    gradient_pipeline_id = {};
    gradient_pipeline_color_format = VK_FORMAT_UNDEFINED;
    gradient_depth_tested_pipeline_id = {};
    gradient_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    gradient_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    environment_image_pipeline_layout_id = {};
    environment_image_shader_fragment_id = {};
    environment_image_pipeline_id = {};
    environment_image_pipeline_color_format = VK_FORMAT_UNDEFINED;
    environment_image_depth_tested_pipeline_id = {};
    environment_image_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    environment_image_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    equirect_pipeline_layout_id = {};
    equirect_shader_fragment_id = {};
    equirect_pipeline_id = {};
    equirect_pipeline_color_format = VK_FORMAT_UNDEFINED;
    equirect_depth_tested_pipeline_id = {};
    equirect_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    equirect_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    atmosphere_pipeline_layout_id = {};
    atmosphere_shader_fragment_id = {};
    atmosphere_pipeline_id = {};
    atmosphere_pipeline_color_format = VK_FORMAT_UNDEFINED;
    atmosphere_depth_tested_pipeline_id = {};
    atmosphere_depth_tested_pipeline_color_format = VK_FORMAT_UNDEFINED;
    atmosphere_depth_tested_pipeline_depth_format = VK_FORMAT_UNDEFINED;
    output_target_config = {};
    depth_output_target_config = {};
    has_depth_output_target_config = false;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    initialized = false;
}

void SkyEnvironmentPass::SetOutputTargetConfig(
    const RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void SkyEnvironmentPass::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void SkyEnvironmentPass::SetDepthTargetConfig(
    const RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept {
    depth_output_target_config = depth_output_target_config_;
    has_depth_output_target_config = true;
}

void SkyEnvironmentPass::ResetDepthTargetConfig() noexcept {
    depth_output_target_config = {};
    has_depth_output_target_config = false;
}

void SkyEnvironmentPass::PrepareFrame(const SkyEnvironmentPassPrepareView& prepare_view_,
                                      const scene::SkyEnvironmentRenderState& state_,
                                      scene::SkyEnvironmentGpuHandle) {
    if (!initialized) {
        throw std::runtime_error("SkyEnvironmentPass::PrepareFrame called before Initialize");
    }

    context = &prepare_view_.device;
    texture_host = prepare_view_.texture;
    bindless_resources = prepare_view_.bindless;
    descriptor_host = prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    ibl_host = prepare_view_.ibl;
    sampler_host = prepare_view_.sampler;
    active_environment_texture_slot = 0U;
    active_environment_sampler_slot = 0U;
    active_equirect_texture_slot = 0U;
    active_equirect_sampler_slot = 0U;
    has_active_environment_texture = false;
    has_active_equirect_texture = false;

    if (SupportsCubemapEnvironmentMode(state_.mode) && ibl_host != nullptr) {
        if (bindless_resources == nullptr || !bindless_resources->IsInitialized() || texture_host == nullptr) {
            throw std::runtime_error(
                "SkyEnvironmentPass::PrepareFrame cubemap mode requires initialized BindlessResourceSystem and TextureHost");
        }
        const IblEnvironmentId environment_id{prepare_view_.ibl_environment_id};
        const asset::TextureId brdf_lut_texture_id{prepare_view_.ibl_brdf_lut_texture_id};
        if (environment_id.IsValid() || brdf_lut_texture_id.IsValid()) {
            ibl_host->PrepareEnvironmentFrame(MakeIblHostPrepareView(prepare_view_),
                                              environment_id,
                                              brdf_lut_texture_id);
        } else {
            ibl_host->PrepareFrame(MakeIblHostPrepareView(prepare_view_));
        }

        const asset::TextureId skybox_texture_id = ibl_host->ActiveSkyboxTexture();
        const asset::TextureHost::TextureRecord* skybox_record = texture_host->FindTexture(skybox_texture_id);
        if (skybox_record != nullptr &&
            skybox_record->resource.default_view != VK_NULL_HANDLE &&
            (skybox_record->default_view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
             skybox_record->default_view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)) {
            active_environment_texture_slot =
                bindless_resources->ResolveTextureImageSlot(*texture_host, skybox_texture_id).index;
            active_environment_sampler_slot =
                bindless_resources->ResolveTextureSamplerSlot(*texture_host, skybox_texture_id).index;
            has_active_environment_texture = true;
        }
    }

    if (SupportsEquirectEnvironmentMode(state_.mode)) {
        if (bindless_resources == nullptr || !bindless_resources->IsInitialized() || texture_host == nullptr) {
            throw std::runtime_error(
                "SkyEnvironmentPass::PrepareFrame equirect mode requires initialized BindlessResourceSystem and TextureHost");
        }
        const asset::TextureId texture_id{state_.sky_texture_id};
        const asset::TextureHost::TextureRecord* texture_record =
            texture_host->FindTexture(texture_id);
        if (texture_record != nullptr &&
            texture_record->resource.default_view != VK_NULL_HANDLE &&
            texture_record->default_view_type == VK_IMAGE_VIEW_TYPE_2D) {
            active_equirect_texture_slot =
                bindless_resources->ResolveTextureImageSlot(*texture_host, texture_id).index;
            active_equirect_sampler_slot =
                bindless_resources->ResolveTextureSamplerSlot(*texture_host, texture_id).index;
            has_active_equirect_texture = true;
        }
    }

    ++stats.prepare_count;
}

void SkyEnvironmentPass::Record(const FrameRecordContext& record_context_,
                                const RenderView3D& view_,
                                const scene::SkyEnvironmentRenderState& state_,
                                scene::SkyEnvironmentGpuHandle) {
    if (!initialized) {
        throw std::runtime_error("SkyEnvironmentPass::Record called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("SkyEnvironmentPass::Record called before PrepareFrame");
    }

    const bool depth_tested = has_depth_output_target_config;

    auto begin_rendering = [&](auto&& record_fn_) {
        if (depth_tested) {
            const ResolvedColorRenderPass color_pass =
                BuildColorDepthRenderPass(record_context_,
                                          output_target_config,
                                          depth_output_target_config,
                                          false,
                                          create_info_cache.fallback_clear_color,
                                          true,
                                          true);
            record_fn_(color_pass);
            RecordEndColorDepthPass(record_context_,
                                    output_target_config,
                                    depth_output_target_config);
            return;
        }

        const ResolvedColorRenderPass color_pass =
            BuildColorRenderPass(record_context_,
                                 output_target_config,
                                 true,
                                 create_info_cache.fallback_clear_color,
                                 false);
        record_fn_(color_pass);
        RecordEndColorPass(record_context_, output_target_config);
    };

    if (SupportsGradientMode(state_.mode)) {
        begin_rendering([&](const ResolvedColorRenderPass& color_pass) {
            EnsureGradientPipelineObjects(*context,
                                          *pipeline_host,
                                          color_pass.target.format,
                                          depth_tested,
                                          color_pass.depth_target.format);

            vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

            const VkViewport viewport = ToVkViewport(view_.viewport);
            const VkRect2D scissor = ToVkRect2D(view_.scissor);
            vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);
            vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

            const GraphicsPipelineId pipeline_id = depth_tested
                ? gradient_depth_tested_pipeline_id
                : gradient_pipeline_id;
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));

            const GradientPushBlock push_block{
                .color0 = state_.zenith_color,
                .color1 = state_.horizon_color,
                .exposure = state_.exposure,
                .mode = static_cast<std::uint32_t>(state_.mode),
                .reserved0 = 0.0F,
                .reserved1 = 0.0F,
            };
            vkCmdPushConstants(record_context_.command_buffer,
                               pipeline_host->GetPipelineLayout(gradient_pipeline_layout_id),
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(GradientPushBlock),
                               &push_block);

            vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
            ++stats.draw_call_count;

            vkCmdEndRendering(record_context_.command_buffer);
        });
        return;
    }

    if (SupportsProceduralAtmosphereMode(state_.mode) &&
        view_.camera != nullptr &&
        view_.camera_transform != nullptr) {
        begin_rendering([&](const ResolvedColorRenderPass& color_pass) {
            EnsureAtmospherePipelineObjects(*context,
                                            *pipeline_host,
                                            color_pass.target.format,
                                            depth_tested,
                                            color_pass.depth_target.format);

            vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

            const VkViewport viewport = ToVkViewport(view_.viewport);
            const VkRect2D scissor = ToVkRect2D(view_.scissor);
            vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);
            vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

            const GraphicsPipelineId pipeline_id = depth_tested
                ? atmosphere_depth_tested_pipeline_id
                : atmosphere_pipeline_id;
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));

            const AtmospherePushBlock push_block = BuildAtmospherePushBlock(view_, state_);
            vkCmdPushConstants(record_context_.command_buffer,
                               pipeline_host->GetPipelineLayout(atmosphere_pipeline_layout_id),
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(AtmospherePushBlock),
                               &push_block);

            vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
            ++stats.draw_call_count;

            vkCmdEndRendering(record_context_.command_buffer);
        });
        return;
    }

    if (SupportsEquirectEnvironmentMode(state_.mode) &&
        has_active_equirect_texture &&
        bindless_resources != nullptr &&
        view_.camera != nullptr &&
        view_.camera_transform != nullptr) {
        begin_rendering([&](const ResolvedColorRenderPass& color_pass) {
            EnsureEquirectPipelineObjects(*context,
                                          *descriptor_host,
                                          *pipeline_host,
                                          color_pass.target.format,
                                          depth_tested,
                                          color_pass.depth_target.format);

            vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

            const VkViewport viewport = ToVkViewport(view_.viewport);
            const VkRect2D scissor = ToVkRect2D(view_.scissor);
            vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);
            vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

            const GraphicsPipelineId pipeline_id = depth_tested
                ? equirect_depth_tested_pipeline_id
                : equirect_pipeline_id;
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));
            const std::array<VkDescriptorSet, 2U> descriptor_sets{
                bindless_resources->SampledImageSet(),
                bindless_resources->SamplerSet()
            };
            vkCmdBindDescriptorSets(record_context_.command_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_host->GetPipelineLayout(equirect_pipeline_layout_id),
                                    0U,
                                    static_cast<std::uint32_t>(descriptor_sets.size()),
                                    descriptor_sets.data(),
                                    0U,
                                    nullptr);
            ++stats.descriptor_set_bind_count;

            const EquirectPushBlock push_block =
                BuildEquirectPushBlock(view_, state_, active_equirect_texture_slot, active_equirect_sampler_slot);
            vkCmdPushConstants(record_context_.command_buffer,
                               pipeline_host->GetPipelineLayout(equirect_pipeline_layout_id),
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(EquirectPushBlock),
                               &push_block);

            vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
            ++stats.draw_call_count;

            vkCmdEndRendering(record_context_.command_buffer);
        });
        return;
    }

    if (SupportsCubemapEnvironmentMode(state_.mode) &&
        has_active_environment_texture &&
        bindless_resources != nullptr &&
        ibl_host != nullptr &&
        view_.camera != nullptr &&
        view_.camera_transform != nullptr) {
        begin_rendering([&](const ResolvedColorRenderPass& color_pass) {
            EnsureImageEnvironmentPipelineObjects(*context,
                                                  *descriptor_host,
                                                  *pipeline_host,
                                                  color_pass.target.format,
                                                  depth_tested,
                                                  color_pass.depth_target.format);

            vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

            const VkViewport viewport = ToVkViewport(view_.viewport);
            const VkRect2D scissor = ToVkRect2D(view_.scissor);
            vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);
            vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

            const GraphicsPipelineId pipeline_id = depth_tested
                ? environment_image_depth_tested_pipeline_id
                : environment_image_pipeline_id;
            vkCmdBindPipeline(record_context_.command_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_host->GetGraphicsPipeline(pipeline_id));
            const std::array<VkDescriptorSet, 2U> descriptor_sets{
                bindless_resources->SampledImageSet(),
                bindless_resources->SamplerSet()
            };
            vkCmdBindDescriptorSets(record_context_.command_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_host->GetPipelineLayout(environment_image_pipeline_layout_id),
                                    0U,
                                    static_cast<std::uint32_t>(descriptor_sets.size()),
                                    descriptor_sets.data(),
                                    0U,
                                    nullptr);
            ++stats.descriptor_set_bind_count;

            const EnvironmentImagePushBlock push_block = BuildEnvironmentImagePushBlock(
                view_,
                ibl_host->ActiveParams(),
                active_environment_texture_slot,
                active_environment_sampler_slot);
            vkCmdPushConstants(record_context_.command_buffer,
                               pipeline_host->GetPipelineLayout(environment_image_pipeline_layout_id),
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U,
                               sizeof(EnvironmentImagePushBlock),
                               &push_block);

            vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
            ++stats.draw_call_count;

            vkCmdEndRendering(record_context_.command_buffer);
        });
        return;
    }

    ++stats.skipped_draw_count;
}

void SkyEnvironmentPass::OnSwapchainRecreated(std::uint32_t,
                                              VkExtent2D extent_,
                                              VkFormat format_,
                                              std::uint64_t,
                                              std::uint64_t) {
    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool SkyEnvironmentPass::IsInitialized() const noexcept {
    return initialized;
}

const SkyEnvironmentPassStats& SkyEnvironmentPass::Stats() const noexcept {
    return stats;
}

void SkyEnvironmentPass::EnsureGradientPipelineObjects(VulkanContext& context_,
                                                       PipelineHost& pipeline_host_,
                                                       VkFormat color_format_,
                                                       bool depth_tested_,
                                                       VkFormat depth_format_) {
    if (!shader_vertex_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (depth_tested_ && !shader_vertex_far_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_far_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_far_vert_spv_word_count;
        shader_vertex_far_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!gradient_shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_sky_environment_frag_spv;
        shader_create_info.word_count = generated::k_sky_environment_frag_spv_word_count;
        gradient_shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!gradient_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(GradientPushBlock),
        });
        gradient_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (depth_tested_) {
        if (gradient_depth_tested_pipeline_id.IsValid() &&
            gradient_depth_tested_pipeline_color_format == color_format_ &&
            gradient_depth_tested_pipeline_depth_format == depth_format_) {
            return;
        }
    } else if (gradient_pipeline_id.IsValid() && gradient_pipeline_color_format == color_format_) {
        return;
    }

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(gradient_pipeline_layout_id);
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(depth_tested_ ? shader_vertex_far_id : shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(gradient_shader_fragment_id),
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
    pipeline_desc.depth_stencil.depth_test_enable = depth_tested_;
    pipeline_desc.depth_stencil.depth_write_enable = false;
    if (depth_tested_) {
        pipeline_desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_EQUAL;
        pipeline_desc.rendering.depth_attachment_format = depth_format_;
    }

    VkPipelineColorBlendAttachmentState blend_state{};
    blend_state.blendEnable = VK_FALSE;
    blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;
    pipeline_desc.color_blend.attachments.push_back(blend_state);
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);

    if (depth_tested_) {
        gradient_depth_tested_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        gradient_depth_tested_pipeline_color_format = color_format_;
        gradient_depth_tested_pipeline_depth_format = depth_format_;
    } else {
        gradient_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        gradient_pipeline_color_format = color_format_;
    }
}

void SkyEnvironmentPass::EnsureImageEnvironmentPipelineObjects(VulkanContext& context_,
                                                               DescriptorHost& descriptor_host_,
                                                               PipelineHost& pipeline_host_,
                                                               VkFormat color_format_,
                                                               bool depth_tested_,
                                                               VkFormat depth_format_) {
    (void)descriptor_host_;
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "SkyEnvironmentPass::EnsureImageEnvironmentPipelineObjects requires BindlessResourceSystem");
    }

    if (!shader_vertex_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (depth_tested_ && !shader_vertex_far_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_far_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_far_vert_spv_word_count;
        shader_vertex_far_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!environment_image_shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_sky_environment_image_frag_spv;
        shader_create_info.word_count = generated::k_sky_environment_image_frag_spv_word_count;
        environment_image_shader_fragment_id =
            pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!environment_image_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        const VkDescriptorSetLayout sampled_image_layout = bindless_resources->SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout = bindless_resources->SamplerLayout();
        if (sampled_image_layout == VK_NULL_HANDLE || sampler_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "SkyEnvironmentPass::EnsureImageEnvironmentPipelineObjects requires valid bindless layouts");
        }
        layout_desc.set_layouts.push_back(sampled_image_layout);
        layout_desc.set_layouts.push_back(sampler_layout);
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(EnvironmentImagePushBlock),
        });
        environment_image_pipeline_layout_id =
            pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (depth_tested_) {
        if (environment_image_depth_tested_pipeline_id.IsValid() &&
            environment_image_depth_tested_pipeline_color_format == color_format_ &&
            environment_image_depth_tested_pipeline_depth_format == depth_format_) {
            return;
        }
    } else if (environment_image_pipeline_id.IsValid() &&
               environment_image_pipeline_color_format == color_format_) {
        return;
    }

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(environment_image_pipeline_layout_id);
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(depth_tested_ ? shader_vertex_far_id : shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(environment_image_shader_fragment_id),
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
    pipeline_desc.depth_stencil.depth_test_enable = depth_tested_;
    pipeline_desc.depth_stencil.depth_write_enable = false;
    if (depth_tested_) {
        pipeline_desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_EQUAL;
        pipeline_desc.rendering.depth_attachment_format = depth_format_;
    }

    VkPipelineColorBlendAttachmentState blend_state{};
    blend_state.blendEnable = VK_FALSE;
    blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;
    pipeline_desc.color_blend.attachments.push_back(blend_state);
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);

    if (depth_tested_) {
        environment_image_depth_tested_pipeline_id =
            pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        environment_image_depth_tested_pipeline_color_format = color_format_;
        environment_image_depth_tested_pipeline_depth_format = depth_format_;
    } else {
        environment_image_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        environment_image_pipeline_color_format = color_format_;
    }
}

void SkyEnvironmentPass::EnsureEquirectPipelineObjects(VulkanContext& context_,
                                                       DescriptorHost& descriptor_host_,
                                                       PipelineHost& pipeline_host_,
                                                       VkFormat color_format_,
                                                       bool depth_tested_,
                                                       VkFormat depth_format_) {
    (void)descriptor_host_;
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "SkyEnvironmentPass::EnsureEquirectPipelineObjects requires BindlessResourceSystem");
    }

    if (!shader_vertex_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (depth_tested_ && !shader_vertex_far_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_far_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_far_vert_spv_word_count;
        shader_vertex_far_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!equirect_shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_sky_environment_equirect_frag_spv;
        shader_create_info.word_count = generated::k_sky_environment_equirect_frag_spv_word_count;
        equirect_shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!equirect_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        const VkDescriptorSetLayout sampled_image_layout = bindless_resources->SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout = bindless_resources->SamplerLayout();
        if (sampled_image_layout == VK_NULL_HANDLE || sampler_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "SkyEnvironmentPass::EnsureEquirectPipelineObjects requires valid bindless layouts");
        }
        layout_desc.set_layouts.push_back(sampled_image_layout);
        layout_desc.set_layouts.push_back(sampler_layout);
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(EquirectPushBlock),
        });
        equirect_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (depth_tested_) {
        if (equirect_depth_tested_pipeline_id.IsValid() &&
            equirect_depth_tested_pipeline_color_format == color_format_ &&
            equirect_depth_tested_pipeline_depth_format == depth_format_) {
            return;
        }
    } else if (equirect_pipeline_id.IsValid() && equirect_pipeline_color_format == color_format_) {
        return;
    }

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(equirect_pipeline_layout_id);
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(depth_tested_ ? shader_vertex_far_id : shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(equirect_shader_fragment_id),
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
    pipeline_desc.depth_stencil.depth_test_enable = depth_tested_;
    pipeline_desc.depth_stencil.depth_write_enable = false;
    if (depth_tested_) {
        pipeline_desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_EQUAL;
        pipeline_desc.rendering.depth_attachment_format = depth_format_;
    }

    VkPipelineColorBlendAttachmentState blend_state{};
    blend_state.blendEnable = VK_FALSE;
    blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;
    pipeline_desc.color_blend.attachments.push_back(blend_state);
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);

    if (depth_tested_) {
        equirect_depth_tested_pipeline_id =
            pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        equirect_depth_tested_pipeline_color_format = color_format_;
        equirect_depth_tested_pipeline_depth_format = depth_format_;
    } else {
        equirect_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        equirect_pipeline_color_format = color_format_;
    }
}

void SkyEnvironmentPass::EnsureAtmospherePipelineObjects(VulkanContext& context_,
                                                         PipelineHost& pipeline_host_,
                                                         VkFormat color_format_,
                                                         bool depth_tested_,
                                                         VkFormat depth_format_) {
    if (!shader_vertex_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (depth_tested_ && !shader_vertex_far_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_far_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_far_vert_spv_word_count;
        shader_vertex_far_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!atmosphere_shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_sky_environment_atmosphere_frag_spv;
        shader_create_info.word_count = generated::k_sky_environment_atmosphere_frag_spv_word_count;
        atmosphere_shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!atmosphere_pipeline_layout_id.IsValid()) {
        PipelineLayoutDesc layout_desc{};
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(AtmospherePushBlock),
        });
        atmosphere_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (depth_tested_) {
        if (atmosphere_depth_tested_pipeline_id.IsValid() &&
            atmosphere_depth_tested_pipeline_color_format == color_format_ &&
            atmosphere_depth_tested_pipeline_depth_format == depth_format_) {
            return;
        }
    } else if (atmosphere_pipeline_id.IsValid() &&
               atmosphere_pipeline_color_format == color_format_) {
        return;
    }

    GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(atmosphere_pipeline_layout_id);
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(depth_tested_ ? shader_vertex_far_id : shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(atmosphere_shader_fragment_id),
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
    pipeline_desc.depth_stencil.depth_test_enable = depth_tested_;
    pipeline_desc.depth_stencil.depth_write_enable = false;
    if (depth_tested_) {
        pipeline_desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_EQUAL;
        pipeline_desc.rendering.depth_attachment_format = depth_format_;
    }

    VkPipelineColorBlendAttachmentState blend_state{};
    blend_state.blendEnable = VK_FALSE;
    blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;
    pipeline_desc.color_blend.attachments.push_back(blend_state);
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);

    if (depth_tested_) {
        atmosphere_depth_tested_pipeline_id =
            pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        atmosphere_depth_tested_pipeline_color_format = color_format_;
        atmosphere_depth_tested_pipeline_depth_format = depth_format_;
    } else {
        atmosphere_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
        atmosphere_pipeline_color_format = color_format_;
    }
}

SkyEnvironmentPass::EnvironmentImagePushBlock SkyEnvironmentPass::BuildEnvironmentImagePushBlock(
    const RenderView3D& view_,
    const IblGpuParams& ibl_params_,
    std::uint32_t texture_slot_,
    std::uint32_t sampler_slot_) noexcept {
    EnvironmentImagePushBlock push_constants{};
    if (view_.camera == nullptr || view_.camera_transform == nullptr) {
        push_constants.camera_right_scale_x = ecs::Float4{.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        push_constants.camera_up_scale_y = ecs::Float4{.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
        push_constants.camera_forward_reserved = ecs::Float4{.x = 0.0F, .y = 0.0F, .z = -1.0F, .w = 0.0F};
        push_constants.tint_intensity = ecs::Float4{
            .x = ibl_params_.tint_intensity[0U],
            .y = ibl_params_.tint_intensity[1U],
            .z = ibl_params_.tint_intensity[2U],
            .w = ibl_params_.tint_intensity[3U],
        };
        push_constants.rotation_sin_cos_reserved = ecs::Float4{
            .x = ibl_params_.rotation_max_lod_flags[0U],
            .y = ibl_params_.rotation_max_lod_flags[1U],
            .z = 0.0F,
            .w = 0.0F,
        };
        push_constants.texture_slot = texture_slot_;
        push_constants.sampler_slot = sampler_slot_;
        return push_constants;
    }

    const ecs::Matrix4x4& world_matrix = view_.camera_transform->runtime.world_matrix;
    const ecs::Float3 camera_right =
        NormalizeOrFallback(BuildWorldAxis(world_matrix, 0U, 1.0F),
                            ecs::Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
    const ecs::Float3 camera_up =
        NormalizeOrFallback(BuildWorldAxis(world_matrix, 1U, 1.0F),
                            ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
    const ecs::Float3 camera_forward =
        NormalizeOrFallback(BuildWorldAxis(world_matrix, 2U, -1.0F),
                            ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F});

    const float aspect = std::max(view_.camera->style.aspect_ratio, 1.0e-6F);
    float scale_y = 1.0F;
    if (view_.camera->style.projection_mode == ecs::CameraProjectionMode::perspective) {
        const float vertical_fov =
            std::clamp(view_.camera->style.vertical_fov_radians, 1.0e-4F, 3.13F);
        scale_y = std::tan(0.5F * vertical_fov);
    }
    const float scale_x = scale_y * aspect;

    push_constants.camera_right_scale_x = ecs::Float4{
        .x = camera_right.x,
        .y = camera_right.y,
        .z = camera_right.z,
        .w = scale_x,
    };
    push_constants.camera_up_scale_y = ecs::Float4{
        .x = camera_up.x,
        .y = camera_up.y,
        .z = camera_up.z,
        .w = scale_y,
    };
    push_constants.camera_forward_reserved = ecs::Float4{
        .x = camera_forward.x,
        .y = camera_forward.y,
        .z = camera_forward.z,
        .w = 0.0F,
    };
    push_constants.tint_intensity = ecs::Float4{
        .x = ibl_params_.tint_intensity[0U],
        .y = ibl_params_.tint_intensity[1U],
        .z = ibl_params_.tint_intensity[2U],
        .w = ibl_params_.tint_intensity[3U],
    };
    push_constants.rotation_sin_cos_reserved = ecs::Float4{
        .x = ibl_params_.rotation_max_lod_flags[0U],
        .y = ibl_params_.rotation_max_lod_flags[1U],
        .z = 0.0F,
        .w = 0.0F,
    };
    push_constants.texture_slot = texture_slot_;
    push_constants.sampler_slot = sampler_slot_;
    return push_constants;
}

SkyEnvironmentPass::EquirectPushBlock SkyEnvironmentPass::BuildEquirectPushBlock(
    const RenderView3D& view_,
    const scene::SkyEnvironmentRenderState& state_,
    std::uint32_t texture_slot_,
    std::uint32_t sampler_slot_) noexcept {
    const EnvironmentImagePushBlock camera_push_block =
        BuildEnvironmentImagePushBlock(view_, IblGpuParams{}, 0U, 0U);

    EquirectPushBlock push_block{};
    push_block.camera_right_scale_x = camera_push_block.camera_right_scale_x;
    push_block.camera_up_scale_y = camera_push_block.camera_up_scale_y;
    push_block.camera_forward_reserved = camera_push_block.camera_forward_reserved;
    push_block.tint_exposure = ecs::Float4{
        .x = state_.tint.x,
        .y = state_.tint.y,
        .z = state_.tint.z,
        .w = state_.exposure,
    };
    push_block.rotation_sin_cos_intensity = ecs::Float4{
        .x = std::sin(state_.rotation_y),
        .y = std::cos(state_.rotation_y),
        .z = state_.sky_intensity,
        .w = 0.0F,
    };
    push_block.texture_slot = texture_slot_;
    push_block.sampler_slot = sampler_slot_;
    return push_block;
}

SkyEnvironmentPass::AtmospherePushBlock SkyEnvironmentPass::BuildAtmospherePushBlock(
    const RenderView3D& view_,
    const scene::SkyEnvironmentRenderState& state_) noexcept {
    const EnvironmentImagePushBlock camera_push_block =
        BuildEnvironmentImagePushBlock(view_, IblGpuParams{}, 0U, 0U);

    const float clamped_elevation = std::clamp(state_.sun_elevation, -1.5706963F, 1.5706963F);
    const float clamped_azimuth = state_.sun_azimuth;
    const float cos_elevation = std::cos(clamped_elevation);
    const ecs::Float3 sun_direction = NormalizeOrFallback(
        ecs::Float3{
            .x = std::sin(clamped_azimuth) * cos_elevation,
            .y = std::sin(clamped_elevation),
            .z = -std::cos(clamped_azimuth) * cos_elevation,
        },
        ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});

    AtmospherePushBlock push_block{};
    push_block.camera_right_scale_x = camera_push_block.camera_right_scale_x;
    push_block.camera_up_scale_y = camera_push_block.camera_up_scale_y;
    push_block.camera_forward_reserved = camera_push_block.camera_forward_reserved;
    push_block.zenith_exposure = ecs::Float4{
        .x = state_.zenith_color.x,
        .y = state_.zenith_color.y,
        .z = state_.zenith_color.z,
        .w = state_.exposure,
    };
    push_block.horizon_intensity = ecs::Float4{
        .x = state_.horizon_color.x,
        .y = state_.horizon_color.y,
        .z = state_.horizon_color.z,
        .w = state_.sky_intensity,
    };
    push_block.ground_density = ecs::Float4{
        .x = state_.ground_color.x,
        .y = state_.ground_color.y,
        .z = state_.ground_color.z,
        .w = state_.atmosphere_density,
    };
    push_block.tint_mie = ecs::Float4{
        .x = state_.tint.x,
        .y = state_.tint.y,
        .z = state_.tint.z,
        .w = state_.mie_scattering,
    };
    push_block.sun_direction_rayleigh = ecs::Float4{
        .x = sun_direction.x,
        .y = sun_direction.y,
        .z = sun_direction.z,
        .w = state_.rayleigh_scattering,
    };
    return push_block;
}

} // namespace vr::render
