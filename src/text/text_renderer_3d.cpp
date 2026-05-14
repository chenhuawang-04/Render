#include "vr/text/text_renderer_3d.hpp"

#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
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
#include <limits>
#include <stdexcept>

namespace vr::text {

VkDeviceSize TextRenderer3D::NextPow2(VkDeviceSize value_) noexcept {
    if (value_ <= 1U) {
        return 1U;
    }

    VkDeviceSize result = 1U;
    while (result < value_) {
        if (result > (std::numeric_limits<VkDeviceSize>::max() >> 1U)) {
            return std::numeric_limits<VkDeviceSize>::max();
        }
        result <<= 1U;
    }
    return result;
}

bool TextRenderer3D::AnyTextComponentDirty(const ecs::Text<ecs::Dim3>* components_,
                                           std::uint32_t component_count_) noexcept {
    if (components_ == nullptr || component_count_ == 0U) {
        return false;
    }

    for (std::uint32_t i = 0U; i < component_count_; ++i) {
        if (components_[i].runtime.dirty_flags != 0U) {
            return true;
        }
    }
    return false;
}

bool TextRenderer3D::IsDepthFormatSupported(VulkanContext& context_,
                                            VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }

    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool TextRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags TextRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags mask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return mask;
}

VkFormat TextRenderer3D::ResolveDepthFormat(VulkanContext& context_,
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

    for (const VkFormat candidate : fallback_formats) {
        if (IsDepthFormatSupported(context_, candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error("TextRenderer3D failed to resolve depth format for depth attachment");
}

std::size_t TextRenderer3D::PipelineModeIndex(DepthPipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::uint64_t TextRenderer3D::ComputeTransformRevisionSignature(
    const ecs::Transform<ecs::Dim3>* transforms_,
    std::uint32_t component_count_,
    const std::uint32_t* candidate_component_indices_,
    std::uint32_t candidate_component_count_,
    bool use_candidate_indices_) noexcept {
    if (transforms_ == nullptr || component_count_ == 0U) {
        return 0U;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    if (!use_candidate_indices_) {
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            hash ^= static_cast<std::uint64_t>(transforms_[i].runtime.world_revision);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    hash ^= static_cast<std::uint64_t>(candidate_component_count_);
    hash *= 1099511628211ULL;
    if (candidate_component_indices_ == nullptr) {
        return hash;
    }

    for (std::uint32_t i = 0U; i < candidate_component_count_; ++i) {
        const std::uint32_t component_index = candidate_component_indices_[i];
        if (component_index >= component_count_) {
            continue;
        }
        hash ^= static_cast<std::uint64_t>(transforms_[component_index].runtime.world_revision);
        hash *= 1099511628211ULL;
    }
    return hash;
}

TextRenderer3D::DepthPipelineMode TextRenderer3D::ResolveDepthPipelineMode(
    const ecs::Text3DDrawBatch& batch_,
    bool use_depth_,
    bool reverse_z_) noexcept {
    if (!use_depth_ || (batch_.depth_flags & 0x1U) == 0U) {
        return DepthPipelineMode::no_depth;
    }

    const bool depth_write = (batch_.depth_flags & 0x2U) != 0U;
    if (reverse_z_) {
        return depth_write
            ? DepthPipelineMode::depth_test_write_reverse_z
            : DepthPipelineMode::depth_test_reverse_z;
    }
    return depth_write
        ? DepthPipelineMode::depth_test_write
        : DepthPipelineMode::depth_test;
}

void TextRenderer3D::Initialize(const TextRenderer3DCreateInfo& create_info_) {
    if (initialized) {
        if (context != nullptr) {
            Shutdown(*context);
        } else {
            throw std::runtime_error(
                "TextRenderer3D::Initialize called while already initialized without valid VulkanContext");
        }
    }

    create_info_cache = create_info_;
    if (create_info_cache.initial_vertex_buffer_bytes == 0U) {
        create_info_cache.initial_vertex_buffer_bytes = 1U;
    }
    if (!(create_info_cache.sdf_smooth > 0.0F) || !std::isfinite(create_info_cache.sdf_smooth)) {
        create_info_cache.sdf_smooth = 1.0F;
    }
    if (!(create_info_cache.bitmap_gamma > 0.0F) || !std::isfinite(create_info_cache.bitmap_gamma)) {
        create_info_cache.bitmap_gamma = 1.0F;
    }
    if (!(create_info_cache.bitmap_edge_sharpness > 0.0F) ||
        !std::isfinite(create_info_cache.bitmap_edge_sharpness)) {
        create_info_cache.bitmap_edge_sharpness = 1.0F;
    }
    if (!std::isfinite(create_info_cache.clear_depth_value)) {
        create_info_cache.clear_depth_value = 1.0F;
    }
    create_info_cache.clear_depth_value = std::clamp(create_info_cache.clear_depth_value, 0.0F, 1.0F);

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_glyph_count > 0U) {
        ecs::TextRender3DSystem::Reserve(render_scratch,
                                         create_info_cache.reserve_component_count,
                                         create_info_cache.reserve_glyph_count);
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch,
                                               create_info_cache.reserve_component_count);
    }

    cached_runtime_stats = {};
    cached_render_stats = {};
    frame_data_cache = {};
    cached_components_ptr = nullptr;
    cached_transforms_ptr = nullptr;
    cached_camera_component_ptr = nullptr;
    cached_camera_transform_ptr = nullptr;
    cached_component_count = 0U;
    cached_transform_signature = 0U;
    cached_camera_world_revision = 0U;
    bounds_components = nullptr;
    culling_stats = {};
    runtime_geometry_revision = 1U;
    runtime_geometry_valid = false;
    instance_geometry_valid = false;
    contains_billboard_instances = false;
    active_camera_reverse_z = false;
    stats = {};

    for (auto& pipeline_id : graphics_pipeline_ids) {
        pipeline_id = {};
    }
    output_target_config = {};
    depth_output_target_config = {};
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = true;
}

void TextRenderer3D::Shutdown(VulkanContext& context_) {
    if (context_.Device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.Device());
    }

    for (auto& frame_state : frame_states) {
        resource::BufferHost::DestroyBuffer(context_, frame_state.vertex_buffer);
        frame_state.vertex_buffer_capacity_bytes = 0U;
        frame_state.instance_count = 0U;
        frame_state.uploaded_revision = 0U;
    }
    frame_states.clear();

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);
    image_initialized.clear();

    render_scratch.instances.clear();
    render_scratch.draw_batches.clear();
    render_scratch.runtime_scratch.glyph_quads.clear();
    render_scratch.runtime_scratch.draw_batches.clear();
    render_scratch.runtime_scratch.batch_scratch.visible_items.clear();
    render_scratch.runtime_scratch.batch_scratch.radix_scratch.clear();
    render_scratch.runtime_scratch.batch_scratch.ordered_indices.clear();
    render_scratch.runtime_scratch.utf32_codepoints.clear();
    render_scratch.runtime_scratch.line_widths.clear();
    render_scratch.runtime_scratch.line_x_offsets.clear();
    render_scratch.runtime_scratch.run_glyphs.clear();
    render_scratch.runtime_scratch.face_variants.clear();
    render_scratch.runtime_scratch.glyph_resolve_cache.clear();
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    culling_stats = {};

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : graphics_pipeline_ids) {
        pipeline_id = {};
    }
    output_target_config = {};
    depth_output_target_config = {};
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    text_components = nullptr;
    text_transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    bindless_resources = nullptr;
    gpu_memory_host = nullptr;
    freetype_host = nullptr;
    glyph_atlas_host = nullptr;
    glyph_upload_host = nullptr;

    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;

    cached_runtime_stats = {};
    cached_render_stats = {};
    frame_data_cache = {};
    cached_components_ptr = nullptr;
    cached_transforms_ptr = nullptr;
    cached_camera_component_ptr = nullptr;
    cached_camera_transform_ptr = nullptr;
    cached_component_count = 0U;
    cached_transform_signature = 0U;
    cached_camera_world_revision = 0U;

    runtime_geometry_revision = 1U;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    runtime_geometry_valid = false;
    instance_geometry_valid = false;
    contains_billboard_instances = false;
    active_camera_reverse_z = false;
    stats = {};
    initialized = false;
}

void TextRenderer3D::SetSceneData(ecs::Text<ecs::Dim3>* text_components_,
                                  ecs::Transform<ecs::Dim3>* text_transforms_,
                                  std::uint32_t component_count_,
                                  ecs::Camera<ecs::Dim3>* camera_component_,
                                  ecs::Transform<ecs::Dim3>* camera_transform_,
                                  ecs::Bounds<ecs::Dim3>* bounds_components_) noexcept {
    text_components = text_components_;
    text_transforms = text_transforms_;
    component_count = component_count_;
    camera_component = camera_component_;
    camera_transform = camera_transform_;
    bounds_components = bounds_components_;
    if (component_count_ > 0U) {
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch, component_count_);
    }
}

void TextRenderer3D::SetOutputTargetConfig(
    const render::RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void TextRenderer3D::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void TextRenderer3D::SetDepthTargetConfig(
    const render::RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept {
    depth_output_target_config = depth_output_target_config_;
}

void TextRenderer3D::ResetDepthTargetConfig() noexcept {
    depth_output_target_config = {};
}

void TextRenderer3D::PrepareFrame(const render::TextRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer3D::PrepareFrame called before Initialize");
    }
    ValidateTextRuntimePrepareView(prepare_view_, "TextRenderer3D::PrepareFrame");

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    bindless_resources = prepare_view_.bindless;
    gpu_memory_host = &prepare_view_.gpu_memory;
    freetype_host = &prepare_view_.freetype;
    glyph_atlas_host = &prepare_view_.glyph_atlas;
    glyph_upload_host = &prepare_view_.glyph_upload;
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (glyph_upload_host != nullptr &&
        glyph_upload_host->IsInitialized() &&
        (!glyph_upload_host->BindlessConfig().Enabled() ||
         glyph_upload_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureGlyphUploadHost(*glyph_upload_host);
    }
    active_frame_index = prepare_view_.frame.frame_index;
    last_submitted_value_seen = std::max(last_submitted_value_seen,
                                         prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen,
                                           prepare_view_.progress.completed_submit_value);
    CollectRetiredDepthResources(*context, completed_submit_value_seen);

    stats = {};
    stats.component_count = component_count;
    culling_stats = {};

    if (text_components == nullptr ||
        text_transforms == nullptr ||
        camera_component == nullptr ||
        camera_transform == nullptr ||
        component_count == 0U) {
        render_scratch.instances.clear();
        render_scratch.draw_batches.clear();
        cached_runtime_stats = {};
        cached_render_stats = {};
        frame_data_cache = {};
        cached_components_ptr = nullptr;
        cached_transforms_ptr = nullptr;
        cached_camera_component_ptr = nullptr;
        cached_camera_transform_ptr = nullptr;
        cached_component_count = 0U;
        cached_transform_signature = 0U;
        cached_camera_world_revision = 0U;
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        runtime_geometry_valid = false;
        instance_geometry_valid = false;
        contains_billboard_instances = false;
        active_camera_reverse_z = false;
        ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());
        return;
    }

    frame_data_cache = ecs::TextRender3DSystem::BuildFrameData(*camera_component, *camera_transform);
    active_camera_reverse_z = camera_component->style.reverse_z != 0U;

    ecs::TextRuntimeBuildHint runtime_build_hint{};
    const bool use_bounds_culling = bounds_components != nullptr && camera_component != nullptr;
    if (use_bounds_culling) {
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
        runtime_build_hint.visible_component_indices = culling_scratch.visible_indices.data();
        runtime_build_hint.visible_component_count = culling_stats.visible_count;
        runtime_build_hint.use_visible_component_indices = 1U;
        runtime_build_hint.external_visible_set_signature = culling_stats.visible_set_signature;
        runtime_build_hint.use_external_visible_set_signature = 1U;

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
    } else {
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
    }

    const bool visible_mode_changed =
        runtime_geometry_valid &&
        (cached_runtime_stats.used_visible_component_indices != use_bounds_culling);
    const bool visible_set_changed =
        runtime_geometry_valid &&
        use_bounds_culling &&
        (cached_runtime_stats.visible_set_signature != runtime_build_hint.external_visible_set_signature ||
         cached_runtime_stats.candidate_component_count != runtime_build_hint.visible_component_count);

    const bool runtime_rebuild_required =
        !runtime_geometry_valid ||
        cached_components_ptr != text_components ||
        cached_component_count != component_count ||
        AnyTextComponentDirty(text_components, component_count) ||
        visible_mode_changed ||
        visible_set_changed;

    if (runtime_rebuild_required) {
        cached_runtime_stats = ecs::TextRuntimeSystem<ecs::Dim3>::Build(text_components,
                                                                         component_count,
                                                                         *glyph_atlas_host,
                                                                         *freetype_host,
                                                                         render_scratch.runtime_scratch,
                                                                         create_info_cache.runtime_build,
                                                                         runtime_build_hint);
        runtime_geometry_valid = true;
        instance_geometry_valid = false;
    }

    const bool use_transform_candidates = cached_runtime_stats.used_visible_component_indices;
    const std::uint32_t* transform_candidate_indices = use_transform_candidates
        ? ecs::TextBatchSystem<ecs::Dim3>::OrderedIndices(render_scratch.runtime_scratch.batch_scratch)
        : nullptr;
    const std::uint32_t transform_candidate_count = use_transform_candidates
        ? ecs::TextBatchSystem<ecs::Dim3>::OrderedIndexCount(render_scratch.runtime_scratch.batch_scratch)
        : component_count;
    const std::uint64_t transform_signature =
        ComputeTransformRevisionSignature(text_transforms,
                                          component_count,
                                          transform_candidate_indices,
                                          transform_candidate_count,
                                          use_transform_candidates);
    const std::uint32_t camera_world_revision = camera_transform->runtime.world_revision;

    const bool instance_rebuild_required =
        !instance_geometry_valid ||
        runtime_rebuild_required ||
        cached_transforms_ptr != text_transforms ||
        cached_camera_component_ptr != camera_component ||
        cached_camera_transform_ptr != camera_transform ||
        transform_signature != cached_transform_signature ||
        (contains_billboard_instances && camera_world_revision != cached_camera_world_revision);

    if (instance_rebuild_required) {
        cached_render_stats = ecs::TextRender3DSystem::BuildFromRuntime(text_components,
                                                                         text_transforms,
                                                                         component_count,
                                                                         *camera_component,
                                                                         *camera_transform,
                                                                         render_scratch,
                                                                         cached_runtime_stats);
        cached_transforms_ptr = text_transforms;
        cached_camera_component_ptr = camera_component;
        cached_camera_transform_ptr = camera_transform;
        cached_transform_signature = transform_signature;
        cached_camera_world_revision = camera_world_revision;
        contains_billboard_instances = cached_render_stats.billboard_instance_count > 0U;
        instance_geometry_valid = true;

        if (runtime_geometry_revision == std::numeric_limits<std::uint64_t>::max()) {
            runtime_geometry_revision = 1U;
        } else {
            ++runtime_geometry_revision;
            if (runtime_geometry_revision == 0U) {
                runtime_geometry_revision = 1U;
            }
        }
    }

    cached_components_ptr = text_components;
    cached_component_count = component_count;

    stats.visible_component_count = cached_render_stats.runtime.visible_component_count;
    stats.built_component_count = cached_render_stats.runtime.built_component_count;
    stats.glyph_quad_count = static_cast<std::uint32_t>(render_scratch.runtime_scratch.glyph_quads.size());
    stats.instance_count = static_cast<std::uint32_t>(render_scratch.instances.size());
    stats.draw_batch_count = static_cast<std::uint32_t>(render_scratch.draw_batches.size());
    stats.billboard_instance_count = cached_render_stats.billboard_instance_count;
    stats.depth_test_batch_count = cached_render_stats.depth_test_batch_count;
    stats.depth_write_batch_count = cached_render_stats.depth_write_batch_count;

    const VkDeviceSize required_bytes =
        static_cast<VkDeviceSize>(render_scratch.instances.size()) * sizeof(ecs::Text3DGpuInstance);

    EnsureGpuResourcesForFrame(*context, prepare_view_, active_frame_index, required_bytes);
    ResetPerFrameDrawState(active_frame_index, glyph_atlas_host->PageCount());

    PerFrameState& frame_state = frame_states[active_frame_index];
    frame_state.instance_count = static_cast<std::uint32_t>(render_scratch.instances.size());

    if (required_bytes == 0U) {
        return;
    }

    const bool needs_upload = frame_state.uploaded_revision != runtime_geometry_revision;
    if (!needs_upload) {
        return;
    }

    upload_host->StageAndRecordCopyBuffer(active_frame_index,
                                          frame_state.vertex_buffer.buffer,
                                          0U,
                                          render_scratch.instances.data(),
                                          required_bytes,
                                          16U);

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = frame_state.vertex_buffer.buffer;
    barrier.offset = 0U;
    barrier.size = required_bytes;
    upload_host->RecordBufferBarrier2(active_frame_index, barrier);

    frame_state.uploaded_revision = runtime_geometry_revision;
    stats.uploaded_bytes = required_bytes;
}

void TextRenderer3D::Record(const render::FrameRecordContext& record_context_) {
    RecordInternal(record_context_, 0U, false);
}

void TextRenderer3D::RecordSceneStage(const render::FrameRecordContext& record_context_,
                                      render::SceneRenderStage stage_) {
    RecordInternal(record_context_, render::SceneRenderStagePassHintValue(stage_), true);
}

void TextRenderer3D::RecordInternal(const render::FrameRecordContext& record_context_,
                                    std::uint32_t pass_bucket_,
                                    bool filter_by_pass_bucket_) {
    if (!initialized) {
        throw std::runtime_error("TextRenderer3D::Record called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr ||
        glyph_upload_host == nullptr || bindless_resources == nullptr) {
        throw std::runtime_error("TextRenderer3D::Record called before PrepareFrame");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("TextRenderer3D::Record requires valid command buffer");
    }

    if (record_context_.image_index >= image_initialized.size()) {
        const std::size_t previous_size = image_initialized.size();
        image_initialized.resize(record_context_.image_index + 1U);
        for (std::size_t i = previous_size; i < image_initialized.size(); ++i) {
            image_initialized[i] = 0U;
        }
    }
    bool has_previous_content = image_initialized[record_context_.image_index] != 0U;
    if (record_context_.render_target_host != nullptr) {
        const render::ResolvedColorRenderTarget resolved_color_target =
            render::ResolveColorRenderTarget(record_context_, output_target_config);
        if (resolved_color_target.using_render_target_host &&
            IsValidRenderTargetHandle(resolved_color_target.handle)) {
            const render::RenderTargetResolvedView color_view =
                record_context_.render_target_host->ResolveView(resolved_color_target.handle);
            has_previous_content = color_view.state != render::RenderTargetStateKind::undefined;
        }
    }
    const render::ResolvedColorRenderTarget resolved_color_target =
        render::ResolveColorRenderTarget(record_context_, output_target_config);
    const VkExtent2D render_extent = resolved_color_target.extent;
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("TextRenderer3D::Record resolved zero-sized render extent");
    }

    const bool depth_enabled = create_info_cache.enable_depth;
    bool using_external_depth_target = false;
    VkFormat active_depth_format = VK_FORMAT_UNDEFINED;
    bool use_depth_attachment = false;
    bool has_previous_depth_content = false;

    if (depth_enabled &&
        record_context_.render_target_host != nullptr &&
        record_context_.render_target_host->IsValid(depth_output_target_config.depth_target)) {
        const render::RenderTargetResolvedView depth_view =
            record_context_.render_target_host->ResolveView(depth_output_target_config.depth_target);
        if (depth_view.image_view == VK_NULL_HANDLE) {
            throw std::runtime_error("TextRenderer3D::Record external depth target has null view");
        }
        has_previous_depth_content = depth_view.state != render::RenderTargetStateKind::undefined;
    }

    render::ResolvedColorRenderPass color_pass{};

    if (depth_enabled &&
        record_context_.render_target_host != nullptr &&
        record_context_.render_target_host->IsValid(depth_output_target_config.depth_target)) {
        render::RenderTargetDepthOutputConfig effective_depth_output_config = depth_output_target_config;
        if (!effective_depth_output_config.use_explicit_load_op && create_info_cache.clear_depth) {
            effective_depth_output_config.use_explicit_load_op = true;
            effective_depth_output_config.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        color_pass = render::BuildColorDepthRenderPass(record_context_,
                                                       output_target_config,
                                                       effective_depth_output_config,
                                                       create_info_cache.clear_swapchain,
                                                       create_info_cache.clear_color,
                                                       has_previous_content,
                                                       has_previous_depth_content);
        use_depth_attachment = true;
        using_external_depth_target = true;
        active_depth_format = color_pass.depth_target.format;
    } else if (depth_enabled) {
        color_pass = render::BuildColorRenderPass(record_context_,
                                                  output_target_config,
                                                  create_info_cache.clear_swapchain,
                                                  create_info_cache.clear_color,
                                                  has_previous_content);
        if (depth_format == VK_FORMAT_UNDEFINED) {
            depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
        }

        EnsureDepthResources(*context,
                             static_cast<std::uint32_t>(image_initialized.size()),
                             render_extent);

        use_depth_attachment =
            depth_format != VK_FORMAT_UNDEFINED &&
            record_context_.image_index < depth_images.size() &&
            depth_images[record_context_.image_index].default_view != VK_NULL_HANDLE;

        if (use_depth_attachment) {
            if (record_context_.image_index >= depth_image_initialized.size()) {
                const std::size_t previous_size = depth_image_initialized.size();
                depth_image_initialized.resize(record_context_.image_index + 1U);
                for (std::size_t i = previous_size; i < depth_image_initialized.size(); ++i) {
                    depth_image_initialized[i] = 0U;
                }
            }

            const bool depth_initialized = depth_image_initialized[record_context_.image_index] != 0U;
            RecordDepthTransitionToAttachment(record_context_.command_buffer,
                                              depth_images[record_context_.image_index],
                                              depth_initialized);
            color_pass.rendering_info.depth_attachment = {};
            color_pass.rendering_info.depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_pass.rendering_info.depth_attachment.imageView =
                depth_images[record_context_.image_index].default_view;
            color_pass.rendering_info.depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            color_pass.rendering_info.depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
            color_pass.rendering_info.depth_attachment.resolveImageView = VK_NULL_HANDLE;
            color_pass.rendering_info.depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color_pass.rendering_info.depth_attachment.loadOp =
                (create_info_cache.clear_depth || !depth_initialized)
                    ? VK_ATTACHMENT_LOAD_OP_CLEAR
                    : VK_ATTACHMENT_LOAD_OP_LOAD;
            color_pass.rendering_info.depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_pass.rendering_info.depth_attachment.clearValue.depthStencil.depth =
                create_info_cache.clear_depth_value;
            color_pass.rendering_info.depth_attachment.clearValue.depthStencil.stencil =
                create_info_cache.clear_stencil_value;
            color_pass.rendering_info.has_depth_attachment = true;
            active_depth_format = depth_format;
        }
    } else {
        color_pass = render::BuildColorRenderPass(record_context_,
                                                  output_target_config,
                                                  create_info_cache.clear_swapchain,
                                                  create_info_cache.clear_color,
                                                  has_previous_content);
    }

    EnsurePipelineObjects(*context,
                          *descriptor_host,
                          *pipeline_host,
                          color_pass.target.format,
                          use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED);

    vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);

    PushConstants push_constants{};
    push_constants.view_projection = frame_data_cache.view_projection;
    push_constants.shading_params = ecs::Float4{
        .x = create_info_cache.sdf_smooth,
        .y = create_info_cache.bitmap_gamma,
        .z = create_info_cache.bitmap_edge_sharpness,
        .w = 0.0F,
    };
    push_constants.texture_slot = 0U;
    push_constants.sampler_slot = 0U;
    push_constants.reserved0 = 0U;
    push_constants.reserved1 = 0U;
    vkCmdPushConstants(record_context_.command_buffer,
                       pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(PushConstants),
                       &push_constants);

    const std::uint32_t frame_index = record_context_.frame_index;
    if (frame_index < frame_states.size()) {
        const PerFrameState& frame_state = frame_states[frame_index];
        if (frame_state.instance_count > 0U && frame_state.vertex_buffer.buffer != VK_NULL_HANDLE) {
            std::uint32_t stage_draw_call_count = 0U;
            std::uint32_t stage_filtered_batch_count = 0U;
            const VkDescriptorSet bindless_sets[] = {
                bindless_resources->SampledImageSet(),
                bindless_resources->SamplerSet()
            };
            vkCmdBindDescriptorSets(record_context_.command_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0U,
                                    2U,
                                    bindless_sets,
                                    0U,
                                    nullptr);
            stats.descriptor_set_bind_count += 2U;

            const VkBuffer vertex_buffer = frame_state.vertex_buffer.buffer;
            const VkDeviceSize vertex_offset = 0U;
            vkCmdBindVertexBuffers(record_context_.command_buffer, 0U, 1U, &vertex_buffer, &vertex_offset);

            render::GraphicsPipelineId bound_pipeline_id{};
            for (const auto& batch : render_scratch.draw_batches) {
                if (filter_by_pass_bucket_ &&
                    ecs::TextSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                    ++stage_filtered_batch_count;
                    continue;
                }
                if (batch.glyph_count == 0U) {
                    continue;
                }
                if (batch.atlas_page_id >= glyph_upload_host->PageCount()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const render::BindlessSlot texture_slot =
                    glyph_upload_host->ResolveBindlessImageSlot(batch.atlas_page_id);
                const render::BindlessSlot sampler_slot =
                    glyph_upload_host->BindlessConfig().sampler_slot;
                if (!texture_slot.IsValid() || !sampler_slot.IsValid()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }

                const DepthPipelineMode mode = ResolveDepthPipelineMode(batch,
                                                                        use_depth_attachment,
                                                                        active_camera_reverse_z);
                const render::GraphicsPipelineId pipeline_id =
                    EnsureGraphicsPipelineForMode(*context,
                                                  *pipeline_host,
                                                  color_pass.target.format,
                                                  use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED,
                                                  mode);
                if (!pipeline_id.IsValid()) {
                    ++stats.skipped_draw_batch_count;
                    continue;
                }
                if (!bound_pipeline_id.IsValid() || bound_pipeline_id.value != pipeline_id.value) {
                    vkCmdBindPipeline(record_context_.command_buffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      pipeline_host->GetGraphicsPipeline(pipeline_id));
                    bound_pipeline_id = pipeline_id;
                    ++stats.depth_pipeline_bind_count;
                }

                push_constants.texture_slot = texture_slot.index;
                push_constants.sampler_slot = sampler_slot.index;
                vkCmdPushConstants(record_context_.command_buffer,
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);

                vkCmdDraw(record_context_.command_buffer,
                          4U,
                          batch.glyph_count,
                          0U,
                          batch.glyph_begin);
                ++stats.draw_call_count;
                ++stage_draw_call_count;
                if (mode == DepthPipelineMode::depth_test_reverse_z ||
                    mode == DepthPipelineMode::depth_test_write_reverse_z) {
                    ++stats.reverse_z_draw_call_count;
                }
            }

            if (filter_by_pass_bucket_) {
                stats.stage_filtered_batch_count += stage_filtered_batch_count;
                if (stage_draw_call_count == 0U) {
                    ++stats.empty_stage_pass_count;
                }
                if (pass_bucket_ == static_cast<std::uint32_t>(ecs::TextRenderPassHint::opaque)) {
                    stats.opaque_draw_call_count += stage_draw_call_count;
                } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::TextRenderPassHint::transparent)) {
                    stats.transparent_draw_call_count += stage_draw_call_count;
                }
            }
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    if (using_external_depth_target) {
        render::RecordEndColorDepthPass(record_context_, output_target_config, depth_output_target_config);
    } else {
        render::RecordEndColorPass(record_context_, output_target_config);
    }
    image_initialized[record_context_.image_index] = 1U;
    if (use_depth_attachment &&
        !using_external_depth_target &&
        record_context_.image_index < depth_image_initialized.size()) {
        depth_image_initialized[record_context_.image_index] = 1U;
    }
}

void TextRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                          VkExtent2D extent_,
                                          VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void TextRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
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
    for (auto& initialized_flag : image_initialized) {
        initialized_flag = 0U;
    }

    swapchain_extent = extent_;
    swapchain_format = format_;

}

bool TextRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const TextRenderer3DStats& TextRenderer3D::Stats() const noexcept {
    return stats;
}

void TextRenderer3D::ResetPerFrameDrawState(std::uint32_t frame_index_,
                                            std::uint32_t atlas_page_count_) {
    (void)atlas_page_count_;
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    frame_state.instance_count = 0U;
}

void TextRenderer3D::EnsureGpuResourcesForFrame(VulkanContext& context_,
                                                const render::TextRenderer3DPrepareView& prepare_view_,
                                                std::uint32_t frame_index_,
                                                VkDeviceSize required_bytes_) {
    if (frame_index_ >= frame_states.size()) {
        frame_states.resize(frame_index_ + 1U);
    }

    PerFrameState& frame_state = frame_states[frame_index_];
    const VkDeviceSize required_capacity = (required_bytes_ > 0U)
        ? std::max(create_info_cache.initial_vertex_buffer_bytes,
                   NextPow2(required_bytes_))
        : frame_state.vertex_buffer_capacity_bytes;

    if (required_bytes_ == 0U &&
        frame_state.vertex_buffer.buffer == VK_NULL_HANDLE &&
        frame_state.vertex_buffer_capacity_bytes == 0U) {
        return;
    }
    if (frame_state.vertex_buffer.buffer != VK_NULL_HANDLE &&
        frame_state.vertex_buffer_capacity_bytes >= required_bytes_) {
        return;
    }

    resource::BufferHost::DestroyBuffer(context_, frame_state.vertex_buffer);
    frame_state.vertex_buffer_capacity_bytes = 0U;
    frame_state.uploaded_revision = 0U;

    if (required_capacity == 0U) {
        return;
    }

    resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = required_capacity;
    buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_create_info.persistently_mapped = false;

    const std::uint32_t upload_queue_family_index = prepare_view_.upload.QueueFamilyIndex();
    const std::uint32_t graphics_queue_family_index = context_.QueueFamilies().graphics.value();
    if (upload_queue_family_index != graphics_queue_family_index) {
        buffer_create_info.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        buffer_create_info.queue_family_indices.push_back(upload_queue_family_index);
        buffer_create_info.queue_family_indices.push_back(graphics_queue_family_index);
    }

    frame_state.vertex_buffer = resource::BufferHost::CreateBuffer(context_,
                                                                   buffer_create_info,
                                                                   *gpu_memory_host);
    frame_state.vertex_buffer_capacity_bytes = required_capacity;
    frame_state.uploaded_revision = 0U;
}

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

void TextRenderer3D::RecordImageTransitionToColorAttachment(
    const render::FrameRecordContext& record_context_,
    bool has_previous_content_) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = has_previous_content_ ? VK_ACCESS_MEMORY_READ_BIT : 0U;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = has_previous_content_
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_UNDEFINED;
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

void TextRenderer3D::RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
                                                       const resource::ImageResource& depth_resource_,
                                                       bool initialized_) const {
    if (command_buffer_ == VK_NULL_HANDLE ||
        depth_resource_.image == VK_NULL_HANDLE ||
        depth_format == VK_FORMAT_UNDEFINED) {
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

void TextRenderer3D::RecordImageTransitionToPresent(
    const render::FrameRecordContext& record_context_) const {
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

} // namespace vr::text

