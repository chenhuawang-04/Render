#include "vr/particle/particle_renderer_3d.hpp"

#include "vr/ecs/system/particle_system.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_3d_frag_spv.hpp"
#include "vr/particle/generated/particle_3d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::particle {

namespace {

constexpr VkPrimitiveTopology k_particle_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

} // namespace

void ParticleRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                               render::BindlessResourceSystem& bindless_resources_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_,
                                               VkFormat depth_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer3D requires Vulkan 1.3 dynamicRendering + synchronization2");
    }

    const render::PipelineHostStats pipeline_stats = pipeline_host_.Stats();
    if (shader_vertex_id.IsValid() && pipeline_stats.shader_module_count < shader_vertex_id.value) {
        shader_vertex_id = {};
    }
    if (shader_fragment_id.IsValid() && pipeline_stats.shader_module_count < shader_fragment_id.value) {
        shader_fragment_id = {};
    }
    if (pipeline_layout_id.IsValid() && pipeline_stats.pipeline_layout_count < pipeline_layout_id.value) {
        pipeline_layout_id = {};
    }
    for (auto& per_blend : pipeline_ids) {
        for (auto& pipeline_id : per_blend) {
            if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
                pipeline_id = {};
            }
        }
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_3d_vert_spv;
        shader_create_info.word_count = std::size(generated::k_particle_3d_vert_spv);
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_3d_frag_spv;
        shader_create_info.word_count = std::size(generated::k_particle_3d_frag_spv);
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        const VkDescriptorSetLayout sampled_image_layout = bindless_resources_.SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout = bindless_resources_.SamplerLayout();
        if (sampled_image_layout == VK_NULL_HANDLE || sampler_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "ParticleRenderer3D::EnsurePipelineObjects requires valid bindless set layouts");
        }
        render::PipelineLayoutDesc pipeline_layout_desc{};
        pipeline_layout_desc.set_layouts.push_back(sampled_image_layout);
        pipeline_layout_desc.set_layouts.push_back(sampler_layout);
        pipeline_layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, pipeline_layout_desc);
    }

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        for (auto& per_blend : pipeline_ids) {
            for (auto& pipeline_id : per_blend) {
                pipeline_id = {};
            }
        }
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
    }
}

render::GraphicsPipelineId ParticleRenderer3D::EnsurePipelineForMode(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat color_format_,
    VkFormat depth_format_,
    BlendModeKind blend_mode_,
    DepthPipelineMode depth_mode_) {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer3D::EnsurePipelineForMode requires initialized BindlessResourceSystem");
    }
    EnsurePipelineObjects(context_, *bindless_resources, pipeline_host_, color_format_, depth_format_);

    render::GraphicsPipelineId& cached_pipeline_id =
        pipeline_ids[BlendModeIndex(blend_mode_)][DepthPipelineModeIndex(depth_mode_)];
    if (cached_pipeline_id.IsValid() &&
        pipeline_color_format == color_format_ &&
        pipeline_depth_format == depth_format_) {
        return cached_pipeline_id;
    }

    const bool use_depth = depth_mode_ != DepthPipelineMode::no_depth;
    const bool depth_write = depth_mode_ == DepthPipelineMode::depth_test_write ||
                             depth_mode_ == DepthPipelineMode::depth_test_write_reverse_z;
    const bool reverse_z = depth_mode_ == DepthPipelineMode::depth_test_reverse_z ||
                           depth_mode_ == DepthPipelineMode::depth_test_write_reverse_z;

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
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Particle3DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 0U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, position_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 1U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, size_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 2U,
        .binding = 0U,
        .format = VK_FORMAT_R32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, rotation_radians))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, stretch_factor))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, color_rgba8))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 5U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, velocity_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 6U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, texture_slot))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 7U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, sampler_slot))
    });

    pipeline_desc.input_assembly.topology = k_particle_topology;
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

    pipeline_desc.depth_stencil.depth_test_enable = use_depth;
    pipeline_desc.depth_stencil.depth_write_enable = depth_write;
    pipeline_desc.depth_stencil.depth_compare_op = reverse_z
        ? VK_COMPARE_OP_GREATER_OR_EQUAL
        : VK_COMPARE_OP_LESS_OR_EQUAL;
    pipeline_desc.depth_stencil.depth_bounds_test_enable = false;
    pipeline_desc.depth_stencil.stencil_test_enable = false;
    pipeline_desc.depth_stencil.min_depth_bounds = 0.0F;
    pipeline_desc.depth_stencil.max_depth_bounds = 1.0F;

    render::ColorBlendPreset blend_preset = render::ColorBlendPreset::alpha;
    switch (blend_mode_) {
    case BlendModeKind::additive:
        blend_preset = render::ColorBlendPreset::additive;
        break;
    case BlendModeKind::multiply:
        blend_preset = render::ColorBlendPreset::multiply;
        break;
    case BlendModeKind::premultiplied_alpha:
        blend_preset = render::ColorBlendPreset::premultiplied_alpha;
        break;
    case BlendModeKind::screen:
        blend_preset = render::ColorBlendPreset::screen;
        break;
    case BlendModeKind::alpha:
    default:
        blend_preset = render::ColorBlendPreset::alpha;
        break;
    }
    pipeline_desc.color_blend.attachments.push_back(render::BuildColorBlendAttachment(blend_preset));

    cached_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
    pipeline_depth_format = depth_format_;
    return cached_pipeline_id;
}

void ParticleRenderer3D::RemapCpuInstancesToBindless() {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer3D::RemapCpuInstancesToBindless requires initialized bindless resources");
    }
    for (auto& instance : runtime_scratch.instances) {
        const std::uint32_t raw_texture_id = instance.texture_slot;
        instance.texture_slot = ResolveTextureSlot(raw_texture_id);
        instance.sampler_slot = ResolveSamplerSlot(raw_texture_id);
    }
}

std::uint32_t ParticleRenderer3D::ResolveTextureSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->PlaceholderImageSlot().index;
    }
    return bindless_resources->ResolveTextureImageSlot(*texture_host,
                                                       asset::TextureId{texture_id_}).index;
}

std::uint32_t ParticleRenderer3D::ResolveSamplerSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->DefaultSamplerSlot().index;
    }
    return bindless_resources->ResolveTextureSamplerSlot(*texture_host,
                                                         asset::TextureId{texture_id_}).index;
}

void ParticleRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                              std::uint32_t image_count_,
                                              VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("ParticleRenderer3D::EnsureDepthResources missing GpuMemoryHost");
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.preferred_depth_format);
    }

    const bool compatible =
        depth_images.size() == image_count_ &&
        !depth_images.empty() &&
        depth_images[0U].format == depth_format &&
        depth_images[0U].extent.width == extent_.width &&
        depth_images[0U].extent.height == extent_.height;
    if (compatible) {
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
        resource::ImageCreateInfo create_info{};
        create_info.image_type = VK_IMAGE_TYPE_2D;
        create_info.format = depth_format;
        create_info.extent = VkExtent3D{extent_.width, extent_.height, 1U};
        create_info.mip_levels = 1U;
        create_info.array_layers = 1U;
        create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        create_info.create_default_view = true;
        create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        create_info.default_view_aspect = DepthImageAspectMask(depth_format);
        create_info.default_base_mip_level = 0U;
        create_info.default_level_count = 1U;
        create_info.default_base_array_layer = 0U;
        create_info.default_layer_count = 1U;
        depth_images[i] = resource::ImageHost::CreateImage(context_, create_info, *gpu_memory_host);
    }
}

void ParticleRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
    if (depth_images.empty()) {
        return;
    }

    retired_depth_images.reserve(retired_depth_images.size() + depth_images.size());
    for (auto& depth_image : depth_images) {
        if (depth_image.image == VK_NULL_HANDLE) {
            continue;
        }
        RetiredDepthImage retired{};
        retired.resource = depth_image;
        retired.retire_value = retire_value_;
        retired_depth_images.push_back(retired);
        depth_image = {};
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void ParticleRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
                                                      std::uint64_t completed_value_) {
    if (retired_depth_images.empty() || context_.Device() == VK_NULL_HANDLE) {
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

void ParticleRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_image : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_image);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void ParticleRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

} // namespace vr::particle
