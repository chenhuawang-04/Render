#include "vr/render/skybox_renderer.hpp"

#include "vr/render/generated/render_target_composite_vert_spv.hpp"
#include "vr/render/generated/skybox_frag_spv.hpp"
#include "vr/render/ibl_host.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vr::render {

namespace {

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

void SkyboxRenderer::Initialize(const SkyboxRendererCreateInfo& create_info_) noexcept {
    create_info_cache = create_info_;
    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    ibl_host = nullptr;
    camera_component = nullptr;
    camera_transform = nullptr;
    descriptor_layout_id = {};
    pipeline_layout_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    output_target_config = {};
    depth_output_target_config = {};
    initialized = true;
}

void SkyboxRenderer::Shutdown(VulkanContext& context_) noexcept {
    (void)context_;
    if (!initialized) {
        return;
    }

    stats = {};
    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    ibl_host = nullptr;
    camera_component = nullptr;
    camera_transform = nullptr;
    descriptor_layout_id = {};
    pipeline_layout_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    pipeline_id = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    output_target_config = {};
    depth_output_target_config = {};
    initialized = false;
}

void SkyboxRenderer::SetCameraData(ecs::Camera<ecs::Dim3>* camera_component_,
                                   ecs::Transform<ecs::Dim3>* camera_transform_) noexcept {
    camera_component = camera_component_;
    camera_transform = camera_transform_;
}

void SkyboxRenderer::SetOutputTargetConfig(
    const RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void SkyboxRenderer::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void SkyboxRenderer::SetDepthTargetConfig(
    const RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept {
    depth_output_target_config = depth_output_target_config_;
}

void SkyboxRenderer::ResetDepthTargetConfig() noexcept {
    depth_output_target_config = {};
}

void SkyboxRenderer::PrepareFrame(const SkyboxRendererPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("SkyboxRenderer::PrepareFrame called before Initialize");
    }
    context = &prepare_view_.device;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    ibl_host = &prepare_view_.ibl;
    stats = {};

    ibl_host->PrepareFrame(MakeIblHostPrepareView(prepare_view_));
}

void SkyboxRenderer::Record(const FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("SkyboxRenderer::Record called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr || ibl_host == nullptr) {
        throw std::runtime_error("SkyboxRenderer::Record called before PrepareFrame");
    }
    if (camera_component == nullptr || camera_transform == nullptr) {
        ++stats.skipped_draw_count;
        return;
    }

    const VkDescriptorSet descriptor_set = ibl_host->ActiveDescriptorSet(record_context_.frame_index);
    if (descriptor_set == VK_NULL_HANDLE) {
        ++stats.skipped_draw_count;
        return;
    }

    const bool has_depth_target = IsValidRenderTargetHandle(depth_output_target_config.depth_target);
    ResolvedColorRenderPass color_pass{};
    if (has_depth_target) {
        color_pass = BuildColorDepthRenderPass(record_context_,
                                               output_target_config,
                                               depth_output_target_config,
                                               create_info_cache.clear_swapchain,
                                               create_info_cache.clear_color,
                                               false,
                                               false);
    } else {
        color_pass = BuildColorRenderPass(record_context_,
                                          output_target_config,
                                          create_info_cache.clear_swapchain,
                                          create_info_cache.clear_color,
                                          false);
    }

    const VkFormat depth_format = has_depth_target ? color_pass.depth_target.format : VK_FORMAT_UNDEFINED;
    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          color_pass.target.format,
                          depth_format);

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

    const PushConstants push_constants = BuildPushConstants();
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_host->GetPipelineLayout(pipeline_layout_id),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    vkCmdDraw(record_context_.command_buffer, 3U, 1U, 0U, 0U);
    ++stats.draw_call_count;
    stats.used_depth_output = has_depth_target ? 1U : 0U;

    vkCmdEndRendering(record_context_.command_buffer);
    if (has_depth_target) {
        RecordEndColorDepthPass(record_context_, output_target_config, depth_output_target_config);
    } else {
        RecordEndColorPass(record_context_, output_target_config);
    }
}

void SkyboxRenderer::OnSwapchainRecreated(std::uint32_t image_count_,
                                          VkExtent2D extent_,
                                          VkFormat format_,
                                          std::uint64_t last_submitted_value_,
                                          std::uint64_t completed_submit_value_) noexcept {
    (void)image_count_;
    (void)extent_;
    (void)format_;
    (void)last_submitted_value_;
    (void)completed_submit_value_;
}

bool SkyboxRenderer::IsInitialized() const noexcept {
    return initialized;
}

const SkyboxRendererStats& SkyboxRenderer::Stats() const noexcept {
    return stats;
}

void SkyboxRenderer::EnsurePipelineObjects(VulkanContext& context_,
                                           DescriptorHost& descriptor_host_,
                                           PipelineHost& pipeline_host_,
                                           VkFormat color_format_,
                                           VkFormat depth_format_) {
    if (ibl_host == nullptr) {
        throw std::runtime_error("SkyboxRenderer::EnsurePipelineObjects requires IBL host");
    }

    descriptor_layout_id = ibl_host->DescriptorLayoutId();
    if (!descriptor_layout_id.IsValid()) {
        throw std::runtime_error("SkyboxRenderer::EnsurePipelineObjects requires a valid IBL descriptor layout");
    }

    if (pipeline_layout_id.IsValid() &&
        pipeline_layout_descriptor_layout_id.value != descriptor_layout_id.value) {
        pipeline_layout_id = {};
        pipeline_id = {};
        pipeline_layout_descriptor_layout_id = {};
        pipeline_color_format = VK_FORMAT_UNDEFINED;
        pipeline_depth_format = VK_FORMAT_UNDEFINED;
    }

    if (!shader_vertex_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_render_target_composite_vert_spv;
        shader_create_info.word_count = generated::k_render_target_composite_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_skybox_frag_spv;
        shader_create_info.word_count = generated::k_skybox_frag_spv_word_count;
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
        pipeline_layout_descriptor_layout_id = descriptor_layout_id;
    }

    if (pipeline_id.IsValid() &&
        pipeline_color_format == color_format_ &&
        pipeline_depth_format == depth_format_) {
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
    if (depth_format_ != VK_FORMAT_UNDEFINED) {
        pipeline_desc.rendering.depth_attachment_format = depth_format_;
    }

    pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
    pipeline_depth_format = depth_format_;
}

SkyboxRenderer::PushConstants SkyboxRenderer::BuildPushConstants() const noexcept {
    PushConstants push_constants{};
    if (camera_component == nullptr || camera_transform == nullptr) {
        push_constants.camera_right_scale_x = ecs::Float4{.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        push_constants.camera_up_scale_y = ecs::Float4{.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
        push_constants.camera_forward_reserved = ecs::Float4{.x = 0.0F, .y = 0.0F, .z = -1.0F, .w = 0.0F};
        return push_constants;
    }

    const ecs::Matrix4x4& world_matrix = camera_transform->runtime.world_matrix;
    const ecs::Float3 camera_right =
        NormalizeOrFallback(BuildWorldAxis(world_matrix, 0U, 1.0F),
                            ecs::Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
    const ecs::Float3 camera_up =
        NormalizeOrFallback(BuildWorldAxis(world_matrix, 1U, 1.0F),
                            ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
    const ecs::Float3 camera_forward =
        NormalizeOrFallback(BuildWorldAxis(world_matrix, 2U, -1.0F),
                            ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F});

    const float aspect = std::max(camera_component->style.aspect_ratio, 1.0e-6F);
    float scale_y = 1.0F;
    if (camera_component->style.projection_mode == ecs::CameraProjectionMode::perspective) {
        const float vertical_fov =
            std::clamp(camera_component->style.vertical_fov_radians, 1.0e-4F, 3.13F);
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
    return push_constants;
}

} // namespace vr::render
