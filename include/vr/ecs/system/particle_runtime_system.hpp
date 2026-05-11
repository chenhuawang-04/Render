#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/particle_emitter_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/particle_system.hpp"
#include "vr/ecs/system/spatial_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<typename T>
using ParticleRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct Particle2DGpuInstance final {
    float position_x;
    float position_y;
    float velocity_x;
    float velocity_y;
    float size_x;
    float size_y;
    float rotation_radians;
    float normalized_age;
    std::uint32_t color_rgba8;
    std::uint32_t texture_slot;
    std::uint32_t sampler_slot;
    std::uint32_t component_index;
    std::uint32_t user_data;
};

struct Particle3DGpuInstance final {
    float position_x;
    float position_y;
    float position_z;
    float size_x;
    float size_y;
    float rotation_radians;
    float normalized_age;
    float stretch_factor;
    std::uint32_t color_rgba8;
    std::uint32_t texture_slot;
    std::uint32_t sampler_slot;
    std::uint32_t component_index;
    std::uint32_t user_data;
    float velocity_x;
    float velocity_y;
    float velocity_z;
    float soft_particle_distance;
};

struct ParticleDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t instance_begin;
    std::uint32_t instance_count;
    std::uint32_t material_id;
    std::uint32_t texture_id;
    std::uint32_t first_component_index;
    std::uint32_t batch_tag;
    std::uint32_t pipeline_state;
};

struct ParticleRuntimeBuildConfig final {
    float delta_time_s = 1.0F / 60.0F;
    float fixed_step_s = 1.0F / 60.0F;
    std::uint32_t max_simulation_steps = 4U;
    std::uint32_t max_total_instances = 0U; // 0 = unlimited
    bool simulate = true;
    bool emit_new_particles = true;
    bool build_ordered_batches = true;
    bool build_instances = true;
};

template<DimensionTag DimensionT>
struct ParticleRuntimeBuildHint final {
    const std::uint32_t* visible_component_indices = nullptr;
    std::uint32_t visible_component_count = 0U;
    std::uint8_t use_visible_component_indices = 0U;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
};

struct ParticleRuntimeBuildStats final {
    std::uint32_t emitter_count = 0U;
    std::uint32_t candidate_emitter_count = 0U;
    std::uint32_t simulated_emitter_count = 0U;
    std::uint32_t active_particle_count = 0U;
    std::uint32_t visible_particle_count = 0U;
    std::uint32_t emitted_instance_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t spawned_particle_count = 0U;
    std::uint32_t killed_particle_count = 0U;
    std::uint32_t simulated_particle_count = 0U;
    std::uint32_t truncated_particle_count = 0U;
    std::uint32_t cache_epoch = 0U;
    std::uint32_t max_alive_peak = 0U;
    std::uint64_t component_signature = 0U;
    std::uint64_t transform_signature = 0U;
    std::uint64_t visible_signature = 0U;
    std::uint64_t runtime_state_signature = 0U;
    bool used_visible_component_indices = false;
    bool storage_reused = false;
    bool storage_reset = false;
};

struct ParticleStateSoA final {
    ParticleRuntimeMcVector<float> position_x{};
    ParticleRuntimeMcVector<float> position_y{};
    ParticleRuntimeMcVector<float> position_z{};
    ParticleRuntimeMcVector<float> velocity_x{};
    ParticleRuntimeMcVector<float> velocity_y{};
    ParticleRuntimeMcVector<float> velocity_z{};
    ParticleRuntimeMcVector<float> age_s{};
    ParticleRuntimeMcVector<float> lifetime_s{};
    ParticleRuntimeMcVector<float> start_size{};
    ParticleRuntimeMcVector<float> end_size{};
    ParticleRuntimeMcVector<float> rotation_radians{};
    ParticleRuntimeMcVector<float> angular_velocity_radians{};
    ParticleRuntimeMcVector<std::uint32_t> start_color_rgba8{};
    ParticleRuntimeMcVector<std::uint32_t> end_color_rgba8{};

    void Reserve(std::uint32_t capacity_) {
        position_x.reserve(capacity_);
        position_y.reserve(capacity_);
        position_z.reserve(capacity_);
        velocity_x.reserve(capacity_);
        velocity_y.reserve(capacity_);
        velocity_z.reserve(capacity_);
        age_s.reserve(capacity_);
        lifetime_s.reserve(capacity_);
        start_size.reserve(capacity_);
        end_size.reserve(capacity_);
        rotation_radians.reserve(capacity_);
        angular_velocity_radians.reserve(capacity_);
        start_color_rgba8.reserve(capacity_);
        end_color_rgba8.reserve(capacity_);
    }

    void Resize(std::uint32_t capacity_) {
        position_x.resize(capacity_);
        position_y.resize(capacity_);
        position_z.resize(capacity_);
        velocity_x.resize(capacity_);
        velocity_y.resize(capacity_);
        velocity_z.resize(capacity_);
        age_s.resize(capacity_);
        lifetime_s.resize(capacity_);
        start_size.resize(capacity_);
        end_size.resize(capacity_);
        rotation_radians.resize(capacity_);
        angular_velocity_radians.resize(capacity_);
        start_color_rgba8.resize(capacity_);
        end_color_rgba8.resize(capacity_);
    }
};

struct ParticleEmitterState final {
    ParticleStateSoA particles{};
    std::uint64_t simulation_signature = 0U;
    std::uint32_t capacity = 0U;
    std::uint32_t active_count = 0U;
    std::uint32_t random_state = 1U;
    std::uint32_t total_spawned = 0U;
    std::uint32_t total_killed = 0U;
    std::uint32_t spawned_this_build = 0U;
    std::uint32_t killed_this_build = 0U;
    float spawn_accumulator_s = 0.0F;
    float fixed_step_accumulator_s = 0.0F;
    float burst_accumulator_s = 0.0F;
    float simulation_time_s = 0.0F;
    std::uint8_t prewarm_done = 0U;
    std::uint8_t burst_fired_once = 0U;
    std::uint16_t reserved0 = 0U;
};

template<DimensionTag DimensionT>
struct ParticleRuntimeCache final {
    const Particle<DimensionT>* components = nullptr;
    const ParticleEmitter<DimensionT>* emitters = nullptr;
    const Transform<DimensionT>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    std::uint32_t epoch = 0U;
    bool valid = false;
};

template<DimensionTag DimensionT>
struct ParticleRuntimeScratch final {
    using InstanceType = std::conditional_t<std::same_as<DimensionT, Dim2>,
                                            Particle2DGpuInstance,
                                            Particle3DGpuInstance>;

    ParticleRuntimeMcVector<ParticleEmitterState> emitter_states{};
    ParticleRuntimeMcVector<InstanceType> instances{};
    ParticleRuntimeMcVector<ParticleDrawBatch> draw_batches{};
    ParticleRuntimeMcVector<std::uint32_t> build_component_indices{};
    ParticleRuntimeCache<DimensionT> cache{};
};

using Particle2DRuntimeScratch = ParticleRuntimeScratch<Dim2>;
using Particle3DRuntimeScratch = ParticleRuntimeScratch<Dim3>;
using Particle2DRuntimeBuildHint = ParticleRuntimeBuildHint<Dim2>;
using Particle3DRuntimeBuildHint = ParticleRuntimeBuildHint<Dim3>;

template<DimensionTag DimensionT>
class ParticleRuntimeSystem final {
public:
    using ParticleType = Particle<DimensionT>;
    using ParticleEmitterType = ParticleEmitter<DimensionT>;
    using TransformType = Transform<DimensionT>;
    using ScratchType = ParticleRuntimeScratch<DimensionT>;
    using HintType = ParticleRuntimeBuildHint<DimensionT>;
    using InstanceType = typename ScratchType::InstanceType;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t emitter_capacity_,
                        std::uint32_t particle_capacity_) {
        scratch_.emitter_states.reserve(emitter_capacity_);
        scratch_.build_component_indices.reserve(emitter_capacity_);
        scratch_.instances.reserve(particle_capacity_);
        scratch_.draw_batches.reserve(emitter_capacity_);
    }

    [[nodiscard]] static ParticleRuntimeBuildStats Build(
        const ParticleType* components_,
        const ParticleEmitterType* emitters_,
        const TransformType* transforms_,
        std::uint32_t component_count_,
        ScratchType& scratch_,
        const ParticleRuntimeBuildConfig& build_config_ = {},
        const HintType& build_hint_ = {}) {
        ParticleRuntimeBuildStats stats{};
        stats.emitter_count = component_count_;
        stats.used_visible_component_indices = build_hint_.use_visible_component_indices != 0U;

        if (components_ == nullptr || emitters_ == nullptr || transforms_ == nullptr || component_count_ == 0U) {
            scratch_.instances.clear();
            scratch_.draw_batches.clear();
            scratch_.build_component_indices.clear();
            return stats;
        }

        const bool topology_changed =
            !scratch_.cache.valid ||
            scratch_.cache.components != components_ ||
            scratch_.cache.emitters != emitters_ ||
            scratch_.cache.component_count != component_count_;
        if (topology_changed) {
            ResetScratchForTopology(scratch_, components_, emitters_, transforms_, component_count_);
            stats.storage_reset = true;
        } else {
            scratch_.cache.emitters = emitters_;
            scratch_.cache.transforms = transforms_;
            stats.storage_reused = true;
        }

        stats.cache_epoch = scratch_.cache.epoch;
        stats.component_signature = ComputeComponentSignature(components_, component_count_);
        stats.transform_signature = ComputeTransformSignature(transforms_, component_count_);
        stats.visible_signature = ComputeVisibleSignature(components_, component_count_, build_hint_);

        EnsureEmitterStateCount(scratch_, component_count_);
        for (std::uint32_t component_index = 0U;
             component_index < component_count_;
             ++component_index) {
            ParticleEmitterState& emitter_state = scratch_.emitter_states[component_index];
            const ParticleType& component = components_[component_index];
            const ParticleEmitterType& emitter = emitters_[component_index];
            const std::uint64_t simulation_signature = ComputeSimulationSignature(component, emitter);
            const std::uint32_t requested_capacity =
                std::max<std::uint32_t>(1U, component.style.max_particles);
            if (emitter_state.capacity != requested_capacity ||
                emitter_state.simulation_signature != simulation_signature) {
                ReinitializeEmitterState(component,
                                         emitter,
                                         requested_capacity,
                                         simulation_signature,
                                         emitter_state);
                stats.storage_reset = true;
            }
        }

        if (build_config_.simulate) {
            for (std::uint32_t component_index = 0U;
                 component_index < component_count_;
                 ++component_index) {
                ParticleEmitterState& emitter_state = scratch_.emitter_states[component_index];
                const ParticleType& component = components_[component_index];
                const ParticleEmitterType& emitter = emitters_[component_index];
                const TransformType& transform = transforms_[component_index];
                SimulateEmitter(component, emitter, transform, build_config_, emitter_state, stats);
                stats.active_particle_count += emitter_state.active_count;
                stats.max_alive_peak = std::max(stats.max_alive_peak, emitter_state.active_count);
                if (emitter_state.active_count > 0U) {
                    ++stats.simulated_emitter_count;
                }
            }
        } else {
            for (std::uint32_t component_index = 0U;
                 component_index < component_count_;
                 ++component_index) {
                const ParticleEmitterState& emitter_state = scratch_.emitter_states[component_index];
                stats.active_particle_count += emitter_state.active_count;
                stats.max_alive_peak = std::max(stats.max_alive_peak, emitter_state.active_count);
            }
        }

        stats.runtime_state_signature = ComputeEmitterStateSignature(
            scratch_.emitter_states.data(),
            component_count_);

        BuildComponentIndexList(components_, component_count_, build_hint_, scratch_);

        scratch_.instances.clear();
        scratch_.draw_batches.clear();
        if (build_config_.build_ordered_batches && scratch_.build_component_indices.size() > 1U) {
            std::sort(scratch_.build_component_indices.begin(),
                      scratch_.build_component_indices.end(),
                      [&](std::uint32_t lhs_, std::uint32_t rhs_) {
                          return ParticleSystem<DimensionT>::SortKey(components_[lhs_]) <
                                 ParticleSystem<DimensionT>::SortKey(components_[rhs_]);
                      });
        }

        stats.candidate_emitter_count =
            static_cast<std::uint32_t>(scratch_.build_component_indices.size());

        const std::uint32_t max_total_instances = build_config_.max_total_instances;
        for (std::uint32_t component_index : scratch_.build_component_indices) {
            const ParticleType& component = components_[component_index];
            const ParticleEmitterType& emitter = emitters_[component_index];
            const TransformType& transform = transforms_[component_index];
            const ParticleEmitterState& emitter_state = scratch_.emitter_states[component_index];
            const bool preserve_empty_gpu_batch =
                !build_config_.build_instances &&
                component.style.simulation_mode == ParticleSimulationMode::gpu;
            if (component.runtime.route.visible == 0U ||
                (emitter_state.active_count == 0U && !preserve_empty_gpu_batch)) {
                continue;
            }

            const std::uint32_t instance_begin =
                build_config_.build_instances
                    ? static_cast<std::uint32_t>(scratch_.instances.size())
                    : stats.visible_particle_count;
            std::uint32_t emitted_from_component = 0U;
            for (std::uint32_t particle_index = 0U;
                 particle_index < emitter_state.active_count;
                 ++particle_index) {
                const std::uint32_t current_total_instances =
                    build_config_.build_instances
                        ? static_cast<std::uint32_t>(scratch_.instances.size())
                        : stats.visible_particle_count;
                if (max_total_instances > 0U &&
                    current_total_instances >= max_total_instances) {
                    ++stats.truncated_particle_count;
                    continue;
                }
                if (build_config_.build_instances) {
                    scratch_.instances.push_back(
                        BuildInstance(component,
                                      emitter,
                                      transform,
                                      emitter_state,
                                      component_index,
                                      particle_index));
                }
                ++emitted_from_component;
            }
            if (emitted_from_component == 0U && !preserve_empty_gpu_batch) {
                continue;
            }

            stats.visible_particle_count += emitted_from_component;
            const std::uint64_t sort_key = ParticleSystem<DimensionT>::SortKey(component);
            const std::uint32_t pipeline_state = EncodePipelineState(component);
            if (!scratch_.draw_batches.empty() &&
                scratch_.draw_batches.back().sort_key == sort_key &&
                scratch_.draw_batches.back().material_id == component.runtime.route.material_id &&
                scratch_.draw_batches.back().texture_id == component.runtime.route.texture_id &&
                scratch_.draw_batches.back().batch_tag == component.runtime.route.batch_tag &&
                scratch_.draw_batches.back().pipeline_state == pipeline_state &&
                scratch_.draw_batches.back().instance_begin +
                        scratch_.draw_batches.back().instance_count ==
                    instance_begin) {
                scratch_.draw_batches.back().instance_count += emitted_from_component;
            } else {
                ParticleDrawBatch batch{};
                batch.sort_key = sort_key;
                batch.instance_begin = instance_begin;
                batch.instance_count = emitted_from_component;
                batch.material_id = component.runtime.route.material_id;
                batch.texture_id = component.runtime.route.texture_id;
                batch.first_component_index = component_index;
                batch.batch_tag = component.runtime.route.batch_tag;
                batch.pipeline_state = pipeline_state;
                scratch_.draw_batches.push_back(batch);
            }
        }

        stats.emitted_instance_count = static_cast<std::uint32_t>(scratch_.instances.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        return stats;
    }

private:
    static void ResetScratchForTopology(ScratchType& scratch_,
                                        const ParticleType* components_,
                                        const ParticleEmitterType* emitters_,
                                        const TransformType* transforms_,
                                        std::uint32_t component_count_) {
        scratch_.emitter_states.clear();
        scratch_.emitter_states.resize(component_count_);
        scratch_.instances.clear();
        scratch_.draw_batches.clear();
        scratch_.build_component_indices.clear();
        scratch_.cache.components = components_;
        scratch_.cache.emitters = emitters_;
        scratch_.cache.transforms = transforms_;
        scratch_.cache.component_count = component_count_;
        scratch_.cache.epoch = scratch_.cache.epoch == (std::numeric_limits<std::uint32_t>::max)()
            ? 1U
            : (scratch_.cache.epoch + 1U);
        scratch_.cache.valid = true;
    }

    [[nodiscard]] static std::uint32_t EncodePipelineState(const ParticleType& component_) noexcept {
        const RuntimeBlendPreset blend_preset = ResolveRuntimeBlendPreset(
            component_.style.blend_mode,
            component_.style.premultiplied_alpha != 0U);

        std::uint32_t state =
            (static_cast<std::uint32_t>(blend_preset) & particle_pipeline_state_blend_mask)
            << particle_pipeline_state_blend_shift;
        state |= (static_cast<std::uint32_t>(component_.style.facing_mode) &
                  particle_pipeline_state_facing_mode_mask)
                 << particle_pipeline_state_facing_mode_shift;
        state |= (static_cast<std::uint32_t>(component_.style.render_mode) &
                  particle_pipeline_state_render_mode_mask)
                 << particle_pipeline_state_render_mode_shift;
        state |= (static_cast<std::uint32_t>(component_.style.lighting_mode) &
                  particle_pipeline_state_lighting_mode_mask)
                 << particle_pipeline_state_lighting_mode_shift;
        if constexpr (std::same_as<DimensionT, Dim3>) {
            state |= static_cast<std::uint32_t>(component_.style.depth_test != 0U)
                     << particle_pipeline_state_depth_test_shift;
            state |= static_cast<std::uint32_t>(component_.style.depth_write != 0U)
                     << particle_pipeline_state_depth_write_shift;
            state |= static_cast<std::uint32_t>(component_.style.double_sided != 0U)
                     << particle_pipeline_state_double_sided_shift;
        }
        return state;
    }

    static void EnsureEmitterStateCount(ScratchType& scratch_,
                                        std::uint32_t component_count_) {
        if (scratch_.emitter_states.size() != component_count_) {
            scratch_.emitter_states.resize(component_count_);
        }
    }

    [[nodiscard]] static std::uint64_t ComputeComponentSignature(
        const ParticleType* components_,
        std::uint32_t component_count_) noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            hash ^= static_cast<std::uint64_t>(components_[i].runtime.revision_authoring + 0x9e3779b9U);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(components_[i].runtime.route.dirty_flags);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeTransformSignature(
        const TransformType* transforms_,
        std::uint32_t component_count_) noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            hash ^= static_cast<std::uint64_t>(transforms_[i].runtime.world_revision + 0x85ebca6bU);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeVisibleSignature(
        const ParticleType* components_,
        std::uint32_t component_count_,
        const HintType& build_hint_) noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        if (build_hint_.use_visible_component_indices != 0U &&
            build_hint_.visible_component_indices != nullptr) {
            for (std::uint32_t i = 0U; i < build_hint_.visible_component_count; ++i) {
                hash ^= static_cast<std::uint64_t>(build_hint_.visible_component_indices[i] + 0x27d4eb2dU);
                hash *= 1099511628211ULL;
            }
            return hash;
        }
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            hash ^= static_cast<std::uint64_t>(components_[i].runtime.route.visible + i * 1315423911U);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    [[nodiscard]] static std::uint64_t ComputeEmitterStateSignature(
        const ParticleEmitterState* emitter_states_,
        std::uint32_t component_count_) noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            const ParticleEmitterState& state = emitter_states_[i];
            hash ^= static_cast<std::uint64_t>(state.active_count + 0x9e3779b9U);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(state.total_spawned);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(state.total_killed);
            hash *= 1099511628211ULL;
            union final {
                float f;
                std::uint32_t u;
            } bits{state.simulation_time_s};
            hash ^= static_cast<std::uint64_t>(bits.u);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    static void BuildComponentIndexList(const ParticleType* components_,
                                        std::uint32_t component_count_,
                                        const HintType& build_hint_,
                                        ScratchType& scratch_) {
        scratch_.build_component_indices.clear();
        if (build_hint_.use_visible_component_indices != 0U &&
            build_hint_.visible_component_indices != nullptr) {
            scratch_.build_component_indices.reserve(build_hint_.visible_component_count);
            for (std::uint32_t i = 0U; i < build_hint_.visible_component_count; ++i) {
                const std::uint32_t component_index = build_hint_.visible_component_indices[i];
                if (component_index >= component_count_) {
                    continue;
                }
                scratch_.build_component_indices.push_back(component_index);
            }
            return;
        }

        scratch_.build_component_indices.reserve(component_count_);
        for (std::uint32_t component_index = 0U;
             component_index < component_count_;
             ++component_index) {
            if (components_[component_index].runtime.route.visible == 0U) {
                continue;
            }
            scratch_.build_component_indices.push_back(component_index);
        }
    }

    static void SimulateEmitter(const ParticleType& component_,
                                const ParticleEmitterType& emitter_,
                                const TransformType& transform_,
                                const ParticleRuntimeBuildConfig& build_config_,
                                ParticleEmitterState& emitter_state_,
                                ParticleRuntimeBuildStats& stats_) {
        const std::uint64_t simulation_signature = ComputeSimulationSignature(component_, emitter_);
        const std::uint32_t requested_capacity =
            std::max<std::uint32_t>(1U, component_.style.max_particles);
        if (emitter_state_.capacity != requested_capacity ||
            emitter_state_.simulation_signature != simulation_signature) {
            ReinitializeEmitterState(component_, emitter_, requested_capacity, simulation_signature, emitter_state_);
            stats_.storage_reset = true;
        }

        if (emitter_.config.prewarm != 0U && emitter_state_.prewarm_done == 0U) {
            PrewarmEmitter(component_, emitter_, transform_, emitter_state_, stats_);
        }

        emitter_state_.spawned_this_build = 0U;
        emitter_state_.killed_this_build = 0U;

        const bool can_simulate =
            build_config_.simulate &&
            build_config_.delta_time_s > 0.0F &&
            build_config_.fixed_step_s > 0.0F;
        if (!can_simulate) {
            return;
        }

        emitter_state_.fixed_step_accumulator_s += build_config_.delta_time_s;
        std::uint32_t step_count = static_cast<std::uint32_t>(
            emitter_state_.fixed_step_accumulator_s / build_config_.fixed_step_s);
        step_count = std::min(step_count, std::max<std::uint32_t>(1U, build_config_.max_simulation_steps));
        if (step_count == 0U) {
            return;
        }
        emitter_state_.fixed_step_accumulator_s -=
            static_cast<float>(step_count) * build_config_.fixed_step_s;

        for (std::uint32_t step_index = 0U; step_index < step_count; ++step_index) {
            const float step_dt = build_config_.fixed_step_s;
            if (build_config_.emit_new_particles && emitter_.config.playing != 0U) {
                SpawnStep(component_, emitter_, transform_, step_dt, emitter_state_, stats_);
            }
            IntegrateStep(component_, emitter_, step_dt, emitter_state_, stats_);
            emitter_state_.simulation_time_s += step_dt;
        }
    }

    static void ReinitializeEmitterState(const ParticleType& component_,
                                         const ParticleEmitterType& emitter_,
                                         std::uint32_t requested_capacity_,
                                         std::uint64_t simulation_signature_,
                                         ParticleEmitterState& emitter_state_) {
        emitter_state_ = {};
        emitter_state_.simulation_signature = simulation_signature_;
        emitter_state_.capacity = requested_capacity_;
        emitter_state_.random_state = emitter_.config.random_seed == 0U
            ? 1U
            : emitter_.config.random_seed;
        emitter_state_.particles.Resize(requested_capacity_);
        (void)component_;
    }

    [[nodiscard]] static std::uint64_t ComputeSimulationSignature(
        const ParticleType& component_,
        const ParticleEmitterType& emitter_) noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        auto mix_u32 = [&](std::uint32_t value_) {
            hash ^= static_cast<std::uint64_t>(value_);
            hash *= 1099511628211ULL;
        };
        auto mix_f32 = [&](float value_) {
            union final {
                float f;
                std::uint32_t u;
            } bits{value_};
            mix_u32(bits.u);
        };

        mix_u32(component_.style.max_particles);
        mix_u32(component_.style.max_alive_per_frame);
        mix_u32(static_cast<std::uint32_t>(component_.style.simulation_mode));
        mix_u32(static_cast<std::uint32_t>(component_.style.render_mode));
        mix_u32(static_cast<std::uint32_t>(component_.style.facing_mode));
        mix_f32(emitter_.config.spawn_rate);
        mix_u32(emitter_.config.burst_count);
        mix_f32(emitter_.config.burst_interval_s);
        mix_f32(emitter_.config.lifetime_min_s);
        mix_f32(emitter_.config.lifetime_max_s);
        mix_f32(emitter_.config.speed_min);
        mix_f32(emitter_.config.speed_max);
        mix_f32(emitter_.config.drag_coefficient);
        mix_f32(emitter_.config.gravity_scale);
        mix_f32(emitter_.config.start_size_min);
        mix_f32(emitter_.config.start_size_max);
        mix_f32(emitter_.config.end_size_min);
        mix_f32(emitter_.config.end_size_max);
        mix_f32(emitter_.config.rotation_min_radians);
        mix_f32(emitter_.config.rotation_max_radians);
        mix_f32(emitter_.config.angular_velocity_min);
        mix_f32(emitter_.config.angular_velocity_max);
        mix_f32(emitter_.config.emission_extent.x);
        mix_f32(emitter_.config.emission_extent.y);
        mix_f32(emitter_.config.emission_extent.z);
        mix_f32(emitter_.config.emission_radius);
        mix_f32(emitter_.config.emission_axis.x);
        mix_f32(emitter_.config.emission_axis.y);
        mix_f32(emitter_.config.emission_axis.z);
        mix_f32(emitter_.config.cone_half_angle_radians);
        mix_f32(emitter_.config.spread_angle_radians);
        mix_u32(emitter_.config.random_seed);
        mix_u32(emitter_.config.module_mask);
        mix_u32(emitter_.config.max_spawn_per_step);
        mix_u32(static_cast<std::uint32_t>(emitter_.config.simulation_space));
        mix_u32(static_cast<std::uint32_t>(emitter_.config.emitter_shape));
        mix_u32(emitter_.config.playing);
        mix_u32(emitter_.config.looping);
        mix_u32(emitter_.config.prewarm);
        return hash;
    }

    static void PrewarmEmitter(const ParticleType& component_,
                               const ParticleEmitterType& emitter_,
                               const TransformType& transform_,
                               ParticleEmitterState& emitter_state_,
                               ParticleRuntimeBuildStats& stats_) {
        emitter_state_.prewarm_done = 1U;
        const float average_lifetime =
            0.5F * (emitter_.config.lifetime_min_s + emitter_.config.lifetime_max_s);
        const std::uint32_t target_alive = std::min<std::uint32_t>(
            component_.style.max_particles,
            static_cast<std::uint32_t>(std::ceil(
                std::max(0.0F, emitter_.config.spawn_rate * average_lifetime))) +
                emitter_.config.burst_count);
        for (std::uint32_t i = 0U; i < target_alive; ++i) {
            if (!SpawnSingleParticle(component_, emitter_, transform_, emitter_state_, stats_)) {
                break;
            }
            const std::uint32_t particle_index = emitter_state_.active_count - 1U;
            const float lifetime_s = emitter_state_.particles.lifetime_s[particle_index];
            emitter_state_.particles.age_s[particle_index] =
                RandomRange(emitter_state_.random_state, 0.0F, lifetime_s);
        }
    }

    static void SpawnStep(const ParticleType& component_,
                          const ParticleEmitterType& emitter_,
                          const TransformType& transform_,
                          float step_dt_,
                          ParticleEmitterState& emitter_state_,
                          ParticleRuntimeBuildStats& stats_) {
        emitter_state_.spawn_accumulator_s += emitter_.config.spawn_rate * step_dt_;
        std::uint32_t spawn_count = static_cast<std::uint32_t>(emitter_state_.spawn_accumulator_s);
        emitter_state_.spawn_accumulator_s -= static_cast<float>(spawn_count);

        if (emitter_.config.burst_count > 0U) {
            emitter_state_.burst_accumulator_s += step_dt_;
            const bool should_fire_initial =
                emitter_state_.burst_fired_once == 0U;
            const bool should_fire_interval =
                emitter_.config.burst_interval_s > 0.0F &&
                emitter_state_.burst_accumulator_s >= emitter_.config.burst_interval_s &&
                emitter_.config.looping != 0U;
            if (should_fire_initial || should_fire_interval) {
                spawn_count += emitter_.config.burst_count;
                emitter_state_.burst_fired_once = 1U;
                if (emitter_.config.burst_interval_s > 0.0F) {
                    emitter_state_.burst_accumulator_s =
                        std::fmod(emitter_state_.burst_accumulator_s,
                                  emitter_.config.burst_interval_s);
                } else {
                    emitter_state_.burst_accumulator_s = 0.0F;
                }
            }
        }

        spawn_count = std::min<std::uint32_t>(spawn_count, static_cast<std::uint32_t>(emitter_.config.max_spawn_per_step));
        for (std::uint32_t i = 0U; i < spawn_count; ++i) {
            if (!SpawnSingleParticle(component_, emitter_, transform_, emitter_state_, stats_)) {
                break;
            }
        }
    }

    [[nodiscard]] static bool SpawnSingleParticle(const ParticleType& component_,
                                                  const ParticleEmitterType& emitter_,
                                                  const TransformType& transform_,
                                                  ParticleEmitterState& emitter_state_,
                                                  ParticleRuntimeBuildStats& stats_) {
        if (emitter_state_.active_count >= emitter_state_.capacity) {
            return false;
        }

        const std::uint32_t particle_index = emitter_state_.active_count++;
        Float3 spawn_position_local{};
        Float3 spawn_direction_local{};
        SampleSpawnShape(emitter_, emitter_state_.random_state, spawn_position_local, spawn_direction_local);

        const float speed = RandomRange(emitter_state_.random_state,
                                        emitter_.config.speed_min,
                                        emitter_.config.speed_max);
        const float lifetime_s = RandomRange(emitter_state_.random_state,
                                             emitter_.config.lifetime_min_s,
                                             emitter_.config.lifetime_max_s);
        const float start_size = RandomRange(emitter_state_.random_state,
                                             emitter_.config.start_size_min,
                                             emitter_.config.start_size_max);
        const float end_size = RandomRange(emitter_state_.random_state,
                                           emitter_.config.end_size_min,
                                           emitter_.config.end_size_max);
        const float rotation_radians = RandomRange(emitter_state_.random_state,
                                                   emitter_.config.rotation_min_radians,
                                                   emitter_.config.rotation_max_radians);
        const float angular_velocity_radians = RandomRange(emitter_state_.random_state,
                                                           emitter_.config.angular_velocity_min,
                                                           emitter_.config.angular_velocity_max);

        Float3 spawn_position = spawn_position_local;
        Float3 velocity = Float3{
            .x = spawn_direction_local.x * speed,
            .y = spawn_direction_local.y * speed,
            .z = spawn_direction_local.z * speed,
        };
        if (emitter_.config.simulation_space == ParticleSimulationSpace::world) {
            spawn_position = TransformPoint(transform_, spawn_position_local);
            velocity = TransformVector(transform_, velocity);
        }

        emitter_state_.particles.position_x[particle_index] = spawn_position.x;
        emitter_state_.particles.position_y[particle_index] = spawn_position.y;
        emitter_state_.particles.position_z[particle_index] = spawn_position.z;
        emitter_state_.particles.velocity_x[particle_index] = velocity.x;
        emitter_state_.particles.velocity_y[particle_index] = velocity.y;
        emitter_state_.particles.velocity_z[particle_index] = velocity.z;
        emitter_state_.particles.age_s[particle_index] = 0.0F;
        emitter_state_.particles.lifetime_s[particle_index] = lifetime_s;
        emitter_state_.particles.start_size[particle_index] = start_size;
        emitter_state_.particles.end_size[particle_index] = end_size;
        emitter_state_.particles.rotation_radians[particle_index] = rotation_radians;
        emitter_state_.particles.angular_velocity_radians[particle_index] = angular_velocity_radians;
        emitter_state_.particles.start_color_rgba8[particle_index] =
            PackColor(component_.style.start_color);
        emitter_state_.particles.end_color_rgba8[particle_index] =
            PackColor(component_.style.end_color);
        ++emitter_state_.total_spawned;
        ++emitter_state_.spawned_this_build;
        ++stats_.spawned_particle_count;
        (void)transform_;
        return true;
    }

    static void IntegrateStep(const ParticleType& component_,
                              const ParticleEmitterType& emitter_,
                              float step_dt_,
                              ParticleEmitterState& emitter_state_,
                              ParticleRuntimeBuildStats& stats_) {
        const Float3 gravity = Float3{
            .x = 0.0F,
            .y = -9.81F * emitter_.config.gravity_scale,
            .z = 0.0F,
        };
        const float drag_scale = std::max(0.0F, 1.0F - emitter_.config.drag_coefficient * step_dt_);

        std::uint32_t particle_index = 0U;
        while (particle_index < emitter_state_.active_count) {
            emitter_state_.particles.age_s[particle_index] += step_dt_;
            ++stats_.simulated_particle_count;
            if (emitter_state_.particles.age_s[particle_index] >=
                emitter_state_.particles.lifetime_s[particle_index]) {
                RemoveParticleAt(particle_index, emitter_state_);
                ++stats_.killed_particle_count;
                continue;
            }

            emitter_state_.particles.velocity_x[particle_index] =
                (emitter_state_.particles.velocity_x[particle_index] + gravity.x * step_dt_) * drag_scale;
            emitter_state_.particles.velocity_y[particle_index] =
                (emitter_state_.particles.velocity_y[particle_index] + gravity.y * step_dt_) * drag_scale;
            emitter_state_.particles.velocity_z[particle_index] =
                (emitter_state_.particles.velocity_z[particle_index] + gravity.z * step_dt_) * drag_scale;

            emitter_state_.particles.position_x[particle_index] +=
                emitter_state_.particles.velocity_x[particle_index] * step_dt_;
            emitter_state_.particles.position_y[particle_index] +=
                emitter_state_.particles.velocity_y[particle_index] * step_dt_;
            emitter_state_.particles.position_z[particle_index] +=
                emitter_state_.particles.velocity_z[particle_index] * step_dt_;
            emitter_state_.particles.rotation_radians[particle_index] +=
                emitter_state_.particles.angular_velocity_radians[particle_index] * step_dt_;
            ++particle_index;
        }
    }

    static void RemoveParticleAt(std::uint32_t particle_index_,
                                 ParticleEmitterState& emitter_state_) {
        const std::uint32_t last_index = emitter_state_.active_count - 1U;
        if (particle_index_ != last_index) {
            SwapParticle(particle_index_, last_index, emitter_state_);
        }
        --emitter_state_.active_count;
        ++emitter_state_.total_killed;
        ++emitter_state_.killed_this_build;
    }

    static void SwapParticle(std::uint32_t lhs_index_,
                             std::uint32_t rhs_index_,
                             ParticleEmitterState& emitter_state_) {
        std::swap(emitter_state_.particles.position_x[lhs_index_],
                  emitter_state_.particles.position_x[rhs_index_]);
        std::swap(emitter_state_.particles.position_y[lhs_index_],
                  emitter_state_.particles.position_y[rhs_index_]);
        std::swap(emitter_state_.particles.position_z[lhs_index_],
                  emitter_state_.particles.position_z[rhs_index_]);
        std::swap(emitter_state_.particles.velocity_x[lhs_index_],
                  emitter_state_.particles.velocity_x[rhs_index_]);
        std::swap(emitter_state_.particles.velocity_y[lhs_index_],
                  emitter_state_.particles.velocity_y[rhs_index_]);
        std::swap(emitter_state_.particles.velocity_z[lhs_index_],
                  emitter_state_.particles.velocity_z[rhs_index_]);
        std::swap(emitter_state_.particles.age_s[lhs_index_],
                  emitter_state_.particles.age_s[rhs_index_]);
        std::swap(emitter_state_.particles.lifetime_s[lhs_index_],
                  emitter_state_.particles.lifetime_s[rhs_index_]);
        std::swap(emitter_state_.particles.start_size[lhs_index_],
                  emitter_state_.particles.start_size[rhs_index_]);
        std::swap(emitter_state_.particles.end_size[lhs_index_],
                  emitter_state_.particles.end_size[rhs_index_]);
        std::swap(emitter_state_.particles.rotation_radians[lhs_index_],
                  emitter_state_.particles.rotation_radians[rhs_index_]);
        std::swap(emitter_state_.particles.angular_velocity_radians[lhs_index_],
                  emitter_state_.particles.angular_velocity_radians[rhs_index_]);
        std::swap(emitter_state_.particles.start_color_rgba8[lhs_index_],
                  emitter_state_.particles.start_color_rgba8[rhs_index_]);
        std::swap(emitter_state_.particles.end_color_rgba8[lhs_index_],
                  emitter_state_.particles.end_color_rgba8[rhs_index_]);
    }

    [[nodiscard]] static InstanceType BuildInstance(const ParticleType& component_,
                                                    const ParticleEmitterType& emitter_,
                                                    const TransformType& transform_,
                                                    const ParticleEmitterState& emitter_state_,
                                                    std::uint32_t component_index_,
                                                    std::uint32_t particle_index_) {
        const float age_s = emitter_state_.particles.age_s[particle_index_];
        const float lifetime_s = std::max(1e-6F, emitter_state_.particles.lifetime_s[particle_index_]);
        const float normalized_age = std::clamp(age_s / lifetime_s, 0.0F, 1.0F);
        const float start_size = emitter_state_.particles.start_size[particle_index_];
        const float end_size = emitter_state_.particles.end_size[particle_index_];
        const float size = Lerp(start_size, end_size, normalized_age);
        const std::uint32_t color_rgba8 = LerpColor(
            emitter_state_.particles.start_color_rgba8[particle_index_],
            emitter_state_.particles.end_color_rgba8[particle_index_],
            normalized_age);

        Float3 position{
            .x = emitter_state_.particles.position_x[particle_index_],
            .y = emitter_state_.particles.position_y[particle_index_],
            .z = emitter_state_.particles.position_z[particle_index_],
        };
        Float3 velocity{
            .x = emitter_state_.particles.velocity_x[particle_index_],
            .y = emitter_state_.particles.velocity_y[particle_index_],
            .z = emitter_state_.particles.velocity_z[particle_index_],
        };
        if (emitter_.config.simulation_space == ParticleSimulationSpace::local) {
            position = TransformPoint(transform_, position);
            velocity = TransformVector(transform_, velocity);
        }

        if constexpr (std::same_as<DimensionT, Dim2>) {
            Particle2DGpuInstance instance{};
            instance.position_x = position.x;
            instance.position_y = position.y;
            instance.velocity_x = velocity.x;
            instance.velocity_y = velocity.y;
            instance.size_x = size;
            instance.size_y = size;
            instance.rotation_radians = emitter_state_.particles.rotation_radians[particle_index_];
            instance.normalized_age = normalized_age;
            instance.color_rgba8 = color_rgba8;
            instance.texture_slot = component_.runtime.route.texture_id;
            instance.sampler_slot = 0U;
            instance.component_index = component_index_;
            instance.user_data = component_.runtime.route.user_data;
            return instance;
        } else {
            Particle3DGpuInstance instance{};
            instance.position_x = position.x;
            instance.position_y = position.y;
            instance.position_z = position.z;
            instance.size_x = size;
            instance.size_y = size;
            instance.rotation_radians = emitter_state_.particles.rotation_radians[particle_index_];
            instance.normalized_age = normalized_age;
            instance.stretch_factor = 1.0F +
                std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z) *
                    component_.style.stretch_velocity_scale;
            instance.color_rgba8 = color_rgba8;
            instance.texture_slot = component_.runtime.route.texture_id;
            instance.sampler_slot = 0U;
            instance.component_index = component_index_;
            instance.user_data = component_.runtime.route.user_data;
            instance.velocity_x = velocity.x;
            instance.velocity_y = velocity.y;
            instance.velocity_z = velocity.z;
            instance.soft_particle_distance = component_.style.soft_particle_distance;
            return instance;
        }
    }

    [[nodiscard]] static float Lerp(float lhs_,
                                    float rhs_,
                                    float t_) noexcept {
        return lhs_ + (rhs_ - lhs_) * t_;
    }

    [[nodiscard]] static std::uint32_t PackColor(const Rgba8& color_) noexcept {
        return static_cast<std::uint32_t>(color_.r) |
               (static_cast<std::uint32_t>(color_.g) << 8U) |
               (static_cast<std::uint32_t>(color_.b) << 16U) |
               (static_cast<std::uint32_t>(color_.a) << 24U);
    }

    [[nodiscard]] static Rgba8 UnpackColor(std::uint32_t packed_color_) noexcept {
        return Rgba8{
            .r = static_cast<std::uint8_t>(packed_color_ & 0xFFU),
            .g = static_cast<std::uint8_t>((packed_color_ >> 8U) & 0xFFU),
            .b = static_cast<std::uint8_t>((packed_color_ >> 16U) & 0xFFU),
            .a = static_cast<std::uint8_t>((packed_color_ >> 24U) & 0xFFU),
        };
    }

    [[nodiscard]] static std::uint32_t LerpColor(std::uint32_t start_color_rgba8_,
                                                 std::uint32_t end_color_rgba8_,
                                                 float t_) noexcept {
        const Rgba8 start = UnpackColor(start_color_rgba8_);
        const Rgba8 end = UnpackColor(end_color_rgba8_);
        const auto lerp_channel = [&](std::uint8_t lhs_, std::uint8_t rhs_) noexcept {
            return static_cast<std::uint8_t>(
                std::clamp(std::lround(Lerp(static_cast<float>(lhs_),
                                            static_cast<float>(rhs_),
                                            t_)),
                           0L,
                           255L));
        };
        return PackColor(Rgba8{
            .r = lerp_channel(start.r, end.r),
            .g = lerp_channel(start.g, end.g),
            .b = lerp_channel(start.b, end.b),
            .a = lerp_channel(start.a, end.a),
        });
    }

    [[nodiscard]] static std::uint32_t NextRandom(std::uint32_t& state_) noexcept {
        state_ ^= (state_ << 13U);
        state_ ^= (state_ >> 17U);
        state_ ^= (state_ << 5U);
        state_ = state_ == 0U ? 1U : state_;
        return state_;
    }

    [[nodiscard]] static float Random01(std::uint32_t& state_) noexcept {
        constexpr float k_inv_uint32 = 1.0F / 4294967295.0F;
        return static_cast<float>(NextRandom(state_)) * k_inv_uint32;
    }

    [[nodiscard]] static float RandomRange(std::uint32_t& state_,
                                           float min_value_,
                                           float max_value_) noexcept {
        if (max_value_ <= min_value_) {
            return min_value_;
        }
        return min_value_ + (max_value_ - min_value_) * Random01(state_);
    }

    [[nodiscard]] static Float3 NormalizeSafe(const Float3& value_,
                                              const Float3& fallback_) noexcept {
        const float length_sq =
            value_.x * value_.x + value_.y * value_.y + value_.z * value_.z;
        if (length_sq <= 1e-12F) {
            return fallback_;
        }
        const float inv_length = 1.0F / std::sqrt(length_sq);
        return Float3{
            .x = value_.x * inv_length,
            .y = value_.y * inv_length,
            .z = value_.z * inv_length,
        };
    }

    static void SampleSpawnShape(const ParticleEmitterType& emitter_,
                                 std::uint32_t& random_state_,
                                 Float3& out_position_,
                                 Float3& out_direction_) noexcept {
        const Float3 axis = NormalizeSafe(emitter_.config.emission_axis,
                                          Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
        out_position_ = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        out_direction_ = axis;

        switch (emitter_.config.emitter_shape) {
        case ParticleEmitterShape::box:
            out_position_ = Float3{
                .x = RandomRange(random_state_,
                                 -emitter_.config.emission_extent.x,
                                 emitter_.config.emission_extent.x),
                .y = RandomRange(random_state_,
                                 -emitter_.config.emission_extent.y,
                                 emitter_.config.emission_extent.y),
                .z = std::same_as<DimensionT, Dim2>
                    ? 0.0F
                    : RandomRange(random_state_,
                                  -emitter_.config.emission_extent.z,
                                  emitter_.config.emission_extent.z),
            };
            out_direction_ = JitterDirection(axis,
                                             emitter_.config.spread_angle_radians,
                                             random_state_);
            break;
        case ParticleEmitterShape::circle: {
            const float angle = RandomRange(random_state_, 0.0F, 6.28318530717958647692F);
            const float radius = std::sqrt(Random01(random_state_)) * emitter_.config.emission_radius;
            out_position_ = Float3{
                .x = std::cos(angle) * radius,
                .y = std::sin(angle) * radius,
                .z = 0.0F,
            };
            out_direction_ = JitterDirection(axis,
                                             emitter_.config.spread_angle_radians,
                                             random_state_);
            break;
        }
        case ParticleEmitterShape::sphere: {
            const float u = RandomRange(random_state_, -1.0F, 1.0F);
            const float theta = RandomRange(random_state_, 0.0F, 6.28318530717958647692F);
            const float radius = std::cbrt(Random01(random_state_)) * emitter_.config.emission_radius;
            const float r_xy = std::sqrt(std::max(0.0F, 1.0F - u * u));
            out_position_ = Float3{
                .x = radius * r_xy * std::cos(theta),
                .y = radius * u,
                .z = radius * r_xy * std::sin(theta),
            };
            out_direction_ = NormalizeSafe(out_position_, axis);
            break;
        }
        case ParticleEmitterShape::cone:
            out_direction_ = SampleConeDirection(axis,
                                                 emitter_.config.cone_half_angle_radians,
                                                 random_state_);
            break;
        case ParticleEmitterShape::point:
        default:
            out_direction_ = JitterDirection(axis,
                                             emitter_.config.spread_angle_radians,
                                             random_state_);
            break;
        }
        if constexpr (std::same_as<DimensionT, Dim2>) {
            out_position_.z = 0.0F;
            out_direction_.z = 0.0F;
            out_direction_ = NormalizeSafe(out_direction_,
                                           Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
        }
    }

    [[nodiscard]] static Float3 JitterDirection(const Float3& axis_,
                                                float spread_angle_radians_,
                                                std::uint32_t& random_state_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const float base_angle = std::atan2(axis_.y, axis_.x);
            const float offset = RandomRange(random_state_,
                                             -spread_angle_radians_,
                                             spread_angle_radians_);
            return Float3{
                .x = std::cos(base_angle + offset),
                .y = std::sin(base_angle + offset),
                .z = 0.0F,
            };
        }
        return SampleConeDirection(axis_, spread_angle_radians_, random_state_);
    }

    [[nodiscard]] static Float3 SampleConeDirection(const Float3& axis_,
                                                    float half_angle_radians_,
                                                    std::uint32_t& random_state_) noexcept {
        const Float3 axis = NormalizeSafe(axis_, Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
        if (half_angle_radians_ <= 1e-6F) {
            return axis;
        }

        const float cos_theta = RandomRange(random_state_, std::cos(half_angle_radians_), 1.0F);
        const float sin_theta = std::sqrt(std::max(0.0F, 1.0F - cos_theta * cos_theta));
        const float phi = RandomRange(random_state_, 0.0F, 6.28318530717958647692F);

        Float3 tangent = std::abs(axis.y) < 0.99F
            ? Float3{.x = -axis.z, .y = 0.0F, .z = axis.x}
            : Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F};
        tangent = NormalizeSafe(tangent, Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
        const Float3 bitangent = Float3{
            .x = axis.y * tangent.z - axis.z * tangent.y,
            .y = axis.z * tangent.x - axis.x * tangent.z,
            .z = axis.x * tangent.y - axis.y * tangent.x,
        };

        return NormalizeSafe(Float3{
                                 .x = tangent.x * (std::cos(phi) * sin_theta) +
                                      bitangent.x * (std::sin(phi) * sin_theta) +
                                      axis.x * cos_theta,
                                 .y = tangent.y * (std::cos(phi) * sin_theta) +
                                      bitangent.y * (std::sin(phi) * sin_theta) +
                                      axis.y * cos_theta,
                                 .z = tangent.z * (std::cos(phi) * sin_theta) +
                                      bitangent.z * (std::sin(phi) * sin_theta) +
                                      axis.z * cos_theta,
                             },
                             axis);
    }

    [[nodiscard]] static Float3 TransformPoint(const TransformType& transform_,
                                               const Float3& local_point_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const auto& m = transform_.runtime.world_matrix;
            return Float3{
                .x = m.m00 * local_point_.x + m.m01 * local_point_.y + m.m02,
                .y = m.m10 * local_point_.x + m.m11 * local_point_.y + m.m12,
                .z = 0.0F,
            };
        } else {
            const auto& m = transform_.runtime.world_matrix.m;
            return Float3{
                .x = m[0] * local_point_.x + m[4] * local_point_.y + m[8] * local_point_.z + m[12],
                .y = m[1] * local_point_.x + m[5] * local_point_.y + m[9] * local_point_.z + m[13],
                .z = m[2] * local_point_.x + m[6] * local_point_.y + m[10] * local_point_.z + m[14],
            };
        }
    }

    [[nodiscard]] static Float3 TransformVector(const TransformType& transform_,
                                                const Float3& local_vector_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const auto& m = transform_.runtime.world_matrix;
            return Float3{
                .x = m.m00 * local_vector_.x + m.m01 * local_vector_.y,
                .y = m.m10 * local_vector_.x + m.m11 * local_vector_.y,
                .z = 0.0F,
            };
        } else {
            const auto& m = transform_.runtime.world_matrix.m;
            return Float3{
                .x = m[0] * local_vector_.x + m[4] * local_vector_.y + m[8] * local_vector_.z,
                .y = m[1] * local_vector_.x + m[5] * local_vector_.y + m[9] * local_vector_.z,
                .z = m[2] * local_vector_.x + m[6] * local_vector_.y + m[10] * local_vector_.z,
            };
        }
    }
};

static_assert(std::is_standard_layout_v<Particle2DGpuInstance> &&
              std::is_trivial_v<Particle2DGpuInstance>);
static_assert(std::is_standard_layout_v<Particle3DGpuInstance> &&
              std::is_trivial_v<Particle3DGpuInstance>);
static_assert(std::is_standard_layout_v<ParticleDrawBatch> &&
              std::is_trivial_v<ParticleDrawBatch>);

} // namespace vr::ecs
