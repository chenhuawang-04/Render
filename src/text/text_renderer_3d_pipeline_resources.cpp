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

void TextRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                           render::DescriptorHost& descriptor_host_,
                                           render::PipelineHost& pipeline_host_,
                                           VkFormat color_format_,
                                           VkFormat depth_format_) {
    (void)descriptor_host_;
    RequireTextRuntimeFeatures(context_, "TextRenderer3D::EnsurePipelineObjects");

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
    for (auto& pipeline_id : graphics_pipeline_ids) {
        if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
            pipeline_id = {};
        }
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_text_3d_vert_spv;
        shader_create_info.word_count = generated::k_text_3d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_text_3d_frag_spv;
        shader_create_info.word_count = generated::k_text_3d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        if (bindless_resources == nullptr ||
            bindless_resources->SampledImageLayout() == VK_NULL_HANDLE ||
            bindless_resources->SamplerLayout() == VK_NULL_HANDLE) {
            throw std::runtime_error("TextRenderer3D::EnsurePipelineObjects requires bindless resource layouts");
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

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        for (auto& pipeline_id : graphics_pipeline_ids) {
            pipeline_id = {};
        }
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
    }
}

render::GraphicsPipelineId TextRenderer3D::EnsureGraphicsPipelineForMode(VulkanContext& context_,
                                                                          render::PipelineHost& pipeline_host_,
                                                                          VkFormat color_format_,
                                                                          VkFormat depth_format_,
                                                                          DepthPipelineMode mode_) {
    const std::size_t mode_index = PipelineModeIndex(mode_);
    if (mode_index >= graphics_pipeline_ids.size()) {
        throw std::out_of_range("TextRenderer3D::EnsureGraphicsPipelineForMode mode index out of range");
    }
    if (graphics_pipeline_ids[mode_index].IsValid()) {
        return graphics_pipeline_ids[mode_index];
    }
    if (!pipeline_layout_id.IsValid() || !shader_vertex_id.IsValid() || !shader_fragment_id.IsValid()) {
        throw std::runtime_error("TextRenderer3D::EnsureGraphicsPipelineForMode requires valid pipeline objects");
    }
    if (color_format_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("TextRenderer3D::EnsureGraphicsPipelineForMode requires valid color format");
    }

    const bool depth_test_enabled =
        mode_ == DepthPipelineMode::depth_test ||
        mode_ == DepthPipelineMode::depth_test_write ||
        mode_ == DepthPipelineMode::depth_test_reverse_z ||
        mode_ == DepthPipelineMode::depth_test_write_reverse_z;
    const bool depth_write_enabled =
        mode_ == DepthPipelineMode::depth_test_write ||
        mode_ == DepthPipelineMode::depth_test_write_reverse_z;
    const bool reverse_z_enabled =
        mode_ == DepthPipelineMode::depth_test_reverse_z ||
        mode_ == DepthPipelineMode::depth_test_write_reverse_z;

    render::GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    pipeline_desc.use_dynamic_rendering = true;
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);
    pipeline_desc.rendering.depth_attachment_format = depth_format_;

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
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Text3DGpuInstance)),
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
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 32U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 48U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 64U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 5U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = 80U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 6U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = 84U
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 7U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = 88U
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
    pipeline_desc.depth_stencil.depth_test_enable = depth_test_enabled;
    pipeline_desc.depth_stencil.depth_write_enable = depth_write_enabled;
    pipeline_desc.depth_stencil.depth_compare_op = reverse_z_enabled
        ? VK_COMPARE_OP_GREATER_OR_EQUAL
        : VK_COMPARE_OP_LESS_OR_EQUAL;
    pipeline_desc.depth_stencil.depth_bounds_test_enable = false;
    pipeline_desc.depth_stencil.stencil_test_enable = false;
    pipeline_desc.depth_stencil.min_depth_bounds = 0.0F;
    pipeline_desc.depth_stencil.max_depth_bounds = 1.0F;

    const VkPipelineColorBlendAttachmentState blend_attachment =
        render::BuildColorBlendAttachment(render::ColorBlendPreset::alpha);
    pipeline_desc.color_blend.attachments.push_back(blend_attachment);

    const render::GraphicsPipelineId pipeline_id =
        pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    graphics_pipeline_ids[mode_index] = pipeline_id;
    return pipeline_id;
}

void TextRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_resource : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_resource);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void TextRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

void TextRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
    if (depth_images.empty()) {
        return;
    }

    retired_depth_images.reserve(retired_depth_images.size() + depth_images.size());
    for (auto& depth_resource : depth_images) {
        if (depth_resource.image == VK_NULL_HANDLE) {
            continue;
        }

        RetiredDepthImage retired{};
        retired.resource = depth_resource;
        retired.retire_value = retire_value_;
        retired_depth_images.push_back(retired);
        depth_resource = {};
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void TextRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
                                                  std::uint64_t completed_value_) {
    if (retired_depth_images.empty()) {
        return;
    }
    if (context_.Device() == VK_NULL_HANDLE) {
        return;
    }

    std::size_t write_index = 0U;
    for (std::size_t read_index = 0U; read_index < retired_depth_images.size(); ++read_index) {
        auto& retired = retired_depth_images[read_index];
        if (retired.retire_value <= completed_value_) {
            resource::ImageHost::DestroyImage(context_, retired.resource);
            continue;
        }

        if (write_index != read_index) {
            retired_depth_images[write_index] = retired;
        }
        ++write_index;
    }
    retired_depth_images.resize(write_index);
}

void TextRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                          std::uint32_t image_count_,
                                          VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("TextRenderer3D::EnsureDepthResources requires initialized GpuMemoryHost");
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.preferred_depth_format);
    }

    const bool compatible_existing =
        depth_images.size() == image_count_ &&
        !depth_images.empty() &&
        depth_images[0U].format == depth_format &&
        depth_images[0U].extent.width == extent_.width &&
        depth_images[0U].extent.height == extent_.height;
    if (compatible_existing) {
        return;
    }

    RetireDepthResources(last_submitted_value_seen);
    CollectRetiredDepthResources(context_, completed_submit_value_seen);
    depth_images.resize(image_count_);
    depth_image_initialized.resize(image_count_);
    for (auto& initialized_flag : depth_image_initialized) {
        initialized_flag = 0U;
    }

    for (std::uint32_t i = 0U; i < image_count_; ++i) {
        resource::ImageCreateInfo image_create_info{};
        image_create_info.image_type = VK_IMAGE_TYPE_2D;
        image_create_info.format = depth_format;
        image_create_info.extent = VkExtent3D{extent_.width, extent_.height, 1U};
        image_create_info.mip_levels = 1U;
        image_create_info.array_layers = 1U;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image_create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        image_create_info.create_default_view = true;
        image_create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        image_create_info.default_view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        image_create_info.default_base_mip_level = 0U;
        image_create_info.default_level_count = 1U;
        image_create_info.default_base_array_layer = 0U;
        image_create_info.default_layer_count = 1U;

        depth_images[i] = resource::ImageHost::CreateImage(context_,
                                                           image_create_info,
                                                           *gpu_memory_host);
    }
}

} // namespace vr::text
