#include "vr/particle/particle_renderer_2d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_2d_frag_spv.hpp"
#include "vr/particle/generated/particle_2d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::particle {

namespace {

constexpr VkPrimitiveTopology k_particle_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

} // namespace

void ParticleRenderer2D::EnsurePipelineObjects(VulkanContext& context_,
                                               render::BindlessResourceSystem& bindless_resources_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer2D requires Vulkan 1.3 dynamicRendering + synchronization2");
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
    for (auto& pipeline_id : pipeline_ids) {
        if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
            pipeline_id = {};
        }
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_2d_vert_spv;
        shader_create_info.word_count = std::size(generated::k_particle_2d_vert_spv);
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_2d_frag_spv;
        shader_create_info.word_count = std::size(generated::k_particle_2d_frag_spv);
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        const VkDescriptorSetLayout sampled_image_layout =
            bindless_resources_.SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout =
            bindless_resources_.SamplerLayout();
        if (sampled_image_layout == VK_NULL_HANDLE || sampler_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "ParticleRenderer2D::EnsurePipelineObjects requires valid bindless set layouts");
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

    if (pipeline_color_format != color_format_) {
        for (auto& pipeline_id : pipeline_ids) {
            pipeline_id = {};
        }
        pipeline_color_format = color_format_;
    }
}

render::GraphicsPipelineId ParticleRenderer2D::EnsurePipelineForBlendMode(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat color_format_,
    BlendModeKind blend_mode_) {
    EnsurePipelineObjects(context_, *bindless_resources, pipeline_host_, color_format_);

    render::GraphicsPipelineId& cached_pipeline_id = pipeline_ids[BlendModeIndex(blend_mode_)];
    if (cached_pipeline_id.IsValid() && pipeline_color_format == color_format_) {
        return cached_pipeline_id;
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
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Particle2DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 0U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, position_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 1U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, size_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 2U,
        .binding = 0U,
        .format = VK_FORMAT_R32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, rotation_radians))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, color_rgba8))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, texture_slot))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 5U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, sampler_slot))
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
    const VkPipelineColorBlendAttachmentState blend_attachment =
        render::BuildColorBlendAttachment(blend_preset);
    pipeline_desc.color_blend.attachments.push_back(blend_attachment);

    cached_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
    return cached_pipeline_id;
}

void ParticleRenderer2D::RemapCpuInstancesToBindless() {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer2D::RemapCpuInstancesToBindless requires initialized bindless resources");
    }
    for (auto& instance : runtime_scratch.instances) {
        const std::uint32_t raw_texture_id = instance.texture_slot;
        instance.texture_slot = ResolveTextureSlot(raw_texture_id);
        instance.sampler_slot = ResolveSamplerSlot(raw_texture_id);
    }
}

std::uint32_t ParticleRenderer2D::ResolveTextureSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->PlaceholderImageSlot().index;
    }
    return bindless_resources->ResolveTextureImageSlot(*texture_host,
                                                       asset::TextureId{texture_id_}).index;
}

std::uint32_t ParticleRenderer2D::ResolveSamplerSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->DefaultSamplerSlot().index;
    }
    return bindless_resources->ResolveTextureSamplerSlot(*texture_host,
                                                         asset::TextureId{texture_id_}).index;
}

} // namespace vr::particle
