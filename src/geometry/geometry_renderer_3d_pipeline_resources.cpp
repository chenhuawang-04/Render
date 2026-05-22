#include "vr/geometry/geometry_renderer_3d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/geometry/geometry_appearance_resolver.hpp"
#include "vr/geometry/generated/geometry_3d_frag_spv.hpp"
#include "vr/geometry/generated/geometry_3d_vert_spv.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/scene_3d_descriptor_contract.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::geometry {

void GeometryRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_,
                                               VkFormat depth_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        throw std::runtime_error("GeometryRenderer3D requires Vulkan 1.3 dynamicRendering");
    }
    if (descriptor_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D requires DescriptorHost");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D requires initialized BindlessResourceSystem");
    }
    if (ibl_host == nullptr || !ibl_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D requires initialized IblHost");
    }
    if (!ibl_host->ParamsDescriptorLayoutId().IsValid()) {
        throw std::runtime_error("GeometryRenderer3D requires valid IBL params descriptor layout");
    }

    EnsureLightingDescriptorObjects(context_, *descriptor_host);

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_geometry_3d_vert_spv;
        shader_info.word_count = generated::k_geometry_3d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_geometry_3d_frag_spv;
        shader_info.word_count = generated::k_geometry_3d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!pipeline_layout_id.IsValid()) {
        const VkDescriptorSetLayout sampled_image_layout = bindless_resources->SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout = bindless_resources->SamplerLayout();
        const VkDescriptorSetLayout lighting_layout = descriptor_host->GetLayout(lighting_descriptor_layout_id);
        const VkDescriptorSetLayout ibl_params_layout = descriptor_host->GetLayout(ibl_host->ParamsDescriptorLayoutId());
        if (sampled_image_layout == VK_NULL_HANDLE ||
            sampler_layout == VK_NULL_HANDLE ||
            lighting_layout == VK_NULL_HANDLE ||
            ibl_params_layout == VK_NULL_HANDLE) {
            throw std::runtime_error("GeometryRenderer3D::EnsurePipelineObjects requires valid bindless and lighting layouts");
        }

        render::PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(sampled_image_layout);
        layout_desc.set_layouts.push_back(sampler_layout);
        layout_desc.set_layouts.push_back(lighting_layout);
        layout_desc.set_layouts.push_back(ibl_params_layout);
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
        for (auto& blend_pipelines : pipeline_ids) {
            for (auto& mode_pipelines : blend_pipelines) {
                for (auto& topology_pipelines : mode_pipelines) {
                    for (auto& pipeline_id : topology_pipelines) {
                        pipeline_id = {};
                    }
                }
            }
        }
    }
}

render::GraphicsPipelineId GeometryRenderer3D::EnsurePipelineForMode(VulkanContext& context_,
                                                                     render::PipelineHost& pipeline_host_,
                                                                     VkFormat color_format_,
                                                                     VkFormat depth_format_,
                                                                     BlendMode blend_mode_,
                                                                     PipelineMode mode_,
                                                                     TopologyMode topology_mode_,
                                                                     CullMode cull_mode_) {
    const std::size_t blend_index = BlendModeIndex(blend_mode_);
    const std::size_t mode_index = PipelineModeIndex(mode_);
    const std::size_t topology_index = TopologyModeIndex(topology_mode_);
    const std::size_t cull_index = CullModeIndex(cull_mode_);
    if (blend_index >= pipeline_ids.size()) {
        throw std::out_of_range("GeometryRenderer3D blend mode out of range");
    }
    if (mode_index >= pipeline_ids[blend_index].size()) {
        throw std::out_of_range("GeometryRenderer3D pipeline mode out of range");
    }
    if (topology_index >= pipeline_ids[blend_index][mode_index].size()) {
        throw std::out_of_range("GeometryRenderer3D topology mode out of range");
    }
    if (cull_index >= pipeline_ids[blend_index][mode_index][topology_index].size()) {
        throw std::out_of_range("GeometryRenderer3D cull mode out of range");
    }
    if (pipeline_ids[blend_index][mode_index][topology_index][cull_index].IsValid()) {
        return pipeline_ids[blend_index][mode_index][topology_index][cull_index];
    }

    const bool depth_test = mode_ == PipelineMode::depth_read || mode_ == PipelineMode::depth_read_write;
    const bool depth_write = mode_ == PipelineMode::depth_read_write;
    const bool cull_back = cull_mode_ == CullMode::back;

    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    desc.use_dynamic_rendering = true;
    desc.rendering.color_attachment_formats.push_back(color_format_);
    desc.rendering.depth_attachment_format = depth_test ? depth_format_ : VK_FORMAT_UNDEFINED;

    desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(shader_fragment_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });

    desc.vertex_input.bindings.push_back({
        .binding = 0U,
        .stride = static_cast<std::uint32_t>(sizeof(GeometryMeshVertex)),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    });
    desc.vertex_input.bindings.push_back({
        .binding = 1U,
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Geometry3DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    desc.vertex_input.attributes.push_back({.location = 0U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, position_x))});
    desc.vertex_input.attributes.push_back({.location = 1U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, normal_x))});
    desc.vertex_input.attributes.push_back({.location = 2U, .binding = 0U, .format = VK_FORMAT_R32G32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, uv_u))});
    desc.vertex_input.attributes.push_back({.location = 22U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, tangent_x))});
    desc.vertex_input.attributes.push_back({.location = 12U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, morph0_position_delta_x))});
    desc.vertex_input.attributes.push_back({.location = 13U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, morph0_normal_delta_x))});
    desc.vertex_input.attributes.push_back({.location = 14U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, morph1_position_delta_x))});
    desc.vertex_input.attributes.push_back({.location = 15U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, morph1_normal_delta_x))});
    desc.vertex_input.attributes.push_back({.location = 17U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_UINT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, joint_index0))});
    desc.vertex_input.attributes.push_back({.location = 18U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(GeometryMeshVertex, joint_weight0))});
    desc.vertex_input.attributes.push_back({.location = 3U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, world_m00))});
    desc.vertex_input.attributes.push_back({.location = 4U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, world_m10))});
    desc.vertex_input.attributes.push_back({.location = 5U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, world_m20))});
    desc.vertex_input.attributes.push_back({.location = 6U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, world_m30))});
    desc.vertex_input.attributes.push_back({.location = 7U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, deform_param0_x))});
    desc.vertex_input.attributes.push_back({.location = 8U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, deform_param1_x))});
    desc.vertex_input.attributes.push_back({.location = 9U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, morph_weight0))});
    desc.vertex_input.attributes.push_back({.location = 10U, .binding = 1U, .format = VK_FORMAT_R32_UINT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, component_index))});
    desc.vertex_input.attributes.push_back({.location = 11U, .binding = 1U, .format = VK_FORMAT_R32_UINT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Geometry3DGpuInstance, appearance_record_index))});

    switch (topology_mode_) {
    case TopologyMode::triangles:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    case TopologyMode::lines:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case TopologyMode::points:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        break;
    default:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    }
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    desc.rasterization.cull_mode = cull_back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    desc.depth_stencil.depth_test_enable = depth_test;
    desc.depth_stencil.depth_write_enable = depth_write;
    desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;

    render::ColorBlendPreset blend_preset = render::ColorBlendPreset::disabled;
    switch (blend_mode_) {
    case BlendMode::opaque:
        blend_preset = render::ColorBlendPreset::disabled;
        break;
    case BlendMode::alpha:
        blend_preset = render::ColorBlendPreset::alpha;
        break;
    case BlendMode::additive:
        blend_preset = render::ColorBlendPreset::additive;
        break;
    case BlendMode::multiply:
        blend_preset = render::ColorBlendPreset::multiply;
        break;
    case BlendMode::premultiplied_alpha:
        blend_preset = render::ColorBlendPreset::premultiplied_alpha;
        break;
    case BlendMode::screen:
        blend_preset = render::ColorBlendPreset::screen;
        break;
    default:
        break;
    }
    const VkPipelineColorBlendAttachmentState blend =
        render::BuildColorBlendAttachment(blend_preset);
    desc.color_blend.attachments.push_back(blend);

    const render::GraphicsPipelineId pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, desc);
    pipeline_ids[blend_index][mode_index][topology_index][cull_index] = pipeline_id;
    return pipeline_id;
}

void GeometryRenderer3D::PrewarmCommonPipelines(VulkanContext& context_,
                                                render::PipelineHost& pipeline_host_,
                                                VkFormat color_format_,
                                                VkFormat depth_format_) {
    auto warm_variant = [&](BlendMode blend_mode_, PipelineMode mode_, TopologyMode topology_, CullMode cull_) {
        if (mode_ != PipelineMode::no_depth && depth_format_ == VK_FORMAT_UNDEFINED) {
            return;
        }
        const std::size_t blend_index = BlendModeIndex(blend_mode_);
        const std::size_t mode_index = PipelineModeIndex(mode_);
        const std::size_t topology_index = TopologyModeIndex(topology_);
        const std::size_t cull_index = CullModeIndex(cull_);
        if (pipeline_ids[blend_index][mode_index][topology_index][cull_index].IsValid()) {
            return;
        }
        (void)EnsurePipelineForMode(context_,
                                    pipeline_host_,
                                    color_format_,
                                    depth_format_,
                                    blend_mode_,
                                    mode_,
                                    topology_,
                                    cull_);
        ++stats.prewarmed_pipeline_count;
    };

    warm_variant(BlendMode::opaque, PipelineMode::no_depth, TopologyMode::triangles, CullMode::back);
    if (create_info_cache.enable_depth) {
        warm_variant(BlendMode::opaque, PipelineMode::depth_read_write, TopologyMode::triangles, CullMode::back);
        if (create_info_cache.prewarm_depth_read_variant) {
            warm_variant(BlendMode::opaque, PipelineMode::depth_read, TopologyMode::triangles, CullMode::back);
        }
    }

    if (create_info_cache.prewarm_double_sided_variant) {
        warm_variant(BlendMode::opaque, PipelineMode::no_depth, TopologyMode::triangles, CullMode::none);
        if (create_info_cache.enable_depth) {
            warm_variant(BlendMode::opaque, PipelineMode::depth_read_write, TopologyMode::triangles, CullMode::none);
            if (create_info_cache.prewarm_depth_read_variant) {
                warm_variant(BlendMode::opaque, PipelineMode::depth_read, TopologyMode::triangles, CullMode::none);
            }
        }
    }

    if (create_info_cache.prewarm_line_and_point_variants) {
        warm_variant(BlendMode::opaque, PipelineMode::no_depth, TopologyMode::lines, CullMode::none);
        warm_variant(BlendMode::opaque, PipelineMode::no_depth, TopologyMode::points, CullMode::none);
        if (create_info_cache.enable_depth) {
            warm_variant(BlendMode::opaque, PipelineMode::depth_read, TopologyMode::lines, CullMode::none);
            warm_variant(BlendMode::opaque, PipelineMode::depth_read, TopologyMode::points, CullMode::none);
        }
    }
}

void GeometryRenderer3D::CompileRequiredPipelinesForCurrentFrame(VulkanContext& context_,
                                                                 render::PipelineHost& pipeline_host_,
                                                                 VkFormat color_format_,
                                                                 VkFormat depth_format_) {
    if (runtime_scratch.draw_batches.empty()) {
        return;
    }

    const bool use_depth = depth_format_ != VK_FORMAT_UNDEFINED;
    for (const ecs::Geometry3DDrawBatch& batch : runtime_scratch.draw_batches) {
        if (batch.instance_count == 0U) {
            continue;
        }
        const GeometryResourceHost::MeshRecord* mesh = geometry_resource_host->FindMesh(batch.geometry_id);
        if (mesh == nullptr) {
            continue;
        }
        const BlendMode blend_mode = ResolveBlendMode(batch);
        const PipelineMode mode = ResolvePipelineMode(batch, use_depth);
        const TopologyMode topology = ResolveTopologyMode(mesh->topology, batch);
        const CullMode cull = ResolveCullMode(batch);
        const std::size_t blend_index = BlendModeIndex(blend_mode);
        const std::size_t mode_index = PipelineModeIndex(mode);
        const std::size_t topology_index = TopologyModeIndex(topology);
        const std::size_t cull_index = CullModeIndex(cull);
        const bool already_compiled =
            pipeline_ids[blend_index][mode_index][topology_index][cull_index].IsValid();
        (void)EnsurePipelineForMode(context_,
                                    pipeline_host_,
                                    color_format_,
                                    depth_format_,
                                    blend_mode,
                                    mode,
                                    topology,
                                    cull);
        if (!already_compiled) {
            ++stats.prepare_compiled_pipeline_count;
        }
    }
}

void GeometryRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                              std::uint32_t image_count_,
                                              VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::EnsureDepthResources missing GpuMemoryHost");
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.preferred_depth_format);
    }

    const bool compatible = depth_images.size() == image_count_ &&
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
    for (auto& flag : depth_image_initialized) {
        flag = 0U;
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

void GeometryRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
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

void GeometryRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
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

void GeometryRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_image : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_image);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void GeometryRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

} // namespace vr::geometry
