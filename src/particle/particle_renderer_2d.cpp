#include "vr/particle/particle_renderer_2d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_2d_frag_spv.hpp"
#include "vr/particle/generated/particle_2d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/renderer_prepare_views_2d.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
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

[[nodiscard]] ParticleSimulationPrepareDesc BuildSimulationPrepareDesc2D(
    const ecs::Particle<ecs::Dim2>* particle_components_,
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
        if (component.style.sort_mode == ecs::ParticleSortMode::gpu_radix) {
            prepare_desc.require_sort_buffers = true;
            prepare_desc.sort_key_capacity = SaturatingAdd(prepare_desc.sort_key_capacity,
                                                           component.style.max_particles);
        }
    }

    if (prepare_desc.requested_mode != ecs::ParticleSimulationMode::cpu) {
        prepare_desc.require_draw_instance_buffer = true;
        prepare_desc.draw_instance_stride_bytes = sizeof(ecs::Particle2DGpuInstance);
    }
    return prepare_desc;
}

} // namespace

std::size_t ParticleRenderer2D::BlendModeIndex(BlendModeKind blend_mode_) noexcept {
    return static_cast<std::size_t>(blend_mode_);
}

ParticleRenderer2D::BlendModeKind ParticleRenderer2D::DecodeBlendModeKind(
    std::uint32_t pipeline_state_) noexcept {
    switch (static_cast<ecs::RuntimeBlendPreset>(pipeline_state_ & 0xFFU)) {
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

std::uint64_t ParticleRenderer2D::ComposeBindlessUploadRevision(
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

void ParticleRenderer2D::Initialize(const ParticleRenderer2DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_particle_count > 0U) {
        ecs::ParticleRuntimeSystem<ecs::Dim2>::Reserve(runtime_scratch,
                                                       create_info_cache.reserve_component_count,
                                                       create_info_cache.reserve_particle_count);
    }

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    image_initialized.clear();
    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    stats = {};

    particle_components = nullptr;
    particle_emitters = nullptr;
    transforms = nullptr;
    component_count = 0U;
    particle_upload_host = nullptr;
    particle_simulation_host = nullptr;
    texture_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    sampler_host = nullptr;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    gpu_build_active = false;
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;
    initialized = true;
}

void ParticleRenderer2D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    particle_components = nullptr;
    particle_emitters = nullptr;
    transforms = nullptr;
    component_count = 0U;
    particle_upload_host = nullptr;
    particle_simulation_host = nullptr;
    texture_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    sampler_host = nullptr;

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    image_initialized.clear();

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.build_component_indices.clear();
    runtime_scratch.cache = {};

    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    stats = {};
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    gpu_build_active = false;
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;
    initialized = false;
}

void ParticleRenderer2D::SetHost(ParticleUploadHost* upload_host_) noexcept {
    particle_upload_host = upload_host_;
}

void ParticleRenderer2D::SetSimulationHost(ParticleSimulationHost* simulation_host_) noexcept {
    particle_simulation_host = simulation_host_;
}

void ParticleRenderer2D::SetTextureHost(asset::TextureHost* texture_host_) noexcept {
    texture_host = texture_host_;
}

void ParticleRenderer2D::SetHosts(ParticleUploadHost* upload_host_,
                                  asset::TextureHost* texture_host_) noexcept {
    particle_upload_host = upload_host_;
    texture_host = texture_host_;
}

void ParticleRenderer2D::SetSceneData(ecs::Particle<ecs::Dim2>* particle_components_,
                                      ecs::ParticleEmitter<ecs::Dim2>* particle_emitters_,
                                      ecs::Transform<ecs::Dim2>* transforms_,
                                      std::uint32_t component_count_) noexcept {
    particle_components = particle_components_;
    particle_emitters = particle_emitters_;
    transforms = transforms_;
    component_count = component_count_;
}

void ParticleRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void ParticleRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
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

bool ParticleRenderer2D::IsInitialized() const noexcept {
    return initialized;
}

const ParticleRenderer2DStats& ParticleRenderer2D::Stats() const noexcept {
    return stats;
}

} // namespace vr::particle

