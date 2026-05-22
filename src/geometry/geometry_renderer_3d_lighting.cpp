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

} // namespace vr::geometry
