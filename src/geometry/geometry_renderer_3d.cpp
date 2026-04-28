#include "vr/geometry/geometry_renderer_3d.hpp"

#include "vr/geometry/generated/geometry_3d_frag_spv.hpp"
#include "vr/geometry/generated/geometry_3d_vert_spv.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_context.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::geometry {

bool GeometryRenderer3D::IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool GeometryRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags GeometryRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags;
}

VkFormat GeometryRenderer3D::ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (const VkFormat format : fallback_formats) {
        if (IsDepthFormatSupported(context_, format)) {
            return format;
        }
    }
    throw std::runtime_error("GeometryRenderer3D failed to resolve usable depth format");
}

std::size_t GeometryRenderer3D::PipelineModeIndex(PipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t GeometryRenderer3D::TopologyModeIndex(TopologyMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t GeometryRenderer3D::CullModeIndex(CullMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t GeometryRenderer3D::LowerBoundMaterialSetIndex(
    const GeometryRenderer3DMcVector<MaterialSetEntry>& entries_,
    std::uint32_t material_id_) noexcept {
    std::size_t first = 0U;
    std::size_t count = entries_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (entries_[it].material_id < material_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

std::size_t GeometryRenderer3D::LowerBoundResolvedMaterialIndex(
    const GeometryRenderer3DMcVector<ResolvedMaterialEntry>& entries_,
    std::uint32_t material_id_) noexcept {
    std::size_t first = 0U;
    std::size_t count = entries_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (entries_[it].material_id < material_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

GeometryRenderer3D::PipelineMode GeometryRenderer3D::ResolvePipelineMode(
    const ecs::Geometry3DDrawBatch& batch_,
    bool use_depth_) noexcept {
    if (!use_depth_ || (batch_.params & 0x1U) == 0U) {
        return PipelineMode::no_depth;
    }
    if ((batch_.params & 0x2U) != 0U) {
        return PipelineMode::depth_read_write;
    }
    return PipelineMode::depth_read;
}

GeometryRenderer3D::TopologyMode GeometryRenderer3D::ResolveTopologyMode(
    VkPrimitiveTopology mesh_topology_,
    const ecs::Geometry3DDrawBatch& batch_) noexcept {
    switch (mesh_topology_) {
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        return TopologyMode::triangles;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
        return TopologyMode::lines;
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
        return TopologyMode::points;
    default:
        break;
    }

    const std::uint32_t topology_bits = (batch_.params >> 5U) & 0x3U;
    if (topology_bits == 1U) {
        return TopologyMode::lines;
    }
    if (topology_bits == 2U) {
        return TopologyMode::points;
    }
    return TopologyMode::triangles;
}

GeometryRenderer3D::CullMode GeometryRenderer3D::ResolveCullMode(const ecs::Geometry3DDrawBatch& batch_) noexcept {
    const bool double_sided = (batch_.params & 0x4U) != 0U;
    return double_sided ? CullMode::none : CullMode::back;
}

void GeometryRenderer3D::Initialize(const GeometryRenderer3DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::GeometryRuntimeSystem<ecs::Dim3>::Reserve(runtime_scratch,
                                                       create_info_cache.reserve_component_count,
                                                       create_info_cache.reserve_instance_count);
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch,
                                               create_info_cache.reserve_component_count);
    }

    if (!std::isfinite(create_info_cache.clear_depth_value)) {
        create_info_cache.clear_depth_value = 1.0F;
    }
    create_info_cache.clear_depth_value = std::clamp(create_info_cache.clear_depth_value, 0.0F, 1.0F);

    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& mode_pipelines : pipeline_ids) {
        for (auto& topology_pipelines : mode_pipelines) {
            for (auto& pipeline_id : topology_pipelines) {
                pipeline_id = {};
            }
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    image_initialized.clear();
    frame_material_sets.clear();
    resolved_materials.clear();
    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    descriptor_image_write_scratch.reserve(1U);
    if (create_info_cache.reserve_material_set_count > 0U) {
        resolved_materials.reserve(create_info_cache.reserve_material_set_count);
    }
    runtime_stats = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    culling_stats = {};
    instance_range = {};
    fallback_material_image = {};
    fallback_material_sampler_id = {};
    fallback_material_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    material_host_revision_seen = 0U;
    image_host_revision_seen = 0U;
    pending_appearance_dirty_component_indices = nullptr;
    pending_appearance_dirty_component_count = 0U;
    stats = {};
    initialized = true;
}

void GeometryRenderer3D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);
    image_initialized.clear();
    for (auto& frame_sets : frame_material_sets) {
        frame_sets.clear();
    }
    frame_material_sets.clear();
    resolved_materials.clear();

    if (fallback_material_image.image != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyImage(context_, fallback_material_image);
    }
    fallback_material_image = {};
    fallback_material_sampler_id = {};
    fallback_material_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& mode_pipelines : pipeline_ids) {
        for (auto& topology_pipelines : mode_pipelines) {
            for (auto& pipeline_id : topology_pipelines) {
                pipeline_id = {};
            }
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    geometry_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    appearance_components = nullptr;
    appearance_component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    geometry_resource_host = nullptr;
    geometry_upload_host = nullptr;
    geometry_material_host = nullptr;
    geometry_image_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    instance_range = {};

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.cache = {};
    runtime_stats = {};
    appearance_runtime_scratch.gpu_records.clear();
    appearance_runtime_scratch.upload_ranges.clear();
    appearance_runtime_scratch.dirty_component_indices.clear();
    appearance_runtime_scratch.handle_generations.clear();
    appearance_runtime_scratch.cache = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    culling_stats = {};
    stats = {};

    descriptor_image_write_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();

    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    material_host_revision_seen = 0U;
    image_host_revision_seen = 0U;
    pending_appearance_dirty_component_indices = nullptr;
    pending_appearance_dirty_component_count = 0U;
    initialized = false;
}

void GeometryRenderer3D::SetHosts(GeometryResourceHost* resource_host_,
                                  GeometryUploadHost* upload_host_) noexcept {
    geometry_resource_host = resource_host_;
    geometry_upload_host = upload_host_;
}

void GeometryRenderer3D::SetMaterialHosts(GeometryMaterialHost* material_host_,
                                          GeometryImageHost* image_host_) noexcept {
    if (geometry_material_host != material_host_ || geometry_image_host != image_host_) {
        resolved_materials.clear();
        material_host_revision_seen = 0U;
        image_host_revision_seen = 0U;
    }
    geometry_material_host = material_host_;
    geometry_image_host = image_host_;
}

void GeometryRenderer3D::SetSceneData(ecs::Geometry<ecs::Dim3>* geometry_components_,
                                      ecs::Transform<ecs::Dim3>* transforms_,
                                      std::uint32_t component_count_,
                                      ecs::Camera<ecs::Dim3>* camera_component_,
                                      ecs::Transform<ecs::Dim3>* camera_transform_,
                                      ecs::Bounds<ecs::Dim3>* bounds_components_) noexcept {
    geometry_components = geometry_components_;
    transforms = transforms_;
    component_count = component_count_;
    camera_component = camera_component_;
    camera_transform = camera_transform_;
    bounds_components = bounds_components_;
    if (component_count_ > 0U) {
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch, component_count_);
    }
}

void GeometryRenderer3D::SetAppearanceData(ecs::Appearance<ecs::Dim3>* appearance_components_,
                                           std::uint32_t appearance_component_count_) noexcept {
    appearance_components = appearance_components_;
    appearance_component_count = appearance_component_count_;
    if (appearance_component_count_ > 0U) {
        ecs::AppearanceRuntimeSystem<ecs::Dim3>::Reserve(appearance_runtime_scratch,
                                                         appearance_component_count_);
    }
}

void GeometryRenderer3D::SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                                std::uint32_t dirty_component_count_) noexcept {
    pending_appearance_dirty_component_indices = dirty_component_indices_;
    pending_appearance_dirty_component_count = dirty_component_count_;
}

void GeometryRenderer3D::PrepareFrame(const render::RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr ||
        prepare_context_.upload_host == nullptr ||
        prepare_context_.descriptor_host == nullptr ||
        prepare_context_.pipeline_host == nullptr ||
        prepare_context_.gpu_memory_host == nullptr ||
        prepare_context_.sampler_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame missing runtime dependencies");
    }
    if (geometry_resource_host == nullptr || !geometry_resource_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryResourceHost");
    }
    if (geometry_upload_host == nullptr || !geometry_upload_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryUploadHost");
    }

    context = prepare_context_.context;
    upload_host = prepare_context_.upload_host;
    descriptor_host = prepare_context_.descriptor_host;
    pipeline_host = prepare_context_.pipeline_host;
    gpu_memory_host = prepare_context_.gpu_memory_host;
    sampler_host = prepare_context_.sampler_host;
    active_frame_index = prepare_context_.frame_index;
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_context_.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen, prepare_context_.completed_submit_value);

    CollectRetiredDepthResources(*context, completed_submit_value_seen);
    geometry_resource_host->BeginFrame(*context, completed_submit_value_seen);
    geometry_upload_host->BeginFrame(*context, active_frame_index);
    if (geometry_image_host != nullptr && geometry_image_host->IsInitialized()) {
        geometry_image_host->BeginFrame(*context, completed_submit_value_seen);
    }

    std::uint32_t material_revision_now = 0U;
    if (geometry_material_host != nullptr && geometry_material_host->IsInitialized()) {
        material_revision_now = geometry_material_host->Stats().revision;
    }
    std::uint32_t image_revision_now = 0U;
    if (geometry_image_host != nullptr && geometry_image_host->IsInitialized()) {
        image_revision_now = geometry_image_host->Stats().revision;
    }
    if (material_revision_now != material_host_revision_seen ||
        image_revision_now != image_host_revision_seen) {
        resolved_materials.clear();
        material_host_revision_seen = material_revision_now;
        image_host_revision_seen = image_revision_now;
    }

    EnsureFallbackMaterialResources(*context);
    EnsureMaterialPipelineObjects(*context, *descriptor_host);

    if (active_frame_index >= frame_material_sets.size()) {
        frame_material_sets.resize(active_frame_index + 1U);
    }
    frame_material_sets[active_frame_index].clear();
    if (create_info_cache.reserve_material_set_count > 0U &&
        frame_material_sets[active_frame_index].capacity() < create_info_cache.reserve_material_set_count) {
        frame_material_sets[active_frame_index].reserve(create_info_cache.reserve_material_set_count);
    }

    stats = {};
    stats.component_count = component_count;
    stats.appearance_component_count = appearance_component_count;
    stats.material_resolve_cache_entry_count = static_cast<std::uint32_t>(resolved_materials.size());
    instance_range = {};
    culling_stats = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};

    if (appearance_components != nullptr && appearance_component_count > 0U) {
        ecs::AppearanceRuntimeBuildHint appearance_build_hint{};
        appearance_build_hint.dirty_component_indices = pending_appearance_dirty_component_indices;
        appearance_build_hint.dirty_component_count = pending_appearance_dirty_component_count;
        appearance_build_hint.use_dirty_component_indices =
            (pending_appearance_dirty_component_indices != nullptr &&
             pending_appearance_dirty_component_count > 0U)
                ? 1U
                : 0U;

        appearance_runtime_stats = ecs::AppearanceRuntimeSystem<ecs::Dim3>::Build(
            appearance_components,
            appearance_component_count,
            appearance_runtime_scratch,
            ecs::AppearanceRuntimeSystem<ecs::Dim3>::DefaultPipelinePolicy(),
            ecs::AppearanceRuntimeSystem<ecs::Dim3>::DefaultSortPolicy(),
            ecs::AppearanceRuntimeSystem<ecs::Dim3>::DefaultBuildConfig(),
            appearance_build_hint);

        stats.appearance_visible_count = appearance_runtime_stats.visible_count;
        stats.appearance_updated_record_count = appearance_runtime_stats.updated_record_count;
        stats.appearance_cache_reused = appearance_runtime_stats.full_rebuild == 0U;
    }

    if (geometry_components == nullptr || component_count == 0U) {
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        runtime_stats = {};
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        pending_appearance_dirty_component_indices = nullptr;
        pending_appearance_dirty_component_count = 0U;
        return;
    }

    if (appearance_components != nullptr && appearance_component_count > 0U) {
        appearance_link_stats = ecs::AppearanceLinkSystem<ecs::Dim3>::ApplyToGeometryAligned(
            geometry_components,
            component_count,
            appearance_components,
            appearance_component_count);
        stats.appearance_link_scanned_count = appearance_link_stats.scanned_count;
        stats.appearance_link_updated_count = appearance_link_stats.updated_count;
    }

    ecs::Geometry3DRuntimeBuildHint build_hint{};
    if (bounds_components != nullptr && camera_component != nullptr) {
        const ecs::CullingBuildOptions culling_options{
            .enable_culling_mask_filter = true,
            .enable_frustum_culling = true,
            .enable_aabb_refine = true,
            .write_visibility_bits = false
        };
        culling_stats = ecs::CullingSystem<ecs::Dim3>::BuildVisibleSet(bounds_components,
                                                                        component_count,
                                                                        camera_component,
                                                                        culling_scratch,
                                                                        culling_options);
        build_hint.visible_component_indices = culling_scratch.visible_indices.data();
        build_hint.visible_component_count = culling_stats.visible_count;
        build_hint.use_visible_component_indices = 1U;
        build_hint.external_visible_set_signature = culling_stats.visible_set_signature;
        build_hint.use_external_visible_set_signature = 1U;

        stats.used_bounds_culling = true;
        stats.culling_input_count = culling_stats.input_count;
        stats.culling_visible_count = culling_stats.visible_count;
        stats.culling_culled_count = culling_stats.culled_by_mask_count +
                                     culling_stats.culled_by_frustum_count +
                                     culling_stats.culled_by_invalid_bounds_count;
        stats.culling_mask_reject_count = culling_stats.culled_by_mask_count;
        stats.culling_frustum_reject_count = culling_stats.culled_by_frustum_count;
        stats.culling_invalid_bounds_count = culling_stats.culled_by_invalid_bounds_count;
        stats.culling_plane_test_count = culling_stats.plane_test_count;
    }

    runtime_stats = ecs::GeometryRuntimeSystem<ecs::Dim3>::Build(geometry_components,
                                                                  transforms,
                                                                  component_count,
                                                                  runtime_scratch,
                                                                  create_info_cache.runtime_build,
                                                                  build_hint);
    stats.visible_component_count = runtime_stats.batch.visible_count;
    stats.instance_count = runtime_stats.emitted_instance_count;
    stats.draw_batch_count = runtime_stats.emitted_batch_count;
    stats.depth_test_batch_count = runtime_stats.depth_test_batch_count;
    stats.depth_write_batch_count = runtime_stats.depth_write_batch_count;
    stats.shadow_cast_batch_count = runtime_stats.shadow_cast_batch_count;
    stats.cache_reused = runtime_stats.cache_reused;
    stats.transform_only_update = runtime_stats.transform_only_update;

    if (!runtime_scratch.instances.empty()) {
        const std::uint64_t upload_revision =
            runtime_stats.geometry_signature ^ (runtime_stats.transform_signature * 0x9e3779b97f4a7c15ULL);
        instance_range = geometry_upload_host->Upload3DInstances(*context,
                                                                 *upload_host,
                                                                 active_frame_index,
                                                                 runtime_scratch.instances.data(),
                                                                 static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                                                                 upload_revision);
        if (instance_range.uploaded) {
            stats.uploaded_instance_count = instance_range.element_count;
            stats.uploaded_bytes = instance_range.size_bytes;
        }
    }

    if (swapchain_format != VK_FORMAT_UNDEFINED) {
        const bool use_depth = create_info_cache.enable_depth;
        const VkFormat active_depth_format = use_depth
            ? ResolveDepthFormat(*context, create_info_cache.preferred_depth_format)
            : VK_FORMAT_UNDEFINED;
        EnsurePipelineObjects(*context, *pipeline_host, swapchain_format, active_depth_format);

        if (create_info_cache.prewarm_common_pipelines) {
            PrewarmCommonPipelines(*context, *pipeline_host, swapchain_format, active_depth_format);
        }
        if (create_info_cache.compile_required_pipelines_in_prepare) {
            CompileRequiredPipelinesForCurrentFrame(*context, *pipeline_host, swapchain_format, active_depth_format);
        }
    }

    pending_appearance_dirty_component_indices = nullptr;
    pending_appearance_dirty_component_count = 0U;
}

void GeometryRenderer3D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::Record called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr || geometry_resource_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::Record called before PrepareFrame");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE ||
        record_context_.image == VK_NULL_HANDLE ||
        record_context_.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("GeometryRenderer3D::Record requires valid frame context image handles");
    }
    if (record_context_.extent.width == 0U || record_context_.extent.height == 0U) {
        throw std::runtime_error("GeometryRenderer3D::Record received zero-sized swapchain extent");
    }

    if (record_context_.image_index >= image_initialized.size()) {
        const std::size_t previous_size = image_initialized.size();
        image_initialized.resize(record_context_.image_index + 1U);
        for (std::size_t i = previous_size; i < image_initialized.size(); ++i) {
            image_initialized[i] = 0U;
        }
    }
    const bool has_previous_content = image_initialized[record_context_.image_index] != 0U;
    RecordImageTransitionToColorAttachment(record_context_, has_previous_content);

    bool use_depth_attachment = false;
    if (create_info_cache.enable_depth) {
        if (depth_format == VK_FORMAT_UNDEFINED) {
            depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
        }
        EnsureDepthResources(*context,
                             static_cast<std::uint32_t>(image_initialized.size()),
                             record_context_.extent);
        use_depth_attachment = record_context_.image_index < depth_images.size() &&
                              depth_images[record_context_.image_index].default_view != VK_NULL_HANDLE;
    }

    EnsurePipelineObjects(*context,
                          *pipeline_host,
                          record_context_.format,
                          use_depth_attachment ? depth_format : VK_FORMAT_UNDEFINED);

    if (use_depth_attachment) {
        if (record_context_.image_index >= depth_image_initialized.size()) {
            const std::size_t old_size = depth_image_initialized.size();
            depth_image_initialized.resize(record_context_.image_index + 1U);
            for (std::size_t i = old_size; i < depth_image_initialized.size(); ++i) {
                depth_image_initialized[i] = 0U;
            }
        }
        RecordDepthTransitionToAttachment(record_context_.command_buffer,
                                          depth_images[record_context_.image_index],
                                          depth_image_initialized[record_context_.image_index] != 0U);
    }

    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = record_context_.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.resolveImageView = VK_NULL_HANDLE;
    color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.loadOp = (create_info_cache.clear_swapchain || !has_previous_content)
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = create_info_cache.clear_color;

    VkRenderingAttachmentInfo depth_attachment{};
    if (use_depth_attachment) {
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = depth_images[record_context_.image_index].default_view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment.resolveImageView = VK_NULL_HANDLE;
        depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.loadOp = create_info_cache.clear_depth
            ? VK_ATTACHMENT_LOAD_OP_CLEAR
            : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue.depthStencil.depth = create_info_cache.clear_depth_value;
        depth_attachment.clearValue.depthStencil.stencil = create_info_cache.clear_stencil_value;
    }

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = VkOffset2D{0, 0};
    rendering_info.renderArea.extent = record_context_.extent;
    rendering_info.layerCount = 1U;
    rendering_info.colorAttachmentCount = 1U;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = use_depth_attachment ? &depth_attachment : nullptr;
    rendering_info.pStencilAttachment = nullptr;
    vkCmdBeginRendering(record_context_.command_buffer, &rendering_info);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(record_context_.extent.width);
    viewport.height = static_cast<float>(record_context_.extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = record_context_.extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    FramePushConstants frame_push_constants{};
    if (camera_component != nullptr) {
        frame_push_constants.view_projection = camera_component->runtime.view_projection_matrix;
    } else {
        frame_push_constants.view_projection = ecs::spatial_math::IdentityMatrix4x4();
    }
    frame_push_constants.directional_light_x = create_info_cache.directional_light_x;
    frame_push_constants.directional_light_y = create_info_cache.directional_light_y;
    frame_push_constants.directional_light_z = create_info_cache.directional_light_z;
    frame_push_constants.directional_light_intensity = std::max(0.0F, create_info_cache.directional_light_intensity);

    const VkPipelineLayout pipeline_layout = pipeline_layout_id.IsValid()
        ? pipeline_host->GetPipelineLayout(pipeline_layout_id)
        : VK_NULL_HANDLE;
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkCmdPushConstants(record_context_.command_buffer,
                           pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0U,
                           sizeof(FramePushConstants),
                           &frame_push_constants);
    }

    render::GraphicsPipelineId active_pipeline_id{};
    VkBuffer active_vertex_buffer = VK_NULL_HANDLE;
    VkBuffer active_index_buffer = VK_NULL_HANDLE;
    VkDescriptorSet active_descriptor_set = VK_NULL_HANDLE;
    std::uint32_t active_material_id = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t cached_material_id = std::numeric_limits<std::uint32_t>::max();
    VkDescriptorSet cached_material_descriptor_set = VK_NULL_HANDLE;
    MaterialPushConstants cached_material_push_constants{};
    std::uint32_t cached_geometry_id = 0U;
    const GeometryResourceHost::MeshRecord* cached_mesh = nullptr;

    if (instance_range.buffer != VK_NULL_HANDLE && !runtime_scratch.draw_batches.empty()) {
        const VkBuffer instance_vertex_buffer = instance_range.buffer;
        const VkDeviceSize instance_vertex_offset = instance_range.offset;
        vkCmdBindVertexBuffers(record_context_.command_buffer,
                               1U,
                               1U,
                               &instance_vertex_buffer,
                               &instance_vertex_offset);

        for (const ecs::Geometry3DDrawBatch& batch : runtime_scratch.draw_batches) {
            if (batch.instance_count == 0U) {
                continue;
            }

            const GeometryResourceHost::MeshRecord* mesh = nullptr;
            if (cached_mesh != nullptr && cached_geometry_id == batch.geometry_id) {
                mesh = cached_mesh;
            } else {
                mesh = geometry_resource_host->FindMesh(batch.geometry_id);
                cached_mesh = mesh;
                cached_geometry_id = batch.geometry_id;
            }

            if (mesh == nullptr || mesh->index_buffer.buffer == VK_NULL_HANDLE ||
                mesh->vertex_buffer.buffer == VK_NULL_HANDLE || mesh->submeshes.empty()) {
                ++stats.skipped_batch_count;
                continue;
            }

            const std::uint32_t submesh_index = std::min(batch.submesh_index,
                                                         static_cast<std::uint32_t>(mesh->submeshes.size() - 1U));
            const GeometrySubmeshRange& submesh = mesh->submeshes[submesh_index];
            if (submesh.index_count == 0U) {
                ++stats.skipped_batch_count;
                continue;
            }

            const PipelineMode mode = ResolvePipelineMode(batch, use_depth_attachment);
            const TopologyMode topology_mode = ResolveTopologyMode(mesh->topology, batch);
            const CullMode cull_mode = ResolveCullMode(batch);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                  *pipeline_host,
                                                                                  record_context_.format,
                                                                                  use_depth_attachment ? depth_format : VK_FORMAT_UNDEFINED,
                                                                                  mode,
                                                                                  topology_mode,
                                                                                  cull_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                continue;
            }

            MaterialPushConstants material_push_constants{};
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
            if (batch.material_id == cached_material_id) {
                descriptor_set = cached_material_descriptor_set;
                material_push_constants = cached_material_push_constants;
            } else {
                descriptor_set = AcquireMaterialDescriptorSet(active_frame_index,
                                                              batch.material_id,
                                                              &material_push_constants);
                if (descriptor_set != VK_NULL_HANDLE) {
                    cached_material_id = batch.material_id;
                    cached_material_descriptor_set = descriptor_set;
                    cached_material_push_constants = material_push_constants;
                }
            }
            if (descriptor_set == VK_NULL_HANDLE) {
                ++stats.skipped_batch_count;
                continue;
            }

            if (active_pipeline_id.value != pipeline_id.value) {
                vkCmdBindPipeline(record_context_.command_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                active_pipeline_id = pipeline_id;
            }

            if (active_descriptor_set != descriptor_set) {
                vkCmdBindDescriptorSets(record_context_.command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_layout,
                                        0U,
                                        1U,
                                        &descriptor_set,
                                        0U,
                                        nullptr);
                active_descriptor_set = descriptor_set;
                ++stats.descriptor_set_bind_count;
            }

            if (pipeline_layout != VK_NULL_HANDLE && active_material_id != batch.material_id) {
                vkCmdPushConstants(record_context_.command_buffer,
                                   pipeline_layout,
                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                                   static_cast<std::uint32_t>(offsetof(PushConstants, material)),
                                   sizeof(MaterialPushConstants),
                                   &material_push_constants);
                active_material_id = batch.material_id;
                ++stats.material_push_constant_update_count;
            }

            if (active_vertex_buffer != mesh->vertex_buffer.buffer) {
                const VkBuffer vertex_buffer = mesh->vertex_buffer.buffer;
                const VkDeviceSize vertex_offset = 0U;
                vkCmdBindVertexBuffers(record_context_.command_buffer,
                                       0U,
                                       1U,
                                       &vertex_buffer,
                                       &vertex_offset);
                active_vertex_buffer = mesh->vertex_buffer.buffer;
            }

            if (active_index_buffer != mesh->index_buffer.buffer) {
                vkCmdBindIndexBuffer(record_context_.command_buffer,
                                     mesh->index_buffer.buffer,
                                     0U,
                                     VK_INDEX_TYPE_UINT32);
                active_index_buffer = mesh->index_buffer.buffer;
            }

            vkCmdDrawIndexed(record_context_.command_buffer,
                             submesh.index_count,
                             batch.instance_count,
                             submesh.first_index,
                             submesh.vertex_offset,
                             batch.instance_begin);
            ++stats.draw_call_count;
        }
    }

    if (active_frame_index < frame_material_sets.size()) {
        stats.material_set_count = static_cast<std::uint32_t>(frame_material_sets[active_frame_index].size());
    }
    stats.material_resolve_cache_entry_count = static_cast<std::uint32_t>(resolved_materials.size());

    vkCmdEndRendering(record_context_.command_buffer);
    RecordImageTransitionToPresent(record_context_);
    image_initialized[record_context_.image_index] = 1U;
    if (use_depth_attachment && record_context_.image_index < depth_image_initialized.size()) {
        depth_image_initialized[record_context_.image_index] = 1U;
    }
}

void GeometryRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void GeometryRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_,
                                              std::uint64_t last_submitted_value_,
                                              std::uint64_t completed_submit_value_) {
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);

    if (context != nullptr && create_info_cache.enable_depth) {
        RetireDepthResources(last_submitted_value_seen);
        CollectRetiredDepthResources(*context, completed_submit_value_seen);
    }

    image_initialized.resize(image_count_);
    for (auto& value : image_initialized) {
        value = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;
}

bool GeometryRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const GeometryRenderer3DStats& GeometryRenderer3D::Stats() const noexcept {
    return stats;
}

void GeometryRenderer3D::EnsureMaterialPipelineObjects(VulkanContext& context_,
                                                       render::DescriptorHost& descriptor_host_) {
    if (!descriptor_layout_id.IsValid()) {
        render::DescriptorSetLayoutDesc layout_desc{};
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0U;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1U;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = nullptr;
        layout_desc.bindings.push_back(binding);
        descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
    }
}

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

    EnsureMaterialPipelineObjects(context_, *descriptor_host);

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
        render::PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(descriptor_host->GetLayout(descriptor_layout_id));
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0U,
            .size = sizeof(FramePushConstants)
        });
        layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
        for (auto& mode_pipelines : pipeline_ids) {
            for (auto& topology_pipelines : mode_pipelines) {
                for (auto& pipeline_id : topology_pipelines) {
                    pipeline_id = {};
                }
            }
        }
    }
}

render::GraphicsPipelineId GeometryRenderer3D::EnsurePipelineForMode(VulkanContext& context_,
                                                                     render::PipelineHost& pipeline_host_,
                                                                     VkFormat color_format_,
                                                                     VkFormat depth_format_,
                                                                     PipelineMode mode_,
                                                                     TopologyMode topology_mode_,
                                                                     CullMode cull_mode_) {
    const std::size_t mode_index = PipelineModeIndex(mode_);
    const std::size_t topology_index = TopologyModeIndex(topology_mode_);
    const std::size_t cull_index = CullModeIndex(cull_mode_);
    if (mode_index >= pipeline_ids.size()) {
        throw std::out_of_range("GeometryRenderer3D pipeline mode out of range");
    }
    if (topology_index >= pipeline_ids[mode_index].size()) {
        throw std::out_of_range("GeometryRenderer3D topology mode out of range");
    }
    if (cull_index >= pipeline_ids[mode_index][topology_index].size()) {
        throw std::out_of_range("GeometryRenderer3D cull mode out of range");
    }
    if (pipeline_ids[mode_index][topology_index][cull_index].IsValid()) {
        return pipeline_ids[mode_index][topology_index][cull_index];
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
    desc.vertex_input.attributes.push_back({.location = 0U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0U});
    desc.vertex_input.attributes.push_back({.location = 1U, .binding = 0U, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12U});
    desc.vertex_input.attributes.push_back({.location = 2U, .binding = 0U, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 24U});
    desc.vertex_input.attributes.push_back({.location = 3U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0U});
    desc.vertex_input.attributes.push_back({.location = 4U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 16U});
    desc.vertex_input.attributes.push_back({.location = 5U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 32U});
    desc.vertex_input.attributes.push_back({.location = 6U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 48U});
    desc.vertex_input.attributes.push_back({.location = 7U, .binding = 1U, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 96U});
    desc.vertex_input.attributes.push_back({.location = 8U, .binding = 1U, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 112U});
    desc.vertex_input.attributes.push_back({.location = 9U, .binding = 1U, .format = VK_FORMAT_R32_UINT, .offset = 116U});

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

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                           VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    desc.color_blend.attachments.push_back(blend);

    const render::GraphicsPipelineId pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, desc);
    pipeline_ids[mode_index][topology_index][cull_index] = pipeline_id;
    return pipeline_id;
}

void GeometryRenderer3D::PrewarmCommonPipelines(VulkanContext& context_,
                                                render::PipelineHost& pipeline_host_,
                                                VkFormat color_format_,
                                                VkFormat depth_format_) {
    auto warm_variant = [&](PipelineMode mode_, TopologyMode topology_, CullMode cull_) {
        if (mode_ != PipelineMode::no_depth && depth_format_ == VK_FORMAT_UNDEFINED) {
            return;
        }
        const std::size_t mode_index = PipelineModeIndex(mode_);
        const std::size_t topology_index = TopologyModeIndex(topology_);
        const std::size_t cull_index = CullModeIndex(cull_);
        if (pipeline_ids[mode_index][topology_index][cull_index].IsValid()) {
            return;
        }
        (void)EnsurePipelineForMode(context_,
                                    pipeline_host_,
                                    color_format_,
                                    depth_format_,
                                    mode_,
                                    topology_,
                                    cull_);
        ++stats.prewarmed_pipeline_count;
    };

    warm_variant(PipelineMode::no_depth, TopologyMode::triangles, CullMode::back);
    if (create_info_cache.enable_depth) {
        warm_variant(PipelineMode::depth_read_write, TopologyMode::triangles, CullMode::back);
        if (create_info_cache.prewarm_depth_read_variant) {
            warm_variant(PipelineMode::depth_read, TopologyMode::triangles, CullMode::back);
        }
    }

    if (create_info_cache.prewarm_double_sided_variant) {
        warm_variant(PipelineMode::no_depth, TopologyMode::triangles, CullMode::none);
        if (create_info_cache.enable_depth) {
            warm_variant(PipelineMode::depth_read_write, TopologyMode::triangles, CullMode::none);
            if (create_info_cache.prewarm_depth_read_variant) {
                warm_variant(PipelineMode::depth_read, TopologyMode::triangles, CullMode::none);
            }
        }
    }

    if (create_info_cache.prewarm_line_and_point_variants) {
        warm_variant(PipelineMode::no_depth, TopologyMode::lines, CullMode::none);
        warm_variant(PipelineMode::no_depth, TopologyMode::points, CullMode::none);
        if (create_info_cache.enable_depth) {
            warm_variant(PipelineMode::depth_read, TopologyMode::lines, CullMode::none);
            warm_variant(PipelineMode::depth_read, TopologyMode::points, CullMode::none);
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
        const PipelineMode mode = ResolvePipelineMode(batch, use_depth);
        const TopologyMode topology = ResolveTopologyMode(mesh->topology, batch);
        const CullMode cull = ResolveCullMode(batch);
        const std::size_t mode_index = PipelineModeIndex(mode);
        const std::size_t topology_index = TopologyModeIndex(topology);
        const std::size_t cull_index = CullModeIndex(cull);
        const bool already_compiled = pipeline_ids[mode_index][topology_index][cull_index].IsValid();
        (void)EnsurePipelineForMode(context_,
                                    pipeline_host_,
                                    color_format_,
                                    depth_format_,
                                    mode,
                                    topology,
                                    cull);
        if (!already_compiled) {
            ++stats.prepare_compiled_pipeline_count;
        }
    }
}

void GeometryRenderer3D::EnsureFallbackMaterialResources(VulkanContext& context_) {
    if (sampler_host == nullptr || upload_host == nullptr || gpu_memory_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D missing runtime hosts required for fallback material resources");
    }
    if (fallback_material_sampler_id.IsValid() &&
        fallback_material_image.image != VK_NULL_HANDLE &&
        fallback_material_image.default_view != VK_NULL_HANDLE &&
        fallback_material_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }
    if (context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("GeometryRenderer3D fallback material texture requires synchronization2");
    }

    if (!fallback_material_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_LINEAR;
        sampler_desc.min_filter = VK_FILTER_LINEAR;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_desc.min_lod = 0.0F;
        sampler_desc.max_lod = 0.0F;
        fallback_material_sampler_id = sampler_host->RegisterSampler(context_, sampler_desc);
    }

    if (fallback_material_image.image == VK_NULL_HANDLE) {
        resource::ImageCreateInfo image_create_info{};
        image_create_info.image_type = VK_IMAGE_TYPE_2D;
        image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_create_info.extent = VkExtent3D{1U, 1U, 1U};
        image_create_info.mip_levels = 1U;
        image_create_info.array_layers = 1U;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        image_create_info.create_default_view = true;
        image_create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        image_create_info.default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        image_create_info.default_base_mip_level = 0U;
        image_create_info.default_level_count = 1U;
        image_create_info.default_base_array_layer = 0U;
        image_create_info.default_layer_count = 1U;
        fallback_material_image = resource::ImageHost::CreateImage(context_, image_create_info, *gpu_memory_host);
        fallback_material_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    if (fallback_material_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        VkImageMemoryBarrier2 to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        to_transfer.srcAccessMask = 0U;
        to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = fallback_material_image.image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.baseMipLevel = 0U;
        to_transfer.subresourceRange.levelCount = 1U;
        to_transfer.subresourceRange.baseArrayLayer = 0U;
        to_transfer.subresourceRange.layerCount = 1U;
        upload_host->RecordImageBarrier2(active_frame_index, to_transfer);

        const std::uint32_t white_pixel = 0xFFFFFFFFU;
        VkBufferImageCopy copy_region{};
        copy_region.bufferOffset = 0U;
        copy_region.bufferRowLength = 0U;
        copy_region.bufferImageHeight = 0U;
        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.imageSubresource.mipLevel = 0U;
        copy_region.imageSubresource.baseArrayLayer = 0U;
        copy_region.imageSubresource.layerCount = 1U;
        copy_region.imageOffset = VkOffset3D{0, 0, 0};
        copy_region.imageExtent = VkExtent3D{1U, 1U, 1U};
        upload_host->StageAndRecordCopyImage(active_frame_index,
                                             fallback_material_image.image,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             copy_region,
                                             &white_pixel,
                                             static_cast<VkDeviceSize>(sizeof(white_pixel)),
                                             4U);

        VkImageMemoryBarrier2 to_shader_read{};
        to_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        to_shader_read.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        to_shader_read.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        to_shader_read.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        to_shader_read.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader_read.image = fallback_material_image.image;
        to_shader_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_shader_read.subresourceRange.baseMipLevel = 0U;
        to_shader_read.subresourceRange.levelCount = 1U;
        to_shader_read.subresourceRange.baseArrayLayer = 0U;
        to_shader_read.subresourceRange.layerCount = 1U;
        upload_host->RecordImageBarrier2(active_frame_index, to_shader_read);

        fallback_material_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

GeometryRenderer3D::MaterialPushConstants GeometryRenderer3D::BuildMaterialPushConstants(
    const GeometryMaterialDesc* material_desc_) noexcept {
    MaterialPushConstants push_constants{};
    push_constants.uv_scale_u = 1.0F;
    push_constants.uv_scale_v = 1.0F;
    push_constants.uv_bias_u = 0.0F;
    push_constants.uv_bias_v = 0.0F;
    push_constants.flags = 0U;
    push_constants.alpha_cutoff = 0.0F;
    push_constants.reserved0 = 0.0F;
    push_constants.reserved1 = 0.0F;

    if (material_desc_ == nullptr) {
        return push_constants;
    }

    const auto sanitize_value = [](float value_, float fallback_) noexcept {
        return std::isfinite(value_) ? value_ : fallback_;
    };

    push_constants.uv_scale_u = sanitize_value(material_desc_->uv_scale_u, 1.0F);
    push_constants.uv_scale_v = sanitize_value(material_desc_->uv_scale_v, 1.0F);
    push_constants.uv_bias_u = sanitize_value(material_desc_->uv_bias_u, 0.0F);
    push_constants.uv_bias_v = sanitize_value(material_desc_->uv_bias_v, 0.0F);
    push_constants.flags = material_desc_->flags;
    push_constants.alpha_cutoff = std::clamp(sanitize_value(material_desc_->alpha_cutoff, 0.0F),
                                             0.0F,
                                             1.0F);
    return push_constants;
}

bool GeometryRenderer3D::ResolveMaterialBinding(std::uint32_t material_id_,
                                                VkSampler& out_sampler_,
                                                VkImageView& out_image_view_,
                                                VkImageLayout& out_image_layout_,
                                                MaterialPushConstants& out_material_push_constants_) {
    out_sampler_ = fallback_material_sampler_id.IsValid()
        ? sampler_host->GetSampler(fallback_material_sampler_id)
        : VK_NULL_HANDLE;
    out_image_view_ = fallback_material_image.default_view;
    out_image_layout_ = fallback_material_layout;
    out_material_push_constants_ = BuildMaterialPushConstants(nullptr);

    std::size_t lower_bound_index = 0U;
    bool use_append_fast_path = false;
    if (!resolved_materials.empty()) {
        const ResolvedMaterialEntry& back_entry = resolved_materials.back();
        if (back_entry.material_id == material_id_) {
            out_sampler_ = back_entry.sampler;
            out_image_view_ = back_entry.image_view;
            out_image_layout_ = back_entry.image_layout;
            out_material_push_constants_ = back_entry.material_push_constants;
            ++stats.material_resolve_cache_hit_count;
            return out_sampler_ != VK_NULL_HANDLE &&
                   out_image_view_ != VK_NULL_HANDLE &&
                   out_image_layout_ != VK_IMAGE_LAYOUT_UNDEFINED;
        }
        if (back_entry.material_id < material_id_) {
            lower_bound_index = resolved_materials.size();
            use_append_fast_path = true;
        }
    }
    if (!use_append_fast_path) {
        lower_bound_index = LowerBoundResolvedMaterialIndex(resolved_materials, material_id_);
        if (lower_bound_index < resolved_materials.size() &&
            resolved_materials[lower_bound_index].material_id == material_id_) {
            const ResolvedMaterialEntry& cached_entry = resolved_materials[lower_bound_index];
            out_sampler_ = cached_entry.sampler;
            out_image_view_ = cached_entry.image_view;
            out_image_layout_ = cached_entry.image_layout;
            out_material_push_constants_ = cached_entry.material_push_constants;
            ++stats.material_resolve_cache_hit_count;
            return out_sampler_ != VK_NULL_HANDLE &&
                   out_image_view_ != VK_NULL_HANDLE &&
                   out_image_layout_ != VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    ++stats.material_resolve_cache_miss_count;

    const GeometryMaterialHost::MaterialRecord* material_record = nullptr;
    if (geometry_material_host != nullptr && geometry_material_host->IsInitialized()) {
        material_record = geometry_material_host->FindMaterial(material_id_);
    }

    const GeometryMaterialDesc* material_desc = material_record != nullptr
        ? &material_record->desc
        : nullptr;
    out_material_push_constants_ = BuildMaterialPushConstants(material_desc);

    std::uint32_t image_id = 0U;
    std::uint32_t image_revision = 0U;
    if (material_desc != nullptr) {
        image_id = material_desc->image_id;
        if (material_desc->sampler_id.IsValid()) {
            const VkSampler candidate_sampler = sampler_host->GetSampler(material_desc->sampler_id);
            if (candidate_sampler != VK_NULL_HANDLE) {
                out_sampler_ = candidate_sampler;
            }
        }
        if (image_id != 0U &&
            geometry_image_host != nullptr &&
            geometry_image_host->IsInitialized()) {
            const GeometryImageHost::ImageRecord* image_record = geometry_image_host->FindImage(image_id);
            if (image_record != nullptr &&
                image_record->resource.default_view != VK_NULL_HANDLE &&
                image_record->current_layout != VK_IMAGE_LAYOUT_UNDEFINED) {
                out_image_view_ = image_record->resource.default_view;
                out_image_layout_ = image_record->current_layout;
                image_revision = image_record->revision;
            }
        }
    }

    if (out_sampler_ == VK_NULL_HANDLE ||
        out_image_view_ == VK_NULL_HANDLE ||
        out_image_layout_ == VK_IMAGE_LAYOUT_UNDEFINED) {
        return false;
    }

    const std::size_t old_size = resolved_materials.size();
    resolved_materials.resize(old_size + 1U);
    if (lower_bound_index < old_size) {
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            resolved_materials[index] = std::move(resolved_materials[index - 1U]);
        }
    }
    resolved_materials[lower_bound_index] = ResolvedMaterialEntry{
        .material_id = material_id_,
        .material_revision = material_record != nullptr ? material_record->revision : 0U,
        .image_id = image_id,
        .image_revision = image_revision,
        .image_view = out_image_view_,
        .image_layout = out_image_layout_,
        .sampler = out_sampler_,
        .material_push_constants = out_material_push_constants_
    };
    return true;
}

VkDescriptorSet GeometryRenderer3D::AcquireMaterialDescriptorSet(std::uint32_t frame_index_,
                                                                 std::uint32_t material_id_,
                                                                 MaterialPushConstants* out_material_push_constants_) {
    if (context == nullptr || descriptor_host == nullptr || sampler_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::AcquireMaterialDescriptorSet requires prepared runtime hosts");
    }
    if (!descriptor_layout_id.IsValid()) {
        return VK_NULL_HANDLE;
    }
    if (frame_index_ >= frame_material_sets.size()) {
        throw std::out_of_range("GeometryRenderer3D::AcquireMaterialDescriptorSet frame index out of range");
    }

    GeometryRenderer3DMcVector<MaterialSetEntry>& entries = frame_material_sets[frame_index_];
    std::size_t lower_bound_index = 0U;
    bool use_append_fast_path = false;
    if (!entries.empty()) {
        const MaterialSetEntry& back_entry = entries.back();
        if (back_entry.material_id == material_id_) {
            if (out_material_push_constants_ != nullptr) {
                *out_material_push_constants_ = back_entry.material_push_constants;
            }
            return back_entry.descriptor_set;
        }
        if (back_entry.material_id < material_id_) {
            lower_bound_index = entries.size();
            use_append_fast_path = true;
        }
    }
    if (!use_append_fast_path) {
        lower_bound_index = LowerBoundMaterialSetIndex(entries, material_id_);
        if (lower_bound_index < entries.size() && entries[lower_bound_index].material_id == material_id_) {
            if (out_material_push_constants_ != nullptr) {
                *out_material_push_constants_ = entries[lower_bound_index].material_push_constants;
            }
            return entries[lower_bound_index].descriptor_set;
        }
    }

    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    MaterialPushConstants material_push_constants{};
    if (!ResolveMaterialBinding(material_id_,
                                sampler,
                                image_view,
                                image_layout,
                                material_push_constants)) {
        return VK_NULL_HANDLE;
    }

    const VkDescriptorSet descriptor_set = descriptor_host->AllocateSet(*context, frame_index_, descriptor_layout_id);

    descriptor_buffer_write_scratch.clear();
    descriptor_image_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    descriptor_image_write_scratch.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = sampler,
        .image_view = image_view,
        .image_layout = image_layout
    });
    descriptor_host->UpdateSet(*context,
                               descriptor_set,
                               descriptor_buffer_write_scratch,
                               descriptor_image_write_scratch,
                               descriptor_texel_write_scratch);
    ++stats.descriptor_set_update_count;

    const std::size_t old_size = entries.size();
    entries.resize(old_size + 1U);
    if (lower_bound_index < old_size) {
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            entries[index] = std::move(entries[index - 1U]);
        }
    }
    entries[lower_bound_index] = MaterialSetEntry{
        .material_id = material_id_,
        .descriptor_set = descriptor_set,
        .material_push_constants = material_push_constants
    };
    if (out_material_push_constants_ != nullptr) {
        *out_material_push_constants_ = material_push_constants;
    }
    return descriptor_set;
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

void GeometryRenderer3D::RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_,
                                                                bool has_previous_content_) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = has_previous_content_ ? VK_ACCESS_MEMORY_READ_BIT : 0U;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = has_previous_content_ ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record_context_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(record_context_.command_buffer,
                         has_previous_content_
                             ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

void GeometryRenderer3D::RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record_context_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(record_context_.command_buffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

void GeometryRenderer3D::RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
                                                           const resource::ImageResource& depth_resource_,
                                                           bool initialized_) const {
    if (command_buffer_ == VK_NULL_HANDLE || depth_resource_.image == VK_NULL_HANDLE) {
        return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = initialized_
        ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
        : 0U;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = initialized_
        ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depth_resource_.image;
    barrier.subresourceRange.aspectMask = DepthImageAspectMask(depth_format);
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(command_buffer_,
                         initialized_
                             ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

} // namespace vr::geometry
