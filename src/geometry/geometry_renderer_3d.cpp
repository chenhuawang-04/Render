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
#include "vr/render/runtime_prepare_views.hpp"
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

void GeometryRenderer3D::PrepareFrame(const render::GeometryRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame called before Initialize");
    }
    if (geometry_resource_host == nullptr || !geometry_resource_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryResourceHost");
    }
    if (geometry_upload_host == nullptr || !geometry_upload_host->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized GeometryUploadHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    bindless_resources = prepare_view_.bindless;
    pipeline_host = &prepare_view_.pipeline;
    gpu_memory_host = &prepare_view_.gpu_memory;
    ibl_host = &prepare_view_.ibl;
    sampler_host = &prepare_view_.sampler;
    texture_host = prepare_view_.texture;
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("GeometryRenderer3D::PrepareFrame requires initialized BindlessResourceSystem");
    }
    const std::uint64_t bindless_revision_now = bindless_resources->Revision();
    if (texture_host != nullptr &&
        texture_host->IsInitialized() &&
        (!texture_host->BindlessConfig().Enabled() ||
         texture_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureTextureHost(*texture_host);
    }
    if (geometry_image_host != nullptr &&
        geometry_image_host->IsInitialized() &&
        (!geometry_image_host->BindlessConfig().Enabled() ||
         geometry_image_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureGeometryImageHost(*geometry_image_host);
    }
    if (shadow_atlas_host != nullptr &&
        shadow_atlas_host->IsInitialized() &&
        (!shadow_atlas_host->BindlessConfig().Enabled() ||
         shadow_atlas_host->BindlessConfig().bindless_revision != bindless_revision_now)) {
        bindless_resources->ConfigureShadowAtlasHost(*shadow_atlas_host);
    }

    active_frame_index = prepare_view_.frame.frame_index;
    if (active_frame_index >= frame_lighting_resources.size()) {
        frame_lighting_resources.resize(active_frame_index + 1U);
    }
    {
        FrameLightingResources& frame_resources = frame_lighting_resources[active_frame_index];
        frame_resources.descriptor_set = VK_NULL_HANDLE;
        frame_resources.descriptor_buffer_signature = 0U;
        frame_resources.descriptor_image_signature = 0U;
        frame_resources.descriptor_set_signature = 0U;
    }
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen, prepare_view_.progress.completed_submit_value);

    CollectRetiredDepthResources(*context, completed_submit_value_seen);
    geometry_resource_host->BeginFrame(*context, completed_submit_value_seen);
    geometry_upload_host->BeginFrame(*context,
                                     active_frame_index,
                                     last_submitted_value_seen,
                                     completed_submit_value_seen);
    if (geometry_image_host != nullptr && geometry_image_host->IsInitialized()) {
        geometry_image_host->BeginFrame(*context, completed_submit_value_seen);
    }

    std::uint32_t appearance_revision_now = 0U;
    if (geometry_appearance_host != nullptr && geometry_appearance_host->IsInitialized()) {
        appearance_revision_now = geometry_appearance_host->Stats().revision;
    }
    if (appearance_revision_now != appearance_host_revision_seen) {
        resolved_appearances.clear();
        appearance_host_revision_seen = appearance_revision_now;
    }
    bindless_revision_seen = bindless_revision_now;

    stats = {};
    EnsureLightingDescriptorObjects(*context, *descriptor_host);

    if (!light_shadow_upload_host.IsInitialized()) {
        light::LightShadowUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = descriptor_host->FramesInFlight();
        light_shadow_upload_host.Initialize(*context, *gpu_memory_host, upload_create_info);
    }
    light_shadow_upload_host.BeginFrame(*context,
                                        active_frame_index,
                                        last_submitted_value_seen,
                                        completed_submit_value_seen);

    stats.component_count = component_count;
    stats.appearance_component_count = appearance_component_count;
    stats.appearance_resolve_cache_entry_count = static_cast<std::uint32_t>(resolved_appearances.size());
    instance_range = {};
    culling_stats = {};
    appearance_runtime_stats = {};
    appearance_link_stats = {};
    appearance_build_invoked = false;
    appearance_full_rebuild = false;
    const render::IblEnvironmentId ibl_environment_id{prepare_view_.ibl_environment_id};
    const asset::TextureId ibl_brdf_lut_texture_id{prepare_view_.ibl_brdf_lut_texture_id};
    if (ibl_environment_id.IsValid() || ibl_brdf_lut_texture_id.IsValid()) {
        ibl_host->PrepareEnvironmentFrame(render::MakeIblHostPrepareView(prepare_view_),
                                          ibl_environment_id,
                                          ibl_brdf_lut_texture_id);
    } else {
        ibl_host->PrepareFrame(render::MakeIblHostPrepareView(prepare_view_));
    }
    const auto appearance_prepare_result = appearance_prepare_bridge.PrepareGeometry(
        geometry_components,
        component_count,
        active_frame_index);
    appearance_build_invoked = appearance_prepare_result.build_invoked;
    appearance_full_rebuild =
        appearance_prepare_result.build_invoked &&
        appearance_prepare_result.runtime_stats.full_rebuild != 0U;
    if (appearance_prepare_result.has_appearance_data) {
        appearance_runtime_stats = appearance_prepare_result.runtime_stats;
        appearance_link_stats = appearance_prepare_result.link_stats;
        stats.appearance_visible_count = appearance_runtime_stats.visible_count;
        stats.appearance_updated_record_count = appearance_runtime_stats.updated_record_count;
        stats.appearance_cache_reused = appearance_prepare_result.cache_reused;
        stats.appearance_link_scanned_count = appearance_link_stats.scanned_count;
        stats.appearance_link_updated_count = appearance_link_stats.updated_count;
    }

    if (geometry_components == nullptr || component_count == 0U) {
        appearance_source_record_scratch.clear();
        EnsureLightingResourcesForFrame(*context);
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        runtime_stats = {};
        culling_scratch.visible_indices.clear();
        culling_scratch.visibility_stamps.clear();
        return;
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
    build_hint.skeletal_outputs = skeletal_outputs;
    build_hint.skeletal_output_count = skeletal_output_count;
    build_hint.vertex_deform_outputs = vertex_deform_outputs;
    build_hint.vertex_deform_output_count = vertex_deform_output_count;
    build_hint.morph_outputs = morph_outputs;
    build_hint.morph_output_count = morph_output_count;
    build_hint.frame_sequence_outputs = frame_sequence_outputs;
    build_hint.frame_sequence_output_count = frame_sequence_output_count;

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
    stats.skeletal_animated_instance_count = runtime_stats.skeletal_animated_instance_count;
    stats.vertex_deform_animated_instance_count = runtime_stats.vertex_deform_animated_instance_count;
    stats.morph_animated_instance_count = runtime_stats.morph_animated_instance_count;
    stats.frame_sequence_animated_instance_count = runtime_stats.frame_sequence_animated_instance_count;
    stats.cache_reused = runtime_stats.cache_reused;
    stats.transform_only_update = runtime_stats.transform_only_update;

    if (!runtime_scratch.instances.empty()) {
        const std::uint64_t appearance_override_signature = ApplyAppearanceStateOverrides();
        const std::uint64_t upload_revision =
            runtime_stats.geometry_signature ^
            (runtime_stats.transform_signature * 0x9e3779b97f4a7c15ULL) ^
            (appearance_override_signature * 0xbf58476d1ce4e5b9ULL);
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
    } else {
        appearance_source_record_scratch.clear();
    }

    EnsureLightingResourcesForFrame(*context);
    VkFormat active_color_format = prepare_view_.frame.swapchain_format;

    VkFormat active_depth_format = VK_FORMAT_UNDEFINED;
    if (create_info_cache.enable_depth) {
        active_depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
    }

    if (active_color_format != VK_FORMAT_UNDEFINED) {
        EnsurePipelineObjects(*context, *pipeline_host, active_color_format, active_depth_format);

        if (create_info_cache.prewarm_common_pipelines) {
            PrewarmCommonPipelines(*context, *pipeline_host, active_color_format, active_depth_format);
        }
        if (create_info_cache.compile_required_pipelines_in_prepare) {
            CompileRequiredPipelinesForCurrentFrame(*context,
                                                   *pipeline_host,
                                                   active_color_format,
                                                   active_depth_format);
        }
    }

}

void GeometryRenderer3D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "GeometryRenderer3D::BuildDirectRuntimeGraph called before Initialize");
    }

    render_graph::ResourceHandle depth_target = render_graph::invalid_resource_handle;
    if (create_info_cache.enable_depth) {
        const render_graph::Extent3D depth_extent{
            .width = graph_view_.reference_extent.width != 0U ? graph_view_.reference_extent.width : 1U,
            .height = graph_view_.reference_extent.height != 0U ? graph_view_.reference_extent.height : 1U,
            .depth = graph_view_.reference_extent.depth != 0U ? graph_view_.reference_extent.depth : 1U,
        };
        render_graph::TextureDesc depth_desc{
            .dimension = render_graph::TextureDimension::image_2d,
            .format = render_graph::TextureFormat::d32_sfloat,
            .extent = depth_extent,
            .usage = render_graph::texture_usage_depth_stencil_attachment_flag,
            .mip_level_count = 1U,
            .array_layer_count = 1U,
            .sample_count = render_graph::SampleCount::x1,
            .prefer_lazy_memory = create_info_cache.clear_depth,
        };
        if (!create_info_cache.clear_depth &&
            descriptor_host != nullptr &&
            descriptor_host->FramesInFlight() > 1U) {
            const std::uint32_t frames_in_flight =
                (std::max)(descriptor_host->FramesInFlight(), 1U);
            const std::uint32_t selected_frame_slot = active_frame_index % frames_in_flight;
            for (std::uint32_t frame_slot = 0U; frame_slot < frames_in_flight; ++frame_slot) {
                char debug_name[68]{};
                std::snprintf(debug_name,
                              sizeof(debug_name),
                              "geometry_renderer_3d_depth_slot_%u",
                              frame_slot);
                const auto candidate = graph_view_.builder.CreateTexture(
                    debug_name,
                    depth_desc,
                    render_graph::ResourceLifetime::persistent);
                if (frame_slot == selected_frame_slot) {
                    depth_target = candidate;
                }
            }
        } else {
            depth_target = graph_view_.builder.CreateTexture(
                "geometry_renderer_3d_depth",
                depth_desc,
                create_info_cache.clear_depth
                    ? render_graph::ResourceLifetime::transient
                    : render_graph::ResourceLifetime::persistent);
        }
    }

    render_graph::ResourceVersionHandle color_version =
        render_graph::invalid_resource_version;
    render_graph::ResourceVersionHandle depth_version =
        render_graph::invalid_resource_version;
    auto append_stage_pass = [&](const render::SceneRenderStage stage_,
                                 const char* debug_name_,
                                 const bool clear_color_,
                                 const bool clear_depth_) {
        const auto pass = graph_view_.builder.AddPass(debug_name_);
        if (render_graph::IsValidResourceVersionHandle(color_version)) {
            (void)graph_view_.builder.Read(
                pass,
                color_version,
                render_graph::AccessDesc{
                    .access = render_graph::AccessKind::color_attachment_read,
                });
        }
        color_version = graph_view_.builder.Write(
            pass,
            graph_view_.present_target,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::color_attachment_write,
            });

        render_graph::RasterPassDesc raster_pass_desc{
            .color_attachments = {
                render_graph::RasterColorAttachmentDesc{
                    .target = graph_view_.present_target,
                    .load_op = clear_color_
                        ? render_graph::AttachmentLoadOp::clear
                        : render_graph::AttachmentLoadOp::load,
                    .store_op = render_graph::AttachmentStoreOp::store,
                    .clear_value = {
                        .red = create_info_cache.clear_color.float32[0],
                        .green = create_info_cache.clear_color.float32[1],
                        .blue = create_info_cache.clear_color.float32[2],
                        .alpha = create_info_cache.clear_color.float32[3],
                    },
                },
            },
        };

        if (render_graph::IsValidResourceHandle(depth_target)) {
            if (render_graph::IsValidResourceVersionHandle(depth_version)) {
                (void)graph_view_.builder.Read(
                    pass,
                    depth_version,
                    render_graph::AccessDesc{
                        .access = render_graph::AccessKind::depth_stencil_read,
                    });
            }
            depth_version = graph_view_.builder.Write(
                pass,
                depth_target,
                render_graph::AccessDesc{
                    .access = render_graph::AccessKind::depth_stencil_write,
                });
            raster_pass_desc.has_depth_attachment = true;
            raster_pass_desc.depth_attachment = render_graph::RasterDepthAttachmentDesc{
                .target = depth_target,
                .load_op = clear_depth_
                    ? render_graph::AttachmentLoadOp::clear
                    : render_graph::AttachmentLoadOp::load,
                .store_op = render_graph::AttachmentStoreOp::store,
                .stencil_load_op = clear_depth_
                    ? render_graph::AttachmentLoadOp::clear
                    : render_graph::AttachmentLoadOp::load,
                .stencil_store_op = render_graph::AttachmentStoreOp::store,
                .clear_value = {
                    .depth = create_info_cache.clear_depth_value,
                    .stencil = create_info_cache.clear_stencil_value,
                },
            };
        }

        graph_view_.builder.SetRasterPassDesc(pass, raster_pass_desc);
        DescribeGraphDescriptorBindings(graph_view_.builder, pass);
        graph_view_.builder.SetExecuteCallback(
            pass,
            [this,
             stage_,
             color_target = graph_view_.present_target,
             depth_target](render_graph::GraphCommandContext& context_) {
                RecordGraphSceneStage(context_, stage_, color_target, depth_target);
            });
    };

    append_stage_pass(render::SceneRenderStage::opaque,
                      "geometry_renderer_3d_direct_opaque",
                      create_info_cache.clear_swapchain,
                      create_info_cache.clear_depth);
    append_stage_pass(render::SceneRenderStage::transparent,
                      "geometry_renderer_3d_direct_transparent",
                      false,
                      false);
    graph_view_.present_ready_version = color_version;
}

void GeometryRenderer3D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                         const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "GeometryRenderer3D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render::BuildSharedScene3DShaderContract("geometry_3d.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     render::scene_3d_sampled_image_set,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     render::scene_3d_sampler_set,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);

    const std::uint32_t light_records_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveLightRecordsExternalBufferBinding,
            .debug_name = "geometry_3d.light_records",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_light_records_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      light_records_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t cluster_headers_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveClusterHeadersExternalBufferBinding,
            .debug_name = "geometry_3d.cluster_headers",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_cluster_headers_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_headers_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t cluster_indices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveClusterIndicesExternalBufferBinding,
            .debug_name = "geometry_3d.cluster_indices",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_cluster_indices_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      cluster_indices_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t shadow_views_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveShadowViewsExternalBufferBinding,
            .debug_name = "geometry_3d.shadow_views",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_shadow_views_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      shadow_views_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t lighting_uniform_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveLightingUniformExternalBufferBinding,
            .debug_name = "geometry_3d.lighting_uniform",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_lighting_uniform_binding,
                                      render_graph::DescriptorBindingKind::uniform_buffer,
                                      lighting_uniform_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    const std::uint32_t skeletal_components_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveSkeletalComponentsExternalBufferBinding,
            .debug_name = "geometry_3d.skeletal_components",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_skeletal_components_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      skeletal_components_resolver_id,
                                      render_graph::shader_stage_vertex_flag);

    const std::uint32_t skeletal_matrices_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveSkeletalMatricesExternalBufferBinding,
            .debug_name = "geometry_3d.skeletal_matrices",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_skeletal_matrices_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      skeletal_matrices_resolver_id,
                                      render_graph::shader_stage_vertex_flag);

    const std::uint32_t appearance_resolver_id =
        builder_.RegisterExternalBufferBindingResolver({
            .user_data = this,
            .resolve_fn = &GeometryRenderer3D::ResolveAppearanceExternalBufferBinding,
            .debug_name = "geometry_3d.appearance_records",
        });
    builder_.AddExternalBufferBinding(pass_,
                                      render::scene_3d_shared_buffer_set,
                                      render::scene_3d_geometry_appearance_binding,
                                      render_graph::DescriptorBindingKind::storage_buffer,
                                      appearance_resolver_id,
                                      render_graph::shader_stage_fragment_flag);

    if (!builder_.HasPassDescriptorBinding(pass_,
                                           render::scene_3d_ibl_set,
                                           render::scene_3d_ibl_params_binding)) {
        const std::uint32_t ibl_params_resolver_id =
            builder_.RegisterExternalBufferBindingResolver({
                .user_data = this,
                .resolve_fn = &GeometryRenderer3D::ResolveIblParamsExternalBufferBinding,
                .debug_name = "geometry_3d.ibl_params",
            });
        builder_.AddExternalBufferBinding(pass_,
                                          render::scene_3d_ibl_set,
                                          render::scene_3d_ibl_params_binding,
                                          render_graph::DescriptorBindingKind::uniform_buffer,
                                          ibl_params_resolver_id,
                                          render_graph::shader_stage_fragment_flag);
    }
}

void GeometryRenderer3D::RecordGraphSceneStage(render_graph::GraphCommandContext& context_,
                                               render::SceneRenderStage stage_,
                                               render_graph::ResourceHandle color_target_,
                                               render_graph::ResourceHandle depth_target_) {
    RecordGraphInternal(context_,
                        render::SceneRenderStagePassHintValue(stage_),
                        true,
                        color_target_,
                        depth_target_);
}

void GeometryRenderer3D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                             std::uint32_t pass_bucket_,
                                             bool filter_by_pass_bucket_,
                                             render_graph::ResourceHandle color_target_,
                                             render_graph::ResourceHandle depth_target_) {
    if (!initialized) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr || geometry_resource_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage called before PrepareFrame");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage resolved zero-sized render extent");
    }

    const bool use_depth_attachment = render_graph::IsValidResourceHandle(depth_target_);
    const VkFormat active_depth_format = use_depth_attachment
        ? context_.ResolveTextureView(depth_target_).format
        : VK_FORMAT_UNDEFINED;

    EnsurePipelineObjects(*context,
                          *pipeline_host,
                          resolved_color.format,
                          use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(context_.CommandBuffer(), 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

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
        vkCmdPushConstants(context_.CommandBuffer(),
                           pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0U,
                           sizeof(FramePushConstants),
                           &frame_push_constants);
    }

    render::GraphicsPipelineId active_pipeline_id{};
    VkBuffer active_vertex_buffer = VK_NULL_HANDLE;
    VkBuffer active_index_buffer = VK_NULL_HANDLE;
    std::uint32_t active_effective_visual_resource_id =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t cached_effective_visual_resource_id =
        std::numeric_limits<std::uint32_t>::max();
    AppearancePushConstants cached_sampling_push_constants{};
    std::uint32_t cached_geometry_id = 0U;
    const GeometryResourceHost::MeshRecord* cached_mesh = nullptr;
    bool shared_state_bound = false;

    if (instance_range.buffer != VK_NULL_HANDLE && !runtime_scratch.draw_batches.empty()) {
        std::uint32_t stage_draw_call_count = 0U;
        std::uint32_t stage_filtered_batch_count = 0U;
        const VkBuffer instance_vertex_buffer = instance_range.buffer;
        const VkDeviceSize instance_vertex_offset = instance_range.offset;
        vkCmdBindVertexBuffers(context_.CommandBuffer(),
                               1U,
                               1U,
                               &instance_vertex_buffer,
                               &instance_vertex_offset);

        for (const ecs::Geometry3DDrawBatch& batch : runtime_scratch.draw_batches) {
            if (filter_by_pass_bucket_ &&
                ecs::GeometrySystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                ++stage_filtered_batch_count;
                continue;
            }
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

            const BlendMode blend_mode = ResolveBlendMode(batch);
            const PipelineMode mode = ResolvePipelineMode(batch, use_depth_attachment);
            const TopologyMode topology_mode = ResolveTopologyMode(mesh->topology, batch);
            const CullMode cull_mode = ResolveCullMode(batch);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                 *pipeline_host,
                                                                                 resolved_color.format,
                                                                                 use_depth_attachment ? active_depth_format : VK_FORMAT_UNDEFINED,
                                                                                 blend_mode,
                                                                                 mode,
                                                                                 topology_mode,
                                                                                 cull_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                continue;
            }

            AppearancePushConstants sampling_push_constants{};
            if (batch.effective_visual_resource_id == cached_effective_visual_resource_id) {
                sampling_push_constants = cached_sampling_push_constants;
            } else if (!ResolveAppearancePushConstants(batch.effective_visual_resource_id,
                                                       sampling_push_constants)) {
                ++stats.skipped_batch_count;
                continue;
            } else {
                cached_effective_visual_resource_id = batch.effective_visual_resource_id;
                cached_sampling_push_constants = sampling_push_constants;
            }

            if (active_pipeline_id.value != pipeline_id.value) {
                vkCmdBindPipeline(context_.CommandBuffer(),
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                active_pipeline_id = pipeline_id;
            }

            if (!shared_state_bound) {
                if (pipeline_layout == VK_NULL_HANDLE) {
                    throw std::runtime_error("GeometryRenderer3D::RecordGraphSceneStage requires valid pipeline layout");
                }
                context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                       pipeline_layout,
                                                       0U,
                                                       4U);
                shared_state_bound = true;
                ++stats.descriptor_set_bind_count;
                ++stats.light_descriptor_set_bind_count;
                ++stats.ibl_descriptor_set_bind_count;
            }

            if (pipeline_layout != VK_NULL_HANDLE &&
                active_effective_visual_resource_id != batch.effective_visual_resource_id) {
                vkCmdPushConstants(context_.CommandBuffer(),
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   static_cast<std::uint32_t>(offsetof(PushConstants, appearance)),
                                   sizeof(AppearancePushConstants),
                                   &sampling_push_constants);
                active_effective_visual_resource_id = batch.effective_visual_resource_id;
                ++stats.appearance_push_constant_update_count;
            }

            if (active_vertex_buffer != mesh->vertex_buffer.buffer) {
                const VkBuffer vertex_buffer = mesh->vertex_buffer.buffer;
                const VkDeviceSize vertex_offset = 0U;
                vkCmdBindVertexBuffers(context_.CommandBuffer(),
                                       0U,
                                       1U,
                                       &vertex_buffer,
                                       &vertex_offset);
                active_vertex_buffer = mesh->vertex_buffer.buffer;
            }

            if (active_index_buffer != mesh->index_buffer.buffer) {
                vkCmdBindIndexBuffer(context_.CommandBuffer(),
                                     mesh->index_buffer.buffer,
                                     0U,
                                     VK_INDEX_TYPE_UINT32);
                active_index_buffer = mesh->index_buffer.buffer;
            }

            vkCmdDrawIndexed(context_.CommandBuffer(),
                             submesh.index_count,
                             batch.instance_count,
                             submesh.first_index,
                             submesh.vertex_offset,
                             batch.instance_begin);
            ++stats.draw_call_count;
            ++stage_draw_call_count;
        }

        if (filter_by_pass_bucket_) {
            stats.stage_filtered_batch_count += stage_filtered_batch_count;
            if (stage_draw_call_count == 0U) {
                ++stats.empty_stage_pass_count;
            }
            if (pass_bucket_ == static_cast<std::uint32_t>(ecs::GeometryRenderPassHint::opaque)) {
                stats.opaque_draw_call_count += stage_draw_call_count;
            } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::GeometryRenderPassHint::transparent)) {
                stats.transparent_draw_call_count += stage_draw_call_count;
            }
        }
    }

    stats.appearance_resolve_cache_entry_count = static_cast<std::uint32_t>(resolved_appearances.size());
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

void GeometryRenderer3D::EnsureLightingDescriptorObjects(VulkanContext& context_,
                                                         render::DescriptorHost& descriptor_host_) {
    if (lighting_descriptor_layout_id.IsValid()) {
        return;
    }

    const render::DescriptorSetLayoutDesc layout_desc =
        render::BuildSharedScene3DBufferLayoutDesc();
    lighting_descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
}

GeometryRenderer3D::LightingParamsGpu GeometryRenderer3D::BuildLightingParamsGpu(VkExtent2D extent_) const noexcept {
    LightingParamsGpu params{};
    params.light_count = 0.0F;
    params.max_fragment_lights = static_cast<float>(std::max<std::uint32_t>(1U, create_info_cache.max_fragment_lights));
    params.cluster_count_x = std::max<std::uint32_t>(1U, create_info_cache.light_culling_config.cluster_count_x);
    params.cluster_count_y = std::max<std::uint32_t>(1U, create_info_cache.light_culling_config.cluster_count_y);
    params.cluster_count_z = std::max<std::uint32_t>(1U, create_info_cache.light_culling_config.cluster_count_z);
    params.reverse_z = create_info_cache.light_culling_config.reverse_z != 0U ? 1U : 0U;
    params.near_plane = std::max(1e-4F, create_info_cache.light_culling_config.near_plane);
    params.far_plane = std::max(params.near_plane + 1e-3F, create_info_cache.light_culling_config.far_plane);
    params.z_slice_scale = std::max(1e-4F, create_info_cache.light_culling_config.z_slice_scale);
    params.z_slice_bias = std::max(1e-4F, create_info_cache.light_culling_config.z_slice_bias);
    params.framebuffer_width = static_cast<float>(std::max<std::uint32_t>(extent_.width, 1U));
    params.framebuffer_height = static_cast<float>(std::max<std::uint32_t>(extent_.height, 1U));
    params.shadow_view_count = 0.0F;
    params.reserved0 = 0.0F;

    if (camera_transform != nullptr) {
        params.camera_position_x = camera_transform->runtime.world_matrix.m[12];
        params.camera_position_y = camera_transform->runtime.world_matrix.m[13];
        params.camera_position_z = camera_transform->runtime.world_matrix.m[14];
    } else {
        params.camera_position_x = 0.0F;
        params.camera_position_y = 0.0F;
        params.camera_position_z = 0.0F;
    }

    if (camera_component != nullptr) {
        const auto& view = camera_component->runtime.view_matrix;
        params.camera_forward_x = -view.m[8];
        params.camera_forward_y = -view.m[9];
        params.camera_forward_z = -view.m[10];
        const float near_plane = std::max(1e-4F, camera_component->style.near_plane);
        const float far_plane = std::max(near_plane + 1e-3F, camera_component->style.far_plane);
        params.near_plane = near_plane;
        params.far_plane = far_plane;
        params.reverse_z = camera_component->style.reverse_z != 0U ? 1U : params.reverse_z;
    } else {
        params.camera_forward_x = 0.0F;
        params.camera_forward_y = 0.0F;
        params.camera_forward_z = -1.0F;
    }

    return params;
}

void GeometryRenderer3D::DestroyStorageBuffer(resource::BufferResource& buffer_) noexcept {
    if (context == nullptr || context->Device() == VK_NULL_HANDLE) {
        buffer_ = {};
        return;
    }
    resource::BufferHost::DestroyBuffer(*context, buffer_);
}

void GeometryRenderer3D::EnsureStorageBufferCapacity(resource::BufferResource& buffer_,
                                                     VkDeviceSize required_bytes_) {
    if (context == nullptr || gpu_memory_host == nullptr) {
        throw std::runtime_error("GeometryRenderer3D::EnsureStorageBufferCapacity missing runtime hosts");
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

void GeometryRenderer3D::EnsureLightingResourcesForFrame(
    VulkanContext& context_) {
    if (upload_host == nullptr ||
        !light_shadow_upload_host.IsInitialized() ||
        active_frame_index >= frame_lighting_resources.size()) {
        return;
    }

    auto hash_combine = [](std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= 1099511628211ULL;
    };
    auto hash_from_float = [&](std::uint64_t& hash_, float value_) noexcept {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value_, sizeof(bits));
        hash_combine(hash_, static_cast<std::uint64_t>(bits));
    };
    auto hash_from_handle = [&](std::uint64_t& hash_, std::uintptr_t handle_value_) noexcept {
        hash_combine(hash_, static_cast<std::uint64_t>(handle_value_));
    };

    const bool lighting_enabled = create_info_cache.enable_light_shadow;

    render::LightPrepareStageResult<ecs::Dim3> light_result{};
    render::ShadowPrepareStageResult<ecs::Dim3> shadow_result{};

    if (lighting_enabled && light_frame_coordinator != nullptr) {
        light_result = light_frame_coordinator->PrepareFrame(active_frame_index,
                                                             {},
                                                             create_info_cache.light_culling_config);
    }
    if (lighting_enabled && shadow_frame_coordinator != nullptr) {
        shadow_result = shadow_frame_coordinator->PrepareFrame(active_frame_index);
    }

    const ecs::LightGpuRecord3D* light_records_ptr = nullptr;
    std::uint32_t light_record_count = 0U;
    const ecs::LightClusterHeader* cluster_headers_ptr = nullptr;
    std::uint32_t cluster_header_count = 0U;
    const std::uint32_t* cluster_indices_ptr = nullptr;
    std::uint32_t cluster_index_count = 0U;
    const ecs::LightUploadRange* light_upload_ranges_ptr = nullptr;
    std::uint32_t light_upload_range_count = 0U;
    const std::uint32_t* light_updated_component_indices_ptr = nullptr;
    std::uint32_t light_updated_component_count = 0U;
    const ecs::ShadowGpuRecord3D* shadow_records_ptr = nullptr;
    std::uint32_t shadow_record_count = 0U;
    const ecs::Shadow<ecs::Dim3>* shadow_components_ptr = nullptr;
    std::uint32_t shadow_component_count = 0U;
    const ecs::ShadowViewGpuRecord* shadow_view_records_ptr = nullptr;
    std::uint32_t shadow_view_count = 0U;
    const ecs::ShadowUploadRange* shadow_view_upload_ranges_ptr = nullptr;
    std::uint32_t shadow_view_upload_range_count = 0U;
    std::uint32_t shadow_namespace_id = 0U;

    if (lighting_enabled && light_frame_coordinator != nullptr) {
        const auto& runtime_scratch_ref = light_frame_coordinator->RuntimeScratch();
        const auto& culling_scratch_ref = light_frame_coordinator->CullingScratch();
        light_records_ptr = ecs::LightRuntimeSystem<ecs::Dim3>::GpuRecords(runtime_scratch_ref);
        light_record_count = ecs::LightRuntimeSystem<ecs::Dim3>::GpuRecordCount(runtime_scratch_ref);
        cluster_headers_ptr = ecs::LightCullingSystem<ecs::Dim3>::ClusterHeaders(culling_scratch_ref);
        cluster_header_count = ecs::LightCullingSystem<ecs::Dim3>::ClusterHeaderCount(culling_scratch_ref);
        cluster_indices_ptr = ecs::LightCullingSystem<ecs::Dim3>::ClusterLightIndices(culling_scratch_ref);
        cluster_index_count = ecs::LightCullingSystem<ecs::Dim3>::ClusterLightIndexCount(culling_scratch_ref);
        light_upload_ranges_ptr = ecs::LightRuntimeSystem<ecs::Dim3>::UploadRanges(runtime_scratch_ref);
        light_upload_range_count = ecs::LightRuntimeSystem<ecs::Dim3>::UploadRangeCount(runtime_scratch_ref);
        light_updated_component_indices_ptr = ecs::LightRuntimeSystem<ecs::Dim3>::UpdatedComponentIndices(runtime_scratch_ref);
        light_updated_component_count = ecs::LightRuntimeSystem<ecs::Dim3>::UpdatedComponentIndexCount(runtime_scratch_ref);
    }
    if (lighting_enabled && shadow_frame_coordinator != nullptr) {
        const auto& shadow_runtime_scratch_ref = shadow_frame_coordinator->RuntimeScratch();
        shadow_records_ptr = ecs::ShadowRuntimeSystem<ecs::Dim3>::GpuRecords(shadow_runtime_scratch_ref);
        shadow_record_count = ecs::ShadowRuntimeSystem<ecs::Dim3>::GpuRecordCount(shadow_runtime_scratch_ref);
        shadow_view_records_ptr = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewRecords(shadow_runtime_scratch_ref);
        shadow_view_count = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewRecordCount(shadow_runtime_scratch_ref);
        shadow_view_upload_ranges_ptr = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewUploadRanges(shadow_runtime_scratch_ref);
        shadow_view_upload_range_count = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewUploadRangeCount(shadow_runtime_scratch_ref);
        shadow_components_ptr = shadow_frame_coordinator->ShadowComponents();
        shadow_component_count = shadow_frame_coordinator->ShadowCount();
        if (shadow_view_count > 0U && shadow_view_records_ptr != nullptr) {
            shadow_namespace_id = shadow_view_records_ptr[0U].atlas_namespace_id;
        }
    }

    render::LightShadowLinkStageResult3D link_result{};
    if (lighting_enabled && light_records_ptr != nullptr && light_record_count > 0U) {
        std::uint64_t link_light_signature = 14695981039346656037ULL;
        hash_combine(link_light_signature, light_result.runtime_stats.style_signature);
        hash_combine(link_light_signature, light_result.runtime_stats.binding_signature);
        hash_combine(link_light_signature, light_result.runtime_stats.transform_signature);
        hash_combine(link_light_signature, static_cast<std::uint64_t>(light_record_count));

        std::uint64_t link_shadow_signature = 14695981039346656037ULL;
        hash_combine(link_shadow_signature, shadow_result.runtime_stats.style_signature);
        hash_combine(link_shadow_signature, shadow_result.runtime_stats.binding_signature);
        hash_combine(link_shadow_signature, shadow_result.runtime_stats.transform_signature);
        hash_combine(link_shadow_signature, shadow_result.runtime_stats.camera_signature);
        hash_combine(link_shadow_signature, static_cast<std::uint64_t>(shadow_component_count));
        hash_combine(link_shadow_signature, static_cast<std::uint64_t>(shadow_record_count));
        hash_combine(link_shadow_signature, static_cast<std::uint64_t>(shadow_view_count));
        hash_combine(link_shadow_signature, static_cast<std::uint64_t>(shadow_namespace_id));

        std::uint64_t link_signature = 14695981039346656037ULL;
        hash_combine(link_signature, link_light_signature);
        hash_combine(link_signature, link_shadow_signature);
        render::LightShadowLinkCoordinator3D* link_coordinator = light_shadow_link_coordinator;
        if (link_coordinator == nullptr) {
            link_coordinator = &local_light_shadow_link_coordinator;
        }
        link_coordinator->Reserve(light_record_count);
        render::LightShadowLinkCoordinator3DPrepareInfo link_prepare_info{};
        link_prepare_info.signature = link_signature;
        link_prepare_info.light_signature = link_light_signature;
        link_prepare_info.shadow_signature = link_shadow_signature;
        link_prepare_info.light_records = light_records_ptr;
        link_prepare_info.light_record_count = light_record_count;
        link_prepare_info.shadow_components = shadow_components_ptr;
        link_prepare_info.shadow_component_count = shadow_component_count;
        link_prepare_info.shadow_records = shadow_records_ptr;
        link_prepare_info.shadow_record_count = shadow_record_count;
        link_prepare_info.shadow_namespace_hint = shadow_namespace_id;
        link_prepare_info.light_updated_component_indices = light_updated_component_indices_ptr;
        link_prepare_info.light_updated_component_count = light_updated_component_count;
        link_prepare_info.allow_incremental_light_patch = 1U;
        const render::LightShadowLinkCoordinator3DResult link_prepare_result =
            link_coordinator->Prepare(link_prepare_info);
        link_result = link_prepare_result.link_result;
        shadow_namespace_id = link_result.shadow_namespace_id;
        if (link_prepare_result.cache_reused) {
            ++stats.light_shadow_link_cache_hit_count;
        }
    } else if (light_shadow_link_coordinator == nullptr) {
        local_light_shadow_link_coordinator.Reset();
    }

    const ecs::LightGpuRecord3D dummy_light_record{};
    const ecs::LightClusterHeader dummy_cluster_header{.offset = 0U, .count = 0U, .flags = 0U};
    const std::uint32_t dummy_cluster_index = 0U;
    const ecs::ShadowViewGpuRecord dummy_shadow_view{};

    const bool has_linked_light_records =
        lighting_enabled &&
        link_result.linked_light_records != nullptr &&
        link_result.linked_light_record_count > 0U;
    const ecs::LightGpuRecord3D* upload_light_records =
        has_linked_light_records
            ? link_result.linked_light_records
            : &dummy_light_record;
    const std::uint32_t upload_light_record_count =
        has_linked_light_records ? link_result.linked_light_record_count : 1U;

    const ecs::LightClusterHeader* upload_cluster_headers =
        (lighting_enabled && cluster_headers_ptr != nullptr && cluster_header_count > 0U)
            ? cluster_headers_ptr
            : &dummy_cluster_header;
    const std::uint32_t upload_cluster_header_count =
        (lighting_enabled && cluster_headers_ptr != nullptr && cluster_header_count > 0U)
            ? cluster_header_count
            : 1U;

    const std::uint32_t* upload_cluster_indices =
        (lighting_enabled && cluster_indices_ptr != nullptr && cluster_index_count > 0U)
            ? cluster_indices_ptr
            : &dummy_cluster_index;
    const std::uint32_t upload_cluster_index_count =
        (lighting_enabled && cluster_indices_ptr != nullptr && cluster_index_count > 0U)
            ? cluster_index_count
            : 1U;

    const ecs::ShadowViewGpuRecord* upload_shadow_views =
        (lighting_enabled && shadow_view_records_ptr != nullptr && shadow_view_count > 0U)
            ? shadow_view_records_ptr
            : &dummy_shadow_view;
    const std::uint32_t upload_shadow_view_count =
        (lighting_enabled && shadow_view_records_ptr != nullptr && shadow_view_count > 0U)
            ? shadow_view_count
            : 1U;

    const std::uint32_t resolved_light_count = lighting_enabled ? light_record_count : 0U;
    const std::uint32_t resolved_shadow_view_count = lighting_enabled ? shadow_view_count : 0U;
    const std::uint32_t resolved_light_upload_range_count = lighting_enabled ? light_upload_range_count : 0U;
    const std::uint32_t resolved_shadow_view_upload_range_count =
        lighting_enabled ? shadow_view_upload_range_count : 0U;
    const std::uint32_t resolved_visible_light_count =
        lighting_enabled ? light_result.culling_stats.accepted_light_count : 0U;
    const std::uint32_t resolved_cluster_count =
        lighting_enabled ? light_result.culling_stats.cluster_count : 0U;
    const std::uint32_t resolved_cluster_index_count =
        lighting_enabled ? light_result.culling_stats.emitted_index_count : 0U;
    const std::uint32_t linked_light_count = lighting_enabled ? link_result.linked_light_count : 0U;
    const std::uint32_t namespace_drop_count = lighting_enabled ? link_result.namespace_drop_count : 0U;
    const std::uint32_t unmapped_light_count = lighting_enabled ? link_result.unmapped_light_count : 0U;

    std::uint64_t light_revision = 14695981039346656037ULL;
    hash_combine(light_revision, static_cast<std::uint64_t>(lighting_enabled ? 1U : 0U));
    hash_combine(light_revision, light_result.runtime_stats.style_signature);
    hash_combine(light_revision, light_result.runtime_stats.binding_signature);
    hash_combine(light_revision, light_result.runtime_stats.transform_signature);
    hash_combine(light_revision, shadow_result.runtime_stats.style_signature);
    hash_combine(light_revision, shadow_result.runtime_stats.binding_signature);
    hash_combine(light_revision, shadow_result.runtime_stats.transform_signature);
    hash_combine(light_revision, static_cast<std::uint64_t>(shadow_namespace_id));
    hash_combine(light_revision, static_cast<std::uint64_t>(linked_light_count));
    hash_combine(light_revision, static_cast<std::uint64_t>(namespace_drop_count));
    hash_combine(light_revision, static_cast<std::uint64_t>(upload_light_record_count));

    std::uint64_t cluster_revision = 14695981039346656037ULL;
    hash_combine(cluster_revision, static_cast<std::uint64_t>(lighting_enabled ? 1U : 0U));
    hash_combine(cluster_revision, light_result.culling_stats.visible_light_signature);
    hash_combine(cluster_revision, light_result.culling_stats.culling_config_signature);
    hash_combine(cluster_revision, static_cast<std::uint64_t>(upload_cluster_header_count));
    hash_combine(cluster_revision, static_cast<std::uint64_t>(upload_cluster_index_count));

    std::uint64_t shadow_view_revision = 14695981039346656037ULL;
    hash_combine(shadow_view_revision, static_cast<std::uint64_t>(lighting_enabled ? 1U : 0U));
    hash_combine(shadow_view_revision, shadow_result.runtime_stats.style_signature);
    hash_combine(shadow_view_revision, shadow_result.runtime_stats.binding_signature);
    hash_combine(shadow_view_revision, shadow_result.runtime_stats.transform_signature);
    hash_combine(shadow_view_revision, shadow_result.runtime_stats.camera_signature);
    hash_combine(shadow_view_revision, static_cast<std::uint64_t>(upload_shadow_view_count));

    LightingParamsGpu lighting_params = BuildLightingParamsGpu(
        swapchain_extent.width > 0U && swapchain_extent.height > 0U
            ? swapchain_extent
            : VkExtent2D{.width = 1U, .height = 1U});
    lighting_params.light_count = static_cast<float>(resolved_light_count);
    lighting_params.shadow_view_count = static_cast<float>(resolved_shadow_view_count);

    render::ShadowAtlasBindingCoordinator* atlas_binding_coordinator = shadow_atlas_binding_coordinator;
    if (atlas_binding_coordinator == nullptr) {
        atlas_binding_coordinator = &local_shadow_atlas_binding_coordinator;
    }

    render::ShadowAtlasBindingResolveInput resolve_input{};
    resolve_input.atlas_host = shadow_atlas_host;
    resolve_input.namespace_id = shadow_namespace_id;
    resolve_input.fallback_namespace_id = 1U;
    resolve_input.allow_namespace_fallback = 1U;
    resolve_input.primary_sampler = bindless_resources != nullptr
        ? bindless_resources->DefaultSampler()
        : VK_NULL_HANDLE;
    const render::ShadowAtlasBindingResolveResult binding_result =
        atlas_binding_coordinator->Resolve(resolve_input);
    if (binding_result.cache_reused) {
        ++stats.light_shadow_atlas_binding_cache_hit_count;
    }

    std::uint32_t shadow_atlas_texture_slot =
        bindless_resources != nullptr ? bindless_resources->PlaceholderImage2DArraySlot().index : 0U;
    if (binding_result.valid &&
        binding_result.atlas_namespace_id != 0U &&
        shadow_atlas_host != nullptr) {
        const auto* atlas_record = shadow_atlas_host->FindAtlas(binding_result.atlas_namespace_id);
        if (atlas_record != nullptr && atlas_record->bindless.image_slot.IsValid()) {
            shadow_atlas_texture_slot = atlas_record->bindless.image_slot.index;
        }
    }
    lighting_params.shadow_atlas_texture_slot = shadow_atlas_texture_slot;
    lighting_params.shadow_atlas_sampler_slot =
        bindless_resources != nullptr ? bindless_resources->DefaultSamplerSlot().index : 0U;

    std::uint64_t uniform_revision = 14695981039346656037ULL;
    hash_combine(uniform_revision, static_cast<std::uint64_t>(lighting_enabled ? 1U : 0U));
    hash_from_float(uniform_revision, lighting_params.camera_position_x);
    hash_from_float(uniform_revision, lighting_params.camera_position_y);
    hash_from_float(uniform_revision, lighting_params.camera_position_z);
    hash_from_float(uniform_revision, lighting_params.camera_forward_x);
    hash_from_float(uniform_revision, lighting_params.camera_forward_y);
    hash_from_float(uniform_revision, lighting_params.camera_forward_z);
    hash_from_float(uniform_revision, lighting_params.light_count);
    hash_from_float(uniform_revision, lighting_params.shadow_view_count);
    hash_combine(uniform_revision, static_cast<std::uint64_t>(lighting_params.cluster_count_x));
    hash_combine(uniform_revision, static_cast<std::uint64_t>(lighting_params.cluster_count_y));
    hash_combine(uniform_revision, static_cast<std::uint64_t>(lighting_params.cluster_count_z));
    hash_combine(uniform_revision, static_cast<std::uint64_t>(lighting_params.reverse_z));
    hash_combine(uniform_revision, static_cast<std::uint64_t>(lighting_params.shadow_atlas_texture_slot));
    hash_combine(uniform_revision, static_cast<std::uint64_t>(lighting_params.shadow_atlas_sampler_slot));

    FrameLightingResources& frame_resources = frame_lighting_resources[active_frame_index];
    frame_resources.light_records = light_shadow_upload_host.UploadLightRecordsRanges(
        context_,
        *upload_host,
        active_frame_index,
        upload_light_records,
        upload_light_record_count,
        light_upload_ranges_ptr,
        light_upload_range_count,
        light_revision);
    frame_resources.cluster_headers = light_shadow_upload_host.UploadClusterHeaders(
        context_,
        *upload_host,
        active_frame_index,
        upload_cluster_headers,
        upload_cluster_header_count,
        cluster_revision);
    frame_resources.cluster_indices = light_shadow_upload_host.UploadClusterIndices(
        context_,
        *upload_host,
        active_frame_index,
        upload_cluster_indices,
        upload_cluster_index_count,
        cluster_revision ^ 0x9E3779B97F4A7C15ULL);
    frame_resources.shadow_views = light_shadow_upload_host.UploadShadowViewsRanges(
        context_,
        *upload_host,
        active_frame_index,
        upload_shadow_views,
        upload_shadow_view_count,
        shadow_view_upload_ranges_ptr,
        shadow_view_upload_range_count,
        shadow_view_revision);
    frame_resources.lighting_uniform = light_shadow_upload_host.UploadLightingUniform(
        context_,
        *upload_host,
        active_frame_index,
        &lighting_params,
        sizeof(LightingParamsGpu),
        uniform_revision);
    frame_resources.shadow_namespace_id = lighting_enabled ? shadow_namespace_id : 0U;
    frame_resources.upload_signature = uniform_revision ^ cluster_revision ^ shadow_view_revision;

    const std::uint32_t appearance_record_count =
        static_cast<std::uint32_t>(appearance_source_record_scratch.size());
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
    if (geometry_image_host != nullptr && geometry_image_host->IsInitialized()) {
        texture_host_revision = mix_surface_source_revision(texture_host_revision,
                                                            geometry_image_host->Stats().revision);
    }

    const std::uint64_t previous_appearance_content_revision = appearance_record_content_revision;
    const bool appearance_record_count_changed =
        appearance_record_scratch.size() != static_cast<std::size_t>(appearance_upload_count);
    const bool appearance_binding_state_changed =
        appearance_record_bindless_revision_seen != bindless_revision_seen ||
        appearance_record_texture_host_revision_seen != texture_host_revision;
    const bool can_attempt_partial_appearance_frame_sync =
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
    VkDeviceSize partial_appearance_upload_bytes = 0U;
    std::uint32_t current_range_begin = 0U;
    std::uint32_t current_range_count = 0U;
    const render::AppearanceSampledSurfaceResolver3D sampled_surface_resolver{
        .bindless_resources = bindless_resources,
        .texture_host = texture_host,
        .surface_image_host = nullptr,
        .geometry_image_host = geometry_image_host
    };
    for (std::uint32_t index = 0U; index < appearance_upload_count; ++index) {
        ecs::AppearanceGpuRecord<ecs::Dim3> encoded_record{};
        if (index < appearance_record_count) {
            render::EncodeAppearanceGpuRecord3DForSampling(
                appearance_source_record_scratch[index],
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
        appearance_record_bindless_revision_seen = bindless_revision_seen;
        appearance_record_texture_host_revision_seen = texture_host_revision;
        ++appearance_record_content_revision;
    }

    const bool appearance_frame_sync_required =
        frame_resources.appearance_records.buffer == VK_NULL_HANDLE ||
        frame_resources.appearance_record_count != appearance_upload_count ||
        frame_resources.appearance_bindless_revision != appearance_record_bindless_revision_seen ||
        frame_resources.appearance_texture_host_revision !=
            appearance_record_texture_host_revision_seen ||
        frame_resources.appearance_content_revision != appearance_record_content_revision;
    if (appearance_frame_sync_required) {
        EnsureStorageBufferCapacity(frame_resources.appearance_records, appearance_record_bytes);

        const bool can_use_partial_appearance_upload =
            !changed_ranges.empty() &&
            frame_resources.appearance_records.buffer != VK_NULL_HANDLE &&
            frame_resources.appearance_record_count == appearance_upload_count &&
            frame_resources.appearance_bindless_revision ==
                appearance_record_bindless_revision_seen &&
            frame_resources.appearance_texture_host_revision ==
                appearance_record_texture_host_revision_seen &&
            frame_resources.appearance_content_revision == previous_appearance_content_revision &&
            appearance_record_content_revision != previous_appearance_content_revision &&
            can_attempt_partial_appearance_frame_sync;
        if (can_use_partial_appearance_upload) {
            for (const ChangedRange& range : changed_ranges) {
                render::CopyAppearanceGpuRecord3DRange(appearance_record_scratch.data(),
                                                       frame_resources.appearance_records,
                                                       range.begin_index,
                                                       range.count);
                partial_appearance_upload_bytes +=
                    static_cast<VkDeviceSize>(range.count) *
                    sizeof(ecs::AppearanceGpuRecord<ecs::Dim3>);
            }
            stats.uploaded_bytes += partial_appearance_upload_bytes;
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

    const GeometrySkeletalPaletteBuildStats skeletal_build_stats =
        GeometrySkeletalPaletteBuilder::Build(geometry_components,
                                             component_count,
                                             skeletal_outputs,
                                             skeletal_output_count,
                                             skeletal_component_scratch,
                                             skeletal_matrix_scratch);
    const std::uint32_t skeletal_component_upload_count =
        std::max<std::uint32_t>(static_cast<std::uint32_t>(skeletal_component_scratch.size()), 1U);
    const std::uint32_t skeletal_matrix_upload_count =
        std::max<std::uint32_t>(static_cast<std::uint32_t>(skeletal_matrix_scratch.size()), 1U);
    if (skeletal_component_scratch.empty()) {
        skeletal_component_scratch.resize(1U);
        skeletal_component_scratch[0U] = {};
    }
    if (skeletal_matrix_scratch.empty()) {
        skeletal_matrix_scratch.resize(1U);
        skeletal_matrix_scratch[0U].matrix = ecs::spatial_math::IdentityMatrix4x4();
    }

    const VkDeviceSize skeletal_component_bytes =
        static_cast<VkDeviceSize>(skeletal_component_upload_count) * sizeof(GeometrySkeletalComponentGpu);
    const VkDeviceSize skeletal_matrix_bytes =
        static_cast<VkDeviceSize>(skeletal_matrix_upload_count) * sizeof(GeometrySkeletalMatrixGpu);
    EnsureStorageBufferCapacity(frame_resources.skeletal_components, skeletal_component_bytes);
    EnsureStorageBufferCapacity(frame_resources.skeletal_matrices, skeletal_matrix_bytes);
    std::memcpy(frame_resources.skeletal_components.mapped_ptr,
                skeletal_component_scratch.data(),
                static_cast<std::size_t>(skeletal_component_bytes));
    std::memcpy(frame_resources.skeletal_matrices.mapped_ptr,
                skeletal_matrix_scratch.data(),
                static_cast<std::size_t>(skeletal_matrix_bytes));
    frame_resources.skeletal_component_count =
        skeletal_component_upload_count;
    frame_resources.skeletal_matrix_count =
        skeletal_matrix_upload_count;

    stats.light_count = resolved_light_count;
    stats.visible_light_count = resolved_visible_light_count;
    stats.shadow_view_count = resolved_shadow_view_count;
    stats.light_upload_range_count = resolved_light_upload_range_count;
    stats.shadow_view_upload_range_count = resolved_shadow_view_upload_range_count;
    stats.light_cluster_count = resolved_cluster_count;
    stats.light_cluster_index_count = resolved_cluster_index_count;
    stats.light_shadow_linked_count = linked_light_count;
    stats.light_shadow_namespace_drop_count = namespace_drop_count;
    stats.light_shadow_unmapped_count = unmapped_light_count;
    stats.skeletal_palette_component_count = skeletal_build_stats.skinned_component_count;
    stats.skeletal_palette_matrix_count = skeletal_build_stats.matrix_count;
    stats.skeletal_palette_upload_count = (skeletal_build_stats.skinned_component_count > 0U) ? 2U : 0U;
    stats.light_buffer_upload_count =
        static_cast<std::uint32_t>(frame_resources.light_records.uploaded) +
        static_cast<std::uint32_t>(frame_resources.cluster_headers.uploaded) +
        static_cast<std::uint32_t>(frame_resources.cluster_indices.uploaded) +
        static_cast<std::uint32_t>(frame_resources.shadow_views.uploaded) +
        static_cast<std::uint32_t>(frame_resources.lighting_uniform.uploaded);
    if (frame_resources.light_records.uploaded) {
        stats.uploaded_bytes += frame_resources.light_records.size_bytes;
    }
    if (frame_resources.cluster_headers.uploaded) {
        stats.uploaded_bytes += frame_resources.cluster_headers.size_bytes;
    }
    if (frame_resources.cluster_indices.uploaded) {
        stats.uploaded_bytes += frame_resources.cluster_indices.size_bytes;
    }
    if (frame_resources.shadow_views.uploaded) {
        stats.uploaded_bytes += frame_resources.shadow_views.size_bytes;
    }
    if (frame_resources.lighting_uniform.uploaded) {
        stats.uploaded_bytes += frame_resources.lighting_uniform.size_bytes;
    }

    std::uint64_t descriptor_signature = 14695981039346656037ULL;
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.light_records.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.cluster_headers.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.cluster_indices.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.shadow_views.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.lighting_uniform.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.appearance_records.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.skeletal_components.buffer));
    hash_from_handle(descriptor_signature,
                     reinterpret_cast<std::uintptr_t>(frame_resources.skeletal_matrices.buffer));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.light_records.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_headers.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_indices.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.shadow_views.offset));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.lighting_uniform.offset));
    hash_combine(descriptor_signature, 0U);
    hash_combine(descriptor_signature, 0U);
    hash_combine(descriptor_signature, 0U);
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.light_records.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_headers.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.cluster_indices.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.shadow_views.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(frame_resources.lighting_uniform.size_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(appearance_record_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(skeletal_component_bytes));
    hash_combine(descriptor_signature, static_cast<std::uint64_t>(skeletal_matrix_bytes));
    frame_resources.descriptor_payload_signature = descriptor_signature;
}

void GeometryRenderer3D::PrepareLightingDescriptorSetForFrame(std::uint32_t frame_index_) {
    if (descriptor_host == nullptr ||
        frame_index_ >= frame_lighting_resources.size()) {
        return;
    }
    if (!lighting_descriptor_layout_id.IsValid()) {
        return;
    }

    FrameLightingResources& frame_resources = frame_lighting_resources[frame_index_];
    if (frame_resources.light_records.buffer == VK_NULL_HANDLE ||
        frame_resources.cluster_headers.buffer == VK_NULL_HANDLE ||
        frame_resources.cluster_indices.buffer == VK_NULL_HANDLE ||
        frame_resources.shadow_views.buffer == VK_NULL_HANDLE ||
        frame_resources.lighting_uniform.buffer == VK_NULL_HANDLE ||
        frame_resources.appearance_records.buffer == VK_NULL_HANDLE ||
        frame_resources.skeletal_components.buffer == VK_NULL_HANDLE ||
        frame_resources.skeletal_matrices.buffer == VK_NULL_HANDLE) {
        return;
    }
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        frame_resources.descriptor_set = descriptor_host->AllocateSet(*context,
                                                                      frame_index_,
                                                                      lighting_descriptor_layout_id);
        frame_resources.descriptor_buffer_signature = 0U;
        frame_resources.descriptor_image_signature = 0U;
        frame_resources.descriptor_set_signature = 0U;
    }
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    auto hash_combine = [](std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= 1099511628211ULL;
    };
    const std::uint64_t buffer_signature = frame_resources.descriptor_payload_signature;
    if (frame_resources.descriptor_buffer_signature == buffer_signature) {
        ++stats.light_descriptor_set_reuse_hit_count;
        return;
    }

    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    descriptor_buffer_write_scratch.reserve(8U);
    descriptor_buffer_write_scratch.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.light_records.buffer,
        .offset = frame_resources.light_records.offset,
        .range = frame_resources.light_records.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.cluster_headers.buffer,
        .offset = frame_resources.cluster_headers.offset,
        .range = frame_resources.cluster_headers.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 2U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.cluster_indices.buffer,
        .offset = frame_resources.cluster_indices.offset,
        .range = frame_resources.cluster_indices.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 3U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.shadow_views.buffer,
        .offset = frame_resources.shadow_views.offset,
        .range = frame_resources.shadow_views.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 4U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffer = frame_resources.lighting_uniform.buffer,
        .offset = frame_resources.lighting_uniform.offset,
        .range = frame_resources.lighting_uniform.size_bytes
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 5U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.skeletal_components.buffer,
        .offset = 0U,
        .range = static_cast<VkDeviceSize>(frame_resources.skeletal_component_count) *
                 sizeof(GeometrySkeletalComponentGpu)
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 6U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.skeletal_matrices.buffer,
        .offset = 0U,
        .range = static_cast<VkDeviceSize>(frame_resources.skeletal_matrix_count) *
                 sizeof(GeometrySkeletalMatrixGpu)
    });
    descriptor_buffer_write_scratch.push_back({
        .binding = 7U,
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
                               {},
                               descriptor_texel_write_scratch);
    frame_resources.descriptor_buffer_signature = buffer_signature;
    frame_resources.descriptor_image_signature = 0U;
    std::uint64_t descriptor_signature = 14695981039346656037ULL;
    hash_combine(descriptor_signature, buffer_signature);
    frame_resources.descriptor_set_signature = descriptor_signature;
    ++stats.descriptor_set_update_count;
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


GeometryRenderer3D::AppearancePushConstants GeometryRenderer3D::BuildAppearancePushConstants(
    const GeometryAppearanceDesc* appearance_desc_) noexcept {
    AppearancePushConstants push_constants{};
    push_constants.uv_scale_u = 1.0F;
    push_constants.uv_scale_v = 1.0F;
    push_constants.uv_bias_u = 0.0F;
    push_constants.uv_bias_v = 0.0F;

    if (appearance_desc_ == nullptr) {
        return push_constants;
    }

    const auto sanitize_value = [](float value_, float fallback_) noexcept {
        return std::isfinite(value_) ? value_ : fallback_;
    };

    push_constants.uv_scale_u = sanitize_value(appearance_desc_->uv_scale_u, 1.0F);
    push_constants.uv_scale_v = sanitize_value(appearance_desc_->uv_scale_v, 1.0F);
    push_constants.uv_bias_u = sanitize_value(appearance_desc_->uv_bias_u, 0.0F);
    push_constants.uv_bias_v = sanitize_value(appearance_desc_->uv_bias_v, 0.0F);
    return push_constants;
}

bool GeometryRenderer3D::ResolveAppearancePushConstants(
    std::uint32_t appearance_id_,
    AppearancePushConstants& out_sampling_push_constants_) {
    out_sampling_push_constants_ = BuildAppearancePushConstants(nullptr);

    std::size_t lower_bound_index = 0U;
    bool use_append_fast_path = false;
    if (!resolved_appearances.empty()) {
        const ResolvedAppearanceEntry& back_entry = resolved_appearances.back();
        if (back_entry.appearance_id == appearance_id_) {
            out_sampling_push_constants_ = back_entry.sampling_push_constants;
            ++stats.appearance_resolve_cache_hit_count;
            return true;
        }
        if (back_entry.appearance_id < appearance_id_) {
            lower_bound_index = resolved_appearances.size();
            use_append_fast_path = true;
        }
    }
    if (!use_append_fast_path) {
        lower_bound_index = LowerBoundResolvedAppearanceIndex(resolved_appearances, appearance_id_);
        if (lower_bound_index < resolved_appearances.size() &&
            resolved_appearances[lower_bound_index].appearance_id == appearance_id_) {
            out_sampling_push_constants_ = resolved_appearances[lower_bound_index].sampling_push_constants;
            ++stats.appearance_resolve_cache_hit_count;
            return true;
        }
    }

    ++stats.appearance_resolve_cache_miss_count;

    const GeometryAppearanceHost::AppearanceRecord* appearance_record = nullptr;
    if (geometry_appearance_host != nullptr && geometry_appearance_host->IsInitialized()) {
        appearance_record = geometry_appearance_host->FindAppearance(appearance_id_);
    }

    const GeometryAppearanceDesc* appearance_desc = appearance_record != nullptr
        ? &appearance_record->desc
        : nullptr;
    out_sampling_push_constants_ = BuildAppearancePushConstants(appearance_desc);

    const std::size_t old_size = resolved_appearances.size();
    resolved_appearances.resize(old_size + 1U);
    if (lower_bound_index < old_size) {
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            resolved_appearances[index] = std::move(resolved_appearances[index - 1U]);
        }
    }
    resolved_appearances[lower_bound_index] = ResolvedAppearanceEntry{
        .appearance_id = appearance_id_,
        .sampling_push_constants = out_sampling_push_constants_,
    };
    return true;
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
