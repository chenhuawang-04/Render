#include "vr/shadow/shadow_renderer_2d.hpp"

#include "vr/shadow/generated/shadow_depth_2d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace vr::shadow {

bool ShadowRenderer2D::IsDepthFormatSupported(VulkanContext& context_,
                                              VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool ShadowRenderer2D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags ShadowRenderer2D::DepthAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags;
}

VkFormat ShadowRenderer2D::ResolveDepthFormat(VulkanContext& context_,
                                              VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };
    for (VkFormat format : fallback_formats) {
        if (IsDepthFormatSupported(context_, format)) {
            return format;
        }
    }
    throw std::runtime_error("ShadowRenderer2D failed to resolve usable depth format");
}

std::size_t ShadowRenderer2D::LowerBoundAtlasRequestIndex(
    const ShadowRenderer2DMcVector<AtlasRequestAggregate>& entries_,
    std::uint32_t namespace_id_) noexcept {
    std::size_t first = 0U;
    std::size_t count = entries_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (entries_[it].namespace_id < namespace_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

bool ShadowRenderer2D::HasValidBounds(const ecs::Bounds<ecs::Dim2>& bounds_) noexcept {
    const bool finite_min = std::isfinite(bounds_.runtime.world_min.x) &&
                            std::isfinite(bounds_.runtime.world_min.y);
    const bool finite_max = std::isfinite(bounds_.runtime.world_max.x) &&
                            std::isfinite(bounds_.runtime.world_max.y);
    if (!finite_min || !finite_max) {
        return false;
    }
    return bounds_.runtime.world_max.x >= bounds_.runtime.world_min.x &&
           bounds_.runtime.world_max.y >= bounds_.runtime.world_min.y;
}

void ShadowRenderer2D::Initialize(const ShadowRenderer2DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.runtime_build.atlas_width == 0U) {
        create_info_cache.runtime_build.atlas_width = 4096U;
    }
    if (create_info_cache.runtime_build.atlas_height == 0U) {
        create_info_cache.runtime_build.atlas_height = 4096U;
    }
    if (create_info_cache.runtime_build.atlas_layer_count == 0U) {
        create_info_cache.runtime_build.atlas_layer_count = 8U;
    }

    create_info_cache.atlas.depth_format = create_info_cache.preferred_depth_format;

    shadow_components = nullptr;
    shadow_transforms = nullptr;
    shadow_component_count = 0U;
    camera_component = nullptr;
    caster_bounds = nullptr;
    caster_count = 0U;

    context = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;

    std::destroy_at(&frame_coordinator);
    std::construct_at(&frame_coordinator);
    frame_coordinator.Reserve(create_info_cache.reserve_shadow_count,
                              create_info_cache.reserve_caster_count);
    if (create_info_cache.reserve_atlas_request_count > 0U) {
        atlas_requests.reserve(create_info_cache.reserve_atlas_request_count);
    }

    last_prepare_result = {};
    atlas_requests.clear();

    pipeline_layout_id = {};
    shader_vertex_id = {};
    pipeline_id = {};
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    resolved_depth_format = VK_FORMAT_UNDEFINED;
    stats = {};
    initialized = true;
}

void ShadowRenderer2D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    atlas_host.Shutdown(context_);

    shadow_components = nullptr;
    shadow_transforms = nullptr;
    shadow_component_count = 0U;
    camera_component = nullptr;
    caster_bounds = nullptr;
    caster_count = 0U;

    context = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;

    std::destroy_at(&frame_coordinator);
    std::construct_at(&frame_coordinator);
    last_prepare_result = {};
    atlas_requests.clear();

    pipeline_layout_id = {};
    shader_vertex_id = {};
    pipeline_id = {};
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    resolved_depth_format = VK_FORMAT_UNDEFINED;
    stats = {};
    initialized = false;
}

void ShadowRenderer2D::SetSceneData(ecs::Shadow<ecs::Dim2>* shadow_components_,
                                    ecs::Transform<ecs::Dim2>* shadow_transforms_,
                                    std::uint32_t shadow_component_count_,
                                    ecs::Camera<ecs::Dim2>* camera_component_,
                                    ecs::Bounds<ecs::Dim2>* caster_bounds_,
                                    std::uint32_t caster_count_) noexcept {
    shadow_components = shadow_components_;
    shadow_transforms = shadow_transforms_;
    shadow_component_count = shadow_component_count_;
    camera_component = camera_component_;
    caster_bounds = caster_bounds_;
    caster_count = caster_count_;

    frame_coordinator.SetShadowData(shadow_components, shadow_transforms, shadow_component_count);
    frame_coordinator.SetCamera(camera_component);
    frame_coordinator.SetCasterBounds(caster_bounds, caster_count);
}

void ShadowRenderer2D::SetShadowDirtyHint(const std::uint32_t* dirty_component_indices_,
                                          std::uint32_t dirty_component_count_) noexcept {
    frame_coordinator.SetShadowDirtyHint(dirty_component_indices_, dirty_component_count_);
}

void ShadowRenderer2D::SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                                             std::uint32_t dirty_component_count_) noexcept {
    frame_coordinator.SetTransformDirtyHint(dirty_component_indices_, dirty_component_count_);
}

void ShadowRenderer2D::PrepareFrame(const render::ShadowRenderer2DPrepareView& prepare_view_) {
    if (!initialized) {
        return;
    }
    context = &prepare_view_.device;
    pipeline_host = &prepare_view_.pipeline;
    gpu_memory_host = &prepare_view_.gpu_memory;
    resolved_depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
    create_info_cache.atlas.depth_format = resolved_depth_format;

    if (!atlas_host.IsInitialized()) {
        atlas_host.Initialize(*context, *gpu_memory_host, create_info_cache.atlas);
    }
    atlas_host.BeginFrame(*context, prepare_view_.progress.completed_submit_value);

    frame_coordinator.SetShadowData(shadow_components, shadow_transforms, shadow_component_count);
    frame_coordinator.SetCamera(camera_component);
    frame_coordinator.SetCasterBounds(caster_bounds, caster_count);
    frame_coordinator.Reserve(shadow_component_count, caster_count);

    last_prepare_result = frame_coordinator.PrepareFrame(prepare_view_.frame.frame_index,
                                                         create_info_cache.runtime_build,
                                                         create_info_cache.caster_build);

    BuildAtlasRequests();
    if (!atlas_requests.empty()) {
        ShadowRenderer2DMcVector<ShadowAtlasRequest> requests{};
        requests.resize(atlas_requests.size());
        for (std::size_t i = 0U; i < atlas_requests.size(); ++i) {
            requests[i] = ShadowAtlasRequest{
                .namespace_id = atlas_requests[i].namespace_id,
                .width = atlas_requests[i].width,
                .height = atlas_requests[i].height,
                .layer_count = atlas_requests[i].layer_count,
            };
        }
        atlas_host.EnsureAtlases(*context,
                                 prepare_view_.progress.last_submitted_value,
                                 prepare_view_.progress.completed_submit_value,
                                 requests.data(),
                                 static_cast<std::uint32_t>(requests.size()));
    }

    stats.shadow_component_count = shadow_component_count;
    stats.shadow_view_count = last_prepare_result.runtime_stats.generated_view_count;
    stats.shadow_runtime_updated_count = last_prepare_result.runtime_stats.updated_record_count;
    stats.shadow_caster_header_count =
        ecs::ShadowCasterSystem<ecs::Dim2>::HeaderCount(frame_coordinator.CasterScratch());
    stats.shadow_caster_index_count =
        ecs::ShadowCasterSystem<ecs::Dim2>::CasterIndexCount(frame_coordinator.CasterScratch());
    stats.atlas_namespace_count = static_cast<std::uint32_t>(atlas_requests.size());
    stats.runtime_cache_reused = last_prepare_result.runtime_stats.cache_reused;
    stats.runtime_transform_only_update = last_prepare_result.runtime_stats.transform_only_update;

    EnsurePipelineObjects(*context, *pipeline_host, resolved_depth_format);
}

void ShadowRenderer2D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized ||
        context == nullptr ||
        pipeline_host == nullptr ||
        record_context_.command_buffer == VK_NULL_HANDLE) {
        return;
    }
    if (shadow_components == nullptr || caster_bounds == nullptr ||
        shadow_component_count == 0U || caster_count == 0U) {
        return;
    }

    stats.draw_call_count = 0U;
    stats.skipped_invalid_bounds_count = 0U;
    stats.skipped_out_of_range_count = 0U;
    stats.atlas_layer_draw_pass_count = 0U;
    stats.atlas_transition_count = 0U;

    EnsurePipelineObjects(*context, *pipeline_host, resolved_depth_format);
    if (!pipeline_id.IsValid()) {
        return;
    }

    for (const AtlasRequestAggregate& request : atlas_requests) {
        ShadowAtlasHost::AtlasRecord* atlas_record = atlas_host.FindAtlas(request.namespace_id);
        if (atlas_record == nullptr || atlas_record->resource.image == VK_NULL_HANDLE) {
            continue;
        }
        RecordOneAtlas(record_context_, *atlas_record);
    }
}

bool ShadowRenderer2D::IsInitialized() const noexcept {
    return initialized;
}

const ShadowRenderer2DStats& ShadowRenderer2D::Stats() const noexcept {
    return stats;
}

const ShadowAtlasHost& ShadowRenderer2D::AtlasHost() const noexcept {
    return atlas_host;
}

ShadowAtlasHost& ShadowRenderer2D::AtlasHostMutable() noexcept {
    return atlas_host;
}

const render::ShadowFrameCoordinator<ecs::Dim2>& ShadowRenderer2D::FrameCoordinator() const noexcept {
    return frame_coordinator;
}

render::ShadowFrameCoordinator<ecs::Dim2>& ShadowRenderer2D::FrameCoordinatorMutable() noexcept {
    return frame_coordinator;
}

void ShadowRenderer2D::EnsurePipelineObjects(VulkanContext& context_,
                                             render::PipelineHost& pipeline_host_,
                                             VkFormat depth_format_) {
    if (!pipeline_layout_id.IsValid()) {
        render::PipelineLayoutDesc layout_desc{};
        render::PushConstantRangeDesc push_constant_range{};
        push_constant_range.stage_flags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0U;
        push_constant_range.size = sizeof(PushConstants);
        layout_desc.push_constant_ranges.push_back(push_constant_range);
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create{};
        shader_create.code_words = generated::k_shadow_depth_2d_vert_spv;
        shader_create.word_count = std::size(generated::k_shadow_depth_2d_vert_spv);
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create);
    }

    if (pipeline_depth_format != depth_format_) {
        pipeline_id = {};
        pipeline_depth_format = depth_format_;
    }

    if (!pipeline_id.IsValid()) {
        pipeline_id = EnsureGraphicsPipeline(context_, pipeline_host_, depth_format_);
    }
}

render::GraphicsPipelineId ShadowRenderer2D::EnsureGraphicsPipeline(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat depth_format_) {
    if (depth_format_ == VK_FORMAT_UNDEFINED) {
        return {};
    }

    const VkPipelineLayout pipeline_layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    const VkShaderModule shader_module = pipeline_host_.GetShaderModule(shader_vertex_id);
    if (pipeline_layout == VK_NULL_HANDLE || shader_module == VK_NULL_HANDLE) {
        return {};
    }

    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_layout;
    desc.use_dynamic_rendering = true;
    desc.rendering.color_attachment_formats.clear();
    desc.rendering.depth_attachment_format = depth_format_;

    render::PipelineShaderStageDesc vertex_stage{};
    vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_stage.module = shader_module;
    vertex_stage.entry_name = "main";
    desc.shader_stages.push_back(vertex_stage);

    desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);

    desc.rasterization.depth_clamp_enable = false;
    desc.rasterization.rasterizer_discard_enable = false;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.depth_bias_enable = true;
    desc.rasterization.depth_bias_constant_factor = 0.0F;
    desc.rasterization.depth_bias_clamp = 0.0F;
    desc.rasterization.depth_bias_slope_factor = 0.0F;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    desc.multisample.sample_shading_enable = false;

    desc.depth_stencil.depth_test_enable = true;
    desc.depth_stencil.depth_write_enable = true;
    desc.depth_stencil.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
    desc.depth_stencil.depth_bounds_test_enable = false;
    desc.depth_stencil.stencil_test_enable = false;

    return pipeline_host_.RegisterGraphicsPipeline(context_, desc);
}

void ShadowRenderer2D::BuildAtlasRequests() {
    atlas_requests.clear();
    if (shadow_components == nullptr || shadow_component_count == 0U) {
        return;
    }

    const std::uint16_t default_layer_count =
        std::max<std::uint16_t>(create_info_cache.runtime_build.atlas_layer_count, 1U);
    for (std::uint32_t shadow_index = 0U; shadow_index < shadow_component_count; ++shadow_index) {
        const ecs::Shadow<ecs::Dim2>& component = shadow_components[shadow_index];
        if (!ecs::ShadowSystem<ecs::Dim2>::IsEnabledForBuild(component)) {
            continue;
        }

        const std::uint32_t namespace_id = component.binding.atlas_namespace_id;
        if (namespace_id == 0U) {
            continue;
        }

        const std::uint16_t width = std::max<std::uint16_t>(component.style.map_width, 1U);
        const std::uint16_t height = std::max<std::uint16_t>(component.style.map_height, 1U);
        const std::uint16_t layer_count = default_layer_count;

        const std::size_t insert_index = LowerBoundAtlasRequestIndex(atlas_requests, namespace_id);
        if (insert_index < atlas_requests.size() && atlas_requests[insert_index].namespace_id == namespace_id) {
            AtlasRequestAggregate& existing = atlas_requests[insert_index];
            existing.width = std::max(existing.width, width);
            existing.height = std::max(existing.height, height);
            existing.layer_count = std::max(existing.layer_count, layer_count);
            continue;
        }

        AtlasRequestAggregate aggregate{};
        aggregate.namespace_id = namespace_id;
        aggregate.width = width;
        aggregate.height = height;
        aggregate.layer_count = layer_count;
        const std::size_t old_size = atlas_requests.size();
        atlas_requests.resize(old_size + 1U);
        for (std::size_t move_index = old_size; move_index > insert_index; --move_index) {
            atlas_requests[move_index] = std::move(atlas_requests[move_index - 1U]);
        }
        atlas_requests[insert_index] = aggregate;
    }
}

void ShadowRenderer2D::RecordAtlasTransition(VkCommandBuffer command_buffer_,
                                             const ShadowAtlasHost::AtlasRecord& atlas_record_,
                                             VkImageLayout old_layout_,
                                             VkImageLayout new_layout_) {
    if (command_buffer_ == VK_NULL_HANDLE || atlas_record_.resource.image == VK_NULL_HANDLE) {
        return;
    }
    if (old_layout_ == new_layout_) {
        return;
    }

    auto stage_access_from_layout = [](VkImageLayout layout_,
                                       VkPipelineStageFlags2& stage_mask_,
                                       VkAccessFlags2& access_mask_) noexcept {
        switch (layout_) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            stage_mask_ = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            access_mask_ = 0U;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            stage_mask_ = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access_mask_ = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            stage_mask_ = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            access_mask_ = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        default:
            stage_mask_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            access_mask_ = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        }
    };

    VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 src_access_mask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkPipelineStageFlags2 dst_stage_mask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 dst_access_mask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    stage_access_from_layout(old_layout_, src_stage_mask, src_access_mask);
    stage_access_from_layout(new_layout_, dst_stage_mask, dst_access_mask);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage_mask;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstStageMask = dst_stage_mask;
    barrier.dstAccessMask = dst_access_mask;
    barrier.oldLayout = old_layout_;
    barrier.newLayout = new_layout_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = atlas_record_.resource.image;
    barrier.subresourceRange.aspectMask = DepthAspectMask(atlas_record_.format);
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = atlas_record_.layer_count;

    VkDependencyInfo dependency_info{};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1U;
    dependency_info.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
    ++stats.atlas_transition_count;
}

void ShadowRenderer2D::RecordOneAtlas(const render::FrameRecordContext& record_context_,
                                      ShadowAtlasHost::AtlasRecord& atlas_record_) {
    const ecs::ShadowViewGpuRecord* view_records =
        ecs::ShadowRuntimeSystem<ecs::Dim2>::ViewRecords(frame_coordinator.RuntimeScratch());
    const std::uint32_t view_count =
        ecs::ShadowRuntimeSystem<ecs::Dim2>::ViewRecordCount(frame_coordinator.RuntimeScratch());
    const ecs::ShadowCasterHeader* headers =
        ecs::ShadowCasterSystem<ecs::Dim2>::Headers(frame_coordinator.CasterScratch());
    const std::uint32_t header_count =
        ecs::ShadowCasterSystem<ecs::Dim2>::HeaderCount(frame_coordinator.CasterScratch());
    const std::uint32_t* caster_indices =
        ecs::ShadowCasterSystem<ecs::Dim2>::CasterIndices(frame_coordinator.CasterScratch());
    const std::uint32_t caster_index_count =
        ecs::ShadowCasterSystem<ecs::Dim2>::CasterIndexCount(frame_coordinator.CasterScratch());
    if (view_records == nullptr ||
        headers == nullptr ||
        caster_indices == nullptr ||
        atlas_record_.layer_views.empty()) {
        return;
    }

    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    const VkPipeline pipeline = pipeline_host->GetGraphicsPipeline(pipeline_id);
    if (pipeline_layout == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBuffer command_buffer = record_context_.command_buffer;
    RecordAtlasTransition(command_buffer,
                          atlas_record_,
                          atlas_record_.current_layout == VK_IMAGE_LAYOUT_UNDEFINED
                              ? VK_IMAGE_LAYOUT_UNDEFINED
                              : atlas_record_.current_layout,
                          VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    atlas_record_.current_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    ShadowRenderer2DMcVector<std::uint8_t> layer_cleared{};
    layer_cleared.resize(atlas_record_.layer_count, 0U);

    for (std::uint32_t header_index = 0U; header_index < header_count; ++header_index) {
        const ecs::ShadowCasterHeader& header = headers[header_index];
        if (header.view_index >= view_count) {
            ++stats.skipped_out_of_range_count;
            continue;
        }
        if (header.offset + header.count > caster_index_count) {
            ++stats.skipped_out_of_range_count;
            continue;
        }

        const ecs::ShadowViewGpuRecord& view_record = view_records[header.view_index];
        if (view_record.atlas_namespace_id != atlas_record_.namespace_id) {
            continue;
        }
        if (view_record.atlas_layer >= atlas_record_.layer_count) {
            ++stats.skipped_out_of_range_count;
            continue;
        }
        if (view_record.atlas_width == 0U || view_record.atlas_height == 0U) {
            continue;
        }

        const std::uint32_t layer_index = view_record.atlas_layer;
        const VkImageView layer_view = atlas_record_.layer_views[layer_index];
        if (layer_view == VK_NULL_HANDLE) {
            ++stats.skipped_out_of_range_count;
            continue;
        }

        const bool clear_layer = create_info_cache.clear_atlas_each_frame && layer_cleared[layer_index] == 0U;
        layer_cleared[layer_index] = 1U;

        VkRenderingAttachmentInfo depth_attachment{};
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = layer_view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment.resolveImageView = VK_NULL_HANDLE;
        depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.loadOp = clear_layer ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue.depthStencil.depth = 1.0F;
        depth_attachment.clearValue.depthStencil.stencil = 0U;

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.offset = VkOffset2D{0, 0};
        rendering_info.renderArea.extent = VkExtent2D{
            .width = atlas_record_.width,
            .height = atlas_record_.height,
        };
        rendering_info.layerCount = 1U;
        rendering_info.viewMask = 0U;
        rendering_info.colorAttachmentCount = 0U;
        rendering_info.pColorAttachments = nullptr;
        rendering_info.pDepthAttachment = &depth_attachment;
        rendering_info.pStencilAttachment = DepthFormatHasStencil(atlas_record_.format)
            ? &depth_attachment
            : nullptr;

        vkCmdBeginRendering(command_buffer, &rendering_info);
        ++stats.atlas_layer_draw_pass_count;

        const VkViewport viewport{
            .x = static_cast<float>(view_record.atlas_x),
            .y = static_cast<float>(view_record.atlas_y),
            .width = static_cast<float>(view_record.atlas_width),
            .height = static_cast<float>(view_record.atlas_height),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        const VkRect2D scissor{
            .offset = VkOffset2D{
                static_cast<std::int32_t>(view_record.atlas_x),
                static_cast<std::int32_t>(view_record.atlas_y),
            },
            .extent = VkExtent2D{
                .width = view_record.atlas_width,
                .height = view_record.atlas_height,
            },
        };
        vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
        vkCmdSetScissor(command_buffer, 0U, 1U, &scissor);
        vkCmdSetDepthBias(command_buffer,
                          view_record.depth_bias + view_record.normal_bias,
                          0.0F,
                          view_record.slope_scaled_bias);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for (std::uint32_t local_index = 0U; local_index < header.count; ++local_index) {
            const std::uint32_t caster_index = caster_indices[header.offset + local_index];
            if (caster_index >= caster_count) {
                ++stats.skipped_out_of_range_count;
                continue;
            }
            const ecs::Bounds<ecs::Dim2>& bounds = caster_bounds[caster_index];
            if (!HasValidBounds(bounds)) {
                ++stats.skipped_invalid_bounds_count;
                continue;
            }

            PushConstants push_constants{};
            push_constants.view_projection = view_record.view_projection_matrix;
            push_constants.rect_min_x = bounds.runtime.world_min.x;
            push_constants.rect_min_y = bounds.runtime.world_min.y;
            push_constants.rect_max_x = bounds.runtime.world_max.x;
            push_constants.rect_max_y = bounds.runtime.world_max.y;
            vkCmdPushConstants(command_buffer,
                               pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0U,
                               sizeof(PushConstants),
                               &push_constants);
            vkCmdDraw(command_buffer, 6U, 1U, 0U, 0U);
            ++stats.draw_call_count;
        }

        vkCmdEndRendering(command_buffer);
    }

    RecordAtlasTransition(command_buffer,
                          atlas_record_,
                          VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
    atlas_record_.current_layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
}

} // namespace vr::shadow
