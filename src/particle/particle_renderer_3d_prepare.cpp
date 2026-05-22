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
            prepare_view_.render_graph_compute_active &&
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

} // namespace vr::particle
