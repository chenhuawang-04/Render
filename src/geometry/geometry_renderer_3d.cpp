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

namespace {

void HashCombine64(std::uint64_t& hash_, std::uint64_t value_) noexcept {
    constexpr std::uint64_t k_hash_prime = 1099511628211ULL;
    hash_ ^= value_;
    hash_ *= k_hash_prime;
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

[[nodiscard]] render_graph::ExternalBufferBindingPayload MakeExternalBufferBindingPayload(
    const light::LightShadowBufferRange& range_) noexcept {
    return MakeExternalBufferBindingPayload(range_.buffer,
                                            range_.offset,
                                            range_.size_bytes);
}

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

[[nodiscard]] render::AppearanceSampledSurfaceBinding3D
MakeGeometryAppearanceSampledSurfaceBinding(
    const GeometryAppearanceDesc* appearance_desc_) noexcept {
    if (appearance_desc_ != nullptr) {
        return appearance_desc_->sampled_surface_binding;
    }

    return render::MakeAppearanceSampledSurfaceBinding3D(
        render::AppearanceSampledSurfaceDomain::geometry_image);
}

} // namespace

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

std::size_t GeometryRenderer3D::BlendModeIndex(BlendMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t GeometryRenderer3D::LowerBoundResolvedAppearanceIndex(
    const GeometryRenderer3DMcVector<ResolvedAppearanceEntry>& entries_,
    std::uint32_t appearance_id_) noexcept {
    std::size_t first = 0U;
    std::size_t count = entries_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (entries_[it].appearance_id < appearance_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

std::uint64_t GeometryRenderer3D::ApplyAppearanceStateOverrides() {
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

    if (runtime_scratch.instances.empty()) {
        return 14695981039346656037ULL;
    }

    const bool has_appearance_host = geometry_appearance_host != nullptr &&
                                   geometry_appearance_host->IsInitialized();
    const GeometryAppearanceHost::AppearanceRecord* cached_appearance_record = nullptr;
    std::uint32_t cached_appearance_lookup_visual_resource_id =
        std::numeric_limits<std::uint32_t>::max();
    std::uint64_t appearance_override_signature = 14695981039346656037ULL;

    for (auto& instance : runtime_scratch.instances) {
        const ecs::Geometry<ecs::Dim3>* component = nullptr;
        ecs::AppearanceRuntimeBridge3D appearance_bridge = ecs::MakeAppearanceRuntimeBridge3D(nullptr);
        if (geometry_components != nullptr && instance.component_index < component_count) {
            component = &geometry_components[instance.component_index];
            appearance_bridge = ecs::ReadAppearanceRuntimeBridge3D(component->runtime);
        }

        const std::uint32_t appearance_lookup_visual_resource_id =
            component != nullptr
                ? component->runtime.route.authoring_visual_resource_id
                : instance.effective_visual_resource_id;
        const GeometryAppearanceDesc* appearance_desc = nullptr;
        if (has_appearance_host && appearance_lookup_visual_resource_id != 0U) {
            if (appearance_lookup_visual_resource_id != cached_appearance_lookup_visual_resource_id) {
                cached_appearance_record =
                    geometry_appearance_host->FindAppearance(appearance_lookup_visual_resource_id);
                cached_appearance_lookup_visual_resource_id =
                    appearance_lookup_visual_resource_id;
            }
            appearance_desc = cached_appearance_record != nullptr ? &cached_appearance_record->desc : nullptr;
        }

        const render::LinkedAppearanceRecord3D linked_appearance =
            (component != nullptr)
                ? render::ResolveLinkedAppearanceRecord(component->runtime.route.appearance_handle,
                                                        appearance_runtime_scratch)
                : render::LinkedAppearanceRecord3D{};
        instance.appearance_record_index = linked_appearance.record_index;
        if (linked_appearance.record == nullptr) {
            ecs::AppearanceGpuRecord<ecs::Dim3> synthesized_record{};
            render::BuildAppearanceGpuRecord3DFromRuntimeBridge(
                appearance_bridge,
                MakeGeometryAppearanceSampledSurfaceBinding(appearance_desc),
                synthesized_record);

            const GeometryAppearanceResolvedState resolved_state =
                ResolveFinalGeometryAppearanceStateFromRuntimeBridge(&appearance_bridge,
                                                                     appearance_desc,
                                                                     nullptr);
            synthesized_record.appearance_params = {
                resolved_state.metallic,
                resolved_state.roughness,
                resolved_state.normal_scale,
                resolved_state.occlusion_strength
            };
            synthesized_record.extras[1U] =
                appearance_desc != nullptr ? appearance_desc->alpha_cutoff : appearance_bridge.alpha_cutoff;
            if (appearance_desc != nullptr &&
                (appearance_desc->flags & geometry_appearance_flag_alpha_test) != 0U) {
                render::SetAppearanceGpuRecord3DAlphaMode(
                    synthesized_record,
                    ecs::AppearanceAlphaMode::mask);
            }

            instance.appearance_record_index =
                static_cast<std::uint32_t>(appearance_source_record_scratch.size());
            appearance_source_record_scratch.push_back(synthesized_record);
        }

        HashCombine64(appearance_override_signature,
                      static_cast<std::uint64_t>(instance.appearance_record_index));
        HashCombine64(appearance_override_signature,
                      static_cast<std::uint64_t>(instance.params));
    }

    return appearance_override_signature;
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

GeometryRenderer3D::BlendMode GeometryRenderer3D::ResolveBlendMode(
    const ecs::Geometry3DDrawBatch& batch_) noexcept {
    switch (ecs::DecodeRuntimeBlendPresetBits(batch_.params,
                                              ecs::geometry_runtime_blend_shift)) {
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

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveLightRecordsExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph light-record resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.light_records.buffer == VK_NULL_HANDLE || frame.light_records.size_bytes == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph light-record resolver requires uploaded light buffers");
    }
    return MakeExternalBufferBindingPayload(frame.light_records);
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveClusterHeadersExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph cluster-header resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.cluster_headers.buffer == VK_NULL_HANDLE || frame.cluster_headers.size_bytes == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph cluster-header resolver requires uploaded cluster headers");
    }
    return MakeExternalBufferBindingPayload(frame.cluster_headers);
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveClusterIndicesExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph cluster-index resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.cluster_indices.buffer == VK_NULL_HANDLE || frame.cluster_indices.size_bytes == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph cluster-index resolver requires uploaded cluster indices");
    }
    return MakeExternalBufferBindingPayload(frame.cluster_indices);
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveShadowViewsExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph shadow-view resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.shadow_views.buffer == VK_NULL_HANDLE || frame.shadow_views.size_bytes == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph shadow-view resolver requires uploaded shadow views");
    }
    return MakeExternalBufferBindingPayload(frame.shadow_views);
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveLightingUniformExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph lighting-uniform resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.lighting_uniform.buffer == VK_NULL_HANDLE || frame.lighting_uniform.size_bytes == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph lighting-uniform resolver requires uploaded uniform buffer");
    }
    return MakeExternalBufferBindingPayload(frame.lighting_uniform);
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveSkeletalComponentsExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph skeletal-component resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.skeletal_components.buffer == VK_NULL_HANDLE || frame.skeletal_component_count == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph skeletal-component resolver requires uploaded skeletal components");
    }
    return MakeExternalBufferBindingPayload(
        frame.skeletal_components.buffer,
        0U,
        static_cast<VkDeviceSize>(frame.skeletal_component_count) * sizeof(GeometrySkeletalComponentGpu));
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveSkeletalMatricesExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph skeletal-matrix resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.skeletal_matrices.buffer == VK_NULL_HANDLE || frame.skeletal_matrix_count == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph skeletal-matrix resolver requires uploaded skeletal matrices");
    }
    return MakeExternalBufferBindingPayload(
        frame.skeletal_matrices.buffer,
        0U,
        static_cast<VkDeviceSize>(frame.skeletal_matrix_count) * sizeof(GeometrySkeletalMatrixGpu));
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveAppearanceExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr ||
        renderer->active_frame_index >= renderer->frame_lighting_resources.size()) {
        throw std::runtime_error(
            "GeometryRenderer3D graph appearance resolver requires prepared frame resources");
    }
    const FrameLightingResources& frame = renderer->frame_lighting_resources[renderer->active_frame_index];
    if (frame.appearance_records.buffer == VK_NULL_HANDLE || frame.appearance_record_count == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph appearance resolver requires uploaded appearance records");
    }
    return MakeExternalBufferBindingPayload(
        frame.appearance_records.buffer,
        0U,
        static_cast<VkDeviceSize>(frame.appearance_record_count) *
            sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>));
}

render_graph::ExternalBufferBindingPayload GeometryRenderer3D::ResolveIblParamsExternalBufferBinding(
    const void* user_data_) {
    const auto* renderer = static_cast<const GeometryRenderer3D*>(user_data_);
    if (renderer == nullptr || renderer->ibl_host == nullptr) {
        throw std::runtime_error(
            "GeometryRenderer3D graph IBL resolver requires initialized IBL host");
    }
    const render::DescriptorBufferBindingView binding =
        renderer->ibl_host->ActiveParamsBufferBinding(renderer->active_frame_index);
    if (binding.buffer == VK_NULL_HANDLE || binding.range == 0U) {
        throw std::runtime_error(
            "GeometryRenderer3D graph IBL resolver requires prepared IBL params buffer");
    }
    return MakeExternalBufferBindingPayload(binding.buffer, binding.offset, binding.range);
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

    geometry_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    appearance_component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    skeletal_outputs = nullptr;
    skeletal_output_count = 0U;
    vertex_deform_outputs = nullptr;
    vertex_deform_output_count = 0U;
    morph_outputs = nullptr;
    morph_output_count = 0U;
    frame_sequence_outputs = nullptr;
    frame_sequence_output_count = 0U;

    geometry_resource_host = nullptr;
    geometry_upload_host = nullptr;
    geometry_appearance_host = nullptr;
    geometry_image_host = nullptr;
    texture_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;

    lighting_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& blend_pipelines : pipeline_ids) {
        for (auto& mode_pipelines : blend_pipelines) {
            for (auto& topology_pipelines : mode_pipelines) {
                for (auto& pipeline_id : topology_pipelines) {
                    pipeline_id = {};
                }
            }
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    active_frame_index = 0U;
    instance_range = {};
    bindless_revision_seen = 0U;
    appearance_record_bindless_revision_seen = 0U;
    appearance_record_texture_host_revision_seen = 0U;
    appearance_record_content_revision = 0U;

    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    image_initialized.clear();
    frame_lighting_resources.clear();
    resolved_appearances.clear();
    appearance_record_scratch.clear();
    skeletal_component_scratch.clear();
    skeletal_matrix_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();

    if (create_info_cache.reserve_appearance_set_count > 0U) {
        resolved_appearances.reserve(create_info_cache.reserve_appearance_set_count);
    }
    if (create_info_cache.reserve_component_count > 0U) {
        appearance_record_scratch.reserve(create_info_cache.reserve_component_count);
        skeletal_component_scratch.reserve(create_info_cache.reserve_component_count);
        skeletal_matrix_scratch.reserve(create_info_cache.reserve_component_count);
    }

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.cache = {};
    runtime_stats = {};
    appearance_prepare_bridge.Reset();
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_build_invoked = false;
    appearance_full_rebuild = false;
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    culling_stats = {};

    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    appearance_host_revision_seen = 0U;
    light_frame_coordinator = nullptr;
    ibl_host = nullptr;
    light_shadow_link_coordinator = nullptr;
    local_light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator = nullptr;
    local_shadow_atlas_binding_coordinator.Reset();
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;
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
    if (light_shadow_upload_host.IsInitialized()) {
        light_shadow_upload_host.Shutdown(context_);
    }
    for (auto& frame_resources : frame_lighting_resources) {
        DestroyStorageBuffer(frame_resources.appearance_records);
        DestroyStorageBuffer(frame_resources.skeletal_components);
        DestroyStorageBuffer(frame_resources.skeletal_matrices);
    }

    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    image_initialized.clear();
    frame_lighting_resources.clear();
    resolved_appearances.clear();
    appearance_record_scratch.clear();
    skeletal_component_scratch.clear();
    skeletal_matrix_scratch.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();

    lighting_descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& blend_pipelines : pipeline_ids) {
        for (auto& mode_pipelines : blend_pipelines) {
            for (auto& topology_pipelines : mode_pipelines) {
                for (auto& pipeline_id : topology_pipelines) {
                    pipeline_id = {};
                }
            }
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    geometry_components = nullptr;
    transforms = nullptr;
    component_count = 0U;
    appearance_component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    skeletal_outputs = nullptr;
    skeletal_output_count = 0U;
    vertex_deform_outputs = nullptr;
    vertex_deform_output_count = 0U;
    morph_outputs = nullptr;
    morph_output_count = 0U;
    frame_sequence_outputs = nullptr;
    frame_sequence_output_count = 0U;
    geometry_resource_host = nullptr;
    geometry_upload_host = nullptr;
    geometry_appearance_host = nullptr;
    geometry_image_host = nullptr;
    texture_host = nullptr;
    light_frame_coordinator = nullptr;
    ibl_host = nullptr;
    light_shadow_link_coordinator = nullptr;
    local_light_shadow_link_coordinator.Reset();
    shadow_atlas_binding_coordinator = nullptr;
    local_shadow_atlas_binding_coordinator.Reset();
    shadow_frame_coordinator = nullptr;
    shadow_atlas_host = nullptr;

    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    instance_range = {};
    bindless_revision_seen = 0U;
    appearance_record_bindless_revision_seen = 0U;
    appearance_record_texture_host_revision_seen = 0U;
    appearance_record_content_revision = 0U;

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.batch_scratch.visible_items.clear();
    runtime_scratch.batch_scratch.radix_scratch.clear();
    runtime_scratch.batch_scratch.ordered_indices.clear();
    runtime_scratch.cache = {};
    runtime_stats = {};
    appearance_prepare_bridge.Reset();
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_build_invoked = false;
    appearance_full_rebuild = false;
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    culling_stats = {};
    stats = {};

    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    appearance_host_revision_seen = 0U;
    initialized = false;
}

void GeometryRenderer3D::SetHosts(GeometryResourceHost* resource_host_,
                                  GeometryUploadHost* upload_host_) noexcept {
    geometry_resource_host = resource_host_;
    geometry_upload_host = upload_host_;
}

void GeometryRenderer3D::SetAppearanceHosts(GeometryAppearanceHost* appearance_host_,
                                          GeometryImageHost* image_host_) noexcept {
    if (geometry_appearance_host != appearance_host_) {
        resolved_appearances.clear();
        appearance_host_revision_seen = 0U;
    }
    geometry_appearance_host = appearance_host_;
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
    appearance_component_count = appearance_component_count_;
    appearance_prepare_bridge.SetAppearanceData(appearance_components_,
                                                appearance_component_count_);
    appearance_prepare_bridge.Reserve(appearance_component_count_);
}

void GeometryRenderer3D::SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                                std::uint32_t dirty_component_count_) noexcept {
    appearance_prepare_bridge.SetDirtyHint(dirty_component_indices_,
                                           dirty_component_count_);
}

void GeometryRenderer3D::SetAnimationOutputs(
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
    std::uint32_t skeletal_output_count_,
    const ecs::VertexDeformOutputState* vertex_deform_outputs_,
    std::uint32_t vertex_deform_output_count_,
    const ecs::MorphWeightOutputState* morph_outputs_,
    std::uint32_t morph_output_count_,
    const ecs::FrameSequenceOutputState* frame_sequence_outputs_,
    std::uint32_t frame_sequence_output_count_) noexcept {
    skeletal_outputs = skeletal_outputs_;
    skeletal_output_count = skeletal_output_count_;
    vertex_deform_outputs = vertex_deform_outputs_;
    vertex_deform_output_count = vertex_deform_output_count_;
    morph_outputs = morph_outputs_;
    morph_output_count = morph_output_count_;
    frame_sequence_outputs = frame_sequence_outputs_;
    frame_sequence_output_count = frame_sequence_output_count_;
}

void GeometryRenderer3D::SetAppearanceCoordinator(
    render::AppearanceFrameCoordinator<ecs::Dim3>* appearance_frame_coordinator_) noexcept {
    appearance_prepare_bridge.SetCoordinator(appearance_frame_coordinator_);
    appearance_prepare_bridge.Reserve(appearance_component_count);
}

void GeometryRenderer3D::SetLightFrameCoordinator(
    render::LightFrameCoordinator<ecs::Dim3>* light_frame_coordinator_) noexcept {
    light_frame_coordinator = light_frame_coordinator_;
}

void GeometryRenderer3D::SetLightShadowLinkCoordinator(
    render::LightShadowLinkCoordinator3D* light_shadow_link_coordinator_) noexcept {
    light_shadow_link_coordinator = light_shadow_link_coordinator_;
}

void GeometryRenderer3D::SetShadowAtlasBindingCoordinator(
    render::ShadowAtlasBindingCoordinator* shadow_atlas_binding_coordinator_) noexcept {
    if (shadow_atlas_binding_coordinator != shadow_atlas_binding_coordinator_) {
        for (auto& frame_resources : frame_lighting_resources) {
            frame_resources.descriptor_buffer_signature = 0U;
            frame_resources.descriptor_image_signature = 0U;
            frame_resources.descriptor_set_signature = 0U;
        }
        if (shadow_atlas_binding_coordinator_ == nullptr) {
            local_shadow_atlas_binding_coordinator.Reset();
        }
    }
    shadow_atlas_binding_coordinator = shadow_atlas_binding_coordinator_;
}

void GeometryRenderer3D::SetShadowFrameCoordinator(
    render::ShadowFrameCoordinator<ecs::Dim3>* shadow_frame_coordinator_) noexcept {
    shadow_frame_coordinator = shadow_frame_coordinator_;
}

void GeometryRenderer3D::SetShadowAtlasHost(shadow::ShadowAtlasHost* shadow_atlas_host_) noexcept {
    if (shadow_atlas_host != shadow_atlas_host_) {
        for (auto& frame_resources : frame_lighting_resources) {
            frame_resources.descriptor_buffer_signature = 0U;
            frame_resources.descriptor_image_signature = 0U;
            frame_resources.descriptor_set_signature = 0U;
        }
        if (shadow_atlas_binding_coordinator == nullptr) {
            local_shadow_atlas_binding_coordinator.Reset();
        }
    }
    shadow_atlas_host = shadow_atlas_host_;
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

} // namespace vr::geometry
