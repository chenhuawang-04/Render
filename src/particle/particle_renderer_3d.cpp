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
#include "vr/render/renderer_prepare_views_3d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
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

} // namespace vr::particle

