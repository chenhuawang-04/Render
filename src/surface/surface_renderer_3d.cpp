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

namespace {

[[nodiscard]] render::BindlessTableId ResolveSampledImageTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SampledImageTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SampledImageTableContractId();
}

[[nodiscard]] render::BindlessTableId ResolveSamplerTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SamplerTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SamplerTableContractId();
}

template<typename HandleT>
[[nodiscard]] std::uintptr_t HandleBits(const HandleT handle_) noexcept {
    if constexpr (std::is_pointer_v<HandleT>) {
        return reinterpret_cast<std::uintptr_t>(handle_);
    } else {
        return static_cast<std::uintptr_t>(handle_);
    }
}

[[nodiscard]] render_graph::ExternalBufferBindingPayload MakeExternalBufferBindingPayload(
    const VkBuffer buffer_,
    const VkDeviceSize offset_,
    const VkDeviceSize range_) noexcept {
    return render_graph::ExternalBufferBindingPayload{
        .native_buffer = HandleBits(buffer_),
        .offset_bytes = static_cast<std::uint64_t>(offset_),
        .size_bytes = static_cast<std::uint64_t>(range_),
    };
}

} // namespace

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

render_graph::ExternalBufferBindingPayload SurfaceRenderer3D::ResolveAppearanceExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const SurfaceRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_appearance_resources.size()) {
        throw std::runtime_error(
            "SurfaceRenderer3D graph appearance descriptor resolver requires prepared frame resources");
    }

    const FrameAppearanceResources& frame_resources =
        renderer->frame_appearance_resources[renderer->active_frame_index];
    if (frame_resources.appearance_records.buffer == VK_NULL_HANDLE ||
        frame_resources.appearance_record_count == 0U) {
        throw std::runtime_error(
            "SurfaceRenderer3D graph appearance descriptor resolver requires uploaded appearance records");
    }

    return MakeExternalBufferBindingPayload(
        frame_resources.appearance_records.buffer,
        0U,
        static_cast<VkDeviceSize>(frame_resources.appearance_record_count) *
            sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>));
}

render_graph::ExternalBufferBindingPayload SurfaceRenderer3D::ResolveIblParamsExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const SurfaceRenderer3D*>(user_data_);
    if (renderer == nullptr || renderer->ibl_host == nullptr) {
        throw std::runtime_error(
            "SurfaceRenderer3D graph IBL descriptor resolver requires initialized IBL host");
    }
    const render::DescriptorBufferBindingView binding =
        renderer->ibl_host->ActiveParamsBufferBinding(renderer->active_frame_index);
    if (binding.buffer == VK_NULL_HANDLE || binding.range == 0U) {
        throw std::runtime_error(
            "SurfaceRenderer3D graph IBL descriptor resolver requires prepared IBL params buffer");
    }
    return MakeExternalBufferBindingPayload(binding.buffer, binding.offset, binding.range);
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
        render::AppearanceSampledSurfaceBinding3D binding =
            render::MakeAppearanceSampledSurfaceBinding3D(
                render::AppearanceSampledSurfaceDomain::surface_image);

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
            binding.base_color_surface.surface_id = component.runtime.source.surface_id;
            binding.surface_sampler_id = component.runtime.source.sampler_id;
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

} // namespace vr::surface

