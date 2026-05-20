#include "vr/particle/particle_renderer_3d.hpp"

#include "vr/ecs/system/particle_system.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_3d_frag_spv.hpp"
#include "vr/particle/generated/particle_3d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
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

namespace vr::particle {

namespace {

constexpr VkPrimitiveTopology k_particle_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
constexpr VkBufferUsageFlags k_graph_particle_draw_instances_imported_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VkBufferUsageFlags k_graph_particle_indirect_commands_imported_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

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

[[nodiscard]] float ResolveDepthClearValue(bool reverse_z_,
                                           float configured_value_) noexcept {
    return reverse_z_ ? 0.0F : configured_value_;
}

[[nodiscard]] ecs::ParticleSimulationMode AccumulateRequestedSimulationMode(
    ecs::ParticleSimulationMode current_,
    ecs::ParticleSimulationMode candidate_) noexcept {
    if (candidate_ == ecs::ParticleSimulationMode::gpu ||
        current_ == ecs::ParticleSimulationMode::gpu) {
        return ecs::ParticleSimulationMode::gpu;
    }
    if (candidate_ == ecs::ParticleSimulationMode::hybrid_gpu ||
        current_ == ecs::ParticleSimulationMode::hybrid_gpu) {
        return ecs::ParticleSimulationMode::hybrid_gpu;
    }
    return ecs::ParticleSimulationMode::cpu;
}

[[nodiscard]] std::uint32_t SaturatingAdd(std::uint32_t lhs_,
                                          std::uint32_t rhs_) noexcept {
    const std::uint64_t sum = static_cast<std::uint64_t>(lhs_) + rhs_;
    return sum > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())
        ? (std::numeric_limits<std::uint32_t>::max)()
        : static_cast<std::uint32_t>(sum);
}

[[nodiscard]] ParticleSimulationPrepareDesc BuildSimulationPrepareDesc3D(
    const ecs::Particle<ecs::Dim3>* particle_components_,
    std::uint32_t component_count_) noexcept {
    ParticleSimulationPrepareDesc prepare_desc{};
    prepare_desc.requested_mode = ecs::ParticleSimulationMode::cpu;
    for (std::uint32_t index = 0U; index < component_count_; ++index) {
        const auto& component = particle_components_[index];
        prepare_desc.requested_mode = AccumulateRequestedSimulationMode(prepare_desc.requested_mode,
                                                                        component.style.simulation_mode);
        prepare_desc.particle_capacity = SaturatingAdd(prepare_desc.particle_capacity,
                                                       component.style.max_particles);
        prepare_desc.visible_particle_capacity = SaturatingAdd(prepare_desc.visible_particle_capacity,
                                                               component.style.max_particles);
        prepare_desc.spawn_packet_capacity = SaturatingAdd(prepare_desc.spawn_packet_capacity, 1U);
        prepare_desc.indirect_command_capacity = SaturatingAdd(prepare_desc.indirect_command_capacity, 1U);
    }

    if (prepare_desc.requested_mode != ecs::ParticleSimulationMode::cpu) {
        prepare_desc.require_draw_instance_buffer = true;
        prepare_desc.draw_instance_stride_bytes = sizeof(ecs::Particle3DGpuInstance);
        prepare_desc.require_sort_buffers = true;
        prepare_desc.sort_key_capacity = std::max(prepare_desc.sort_key_capacity,
                                                  prepare_desc.visible_particle_capacity);
    }
    return prepare_desc;
}

[[nodiscard]] bool HasDedicatedOwnedComputeQueue(const VulkanContext& context_) noexcept {
    if (context_.GraphicsQueue() == VK_NULL_HANDLE ||
        context_.ComputeQueue() == VK_NULL_HANDLE) {
        return false;
    }
    if (context_.ComputeQueue() != context_.GraphicsQueue()) {
        return true;
    }
    return context_.QueueFamilies().graphics.has_value() &&
           context_.QueueFamilies().compute.has_value() &&
           context_.QueueFamilies().graphics.value() != context_.QueueFamilies().compute.value();
}

} // namespace

bool ParticleRenderer3D::IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool ParticleRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags ParticleRenderer3D::DepthImageAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return aspect;
}

VkFormat ParticleRenderer3D::ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };
    for (VkFormat candidate : fallback_formats) {
        if (IsDepthFormatSupported(context_, candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("ParticleRenderer3D failed to resolve usable depth format");
}

std::size_t ParticleRenderer3D::BlendModeIndex(BlendModeKind blend_mode_) noexcept {
    return static_cast<std::size_t>(blend_mode_);
}

std::size_t ParticleRenderer3D::DepthPipelineModeIndex(DepthPipelineMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

ParticleRenderer3D::BlendModeKind ParticleRenderer3D::DecodeBlendModeKind(
    std::uint32_t pipeline_state_) noexcept {
    switch (static_cast<ecs::RuntimeBlendPreset>(
        (pipeline_state_ >> ecs::particle_pipeline_state_blend_shift) &
        ecs::particle_pipeline_state_blend_mask)) {
    case ecs::RuntimeBlendPreset::additive:
        return BlendModeKind::additive;
    case ecs::RuntimeBlendPreset::multiply:
        return BlendModeKind::multiply;
    case ecs::RuntimeBlendPreset::premultiplied_alpha:
        return BlendModeKind::premultiplied_alpha;
    case ecs::RuntimeBlendPreset::screen:
        return BlendModeKind::screen;
    case ecs::RuntimeBlendPreset::alpha:
    default:
        return BlendModeKind::alpha;
    }
}

ecs::ParticleFacingMode ParticleRenderer3D::DecodeFacingMode(std::uint32_t pipeline_state_) noexcept {
    return static_cast<ecs::ParticleFacingMode>(
        (pipeline_state_ >> ecs::particle_pipeline_state_facing_mode_shift) &
        ecs::particle_pipeline_state_facing_mode_mask);
}

ecs::ParticleRenderMode ParticleRenderer3D::DecodeRenderMode(std::uint32_t pipeline_state_) noexcept {
    return static_cast<ecs::ParticleRenderMode>(
        (pipeline_state_ >> ecs::particle_pipeline_state_render_mode_shift) &
        ecs::particle_pipeline_state_render_mode_mask);
}

ecs::ParticleLightingMode ParticleRenderer3D::DecodeLightingMode(std::uint32_t pipeline_state_) noexcept {
    return static_cast<ecs::ParticleLightingMode>(
        (pipeline_state_ >> ecs::particle_pipeline_state_lighting_mode_shift) &
        ecs::particle_pipeline_state_lighting_mode_mask);
}

std::uint64_t ParticleRenderer3D::ComposeBindlessUploadRevision(
    const ecs::ParticleRuntimeBuildStats& runtime_stats_,
    std::uint32_t texture_revision_) noexcept {
    std::uint64_t revision = ParticleUploadHost::ComposeUploadRevision(
        runtime_stats_.component_signature,
        runtime_stats_.transform_signature,
        runtime_stats_.visible_signature,
        runtime_stats_.runtime_state_signature);
    revision ^= static_cast<std::uint64_t>(texture_revision_) + 0x9e3779b97f4a7c15ULL +
                (revision << 6U) + (revision >> 2U);
    return revision;
}

ParticleRenderer3D::DepthPipelineMode ParticleRenderer3D::ResolveDepthPipelineMode(
    std::uint32_t pipeline_state_,
    bool use_depth_,
    bool reverse_z_) noexcept {
    if (!use_depth_ ||
        ((pipeline_state_ >> ecs::particle_pipeline_state_depth_test_shift) & 0x1U) == 0U) {
        return DepthPipelineMode::no_depth;
    }
    const bool depth_write =
        ((pipeline_state_ >> ecs::particle_pipeline_state_depth_write_shift) & 0x1U) != 0U;
    if (reverse_z_) {
        return depth_write
            ? DepthPipelineMode::depth_test_write_reverse_z
            : DepthPipelineMode::depth_test_reverse_z;
    }
    return depth_write
        ? DepthPipelineMode::depth_test_write
        : DepthPipelineMode::depth_test;
}

void ParticleRenderer3D::Initialize(const ParticleRenderer3DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_particle_count > 0U) {
        ecs::ParticleRuntimeSystem<ecs::Dim3>::Reserve(runtime_scratch,
                                                       create_info_cache.reserve_component_count,
                                                       create_info_cache.reserve_particle_count);
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch,
                                               create_info_cache.reserve_component_count);
        ordered_visible_entries.reserve(create_info_cache.reserve_component_count);
        ordered_visible_component_indices.reserve(create_info_cache.reserve_component_count);
    }

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& per_blend : pipeline_ids) {
        for (auto& pipeline_id : per_blend) {
            pipeline_id = {};
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;

    depth_format = VK_FORMAT_UNDEFINED;
    depth_images.clear();
    depth_image_initialized.clear();
    retired_depth_images.clear();
    image_initialized.clear();
    output_target_config = {};
    depth_output_target_config = {};
    culling_stats = {};
    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    stats = {};
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;

    particle_components = nullptr;
    particle_emitters = nullptr;
    transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    particle_upload_host = nullptr;
    particle_simulation_host = nullptr;
    texture_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    active_camera_reverse_z = false;
    gpu_build_active = false;
    initialized = true;
}

void ParticleRenderer3D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    DestroyDepthResources(context_);
    DestroyRetiredDepthResources(context_);

    runtime_scratch.emitter_states.clear();
    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.build_component_indices.clear();
    runtime_scratch.cache = {};
    culling_scratch.visible_indices.clear();
    culling_scratch.visibility_stamps.clear();
    ordered_visible_entries.clear();
    ordered_visible_component_indices.clear();
    image_initialized.clear();

    particle_components = nullptr;
    particle_emitters = nullptr;
    transforms = nullptr;
    component_count = 0U;
    camera_component = nullptr;
    camera_transform = nullptr;
    bounds_components = nullptr;
    particle_upload_host = nullptr;
    particle_simulation_host = nullptr;
    texture_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& per_blend : pipeline_ids) {
        for (auto& pipeline_id : per_blend) {
            pipeline_id = {};
        }
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;

    output_target_config = {};
    depth_output_target_config = {};
    culling_stats = {};
    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    stats = {};
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    active_camera_reverse_z = false;
    gpu_build_active = false;
    initialized = false;
}

void ParticleRenderer3D::SetHost(ParticleUploadHost* upload_host_) noexcept {
    particle_upload_host = upload_host_;
}

void ParticleRenderer3D::SetSimulationHost(ParticleSimulationHost* simulation_host_) noexcept {
    particle_simulation_host = simulation_host_;
}

void ParticleRenderer3D::SetTextureHost(asset::TextureHost* texture_host_) noexcept {
    texture_host = texture_host_;
}

void ParticleRenderer3D::SetHosts(ParticleUploadHost* upload_host_,
                                  asset::TextureHost* texture_host_) noexcept {
    particle_upload_host = upload_host_;
    texture_host = texture_host_;
}

void ParticleRenderer3D::SetSceneData(ecs::Particle<ecs::Dim3>* particle_components_,
                                      ecs::ParticleEmitter<ecs::Dim3>* particle_emitters_,
                                      ecs::Transform<ecs::Dim3>* transforms_,
                                      std::uint32_t component_count_,
                                      ecs::Camera<ecs::Dim3>* camera_component_,
                                      ecs::Transform<ecs::Dim3>* camera_transform_,
                                      ecs::Bounds<ecs::Dim3>* bounds_components_) noexcept {
    particle_components = particle_components_;
    particle_emitters = particle_emitters_;
    transforms = transforms_;
    component_count = component_count_;
    camera_component = camera_component_;
    camera_transform = camera_transform_;
    bounds_components = bounds_components_;
    if (component_count_ > 0U) {
        ecs::CullingSystem<ecs::Dim3>::Reserve(culling_scratch, component_count_);
        ordered_visible_entries.reserve(component_count_);
        ordered_visible_component_indices.reserve(component_count_);
    }
}

void ParticleRenderer3D::SetOutputTargetConfig(
    const render::RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void ParticleRenderer3D::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
}

void ParticleRenderer3D::SetDepthTargetConfig(
    const render::RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept {
    depth_output_target_config = depth_output_target_config_;
}

void ParticleRenderer3D::ResetDepthTargetConfig() noexcept {
    depth_output_target_config = {};
}

ecs::Float3 ParticleRenderer3D::ResolveCameraPosition() const noexcept {
    if (camera_transform == nullptr) {
        return ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    }
    return ecs::Float3{
        .x = camera_transform->runtime.world_matrix.m[12],
        .y = camera_transform->runtime.world_matrix.m[13],
        .z = camera_transform->runtime.world_matrix.m[14],
    };
}

ecs::Float3 ParticleRenderer3D::ResolveCameraRight() const noexcept {
    if (camera_transform == nullptr) {
        return ecs::Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    }
    return ecs::spatial_math::Normalize3(ecs::Float3{
                                             .x = camera_transform->runtime.world_matrix.m[0],
                                             .y = camera_transform->runtime.world_matrix.m[1],
                                             .z = camera_transform->runtime.world_matrix.m[2],
                                         },
                                         ecs::Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
}

ecs::Float3 ParticleRenderer3D::ResolveCameraUp() const noexcept {
    if (camera_transform == nullptr) {
        return ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }
    return ecs::spatial_math::Normalize3(ecs::Float3{
                                             .x = camera_transform->runtime.world_matrix.m[4],
                                             .y = camera_transform->runtime.world_matrix.m[5],
                                             .z = camera_transform->runtime.world_matrix.m[6],
                                         },
                                         ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
}

ecs::Float3 ParticleRenderer3D::ResolveCameraForward() const noexcept {
    if (camera_transform == nullptr) {
        return ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F};
    }
    return ecs::spatial_math::Normalize3(ecs::Float3{
                                             .x = -camera_transform->runtime.world_matrix.m[8],
                                             .y = -camera_transform->runtime.world_matrix.m[9],
                                             .z = -camera_transform->runtime.world_matrix.m[10],
                                         },
                                         ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -1.0F});
}

float ParticleRenderer3D::ResolveComponentDistanceSq(std::uint32_t component_index_) const noexcept {
    const ecs::Float3 camera_position = ResolveCameraPosition();
    ecs::Float3 position = camera_position;
    if (bounds_components != nullptr && component_index_ < component_count) {
        position = bounds_components[component_index_].runtime.world_center;
    } else if (transforms != nullptr && component_index_ < component_count) {
        position = ecs::Float3{
            .x = transforms[component_index_].runtime.world_matrix.m[12],
            .y = transforms[component_index_].runtime.world_matrix.m[13],
            .z = transforms[component_index_].runtime.world_matrix.m[14],
        };
    }
    const float dx = position.x - camera_position.x;
    const float dy = position.y - camera_position.y;
    const float dz = position.z - camera_position.z;
    return dx * dx + dy * dy + dz * dz;
}

bool ParticleRenderer3D::RequiresDepthSorting(const ecs::Particle<ecs::Dim3>& component_) const noexcept {
    const ecs::RuntimeBlendPreset blend_preset = ecs::ResolveRuntimeBlendPreset(
        component_.style.blend_mode,
        component_.style.premultiplied_alpha != 0U);
    if (blend_preset == ecs::RuntimeBlendPreset::additive ||
        blend_preset == ecs::RuntimeBlendPreset::screen) {
        return false;
    }
    switch (component_.style.sort_mode) {
    case ecs::ParticleSortMode::none:
        return false;
    case ecs::ParticleSortMode::weighted_blended_oit:
        return true;
    case ecs::ParticleSortMode::gpu_radix:
    case ecs::ParticleSortMode::by_view_depth:
    case ecs::ParticleSortMode::bucket:
    default:
        return true;
    }
}

void ParticleRenderer3D::BuildOrderedVisibleComponentList() {
    ordered_visible_entries.clear();
    ordered_visible_component_indices.clear();
    culling_stats = {};
    stats.used_bounds_culling = false;

    if (particle_components == nullptr || component_count == 0U) {
        return;
    }

    const std::uint32_t* source_indices = nullptr;
    std::uint32_t source_count = 0U;
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
        source_indices = culling_scratch.visible_indices.data();
        source_count = culling_stats.visible_count;

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

    if (source_indices == nullptr) {
        ordered_visible_entries.reserve(component_count);
        for (std::uint32_t component_index = 0U; component_index < component_count; ++component_index) {
            const ecs::Particle<ecs::Dim3>& component = particle_components[component_index];
            if (!ecs::ParticleSystem<ecs::Dim3>::IsVisibleForBuild(component)) {
                continue;
            }

            OrderedVisibleEntry entry{};
            entry.component_index = component_index;
            entry.pass_hint_value = ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(
                component.runtime.route.sort_key);
            entry.sort_mode_value = static_cast<std::uint32_t>(component.style.sort_mode);
            entry.blend_preset_value = static_cast<std::uint32_t>(ecs::ResolveRuntimeBlendPreset(
                component.style.blend_mode,
                component.style.premultiplied_alpha != 0U));
            entry.distance_sq = ResolveComponentDistanceSq(component_index);
            entry.binding_key = ecs::ParticleSystem<ecs::Dim3>::BindingSortKey(component);
            ordered_visible_entries.push_back(entry);
        }
    } else {
        ordered_visible_entries.reserve(source_count);
        for (std::uint32_t i = 0U; i < source_count; ++i) {
            const std::uint32_t component_index = source_indices[i];
            if (component_index >= component_count) {
                continue;
            }
            const ecs::Particle<ecs::Dim3>& component = particle_components[component_index];
            if (!ecs::ParticleSystem<ecs::Dim3>::IsVisibleForBuild(component)) {
                continue;
            }

            OrderedVisibleEntry entry{};
            entry.component_index = component_index;
            entry.pass_hint_value = ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(
                component.runtime.route.sort_key);
            entry.sort_mode_value = static_cast<std::uint32_t>(component.style.sort_mode);
            entry.blend_preset_value = static_cast<std::uint32_t>(ecs::ResolveRuntimeBlendPreset(
                component.style.blend_mode,
                component.style.premultiplied_alpha != 0U));
            entry.distance_sq = ResolveComponentDistanceSq(component_index);
            entry.binding_key = ecs::ParticleSystem<ecs::Dim3>::BindingSortKey(component);
            ordered_visible_entries.push_back(entry);
        }
    }

    std::stable_sort(ordered_visible_entries.begin(),
                     ordered_visible_entries.end(),
                     [&](const OrderedVisibleEntry& lhs_,
                         const OrderedVisibleEntry& rhs_) {
                         if (lhs_.pass_hint_value != rhs_.pass_hint_value) {
                             return lhs_.pass_hint_value < rhs_.pass_hint_value;
                         }

                         const bool lhs_depth_sorted = lhs_.pass_hint_value ==
                             static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent) &&
                             lhs_.sort_mode_value != static_cast<std::uint32_t>(ecs::ParticleSortMode::none) &&
                             lhs_.blend_preset_value != static_cast<std::uint32_t>(ecs::RuntimeBlendPreset::additive) &&
                             lhs_.blend_preset_value != static_cast<std::uint32_t>(ecs::RuntimeBlendPreset::screen);
                         const bool rhs_depth_sorted = rhs_.pass_hint_value ==
                             static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent) &&
                             rhs_.sort_mode_value != static_cast<std::uint32_t>(ecs::ParticleSortMode::none) &&
                             rhs_.blend_preset_value != static_cast<std::uint32_t>(ecs::RuntimeBlendPreset::additive) &&
                             rhs_.blend_preset_value != static_cast<std::uint32_t>(ecs::RuntimeBlendPreset::screen);

                         if (lhs_depth_sorted != rhs_depth_sorted) {
                             return lhs_depth_sorted;
                         }

                         if (lhs_depth_sorted) {
                             if (lhs_.distance_sq != rhs_.distance_sq) {
                                 return lhs_.distance_sq > rhs_.distance_sq;
                             }
                         } else if (lhs_.pass_hint_value ==
                                        static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::opaque) &&
                                    lhs_.distance_sq != rhs_.distance_sq) {
                             return lhs_.distance_sq < rhs_.distance_sq;
                         }

                         if (lhs_.binding_key != rhs_.binding_key) {
                             return lhs_.binding_key < rhs_.binding_key;
                         }
                         return lhs_.component_index < rhs_.component_index;
                     });

    ordered_visible_component_indices.reserve(ordered_visible_entries.size());
    for (const OrderedVisibleEntry& entry : ordered_visible_entries) {
        ordered_visible_component_indices.push_back(entry.component_index);
    }
}

void ParticleRenderer3D::PrepareFrame(const render::ParticleRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer3D::PrepareFrame called before Initialize");
    }
    if (prepare_view_.device.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        prepare_view_.device.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer3D requires Vulkan 1.3 dynamicRendering + synchronization2");
    }
    if (particle_upload_host == nullptr && prepare_view_.particle_upload != nullptr) {
        particle_upload_host = prepare_view_.particle_upload;
    }
    if (particle_simulation_host == nullptr && prepare_view_.particle_simulation != nullptr) {
        particle_simulation_host = prepare_view_.particle_simulation;
    }
    if (particle_upload_host == nullptr || !particle_upload_host->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer3D::PrepareFrame requires initialized ParticleUploadHost");
    }
    if (particle_simulation_host != nullptr && !particle_simulation_host->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer3D::PrepareFrame received non-initialized ParticleSimulationHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    gpu_memory_host = &prepare_view_.gpu_memory;
    if (prepare_view_.texture != nullptr) {
        texture_host = prepare_view_.texture;
    }
    if (prepare_view_.bindless != nullptr) {
        bindless_resources = prepare_view_.bindless;
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer3D::PrepareFrame requires initialized BindlessResourceSystem");
    }

    active_frame_index = prepare_view_.frame.frame_index;
    swapchain_extent = prepare_view_.frame.swapchain_extent;
    swapchain_format = prepare_view_.frame.swapchain_format;
    last_submitted_value_seen = std::max(last_submitted_value_seen, prepare_view_.progress.last_submitted_value);
    completed_submit_value_seen = std::max(completed_submit_value_seen, prepare_view_.progress.completed_submit_value);
    active_camera_reverse_z = camera_component != nullptr && camera_component->style.reverse_z != 0U;

    CollectRetiredDepthResources(*context, completed_submit_value_seen);

    stats = {};
    stats.component_count = component_count;
    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    gpu_build_active = false;
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;

    if (particle_components == nullptr || particle_emitters == nullptr || transforms == nullptr || component_count == 0U) {
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        runtime_scratch.build_component_indices.clear();
        ordered_visible_entries.clear();
        ordered_visible_component_indices.clear();
        return;
    }

    if (particle_simulation_host != nullptr) {
        particle_simulation_host->BeginFrame(*context,
                                             active_frame_index,
                                             prepare_view_.progress.last_submitted_value,
                                             prepare_view_.progress.completed_submit_value);
        const ParticleSimulationPrepareDesc prepare_desc =
            BuildSimulationPrepareDesc3D(particle_components,
                                         component_count);
        last_simulation_resources = particle_simulation_host->PrepareFrameResources(*context,
                                                                                    active_frame_index,
                                                                                    prepare_desc);
    }

    BuildOrderedVisibleComponentList();

    Particle3DRuntimeUploadOptions upload_options = create_info_cache.runtime_upload_options;
    upload_options.runtime_build.build_ordered_batches = false;

    ecs::Particle3DRuntimeBuildHint build_hint{};
    build_hint.visible_component_indices = ordered_visible_component_indices.data();
    build_hint.visible_component_count = static_cast<std::uint32_t>(ordered_visible_component_indices.size());
    build_hint.use_visible_component_indices = ordered_visible_component_indices.empty() ? 0U : 1U;

    if (last_simulation_resources.resolved_path != ParticleSimulationResolvedPath::cpu) {
        ecs::ParticleRuntimeBuildConfig build_config = upload_options.runtime_build;
        build_config.build_instances = false;
        bool cpu_seeded_this_frame =
            last_simulation_resources.resolved_path != ParticleSimulationResolvedPath::gpu;
        if (last_simulation_resources.resolved_path == ParticleSimulationResolvedPath::gpu) {
            build_config.simulate = false;
            build_config.emit_new_particles = false;
        }
        last_runtime_build_stats = ecs::ParticleRuntimeSystem<ecs::Dim3>::Build(
            particle_components,
            particle_emitters,
            transforms,
            component_count,
            runtime_scratch,
            build_config,
            build_hint);
        if (last_simulation_resources.resolved_path == ParticleSimulationResolvedPath::gpu &&
            !particle_simulation_host->HasPersistentState3D() &&
            particle_simulation_host->NeedsCpuSeed3D(active_frame_index)) {
            build_config = upload_options.runtime_build;
            build_config.build_instances = false;
            cpu_seeded_this_frame = true;
            last_runtime_build_stats = ecs::ParticleRuntimeSystem<ecs::Dim3>::Build(
                particle_components,
                particle_emitters,
                transforms,
                component_count,
                runtime_scratch,
                build_config,
                build_hint);
        }
        last_gpu_build_result = particle_simulation_host->PrepareGpuBuild3D(
            *context,
            *upload_host,
            *descriptor_host,
            *pipeline_host,
            active_frame_index,
            last_simulation_resources,
            particle_components,
            particle_emitters,
            transforms,
            component_count,
            build_config,
            cpu_seeded_this_frame,
            runtime_scratch,
            last_runtime_build_stats,
            texture_host,
            *bindless_resources);
        gpu_build_active = last_gpu_build_result.used_gpu_build;
        graph_compute_pass_owned =
            gpu_build_active &&
            prepare_view_.prefer_render_graph_compute_path &&
            HasDedicatedOwnedComputeQueue(*context);
    }

    if (gpu_build_active) {
        stats.visible_component_count = last_runtime_build_stats.candidate_emitter_count;
        stats.emitter_count = last_runtime_build_stats.emitter_count;
        stats.active_particle_count = last_runtime_build_stats.active_particle_count;
        stats.visible_particle_count = last_runtime_build_stats.visible_particle_count;
        stats.draw_batch_count = last_runtime_build_stats.emitted_batch_count;
        stats.uploaded_instance_count = last_gpu_build_result.state_record_count;
        stats.uploaded_bytes =
            (last_gpu_build_result.state_upload.uploaded ? last_gpu_build_result.state_upload.size_bytes : 0U) +
            (last_gpu_build_result.spawn_upload.uploaded ? last_gpu_build_result.spawn_upload.size_bytes : 0U) +
            (last_gpu_build_result.indirect_upload.uploaded ? last_gpu_build_result.indirect_upload.size_bytes : 0U);
        stats.cache_reused = last_gpu_build_result.cache_reused &&
                             last_runtime_build_stats.visible_particle_count > 0U;
        stats.skipped_upload = !last_gpu_build_result.state_upload.uploaded &&
                               !last_gpu_build_result.indirect_upload.uploaded;
    } else {
        particle_upload_host->BeginFrame(*context,
                                         active_frame_index,
                                         prepare_view_.progress.last_submitted_value,
                                         prepare_view_.progress.completed_submit_value);

        last_upload_result.runtime = ecs::ParticleRuntimeSystem<ecs::Dim3>::Build(
            particle_components,
            particle_emitters,
            transforms,
            component_count,
            runtime_scratch,
            upload_options.runtime_build,
            build_hint);

        if (!runtime_scratch.instances.empty() &&
            last_upload_result.runtime.emitted_instance_count > 0U) {
            RemapCpuInstancesToBindless();
            last_upload_result.upload = particle_upload_host->Upload3DInstances(
                *context,
                *upload_host,
                active_frame_index,
                runtime_scratch.instances.data(),
                static_cast<std::uint32_t>(runtime_scratch.instances.size()),
                ComposeBindlessUploadRevision(
                    last_upload_result.runtime,
                    texture_host != nullptr ? texture_host->Stats().revision : 0U));
        } else {
            last_upload_result.skipped_upload = true;
        }

        stats.visible_component_count = last_upload_result.runtime.candidate_emitter_count;
        stats.emitter_count = last_upload_result.runtime.emitter_count;
        stats.active_particle_count = last_upload_result.runtime.active_particle_count;
        stats.visible_particle_count = last_upload_result.runtime.visible_particle_count;
        stats.draw_batch_count = last_upload_result.runtime.emitted_batch_count;
        stats.uploaded_instance_count = last_upload_result.upload.element_count;
        stats.uploaded_bytes = last_upload_result.upload.uploaded ? last_upload_result.upload.size_bytes : 0U;
        stats.cache_reused = !last_upload_result.upload.uploaded &&
                             last_upload_result.runtime.emitted_instance_count > 0U;
        stats.skipped_upload = last_upload_result.skipped_upload;
    }

    for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
        const auto depth_mode = ResolveDepthPipelineMode(batch.pipeline_state,
                                                         create_info_cache.enable_depth,
                                                         active_camera_reverse_z);
        if (depth_mode != DepthPipelineMode::no_depth) {
            ++stats.depth_test_batch_count;
            stats.depth_interaction_enabled = true;
        }
        if (depth_mode == DepthPipelineMode::depth_test_write ||
            depth_mode == DepthPipelineMode::depth_test_write_reverse_z) {
            ++stats.depth_write_batch_count;
        }
        const ecs::ParticleLightingMode lighting_mode = DecodeLightingMode(batch.pipeline_state);
        if (lighting_mode != ecs::ParticleLightingMode::unlit) {
            ++stats.lighting_mode_fallback_count;
        }
        const std::uint32_t component_index = batch.first_component_index;
        if (component_index < component_count &&
            particle_components[component_index].style.soft_particle_distance > 0.0F) {
            ++stats.soft_particle_disabled_count;
        }
    }
}

void ParticleRenderer3D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                         const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "ParticleRenderer3D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const_cast<ParticleRenderer3D*>(this)->ScheduleGraphComputeBuild(builder_, pass_);

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("particle_3d.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     0U,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     1U,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);
}

void ParticleRenderer3D::RegisterGraphImportedResources(
    runtime::services::RenderGraphRuntimeService& graph_runtime_service_) const {
    if (!graph_compute_pass_owned ||
        !render_graph::IsValidResourceHandle(graph_draw_instances_resource) ||
        !render_graph::IsValidResourceHandle(graph_indirect_commands_resource)) {
        return;
    }

    if (last_gpu_build_result.resources.draw_instances.buffer != VK_NULL_HANDLE &&
        last_gpu_build_result.resources.draw_instances.size_bytes != 0U) {
        graph_runtime_service_.RegisterDirectImportedBuffer(
            graph_draw_instances_resource,
            render_graph::ImportedBufferBinding{
                .buffer = last_gpu_build_result.resources.draw_instances.buffer,
                .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
                .usage = k_graph_particle_draw_instances_imported_usage,
            });
    }
    if (last_gpu_build_result.resources.indirect_commands.buffer != VK_NULL_HANDLE &&
        last_gpu_build_result.resources.indirect_commands.size_bytes != 0U) {
        graph_runtime_service_.RegisterDirectImportedBuffer(
            graph_indirect_commands_resource,
            render_graph::ImportedBufferBinding{
                .buffer = last_gpu_build_result.resources.indirect_commands.buffer,
                .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
                .usage = k_graph_particle_indirect_commands_imported_usage,
            });
    }
}

void ParticleRenderer3D::ScheduleGraphComputeBuild(render_graph::RenderGraphBuilder& builder_,
                                                   const render_graph::PassHandle pass_) {
    if (!graph_compute_pass_owned) {
        return;
    }
    const auto append_scene_reads = [&](const VkDeviceSize draw_instances_size_,
                                        const VkDeviceSize indirect_commands_size_) {
        (void)builder_.Read(
            pass_,
            graph_draw_instances_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::vertex_buffer_read,
                .buffer_range = {
                    .offset_bytes = 0U,
                    .size_bytes = draw_instances_size_,
                },
            });
        (void)builder_.Read(
            pass_,
            graph_indirect_commands_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::indirect_command_read,
                .buffer_range = {
                    .offset_bytes = 0U,
                    .size_bytes = indirect_commands_size_,
                },
            });
    };
    if (graph_compute_pass_scheduled) {
        if (render_graph::IsValidResourceVersionHandle(graph_draw_instances_version) &&
            render_graph::IsValidResourceVersionHandle(graph_indirect_commands_version)) {
            append_scene_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
                               last_gpu_build_result.resources.indirect_commands.size_bytes);
        }
        return;
    }
    if (last_gpu_build_result.resources.draw_instances.buffer == VK_NULL_HANDLE ||
        last_gpu_build_result.resources.indirect_commands.buffer == VK_NULL_HANDLE ||
        last_gpu_build_result.resources.draw_instances.size_bytes == 0U ||
        last_gpu_build_result.resources.indirect_commands.size_bytes == 0U ||
        particle_simulation_host == nullptr) {
        graph_compute_pass_owned = false;
        return;
    }

    graph_draw_instances_resource = builder_.CreateBuffer(
        "particle_3d_draw_instances",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_vertex_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);
    graph_indirect_commands_resource = builder_.CreateBuffer(
        "particle_3d_indirect_commands",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_indirect_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);

    const auto compute_pass = builder_.AddPass("particle_3d_gpu_build",
                                               false,
                                               render_graph::QueueClass::compute);
    graph_draw_instances_version = builder_.Write(
        compute_pass,
        graph_draw_instances_resource,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::shader_storage_write,
            .buffer_range = {
                .offset_bytes = 0U,
                .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
            },
        });
    graph_indirect_commands_version = builder_.Write(
        compute_pass,
        graph_indirect_commands_resource,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::shader_storage_write,
            .buffer_range = {
                .offset_bytes = 0U,
                .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
            },
        });
    builder_.SetExecuteCallback(
        compute_pass,
        [this, frame_index = active_frame_index](render_graph::GraphCommandContext& context_) {
            if (particle_simulation_host == nullptr) {
                return;
            }
            particle_simulation_host->RecordBuild3D(*context,
                                                    *pipeline_host,
                                                    frame_index,
                                                    ResolveCameraPosition(),
                                                    ResolveCameraForward(),
                                                    context_.CommandBuffer());
        });
    append_scene_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
                       last_gpu_build_result.resources.indirect_commands.size_bytes);
    graph_compute_pass_scheduled = true;
}

void ParticleRenderer3D::Record(const render::FrameRecordContext& record_context_) {
    RecordInternal(record_context_, 0U, false);
}

void ParticleRenderer3D::RecordSceneStage(const render::FrameRecordContext& record_context_,
                                          render::SceneRenderStage stage_) {
    RecordInternal(record_context_, render::SceneRenderStagePassHintValue(stage_), true);
}

void ParticleRenderer3D::RecordGraphSceneStage(render_graph::GraphCommandContext& context_,
                                               render::SceneRenderStage stage_,
                                               render_graph::ResourceHandle color_target_,
                                               render_graph::ResourceHandle depth_target_) {
    RecordGraphInternal(context_,
                        render::SceneRenderStagePassHintValue(stage_),
                        true,
                        color_target_,
                        depth_target_);
}

void ParticleRenderer3D::RecordInternal(const render::FrameRecordContext& record_context_,
                                        std::uint32_t pass_bucket_,
                                        bool filter_by_pass_bucket_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer3D::Record called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("ParticleRenderer3D::Record called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer3D::Record missing initialized BindlessResourceSystem");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleRenderer3D::Record requires valid command buffer");
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
        throw std::runtime_error("ParticleRenderer3D::Record resolved zero-sized render extent");
    }

    bool using_external_depth_target = false;
    VkFormat active_depth_format = VK_FORMAT_UNDEFINED;
    bool has_previous_depth_content = false;
    const float depth_clear_value = ResolveDepthClearValue(active_camera_reverse_z,
                                                           create_info_cache.clear_depth_value);

    if (create_info_cache.enable_depth &&
        record_context_.render_target_host != nullptr &&
        record_context_.render_target_host->IsValid(depth_output_target_config.depth_target)) {
        const render::RenderTargetResolvedView depth_view =
            record_context_.render_target_host->ResolveView(depth_output_target_config.depth_target);
        if (depth_view.image_view == VK_NULL_HANDLE) {
            throw std::runtime_error("ParticleRenderer3D::Record external depth target has null view");
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
                effective_depth_output_config.clear_depth_stencil.depth = depth_clear_value;
                effective_depth_output_config.clear_depth_stencil.stencil = create_info_cache.clear_stencil_value;
            }
            color_pass = render::BuildColorDepthRenderPass(record_context_,
                                                           output_target_config,
                                                           effective_depth_output_config,
                                                           create_info_cache.clear_swapchain,
                                                           create_info_cache.clear_color,
                                                           has_previous_content,
                                                           has_previous_depth_content);
            using_external_depth_target = true;
            active_depth_format = color_pass.depth_target.format;
        } else {
            color_pass = render::BuildColorRenderPass(record_context_,
                                                      output_target_config,
                                                      create_info_cache.clear_swapchain,
                                                      create_info_cache.clear_color,
                                                      has_previous_content);
            if (depth_format == VK_FORMAT_UNDEFINED) {
                depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
            }
            const std::uint32_t required_image_count = static_cast<std::uint32_t>(std::max<std::size_t>(
                image_initialized.size(),
                static_cast<std::size_t>(record_context_.image_index + 1U)));
            EnsureDepthResources(*context, required_image_count, render_extent);
            if (record_context_.image_index >= depth_images.size()) {
                throw std::runtime_error("ParticleRenderer3D::Record depth image index out of range");
            }
            if (record_context_.image_index >= depth_image_initialized.size()) {
                const std::size_t previous_size = depth_image_initialized.size();
                depth_image_initialized.resize(record_context_.image_index + 1U);
                for (std::size_t i = previous_size; i < depth_image_initialized.size(); ++i) {
                    depth_image_initialized[i] = 0U;
                }
            }

            const resource::ImageResource& depth_resource = depth_images[record_context_.image_index];
            if (depth_resource.image == VK_NULL_HANDLE || depth_resource.default_view == VK_NULL_HANDLE) {
                throw std::runtime_error("ParticleRenderer3D::Record depth resource is invalid");
            }
            const bool depth_initialized = depth_image_initialized[record_context_.image_index] != 0U;

            VkImageMemoryBarrier depth_barrier{};
            depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            depth_barrier.srcAccessMask = depth_initialized
                ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                : 0U;
            depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depth_barrier.oldLayout = depth_initialized
                ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED;
            depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depth_barrier.image = depth_resource.image;
            depth_barrier.subresourceRange.aspectMask = DepthImageAspectMask(depth_format);
            depth_barrier.subresourceRange.baseMipLevel = 0U;
            depth_barrier.subresourceRange.levelCount = 1U;
            depth_barrier.subresourceRange.baseArrayLayer = 0U;
            depth_barrier.subresourceRange.layerCount = 1U;
            vkCmdPipelineBarrier(record_context_.command_buffer,
                                 depth_initialized
                                     ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                     : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0U,
                                 0U,
                                 nullptr,
                                 0U,
                                 nullptr,
                                 1U,
                                 &depth_barrier);

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
            color_pass.rendering_info.depth_attachment.clearValue.depthStencil.depth = depth_clear_value;
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

    if (gpu_build_active &&
        particle_simulation_host != nullptr &&
        !graph_compute_pass_owned) {
        particle_simulation_host->RecordBuild3D(*context,
                                                *pipeline_host,
                                                active_frame_index,
                                                ResolveCameraPosition(),
                                                ResolveCameraForward(),
                                                record_context_.command_buffer);
    }

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

    if (((gpu_build_active &&
          last_gpu_build_result.resources.draw_instances.buffer != VK_NULL_HANDLE) ||
         (last_upload_result.upload.buffer != VK_NULL_HANDLE)) &&
        !runtime_scratch.draw_batches.empty()) {
        const VkBuffer vertex_buffer = gpu_build_active
            ? last_gpu_build_result.resources.draw_instances.buffer
            : last_upload_result.upload.buffer;
        const VkDeviceSize vertex_offset = gpu_build_active
            ? 0U
            : last_upload_result.upload.offset;
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
        const ecs::Float3 camera_right = ResolveCameraRight();
        const ecs::Float3 camera_up = ResolveCameraUp();
        const ecs::Float3 camera_forward = ResolveCameraForward();
        push_constants.camera_right = ecs::Float4{.x = camera_right.x, .y = camera_right.y, .z = camera_right.z, .w = 0.0F};
        push_constants.camera_up = ecs::Float4{.x = camera_up.x, .y = camera_up.y, .z = camera_up.z, .w = 0.0F};
        push_constants.camera_forward = ecs::Float4{.x = camera_forward.x, .y = camera_forward.y, .z = camera_forward.z, .w = 0.0F};
        push_constants.params = 0U;
        push_constants.reserved0 = 0U;
        push_constants.reserved1 = 0U;
        push_constants.reserved2 = 0U;

        const std::array<VkDescriptorSet, 2U> bindless_sets{
            bindless_resources->SampledImageSet(),
            bindless_resources->SamplerSet()
        };
        if (bindless_sets[0U] == VK_NULL_HANDLE || bindless_sets[1U] == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "ParticleRenderer3D::Record requires valid bindless descriptor sets");
        }
        vkCmdBindDescriptorSets(record_context_.command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout,
                                0U,
                                static_cast<std::uint32_t>(bindless_sets.size()),
                                bindless_sets.data(),
                                0U,
                                nullptr);
        ++stats.descriptor_set_bind_count;

        render::GraphicsPipelineId bound_pipeline{};
        std::uint32_t bound_push_params = (std::numeric_limits<std::uint32_t>::max)();
        std::uint32_t stage_draw_call_count = 0U;
        std::uint32_t stage_filtered_batch_count = 0U;

        std::uint32_t batch_index = 0U;
        for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
            if (filter_by_pass_bucket_ &&
                ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                ++stage_filtered_batch_count;
                ++batch_index;
                continue;
            }
            if (batch.instance_count == 0U) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const ecs::ParticleRenderMode render_mode = DecodeRenderMode(batch.pipeline_state);
            if (render_mode == ecs::ParticleRenderMode::mesh ||
                render_mode == ecs::ParticleRenderMode::trail) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const BlendModeKind blend_mode = DecodeBlendModeKind(batch.pipeline_state);
            const DepthPipelineMode depth_mode = ResolveDepthPipelineMode(batch.pipeline_state,
                                                                          active_depth_format != VK_FORMAT_UNDEFINED,
                                                                          active_camera_reverse_z);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                 *pipeline_host,
                                                                                 color_pass.target.format,
                                                                                 active_depth_format,
                                                                                 blend_mode,
                                                                                 depth_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const std::uint32_t push_params =
                (static_cast<std::uint32_t>(render_mode) & 0xFFU) |
                ((static_cast<std::uint32_t>(DecodeFacingMode(batch.pipeline_state)) & 0xFFU) << 8U);

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(record_context_.command_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                bound_pipeline = pipeline_id;
                bound_push_params = (std::numeric_limits<std::uint32_t>::max)();
            }

            if (bound_push_params != push_params) {
                push_constants.params = push_params;
                vkCmdPushConstants(record_context_.command_buffer,
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                bound_push_params = push_params;
            }

            if (gpu_build_active) {
                const VkDeviceSize indirect_offset =
                    static_cast<VkDeviceSize>(batch_index) * sizeof(ParticleGpuIndirectCommand);
                vkCmdDrawIndirect(record_context_.command_buffer,
                                  last_gpu_build_result.resources.indirect_commands.buffer,
                                  indirect_offset,
                                  1U,
                                  sizeof(ParticleGpuIndirectCommand));
                ++stats.indirect_draw_count;
            } else {
                vkCmdDraw(record_context_.command_buffer,
                          6U,
                          batch.instance_count,
                          0U,
                          batch.instance_begin);
            }
            ++stats.draw_call_count;
            ++stage_draw_call_count;
            ++batch_index;
        }

        if (filter_by_pass_bucket_) {
            stats.stage_filtered_batch_count += stage_filtered_batch_count;
            if (stage_draw_call_count == 0U) {
                ++stats.empty_stage_pass_count;
            }
            if (pass_bucket_ == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::opaque)) {
                stats.opaque_draw_call_count += stage_draw_call_count;
            } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent)) {
                stats.transparent_draw_call_count += stage_draw_call_count;
            }
        } else {
            for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
                if (batch.instance_count == 0U) {
                    continue;
                }
                const std::uint32_t pass_bucket =
                    ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key);
                if (pass_bucket == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::opaque)) {
                    ++stats.opaque_draw_call_count;
                } else if (pass_bucket == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent)) {
                    ++stats.transparent_draw_call_count;
                }
            }
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    render::RecordEndColorPass(record_context_, output_target_config);
    image_initialized[record_context_.image_index] = 1U;
    if (!using_external_depth_target &&
        create_info_cache.enable_depth &&
        record_context_.image_index < depth_image_initialized.size()) {
        depth_image_initialized[record_context_.image_index] = 1U;
    }
}

void ParticleRenderer3D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                             std::uint32_t pass_bucket_,
                                             bool filter_by_pass_bucket_,
                                             render_graph::ResourceHandle color_target_,
                                             render_graph::ResourceHandle depth_target_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage called before Initialize");
    }
    if (context == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage missing initialized BindlessResourceSystem");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("ParticleRenderer3D::RecordGraphSceneStage resolved zero-sized render extent");
    }

    const bool use_depth_attachment = render_graph::IsValidResourceHandle(depth_target_);
    const VkFormat active_depth_format = use_depth_attachment
        ? context_.ResolveTextureView(depth_target_).format
        : VK_FORMAT_UNDEFINED;

    EnsurePipelineObjects(*context,
                          *bindless_resources,
                          *pipeline_host,
                          resolved_color.format,
                          active_depth_format);

    if (gpu_build_active &&
        particle_simulation_host != nullptr &&
        !graph_compute_pass_owned) {
        particle_simulation_host->RecordBuild3D(*context,
                                                *pipeline_host,
                                                active_frame_index,
                                                ResolveCameraPosition(),
                                                ResolveCameraForward(),
                                                context_.CommandBuffer());
    }

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

    if (((gpu_build_active &&
          last_gpu_build_result.resources.draw_instances.buffer != VK_NULL_HANDLE) ||
         (last_upload_result.upload.buffer != VK_NULL_HANDLE)) &&
        !runtime_scratch.draw_batches.empty()) {
        const VkBuffer vertex_buffer = gpu_build_active
            ? last_gpu_build_result.resources.draw_instances.buffer
            : last_upload_result.upload.buffer;
        const VkDeviceSize vertex_offset = gpu_build_active
            ? 0U
            : last_upload_result.upload.offset;
        vkCmdBindVertexBuffers(context_.CommandBuffer(),
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
        const ecs::Float3 camera_right = ResolveCameraRight();
        const ecs::Float3 camera_up = ResolveCameraUp();
        const ecs::Float3 camera_forward = ResolveCameraForward();
        push_constants.camera_right = ecs::Float4{.x = camera_right.x, .y = camera_right.y, .z = camera_right.z, .w = 0.0F};
        push_constants.camera_up = ecs::Float4{.x = camera_up.x, .y = camera_up.y, .z = camera_up.z, .w = 0.0F};
        push_constants.camera_forward = ecs::Float4{.x = camera_forward.x, .y = camera_forward.y, .z = camera_forward.z, .w = 0.0F};
        push_constants.params = 0U;
        push_constants.reserved0 = 0U;
        push_constants.reserved1 = 0U;
        push_constants.reserved2 = 0U;

        context_.BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               pipeline_layout,
                                               0U,
                                               2U);
        ++stats.descriptor_set_bind_count;

        render::GraphicsPipelineId bound_pipeline{};
        std::uint32_t bound_push_params = (std::numeric_limits<std::uint32_t>::max)();
        std::uint32_t stage_draw_call_count = 0U;
        std::uint32_t stage_filtered_batch_count = 0U;

        std::uint32_t batch_index = 0U;
        for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
            if (filter_by_pass_bucket_ &&
                ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key) != pass_bucket_) {
                ++stage_filtered_batch_count;
                ++batch_index;
                continue;
            }
            if (batch.instance_count == 0U) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const ecs::ParticleRenderMode render_mode = DecodeRenderMode(batch.pipeline_state);
            if (render_mode == ecs::ParticleRenderMode::mesh ||
                render_mode == ecs::ParticleRenderMode::trail) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const BlendModeKind blend_mode = DecodeBlendModeKind(batch.pipeline_state);
            const DepthPipelineMode depth_mode = ResolveDepthPipelineMode(batch.pipeline_state,
                                                                          active_depth_format != VK_FORMAT_UNDEFINED,
                                                                          active_camera_reverse_z);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForMode(*context,
                                                                                 *pipeline_host,
                                                                                 resolved_color.format,
                                                                                 active_depth_format,
                                                                                 blend_mode,
                                                                                 depth_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            const std::uint32_t push_params =
                (static_cast<std::uint32_t>(render_mode) & 0xFFU) |
                ((static_cast<std::uint32_t>(DecodeFacingMode(batch.pipeline_state)) & 0xFFU) << 8U);

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(context_.CommandBuffer(),
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                bound_pipeline = pipeline_id;
                bound_push_params = (std::numeric_limits<std::uint32_t>::max)();
            }

            if (bound_push_params != push_params) {
                push_constants.params = push_params;
                vkCmdPushConstants(context_.CommandBuffer(),
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                bound_push_params = push_params;
            }

            if (gpu_build_active) {
                const VkDeviceSize indirect_offset =
                    static_cast<VkDeviceSize>(batch_index) * sizeof(ParticleGpuIndirectCommand);
                vkCmdDrawIndirect(context_.CommandBuffer(),
                                  last_gpu_build_result.resources.indirect_commands.buffer,
                                  indirect_offset,
                                  1U,
                                  sizeof(ParticleGpuIndirectCommand));
                ++stats.indirect_draw_count;
            } else {
                vkCmdDraw(context_.CommandBuffer(),
                          6U,
                          batch.instance_count,
                          0U,
                          batch.instance_begin);
            }
            ++stats.draw_call_count;
            ++stage_draw_call_count;
            ++batch_index;
        }

        if (filter_by_pass_bucket_) {
            stats.stage_filtered_batch_count += stage_filtered_batch_count;
            if (stage_draw_call_count == 0U) {
                ++stats.empty_stage_pass_count;
            }
            if (pass_bucket_ == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::opaque)) {
                stats.opaque_draw_call_count += stage_draw_call_count;
            } else if (pass_bucket_ == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent)) {
                stats.transparent_draw_call_count += stage_draw_call_count;
            }
        } else {
            for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
                if (batch.instance_count == 0U) {
                    continue;
                }
                const std::uint32_t bucket =
                    ecs::ParticleSystem<ecs::Dim3>::ExtractPassBucket(batch.sort_key);
                if (bucket == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::opaque)) {
                    ++stats.opaque_draw_call_count;
                } else if (bucket == static_cast<std::uint32_t>(ecs::ParticleRenderPassHint::transparent)) {
                    ++stats.transparent_draw_call_count;
                }
            }
        }
    }
}

void ParticleRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void ParticleRenderer3D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_,
                                              std::uint64_t last_submitted_value_,
                                              std::uint64_t completed_submit_value_) {
    swapchain_extent = extent_;
    swapchain_format = format_;
    last_submitted_value_seen = last_submitted_value_;
    completed_submit_value_seen = completed_submit_value_;
    image_initialized.clear();
    image_initialized.resize(image_count_);
    for (auto& initialized_flag : image_initialized) {
        initialized_flag = 0U;
    }
}

bool ParticleRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const ParticleRenderer3DStats& ParticleRenderer3D::Stats() const noexcept {
    return stats;
}

void ParticleRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                               render::BindlessResourceSystem& bindless_resources_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_,
                                               VkFormat depth_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer3D requires Vulkan 1.3 dynamicRendering + synchronization2");
    }

    const render::PipelineHostStats pipeline_stats = pipeline_host_.Stats();
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
        for (auto& pipeline_id : per_blend) {
            if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
                pipeline_id = {};
            }
        }
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_3d_vert_spv;
        shader_create_info.word_count = std::size(generated::k_particle_3d_vert_spv);
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_3d_frag_spv;
        shader_create_info.word_count = std::size(generated::k_particle_3d_frag_spv);
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        const VkDescriptorSetLayout sampled_image_layout = bindless_resources_.SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout = bindless_resources_.SamplerLayout();
        if (sampled_image_layout == VK_NULL_HANDLE || sampler_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "ParticleRenderer3D::EnsurePipelineObjects requires valid bindless set layouts");
        }
        render::PipelineLayoutDesc pipeline_layout_desc{};
        pipeline_layout_desc.set_layouts.push_back(sampled_image_layout);
        pipeline_layout_desc.set_layouts.push_back(sampler_layout);
        pipeline_layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, pipeline_layout_desc);
    }

    if (pipeline_color_format != color_format_ || pipeline_depth_format != depth_format_) {
        for (auto& per_blend : pipeline_ids) {
            for (auto& pipeline_id : per_blend) {
                pipeline_id = {};
            }
        }
        pipeline_color_format = color_format_;
        pipeline_depth_format = depth_format_;
    }
}

render::GraphicsPipelineId ParticleRenderer3D::EnsurePipelineForMode(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat color_format_,
    VkFormat depth_format_,
    BlendModeKind blend_mode_,
    DepthPipelineMode depth_mode_) {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer3D::EnsurePipelineForMode requires initialized BindlessResourceSystem");
    }
    EnsurePipelineObjects(context_, *bindless_resources, pipeline_host_, color_format_, depth_format_);

    render::GraphicsPipelineId& cached_pipeline_id =
        pipeline_ids[BlendModeIndex(blend_mode_)][DepthPipelineModeIndex(depth_mode_)];
    if (cached_pipeline_id.IsValid() &&
        pipeline_color_format == color_format_ &&
        pipeline_depth_format == depth_format_) {
        return cached_pipeline_id;
    }

    const bool use_depth = depth_mode_ != DepthPipelineMode::no_depth;
    const bool depth_write = depth_mode_ == DepthPipelineMode::depth_test_write ||
                             depth_mode_ == DepthPipelineMode::depth_test_write_reverse_z;
    const bool reverse_z = depth_mode_ == DepthPipelineMode::depth_test_reverse_z ||
                           depth_mode_ == DepthPipelineMode::depth_test_write_reverse_z;

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
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Particle3DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 0U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, position_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 1U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, size_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 2U,
        .binding = 0U,
        .format = VK_FORMAT_R32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, rotation_radians))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, stretch_factor))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, color_rgba8))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 5U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, velocity_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 6U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, texture_slot))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 7U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle3DGpuInstance, sampler_slot))
    });

    pipeline_desc.input_assembly.topology = k_particle_topology;
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

    pipeline_desc.depth_stencil.depth_test_enable = use_depth;
    pipeline_desc.depth_stencil.depth_write_enable = depth_write;
    pipeline_desc.depth_stencil.depth_compare_op = reverse_z
        ? VK_COMPARE_OP_GREATER_OR_EQUAL
        : VK_COMPARE_OP_LESS_OR_EQUAL;
    pipeline_desc.depth_stencil.depth_bounds_test_enable = false;
    pipeline_desc.depth_stencil.stencil_test_enable = false;
    pipeline_desc.depth_stencil.min_depth_bounds = 0.0F;
    pipeline_desc.depth_stencil.max_depth_bounds = 1.0F;

    render::ColorBlendPreset blend_preset = render::ColorBlendPreset::alpha;
    switch (blend_mode_) {
    case BlendModeKind::additive:
        blend_preset = render::ColorBlendPreset::additive;
        break;
    case BlendModeKind::multiply:
        blend_preset = render::ColorBlendPreset::multiply;
        break;
    case BlendModeKind::premultiplied_alpha:
        blend_preset = render::ColorBlendPreset::premultiplied_alpha;
        break;
    case BlendModeKind::screen:
        blend_preset = render::ColorBlendPreset::screen;
        break;
    case BlendModeKind::alpha:
    default:
        blend_preset = render::ColorBlendPreset::alpha;
        break;
    }
    pipeline_desc.color_blend.attachments.push_back(render::BuildColorBlendAttachment(blend_preset));

    cached_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
    pipeline_depth_format = depth_format_;
    return cached_pipeline_id;
}

void ParticleRenderer3D::RemapCpuInstancesToBindless() {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer3D::RemapCpuInstancesToBindless requires initialized bindless resources");
    }
    for (auto& instance : runtime_scratch.instances) {
        const std::uint32_t raw_texture_id = instance.texture_slot;
        instance.texture_slot = ResolveTextureSlot(raw_texture_id);
        instance.sampler_slot = ResolveSamplerSlot(raw_texture_id);
    }
}

std::uint32_t ParticleRenderer3D::ResolveTextureSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->PlaceholderImageSlot().index;
    }
    return bindless_resources->ResolveTextureImageSlot(*texture_host,
                                                       asset::TextureId{texture_id_}).index;
}

std::uint32_t ParticleRenderer3D::ResolveSamplerSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->DefaultSamplerSlot().index;
    }
    return bindless_resources->ResolveTextureSamplerSlot(*texture_host,
                                                         asset::TextureId{texture_id_}).index;
}

void ParticleRenderer3D::EnsureDepthResources(VulkanContext& context_,
                                              std::uint32_t image_count_,
                                              VkExtent2D extent_) {
    if (!create_info_cache.enable_depth) {
        return;
    }
    if (image_count_ == 0U || extent_.width == 0U || extent_.height == 0U) {
        return;
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("ParticleRenderer3D::EnsureDepthResources missing GpuMemoryHost");
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.preferred_depth_format);
    }

    const bool compatible =
        depth_images.size() == image_count_ &&
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
    for (auto& initialized_flag : depth_image_initialized) {
        initialized_flag = 0U;
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

void ParticleRenderer3D::RetireDepthResources(std::uint64_t retire_value_) {
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

void ParticleRenderer3D::CollectRetiredDepthResources(VulkanContext& context_,
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

void ParticleRenderer3D::DestroyDepthResources(VulkanContext& context_) {
    for (auto& depth_image : depth_images) {
        resource::ImageHost::DestroyImage(context_, depth_image);
    }
    depth_images.clear();
    depth_image_initialized.clear();
}

void ParticleRenderer3D::DestroyRetiredDepthResources(VulkanContext& context_) {
    for (auto& retired : retired_depth_images) {
        resource::ImageHost::DestroyImage(context_, retired.resource);
    }
    retired_depth_images.clear();
}

} // namespace vr::particle

