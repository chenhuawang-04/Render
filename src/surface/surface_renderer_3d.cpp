#include "vr/surface/surface_renderer_3d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
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
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace vr::surface {

bool SurfaceRenderer3D::IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool SurfaceRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags SurfaceRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags;
}

VkFormat SurfaceRenderer3D::ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_) {
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
    throw std::runtime_error("SurfaceRenderer3D failed to resolve usable depth format");
}

std::size_t SurfaceRenderer3D::PipelineModeIndex(PipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t SurfaceRenderer3D::CullModeIndex(CullMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t SurfaceRenderer3D::BlendModeIndex(BlendMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

SurfaceRenderer3D::PipelineMode SurfaceRenderer3D::ResolvePipelineMode(std::uint32_t batch_params_,
                                                                       bool use_depth_) noexcept {
    if (!use_depth_ || (batch_params_ & 0x1U) == 0U) {
        return PipelineMode::no_depth;
    }
    if ((batch_params_ & 0x2U) != 0U) {
        return PipelineMode::depth_read_write;
    }
    return PipelineMode::depth_read;
}

SurfaceRenderer3D::CullMode SurfaceRenderer3D::ResolveCullMode(std::uint32_t batch_params_) noexcept {
    const bool double_sided = (batch_params_ & 0x4U) != 0U;
    return double_sided ? CullMode::none : CullMode::back;
}

SurfaceRenderer3D::BlendMode SurfaceRenderer3D::ResolveBlendMode(std::uint32_t batch_params_) noexcept {
    switch (ecs::DecodeRuntimeBlendPresetBits(batch_params_, ecs::surface3d_runtime_blend_shift)) {
    case ecs::RuntimeBlendPreset::alpha:
        return BlendMode::alpha;
    case ecs::RuntimeBlendPreset::additive:
        return BlendMode::additive;
    case ecs::RuntimeBlendPreset::multiply:
        return BlendMode::multiply;
    case ecs::RuntimeBlendPreset::premultiplied_alpha:
        return BlendMode::premultiplied_alpha;
    case ecs::RuntimeBlendPreset::screen:
        return BlendMode::screen;
    case ecs::RuntimeBlendPreset::opaque:
    default:
        break;
    }
    return BlendMode::opaque;
}

std::uint64_t SurfaceRenderer3D::ComposeBindlessUploadRevision(
    const ecs::Surface3DRuntimeBuildStats& runtime_stats_,
    std::uint32_t image_revision_) noexcept {
    std::uint64_t revision = surface::SurfaceUploadHost::ComposeUploadRevision(
        runtime_stats_.surface_signature,
        runtime_stats_.transform_signature);
    revision ^= static_cast<std::uint64_t>(image_revision_) + 0x9e3779b97f4a7c15ULL +
                (revision << 6U) + (revision >> 2U);
    return revision;
}

void SurfaceRenderer3D::BuildAppearanceRecordsAndAssignIndices() {
    appearance_source_record_scratch.clear();

    const auto& appearance_runtime_scratch = appearance_prepare_bridge.RuntimeScratch();
    const std::uint32_t linked_record_count =
        static_cast<std::uint32_t>(appearance_runtime_scratch.gpu_records.size());
    if (linked_record_count > 0U) {
        appearance_source_record_scratch.resize(linked_record_count);
        for (std::uint32_t index = 0U; index < linked_record_count; ++index) {
            appearance_source_record_scratch[index] = appearance_runtime_scratch.gpu_records[index];
        }
    }

    for (auto& instance : runtime_scratch.instances) {
        ecs::AppearanceGpuRecord<ecs::Dim3> synthesized_record{};
        ecs::AppearanceRuntimeBridge3D appearance_bridge = ecs::MakeAppearanceRuntimeBridge3D(nullptr);
        render::AppearanceTextureBindingIds3D binding{};
        binding.texture_source = render::AppearanceTextureSource3D::surface_image;

        if (surface_components != nullptr && instance.component_index < component_count) {
            const ecs::Surface<ecs::Dim3>& component = surface_components[instance.component_index];
            const auto linked_appearance =
                render::ResolveLinkedAppearanceRecord(component.runtime.route.appearance_handle,
                                                      appearance_runtime_scratch);
            if (linked_appearance.record != nullptr) {
                instance.appearance_record_index = linked_appearance.record_index;
                continue;
            }

            appearance_bridge = ecs::ReadAppearanceRuntimeBridge3D(component.runtime);
            binding.base_color_texture_id = component.runtime.source.surface_id;
            binding.sampler_state_id = component.runtime.source.sampler_id;
        }

        render::BuildAppearanceGpuRecord3DFromRuntimeBridge(
            appearance_bridge,
            binding,
            synthesized_record);
        instance.appearance_record_index =
            static_cast<std::uint32_t>(appearance_source_record_scratch.size());
        appearance_source_record_scratch.push_back(synthesized_record);
    }
}

void SurfaceRenderer3D::Initialize(const SurfaceRenderer3DCreateInfo& create_info_) {
    create_info_cache = create_info_;

    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::SurfaceRuntimeSystem<ecs::Dim3>::Reserve(
            runtime_scratch,
            create_info_cache.reserve_component_count,
            create_info_cache.reserve_instance_count);
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch,
                                               create_info_cache.reserve_component_count);
    }
    if (create_info_cache.reserve_dirty_component_count > 0U ||
        create_info_cache.reserve_instance_count > 0U) {
        ecs::SurfaceUploadPlanSystem<ecs::Dim3>::Reserve(
            plan_scratch,
            create_info_cache.reserve_dirty_component_count,
            create_info_cache.reserve_instance_count);
    }

    appearance_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& per_blend : pipeline_ids) {
        for (auto& per_mode : per_blend) {
            for (auto& pipeline_id : per_mode) {
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
    active_ibl_params_descriptor_set = VK_NULL_HANDLE;
    active_ibl_specular_texture_slot = 0U;
    active_ibl_brdf_lut_texture_slot = 0U;
    active_ibl_sampler_slot = 0U;
    output_target_config = {};
    depth_output_target_config = {};
    image_initialized.clear();
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_build_invoked = false;
    appearance_prepare_bridge.Reset();

    surface_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    appearance_component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    surface_upload_host = nullptr;
    surface_image_host = nullptr;
    texture_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    ibl_host = nullptr;
    gpu_memory_host = nullptr;
    frame_appearance_resources.clear();
    appearance_record_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_image_write_scratch.clear();

    last_upload_result = {};
    culling_stats = {};
    stats = {};
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    appearance_record_bindless_revision_seen = 0U;
    appearance_record_texture_host_revision_seen = 0U;
    appearance_record_content_revision = 0U;
    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;

    initialized = true;
}

void SurfaceRenderer3D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);

    surface_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    appearance_component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    surface_upload_host = nullptr;
    surface_image_host = nullptr;
    texture_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    ibl_host = nullptr;
    gpu_memory_host = nullptr;

    appearance_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& per_blend : pipeline_ids) {
        for (auto& per_mode : per_blend) {
            for (auto& pipeline_id : per_mode) {
                pipeline_id = {};
            }
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.cache = {};
    appearance_prepare_bridge.Reset();
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_build_invoked = false;
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    culling_stats = {};
    plan_scratch.instance_indices.clear();
    plan_scratch.ranges.clear();
    plan_scratch.dense_marks.clear();
    active_ibl_params_descriptor_set = VK_NULL_HANDLE;
    active_ibl_specular_texture_slot = 0U;
    active_ibl_brdf_lut_texture_slot = 0U;
    active_ibl_sampler_slot = 0U;
    output_target_config = {};
    depth_output_target_config = {};
    image_initialized.clear();
    for (auto& frame_resources : frame_appearance_resources) {
        resource::BufferHost::DestroyBuffer(context_, frame_resources.appearance_records);
    }
    frame_appearance_resources.clear();
    appearance_record_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_image_write_scratch.clear();
    last_upload_result = {};
    stats = {};

    depth_format = VK_FORMAT_UNDEFINED;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    appearance_record_bindless_revision_seen = 0U;
    appearance_record_texture_host_revision_seen = 0U;
    appearance_record_content_revision = 0U;
    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
    initialized = false;
}

void SurfaceRenderer3D::SetHost(SurfaceUploadHost* upload_host_) noexcept {
    surface_upload_host = upload_host_;
}

void SurfaceRenderer3D::SetImageHost(SurfaceImageHost* image_host_) noexcept {
    surface_image_host = image_host_;
}

void SurfaceRenderer3D::SetHosts(SurfaceUploadHost* upload_host_,
                                 SurfaceImageHost* image_host_) noexcept {
    surface_upload_host = upload_host_;
    surface_image_host = image_host_;
}

void SurfaceRenderer3D::SetSceneData(ecs::Surface<ecs::Dim3>* surface_components_,
                                     ecs::Transform<ecs::Dim3>* transforms_,
                                     std::uint32_t component_count_,
                                     ecs::Camera<ecs::Dim3>* camera_component_,
                                     ecs::Transform<ecs::Dim3>* camera_transform_,
                                     ecs::Bounds<ecs::Dim3>* bounds_components_) noexcept {
    surface_components = surface_components_;
    transforms = transforms_;
    component_count = component_count_;
    camera_component = camera_component_;
    camera_transform = camera_transform_;
    bounds_components = bounds_components_;
    if (component_count_ > 0U) {
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch, component_count_);
    }
}

void SurfaceRenderer3D::SetAppearanceData(ecs::Appearance<ecs::Dim3>* appearance_components_,
                                          std::uint32_t appearance_component_count_) noexcept {
    appearance_component_count = appearance_component_count_;
    appearance_prepare_bridge.SetAppearanceData(appearance_components_,
                                                appearance_component_count_);
    appearance_prepare_bridge.Reserve(appearance_component_count_);
}

void SurfaceRenderer3D::SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                                              std::uint32_t dirty_component_count_) noexcept {
    pending_dirty_component_indices = dirty_component_indices_;
    pending_dirty_component_count = dirty_component_count_;
}

void SurfaceRenderer3D::SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                               std::uint32_t dirty_component_count_) noexcept {
    appearance_prepare_bridge.SetDirtyHint(dirty_component_indices_,
                                           dirty_component_count_);
}

void SurfaceRenderer3D::SetAppearanceCoordinator(
    render::AppearanceFrameCoordinator<ecs::Dim3>* appearance_frame_coordinator_) noexcept {
    appearance_prepare_bridge.SetCoordinator(appearance_frame_coordinator_);
    appearance_prepare_bridge.Reserve(appearance_component_count);
}

void SurfaceRenderer3D::SetOutputTargetConfig(
    const render::RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void SurfaceRenderer3D::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void SurfaceRenderer3D::SetDepthTargetConfig(
    const render::RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept {
    depth_output_target_config = depth_output_target_config_;
}

void SurfaceRenderer3D::ResetDepthTargetConfig() noexcept {
    depth_output_target_config = {};
}

void SurfaceRenderer3D::PrepareFrame(const render::SurfaceRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame called before Initialize");
    }
    if (surface_upload_host == nullptr || !surface_upload_host->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame requires initialized SurfaceUploadHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    ibl_host = &prepare_view_.ibl;
    gpu_memory_host = &prepare_view_.gpu_memory;
    texture_host = prepare_view_.texture;
    if (prepare_view_.bindless != nullptr) {
        bindless_resources = prepare_view_.bindless;
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "SurfaceRenderer3D::PrepareFrame requires initialized BindlessResourceSystem");
    }
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (texture_host != nullptr &&
        texture_host->IsInitialized() &&
        (!texture_host->BindlessConfig().Enabled() ||
         texture_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureTextureHost(*texture_host);
    }
    if (surface_image_host != nullptr &&
        surface_image_host->IsInitialized() &&
        (!surface_image_host->BindlessConfig().Enabled() ||
         surface_image_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureSurfaceImageHost(*surface_image_host);
    }
    active_frame_index = prepare_view_.frame.frame_index;
    if (active_frame_index >= frame_appearance_resources.size()) {
        frame_appearance_resources.resize(active_frame_index + 1U);
    }
    {
        FrameAppearanceResources& frame_resources = frame_appearance_resources[active_frame_index];
        frame_resources.descriptor_set = VK_NULL_HANDLE;
        frame_resources.descriptor_buffer_signature = 0U;
    }
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen,
                                           prepare_view_.progress.completed_submit_value);

    surface_upload_host->BeginFrame(*context,
                                    active_frame_index,
                                    last_submitted_value_seen,
                                    completed_submit_value_seen);
    if (surface_image_host != nullptr && surface_image_host->IsInitialized()) {
        surface_image_host->BeginFrame(*context, completed_submit_value_seen);
    }
    const render::IblEnvironmentId ibl_environment_id{prepare_view_.ibl_environment_id};
    const asset::TextureId ibl_brdf_lut_texture_id{prepare_view_.ibl_brdf_lut_texture_id};
    if (ibl_environment_id.IsValid() || ibl_brdf_lut_texture_id.IsValid()) {
        ibl_host->PrepareEnvironmentFrame(render::MakeIblHostPrepareView(prepare_view_),
                                          ibl_environment_id,
                                          ibl_brdf_lut_texture_id);
    } else {
        ibl_host->PrepareFrame(render::MakeIblHostPrepareView(prepare_view_));
    }
    active_ibl_params_descriptor_set = ibl_host->ActiveParamsDescriptorSet(active_frame_index);
    active_ibl_specular_texture_slot = ibl_host->ActiveSpecularTextureSlot();
    active_ibl_brdf_lut_texture_slot = ibl_host->ActiveBrdfLutTextureSlot();
    active_ibl_sampler_slot = ibl_host->ActiveSamplerSlot();
    if (active_ibl_params_descriptor_set == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer3D::PrepareFrame failed to resolve IBL params descriptor set");
    }

    stats = {};
    stats.component_count = component_count;
    stats.appearance_component_count = appearance_component_count;
    last_upload_result = {};
    culling_stats = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    const auto appearance_prepare_result = appearance_prepare_bridge.PrepareSurface(
        surface_components,
        component_count,
        active_frame_index);
    appearance_build_invoked = appearance_prepare_result.build_invoked;
    if (appearance_prepare_result.has_appearance_data) {
        appearance_runtime_stats = appearance_prepare_result.runtime_stats;
        appearance_link_stats = appearance_prepare_result.link_stats;
        stats.appearance_visible_count = appearance_runtime_stats.visible_count;
        stats.appearance_updated_record_count = appearance_runtime_stats.updated_record_count;
        stats.appearance_cache_reused = appearance_prepare_result.cache_reused;
        stats.appearance_link_scanned_count = appearance_link_stats.scanned_count;
        stats.appearance_link_updated_count = appearance_link_stats.updated_count;
    }

    if (surface_components == nullptr || component_count == 0U) {
        appearance_source_record_scratch.clear();
        EnsureAppearanceResourcesForFrame(bindless_revision_now);
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        pending_dirty_component_indices = nullptr;
        pending_dirty_component_count = 0U;
        return;
    }

    ecs::Surface3DRuntimeBuildHint runtime_build_hint{};
    runtime_build_hint.transform_dirty_component_indices = pending_dirty_component_indices;
    runtime_build_hint.transform_dirty_component_count = pending_dirty_component_count;
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

    last_upload_result.runtime = ecs::SurfaceRuntimeSystem<ecs::Dim3>::Build(
        surface_components,
        transforms,
        component_count,
        runtime_scratch,
        create_info_cache.runtime_upload_options.runtime_build,
        runtime_build_hint);

    if (!runtime_scratch.instances.empty() &&
        last_upload_result.runtime.emitted_instance_count > 0U) {
        BuildAppearanceRecordsAndAssignIndices();
        EnsureAppearanceResourcesForFrame(bindless_revision_now);
        const std::uint64_t upload_revision = ComposeBindlessUploadRevision(
            last_upload_result.runtime,
            surface_image_host != nullptr ? surface_image_host->Stats().revision : 0U);

        if (surface::SurfaceUploadHost::ShouldAttemptPartialUpload(
                last_upload_result.runtime,
                runtime_build_hint,
                create_info_cache.runtime_upload_options)) {
            last_upload_result.plan = ecs::SurfaceUploadPlanSystem<ecs::Dim3>::BuildRangesFromDirtyComponents(
                runtime_scratch,
                runtime_build_hint.transform_dirty_component_indices,
                runtime_build_hint.transform_dirty_component_count,
                create_info_cache.runtime_upload_options.plan_build,
                plan_scratch);

            if (last_upload_result.plan.range_count > 0U &&
                last_upload_result.plan.covered_instance_count > 0U) {
                static_assert(sizeof(ecs::SurfaceUploadPatchRange) == sizeof(surface::SurfaceUploadPatch));
                static_assert(alignof(ecs::SurfaceUploadPatchRange) == alignof(surface::SurfaceUploadPatch));
                const auto* patches = reinterpret_cast<const surface::SurfaceUploadPatch*>(
                    plan_scratch.ranges.data());
                last_upload_result.upload = surface_upload_host->Upload3DInstancePatches(
                    *context,
                    *upload_host,
                    active_frame_index,
                    runtime_scratch.instances.data(),
                    static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                    patches,
                    last_upload_result.plan.range_count,
                    upload_revision);
                last_upload_result.used_partial_upload =
                    last_upload_result.upload.partial && last_upload_result.upload.uploaded;
                last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;
            } else if (last_upload_result.runtime.transform_rewritten_instance_count == 0U) {
                last_upload_result.skipped_upload = true;
            } else {
                last_upload_result.upload = surface_upload_host->Upload3DInstances(
                    *context,
                    *upload_host,
                    active_frame_index,
                    runtime_scratch.instances.data(),
                    static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                    upload_revision);
                last_upload_result.used_partial_upload = false;
                last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;
            }
        } else {
            last_upload_result.upload = surface_upload_host->Upload3DInstances(
                *context,
                *upload_host,
                active_frame_index,
                runtime_scratch.instances.data(),
                static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                upload_revision);
            last_upload_result.used_partial_upload = false;
            last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;
        }
    } else {
        appearance_source_record_scratch.clear();
        EnsureAppearanceResourcesForFrame(bindless_revision_now);
        last_upload_result.skipped_upload = true;
    }

    stats.visible_component_count = last_upload_result.runtime.batch.visible_count;
    stats.instance_count = last_upload_result.runtime.emitted_instance_count;
    stats.draw_batch_count = last_upload_result.runtime.emitted_batch_count;
    stats.depth_test_batch_count = last_upload_result.runtime.depth_test_batch_count;
    stats.depth_write_batch_count = last_upload_result.runtime.depth_write_batch_count;
    stats.uploaded_instance_count = last_upload_result.upload.element_count;
    stats.uploaded_patch_count = last_upload_result.upload.patch_count;
    stats.uploaded_bytes += last_upload_result.upload.size_bytes;
    stats.cache_reused = last_upload_result.runtime.cache_reused;
    stats.transform_only_update = last_upload_result.runtime.transform_only_update;
    stats.used_partial_upload = last_upload_result.used_partial_upload;
    stats.skipped_upload = last_upload_result.skipped_upload;

    pending_dirty_component_indices = nullptr;
    pending_dirty_component_count = 0U;
}

void SurfaceRenderer3D::Record(const render::FrameRecordContext& record_context_) {
    RecordInternal(record_context_, 0U, false);
}

void SurfaceRenderer3D::RecordSceneStage(const render::FrameRecordContext& record_context_,
                                         render::SceneRenderStage stage_) {
    RecordInternal(record_context_, render::SceneRenderStagePassHintValue(stage_), true);
}

void SurfaceRenderer3D::RecordInternal(const render::FrameRecordContext& record_context_,
                                       std::uint32_t pass_bucket_,
                                       bool filter_by_pass_bucket_) {
    if (!initialized) {
        throw std::runtime_error("SurfaceRenderer3D::Record called before Initialize");
    }
    if (context == nullptr ||
        pipeline_host == nullptr ||
        gpu_memory_host == nullptr) {
        throw std::runtime_error("SurfaceRenderer3D::Record called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("SurfaceRenderer3D::Record requires initialized BindlessResourceSystem");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("SurfaceRenderer3D::Record requires valid command buffer");
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
        throw std::runtime_error("SurfaceRenderer3D::Record resolved zero-sized render extent");
    }

    bool using_external_depth_target = false;
    VkFormat active_depth_format = VK_FORMAT_UNDEFINED;
    bool has_previous_depth_content = false;
    if (create_info_cache.enable_depth &&
        record_context_.render_target_host != nullptr &&
        record_context_.render_target_host->IsValid(depth_output_target_config.depth_target)) {
        const render::RenderTargetResolvedView depth_view =
            record_context_.render_target_host->ResolveView(depth_output_target_config.depth_target);
        if (depth_view.image_view == VK_NULL_HANDLE) {
            throw std::runtime_error("SurfaceRenderer3D::Record external depth target has null view");
        }
        has_previous_depth_content = depth_view.state != render::RenderTargetStateKind::undefined;
    }

    render::ResolvedColorRenderPass color_pass{};
    if (create_info_cache.enable_depth) {
        if (record_context_.render_target_host != nullptr &&
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
            active_depth_format = color_pass.depth_target.format;
            using_external_depth_target = true;
        } else {
            color_pass = render::BuildColorRenderPass(record_context_,
                                                      output_target_config,
                                                      create_info_cache.clear_swapchain,
                                                      create_info_cache.clear_color,
                                                      has_previous_content);
            const std::uint32_t required_image_count = static_cast<std::uint32_t>(std::max<std::size_t>(
                image_initialized.size(),
                static_cast<std::size_t>(record_context_.image_index + 1U)));
            EnsureDepthResources(*context, required_image_count, render_extent);
            if (record_context_.image_index >= depth_images.size()) {
                throw std::runtime_error("SurfaceRenderer3D::Record depth image index out of range");
            }
            const resource::ImageResource& depth_resource = depth_images[record_context_.image_index];
            if (depth_resource.image == VK_NULL_HANDLE || depth_resource.default_view == VK_NULL_HANDLE) {
                throw std::runtime_error("SurfaceRenderer3D::Record depth resource is invalid");
            }
            const bool depth_initialized = depth_image_initialized[record_context_.image_index] != 0U;
            RecordDepthTransitionToAttachment(record_context_.command_buffer,
                                              depth_resource,
                                              depth_initialized);
            color_pass.rendering_info.depth_attachment = {};
            color_pass.rendering_info.depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_pass.rendering_info.depth_attachment.imageView = depth_resource.default_view;
            color_pass.rendering_info.depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            color_pass.rendering_info.depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
            color_pass.rendering_info.depth_attachment.resolveImageView = VK_NULL_HANDLE;
            color_pass.rendering_info.depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color_pass.rendering_info.depth_attachment.loadOp = (create_info_cache.clear_depth || !depth_initialized)
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
                          *bindless_resources,
                          *pipeline_host,
                          color_pass.target.format,
                          active_depth_format);
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
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

    if (last_upload_result.upload.buffer != VK_NULL_HANDLE &&
        !runtime_scratch.draw_batches.empty()) {
        std::uint32_t stage_draw_call_count = 0U;
        std::uint32_t stage_filtered_batch_count = 0U;
        const VkBuffer vertex_buffer = last_upload_result.upload.buffer;
        const VkDeviceSize vertex_offset = last_upload_result.upload.offset;
        vkCmdBindVertexBuffers(record_context_.command_buffer,
                               0U,
                               1U,
                               &vertex_buffer,
                               &vertex_offset);
        const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);

        PushConstants push_constants{};
        if (camera_component != nullptr) {
            push_constants.view_projection = camera_component->runtime.view_projection_matrix;
        } else {
            push_constants.view_projection = ecs::spatial_math::IdentityMatrix4x4();
        }
        if (camera_transform != nullptr) {
            push_constants.camera_position = ecs::Float4{
                .x = camera_transform->runtime.world_matrix.m[12],
                .y = camera_transform->runtime.world_matrix.m[13],
                .z = camera_transform->runtime.world_matrix.m[14],
                .w = 1.0F
            };
        } else {
            push_constants.camera_position = ecs::Float4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        }
        push_constants.params = 0U;
        push_constants.ibl_specular_texture_slot = active_ibl_specular_texture_slot;
        push_constants.ibl_brdf_lut_texture_slot = active_ibl_brdf_lut_texture_slot;
        push_constants.ibl_sampler_slot = active_ibl_sampler_slot;

        const VkDescriptorSet appearance_descriptor_set =
            (active_frame_index < frame_appearance_resources.size())
                ? frame_appearance_resources[active_frame_index].descriptor_set
                : VK_NULL_HANDLE;
        const std::array<VkDescriptorSet, 4U> descriptor_sets{
            bindless_resources->SampledImageSet(),
            bindless_resources->SamplerSet(),
            appearance_descriptor_set,
            active_ibl_params_descriptor_set
        };
        if (descriptor_sets[0U] == VK_NULL_HANDLE || descriptor_sets[1U] == VK_NULL_HANDLE) {
            throw std::runtime_error("SurfaceRenderer3D::Record requires valid bindless descriptor sets");
        }
        if (descriptor_sets[2U] == VK_NULL_HANDLE) {
            throw std::runtime_error("SurfaceRenderer3D::Record requires valid appearance descriptor set");
        }
        if (descriptor_sets[3U] == VK_NULL_HANDLE) {
            throw std::runtime_error("SurfaceRenderer3D::Record requires valid IBL params descriptor set");
        }

        render::GraphicsPipelineId bound_pipeline{};
        bool shared_state_bound = false;
        for (const ecs::Surface3DDrawBatch& batch : runtime_scratch.draw_batches) {
            if (filter_by_pass_bucket_ &&
                ecs::SurfaceSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                ++stage_filtered_batch_count;
                continue;
            }
            if (batch.instance_count == 0U) {
                ++stats.skipped_batch_count;
                continue;
            }

            const BlendMode blend_mode = ResolveBlendMode(batch.params);
            const PipelineMode mode = ResolvePipelineMode(batch.params, active_depth_format != VK_FORMAT_UNDEFINED);
            const CullMode cull_mode = ResolveCullMode(batch.params);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                 *pipeline_host,
                                                                                 color_pass.target.format,
                                                                                 active_depth_format,
                                                                                 blend_mode,
                                                                                 mode,
                                                                                 cull_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                continue;
            }

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(record_context_.command_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                bound_pipeline = pipeline_id;
            }

            if (!shared_state_bound) {
                vkCmdPushConstants(record_context_.command_buffer,
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                vkCmdBindDescriptorSets(record_context_.command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_layout,
                                        0U,
                                        static_cast<std::uint32_t>(descriptor_sets.size()),
                                        descriptor_sets.data(),
                                        0U,
                                        nullptr);
                shared_state_bound = true;
                ++stats.descriptor_set_bind_count;
                ++stats.ibl_descriptor_set_bind_count;
            }

            vkCmdDraw(record_context_.command_buffer,
                      6U,
                      batch.instance_count,
                      0U,
                      batch.instance_begin);
            ++stats.draw_call_count;
            ++stage_draw_call_count;
        }

        if (filter_by_pass_bucket_) {
            stats.stage_filtered_batch_count += stage_filtered_batch_count;
            if (stage_draw_call_count == 0U) {
                ++stats.empty_stage_pass_count;
            }
            if (pass_bucket_ == static_cast<std::uint32_t>(ecs::SurfaceRenderPassHint::opaque)) {
                stats.opaque_draw_call_count += stage_draw_call_count;
            } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::SurfaceRenderPassHint::transparent)) {
                stats.transparent_draw_call_count += stage_draw_call_count;
            }
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    render::RecordEndColorPass(record_context_, output_target_config);
    if (create_info_cache.enable_depth && !using_external_depth_target) {
        depth_image_initialized[record_context_.image_index] = 1U;
    }
    image_initialized[record_context_.image_index] = 1U;
}

void SurfaceRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                             VkExtent2D extent_,
                                             VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void SurfaceRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                             VkExtent2D extent_,
                                             VkFormat format_,
                                             std::uint64_t last_submitted_value_,
                                             std::uint64_t completed_submit_value_) {
    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);

    swapchain_extent = extent_;
    swapchain_format = format_;
    image_initialized.resize(image_count_);
    for (auto& value : image_initialized) {
        value = 0U;
    }
}

bool SurfaceRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const SurfaceRenderer3DStats& SurfaceRenderer3D::Stats() const noexcept {
    return stats;
}

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

    render::DescriptorSetLayoutDesc layout_desc{};
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0U;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1U;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;
    layout_desc.bindings.push_back(binding);

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

    FrameAppearanceResources& frame_resources = frame_appearance_resources[active_frame_index];
    const std::uint32_t appearance_record_count =
        static_cast<std::uint32_t>(appearance_source_record_scratch.size());
    const std::uint32_t appearance_upload_count = std::max<std::uint32_t>(appearance_record_count, 1U);
    const VkDeviceSize appearance_record_bytes =
        static_cast<VkDeviceSize>(appearance_upload_count) *
        sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>);
    auto mix_texture_source_revision = [](std::uint32_t accumulator_,
                                          std::uint32_t revision_) noexcept {
        accumulator_ ^= revision_ + 0x9e3779b9U + (accumulator_ << 6U) + (accumulator_ >> 2U);
        return accumulator_;
    };
    std::uint32_t texture_host_revision = 0U;
    if (texture_host != nullptr && texture_host->IsInitialized()) {
        texture_host_revision =
            mix_texture_source_revision(texture_host_revision, texture_host->Stats().revision);
    }
    if (surface_image_host != nullptr && surface_image_host->IsInitialized()) {
        texture_host_revision = mix_texture_source_revision(texture_host_revision,
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
    for (std::uint32_t index = 0U; index < appearance_upload_count; ++index) {
        ecs::AppearanceGpuRecord<ecs::Dim3> encoded_record{};
        if (index < appearance_record_count) {
            render::EncodeAppearanceGpuRecord3DForSampling(
                appearance_source_record_scratch[index],
                bindless_resources,
                texture_host,
                surface_image_host,
                nullptr,
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

    PrepareAppearanceDescriptorSetForFrame(active_frame_index);
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
        .binding = 0U,
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

void SurfaceRenderer3D::RecordImageTransitionToColorAttachment(
    const render::FrameRecordContext& record_context_,
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

void SurfaceRenderer3D::RecordImageTransitionToPresent(
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

void SurfaceRenderer3D::RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
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

} // namespace vr::surface

