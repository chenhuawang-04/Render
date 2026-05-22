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

void ParticleRenderer2D::PrepareFrame(const render::ParticleRenderer2DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer2D::PrepareFrame called before Initialize");
    }
    if (prepare_view_.device.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        prepare_view_.device.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer2D requires Vulkan 1.3 dynamicRendering + synchronization2");
    }
    if (particle_upload_host == nullptr && prepare_view_.particle_upload != nullptr) {
        particle_upload_host = prepare_view_.particle_upload;
    }
    if (particle_simulation_host == nullptr && prepare_view_.particle_simulation != nullptr) {
        particle_simulation_host = prepare_view_.particle_simulation;
    }
    if (particle_upload_host == nullptr || !particle_upload_host->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer2D::PrepareFrame requires initialized ParticleUploadHost");
    }
    if (particle_simulation_host != nullptr && !particle_simulation_host->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer2D::PrepareFrame received non-initialized ParticleSimulationHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    sampler_host = &prepare_view_.sampler;
    if (prepare_view_.texture != nullptr) {
        texture_host = prepare_view_.texture;
    }
    if (prepare_view_.bindless != nullptr) {
        bindless_resources = prepare_view_.bindless;
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer2D::PrepareFrame requires initialized BindlessResourceSystem");
    }

    active_frame_index = prepare_view_.frame.frame_index;
    swapchain_extent = prepare_view_.frame.swapchain_extent;
    swapchain_format = prepare_view_.frame.swapchain_format;
    last_submitted_value_seen = prepare_view_.progress.last_submitted_value;
    completed_submit_value_seen = prepare_view_.progress.completed_submit_value;

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
        return;
    }

    if (particle_simulation_host != nullptr) {
        particle_simulation_host->BeginFrame(*context,
                                             active_frame_index,
                                             prepare_view_.progress.last_submitted_value,
                                             prepare_view_.progress.completed_submit_value);
        const ParticleSimulationPrepareDesc prepare_desc =
            BuildSimulationPrepareDesc2D(particle_components,
                                         component_count);
        last_simulation_resources = particle_simulation_host->PrepareFrameResources(*context,
                                                                                    active_frame_index,
                                                                                    prepare_desc);
        if (last_simulation_resources.resolved_path != ParticleSimulationResolvedPath::cpu) {
            ecs::ParticleRuntimeBuildConfig build_config = create_info_cache.runtime_upload_options.runtime_build;
            build_config.build_instances = false;
            bool cpu_seeded_this_frame =
                last_simulation_resources.resolved_path != ParticleSimulationResolvedPath::gpu;
            if (last_simulation_resources.resolved_path == ParticleSimulationResolvedPath::gpu) {
                build_config.simulate = false;
                build_config.emit_new_particles = false;
            }
            last_runtime_build_stats = ecs::ParticleRuntimeSystem<ecs::Dim2>::Build(
                particle_components,
                particle_emitters,
                transforms,
                component_count,
                runtime_scratch,
                build_config,
                {});
            if (last_simulation_resources.resolved_path == ParticleSimulationResolvedPath::gpu &&
                !particle_simulation_host->HasPersistentState2D() &&
                particle_simulation_host->NeedsCpuSeed2D(active_frame_index)) {
                build_config = create_info_cache.runtime_upload_options.runtime_build;
                build_config.build_instances = false;
                cpu_seeded_this_frame = true;
                last_runtime_build_stats = ecs::ParticleRuntimeSystem<ecs::Dim2>::Build(
                    particle_components,
                    particle_emitters,
                    transforms,
                    component_count,
                    runtime_scratch,
                    build_config,
                    {});
            }
            last_gpu_build_result = particle_simulation_host->PrepareGpuBuild2D(
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
                gpu_build_active && prepare_view_.render_graph_compute_active;
        }
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
        return;
    }

    particle_upload_host->BeginFrame(*context,
                                     active_frame_index,
                                     prepare_view_.progress.last_submitted_value,
                                     prepare_view_.progress.completed_submit_value);

    last_upload_result.runtime = ecs::ParticleRuntimeSystem<ecs::Dim2>::Build(
        particle_components,
        particle_emitters,
        transforms,
        component_count,
        runtime_scratch,
        create_info_cache.runtime_upload_options.runtime_build,
        {});

    if (!runtime_scratch.instances.empty() &&
        last_upload_result.runtime.emitted_instance_count > 0U) {
        RemapCpuInstancesToBindless();
        last_upload_result.upload = particle_upload_host->Upload2DInstances(
            *context,
            *upload_host,
            active_frame_index,
            runtime_scratch.instances.data(),
            static_cast<std::uint32_t>(runtime_scratch.instances.size()),
            ComposeBindlessUploadRevision(
                last_upload_result.runtime,
                texture_host != nullptr ? texture_host->Stats().revision : 0U));
    }
    last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;

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

} // namespace vr::particle
