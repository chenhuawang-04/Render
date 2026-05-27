#include "vr/surface/surface_renderer_3d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/scene_3d_descriptor_contract.hpp"
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/surface/generated/surface_3d_frag_spv.hpp"
#include "vr/surface/generated/surface_3d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vr::surface {

void SurfaceRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                              render::BindlessResourceSystem& bindless_resources_,
                                              render::PipelineHost& pipeline_host_,
                                              VkFormat color_format_,
                                              VkFormat depth_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE) {
        throw std::runtime_error("SurfaceRenderer3D requires Vulkan 1.3 dynamicRendering");
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
    for (auto& per_blend : pipeline_ids) {
        for (auto& per_mode : per_blend) {
            for (auto& pipeline_id : per_mode) {
                if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
                    pipeline_id = {};
                }
            }
        }
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_surface_3d_vert_spv;
        shader_info.word_count = generated::k_surface_3d_vert_spv_word_count;
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_info{};
        shader_info.code_words = generated::k_surface_3d_frag_spv;
        shader_info.word_count = generated::k_surface_3d_frag_spv_word_count;
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_info);
    }
    if (!pipeline_layout_id.IsValid()) {
        if (ibl_host == nullptr || !ibl_host->ParamsDescriptorLayoutId().IsValid()) {
            throw std::runtime_error("SurfaceRenderer3D requires initialized IBL params descriptor layout");
        }
        EnsureAppearanceDescriptorObjects(context_, *descriptor_host);
        const VkDescriptorSetLayout sampled_image_layout = bindless_resources_.SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout = bindless_resources_.SamplerLayout();
        const VkDescriptorSetLayout appearance_layout =
            descriptor_host->GetLayout(appearance_descriptor_layout_id);
        const VkDescriptorSetLayout ibl_layout =
            descriptor_host->GetLayout(ibl_host->ParamsDescriptorLayoutId());
        if (sampled_image_layout == VK_NULL_HANDLE ||
            sampler_layout == VK_NULL_HANDLE ||
            appearance_layout == VK_NULL_HANDLE ||
            ibl_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "SurfaceRenderer3D::EnsurePipelineObjects requires valid bindless / appearance / IBL params layouts");
        }
        render::PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(sampled_image_layout);
        layout_desc.set_layouts.push_back(sampler_layout);
        layout_desc.set_layouts.push_back(appearance_layout);
        layout_desc.set_layouts.push_back(ibl_layout);
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
        for (auto& per_blend : pipeline_ids) {
            for (auto& per_mode : per_blend) {
                for (auto& pipeline_id : per_mode) {
                    pipeline_id = {};
                }
            }
        }
    }

}

render::GraphicsPipelineId SurfaceRenderer3D::EnsurePipelineForMode(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat color_format_,
    VkFormat depth_format_,
    BlendMode blend_mode_,
    PipelineMode mode_,
    CullMode cull_mode_) {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "SurfaceRenderer3D::EnsurePipelineForMode requires initialized BindlessResourceSystem");
    }
    EnsurePipelineObjects(context_, *bindless_resources, pipeline_host_, color_format_, depth_format_);

    const std::size_t blend_index = BlendModeIndex(blend_mode_);
    const std::size_t mode_index = PipelineModeIndex(mode_);
    const std::size_t cull_index = CullModeIndex(cull_mode_);
    if (blend_index >= pipeline_ids.size() ||
        mode_index >= pipeline_ids[blend_index].size() ||
        cull_index >= pipeline_ids[blend_index][mode_index].size()) {
        throw std::out_of_range("SurfaceRenderer3D pipeline mode out of range");
    }
    if (pipeline_ids[blend_index][mode_index][cull_index].IsValid()) {
        return pipeline_ids[blend_index][mode_index][cull_index];
    }

    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    desc.use_dynamic_rendering = true;
    desc.rendering.color_attachment_formats.push_back(color_format_);
    desc.rendering.depth_attachment_format = depth_format_;

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
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Surface3DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    desc.vertex_input.attributes.push_back({.location = 0U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface3DGpuInstance, world_m00))});
    desc.vertex_input.attributes.push_back({.location = 1U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface3DGpuInstance, world_m10))});
    desc.vertex_input.attributes.push_back({.location = 2U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface3DGpuInstance, world_m20))});
    desc.vertex_input.attributes.push_back({.location = 3U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface3DGpuInstance, world_m30))});
    desc.vertex_input.attributes.push_back({.location = 4U, .binding = 0U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface3DGpuInstance, uv_scale_u))});
    desc.vertex_input.attributes.push_back({.location = 5U, .binding = 0U, .format = VK_FORMAT_R32_UINT, .offset = static_cast<std::uint32_t>(offsetof(ecs::Surface3DGpuInstance, appearance_record_index))});

    desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    desc.rasterization.cull_mode = (cull_mode_ == CullMode::none)
        ? VK_CULL_MODE_NONE
        : VK_CULL_MODE_BACK_BIT;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    switch (mode_) {
    case PipelineMode::no_depth:
        desc.depth_stencil.depth_test_enable = false;
        desc.depth_stencil.depth_write_enable = false;
        desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    case PipelineMode::depth_read:
        desc.depth_stencil.depth_test_enable = true;
        desc.depth_stencil.depth_write_enable = false;
        desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    case PipelineMode::depth_read_write:
        desc.depth_stencil.depth_test_enable = true;
        desc.depth_stencil.depth_write_enable = true;
        desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    default:
        break;
    }

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

    const render::GraphicsPipelineId pipeline_id =
        pipeline_host_.RegisterGraphicsPipeline(context_, desc);
    pipeline_ids[blend_index][mode_index][cull_index] = pipeline_id;
    return pipeline_id;
}

void SurfaceRenderer3D::EnsureAppearanceDescriptorObjects(
    VulkanContext& context_,
    render::DescriptorHost& descriptor_host_) {
    if (appearance_descriptor_layout_id.IsValid()) {
        return;
    }

    const render::DescriptorSetLayoutDesc layout_desc =
        render::BuildSharedScene3DBufferLayoutDesc();
    appearance_descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
}

void SurfaceRenderer3D::DestroyStorageBuffer(resource::BufferResource& buffer_) noexcept {
    if (context == nullptr || context->Device() == VK_NULL_HANDLE) {
        buffer_ = {};
        return;
    }
    resource::BufferHost::DestroyBuffer(*context, buffer_);
}

void SurfaceRenderer3D::EnsureStorageBufferCapacity(resource::BufferResource& buffer_,
                                                    VkDeviceSize required_bytes_) {
    if (context == nullptr || gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::EnsureStorageBufferCapacity missing runtime hosts");
    }
    if (required_bytes_ == 0U) {
        return;
    }
    if (buffer_.buffer != VK_NULL_HANDLE && buffer_.size >= required_bytes_) {
        return;
    }

    if (buffer_.buffer != VK_NULL_HANDLE) {
        resource::BufferHost::DestroyBuffer(*context, buffer_);
    }

    VkDeviceSize capacity = 256U;
    while (capacity < required_bytes_) {
        capacity <<= 1U;
    }

    resource::BufferCreateInfo create_info{};
    create_info.size = capacity;
    create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    create_info.memory_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    create_info.persistently_mapped = true;
    buffer_ = resource::BufferHost::CreateBuffer(*context, create_info, *gpu_memory_host);
}

void SurfaceRenderer3D::EnsureAppearanceResourcesForFrame(std::uint64_t bindless_revision_now) {
    if (descriptor_host == nullptr ||
        active_frame_index >= frame_appearance_resources.size()) {
        return;
    }

    EnsureAppearanceDescriptorObjects(*context, *descriptor_host);

    FrameAppearanceResources& frame_resources =
        frame_appearance_resources[active_frame_index];
    const auto& prepared_appearance_source_records =
        active_prepared_frame_state.artifacts.appearance_source_records;
    const std::uint32_t appearance_record_count =
        static_cast<std::uint32_t>(prepared_appearance_source_records.size());
    const std::uint32_t appearance_upload_count = std::max<std::uint32_t>(appearance_record_count, 1U);
    const VkDeviceSize appearance_record_bytes =
        static_cast<VkDeviceSize>(appearance_upload_count) *
        sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>);
    auto mix_surface_source_revision = [](std::uint32_t accumulator_,
                                          std::uint32_t revision_) noexcept {
        accumulator_ ^= revision_ + 0x9e3779b9U + (accumulator_ << 6U) + (accumulator_ >> 2U);
        return accumulator_;
    };
    std::uint32_t texture_host_revision = 0U;
    if (texture_host != nullptr && texture_host->IsInitialized()) {
        texture_host_revision =
            mix_surface_source_revision(texture_host_revision, texture_host->Stats().revision);
    }
    if (surface_image_host != nullptr && surface_image_host->IsInitialized()) {
        texture_host_revision = mix_surface_source_revision(texture_host_revision,
                                                            surface_image_host->Stats().revision);
    }

    const std::uint64_t previous_appearance_content_revision = appearance_record_content_revision;
    const bool appearance_record_count_changed =
        appearance_record_scratch.size() != static_cast<std::size_t>(appearance_upload_count);
    const bool appearance_binding_state_changed =
        appearance_record_bindless_revision_seen != bindless_revision_now ||
        appearance_record_texture_host_revision_seen != texture_host_revision;
    const bool can_attempt_partial_frame_sync =
        !appearance_record_count_changed &&
        !appearance_binding_state_changed &&
        frame_resources.appearance_records.buffer != VK_NULL_HANDLE &&
        frame_resources.appearance_record_count == appearance_upload_count &&
        frame_resources.appearance_bindless_revision == appearance_record_bindless_revision_seen &&
        frame_resources.appearance_texture_host_revision == appearance_record_texture_host_revision_seen &&
        frame_resources.appearance_content_revision == previous_appearance_content_revision;

    struct ChangedRange final {
        std::uint32_t begin_index = 0U;
        std::uint32_t count = 0U;
    };
    std::vector<ChangedRange> changed_ranges{};
    changed_ranges.reserve(8U);

    appearance_record_scratch.resize(appearance_upload_count);
    bool appearance_scratch_changed = false;
    VkDeviceSize partial_upload_bytes = 0U;
    std::uint32_t current_range_begin = 0U;
    std::uint32_t current_range_count = 0U;
    const render::AppearanceSampledSurfaceResolver3D sampled_surface_resolver{
        .bindless_resources = bindless_resources,
        .texture_host = texture_host,
        .surface_image_host = surface_image_host,
        .geometry_image_host = nullptr
    };
    for (std::uint32_t index = 0U; index < appearance_upload_count; ++index) {
        ecs::AppearanceGpuRecord<ecs::Dim3> encoded_record{};
        if (index < appearance_record_count) {
            render::EncodeAppearanceGpuRecord3DForSampling(
                prepared_appearance_source_records[index],
                sampled_surface_resolver,
                encoded_record);
        }

        const bool changed =
            appearance_record_count_changed ||
            appearance_binding_state_changed ||
            !render::AppearanceGpuRecord3DEquals(appearance_record_scratch[index], encoded_record);
        if (changed) {
            appearance_record_scratch[index] = encoded_record;
            appearance_scratch_changed = true;
            if (current_range_count == 0U) {
                current_range_begin = index;
                current_range_count = 1U;
            } else if (current_range_begin + current_range_count == index) {
                ++current_range_count;
            } else {
                changed_ranges.push_back({.begin_index = current_range_begin, .count = current_range_count});
                current_range_begin = index;
                current_range_count = 1U;
            }
        } else if (current_range_count > 0U) {
            changed_ranges.push_back({.begin_index = current_range_begin, .count = current_range_count});
            current_range_count = 0U;
        }
    }
    if (current_range_count > 0U) {
        changed_ranges.push_back({.begin_index = current_range_begin, .count = current_range_count});
    }

    if (appearance_scratch_changed) {
        appearance_record_bindless_revision_seen = bindless_revision_now;
        appearance_record_texture_host_revision_seen = texture_host_revision;
        ++appearance_record_content_revision;
    }

    const bool frame_sync_required =
        frame_resources.appearance_records.buffer == VK_NULL_HANDLE ||
        frame_resources.appearance_record_count != appearance_upload_count ||
        frame_resources.appearance_bindless_revision != appearance_record_bindless_revision_seen ||
        frame_resources.appearance_texture_host_revision !=
            appearance_record_texture_host_revision_seen ||
        frame_resources.appearance_content_revision != appearance_record_content_revision;
    if (frame_sync_required) {
        EnsureStorageBufferCapacity(frame_resources.appearance_records, appearance_record_bytes);

        const bool can_use_partial_upload =
            !changed_ranges.empty() &&
            frame_resources.appearance_records.buffer != VK_NULL_HANDLE &&
            frame_resources.appearance_record_count == appearance_upload_count &&
            frame_resources.appearance_bindless_revision == appearance_record_bindless_revision_seen &&
            frame_resources.appearance_texture_host_revision ==
                appearance_record_texture_host_revision_seen &&
            frame_resources.appearance_content_revision == previous_appearance_content_revision &&
            appearance_record_content_revision != previous_appearance_content_revision &&
            can_attempt_partial_frame_sync;
        if (can_use_partial_upload) {
            for (const ChangedRange& range : changed_ranges) {
                render::CopyAppearanceGpuRecord3DRange(appearance_record_scratch.data(),
                                                       frame_resources.appearance_records,
                                                       range.begin_index,
                                                       range.count);
                partial_upload_bytes +=
                    static_cast<VkDeviceSize>(range.count) *
                    sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>);
            }
            stats.uploaded_bytes += partial_upload_bytes;
        } else {
            render::CopyAppearanceGpuRecord3DRange(appearance_record_scratch.data(),
                                                   frame_resources.appearance_records,
                                                   0U,
                                                   appearance_upload_count);
            stats.uploaded_bytes += appearance_record_bytes;
        }

        frame_resources.appearance_bindless_revision = appearance_record_bindless_revision_seen;
        frame_resources.appearance_texture_host_revision =
            appearance_record_texture_host_revision_seen;
        frame_resources.appearance_content_revision = appearance_record_content_revision;
    }
    frame_resources.appearance_record_count = appearance_upload_count;

    std::uint64_t descriptor_signature = 14695981039346656037ULL;
    descriptor_signature ^= reinterpret_cast<std::uintptr_t>(frame_resources.appearance_records.buffer);
    descriptor_signature *= 1099511628211ULL;
    descriptor_signature ^= static_cast<std::uint64_t>(appearance_record_bytes);
    descriptor_signature *= 1099511628211ULL;
    frame_resources.descriptor_payload_signature = descriptor_signature;
}

void SurfaceRenderer3D::PrepareAppearanceDescriptorSetForFrame(std::uint32_t frame_index_) {
    if (descriptor_host == nullptr ||
        frame_index_ >= frame_appearance_resources.size() ||
        !appearance_descriptor_layout_id.IsValid()) {
        return;
    }

    FrameAppearanceResources& frame_resources = frame_appearance_resources[frame_index_];
    if (frame_resources.appearance_records.buffer == VK_NULL_HANDLE) {
        return;
    }
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        frame_resources.descriptor_set = descriptor_host->AllocateSet(*context,
                                                                      frame_index_,
                                                                      appearance_descriptor_layout_id);
        frame_resources.descriptor_buffer_signature = 0U;
    }
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    const std::uint64_t buffer_signature = frame_resources.descriptor_payload_signature;
    if (frame_resources.descriptor_buffer_signature == buffer_signature) {
        return;
    }

    descriptor_buffer_write_scratch.clear();
    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.push_back({
        .binding = render::scene_3d_surface_appearance_binding,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.appearance_records.buffer,
        .offset = 0U,
        .range = static_cast<VkDeviceSize>(frame_resources.appearance_record_count) *
                 sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>)
    });

    descriptor_host->UpdateSet(*context,
                               frame_resources.descriptor_set,
                               descriptor_buffer_write_scratch,
                               descriptor_image_write_scratch,
                               {});
    frame_resources.descriptor_buffer_signature = buffer_signature;
    ++stats.descriptor_set_update_count;
}

void SurfaceRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                             std::uint32_t image_count_,
                                             VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::EnsureDepthResources missing GpuMemoryHost");
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
    for (auto& value : depth_image_initialized) {
        value = 0U;
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

void SurfaceRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
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

void SurfaceRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
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

void SurfaceRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_image : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_image);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void SurfaceRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

} // namespace vr::surface
