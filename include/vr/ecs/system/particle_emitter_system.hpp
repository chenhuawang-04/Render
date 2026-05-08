#pragma once

#include "vr/ecs/component/particle_emitter_component.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>

namespace vr::ecs {

template<DimensionTag DimensionT>
class ParticleEmitterSystem final {
public:
    using ParticleType = Particle<DimensionT>;
    using ParticleEmitterType = ParticleEmitter<DimensionT>;
    using ConfigType = typename ParticleEmitterType::ConfigType;

    static void Initialize(ParticleType& particle_,
                           ParticleEmitterType& emitter_) noexcept {
        SetDefaultConfig(particle_, emitter_);
    }

    static void SetDefaultConfig(ParticleType& particle_,
                                 ParticleEmitterType& emitter_) noexcept {
        emitter_.config.spawn_rate = 32.0F;
        emitter_.config.burst_count = 0U;
        emitter_.config.burst_interval_s = 0.0F;
        emitter_.config.lifetime_min_s = 0.5F;
        emitter_.config.lifetime_max_s = 1.0F;
        emitter_.config.speed_min = 0.5F;
        emitter_.config.speed_max = 1.5F;
        emitter_.config.drag_coefficient = 0.0F;
        emitter_.config.gravity_scale = 0.0F;
        emitter_.config.start_size_min = 8.0F;
        emitter_.config.start_size_max = 12.0F;
        emitter_.config.end_size_min = 0.0F;
        emitter_.config.end_size_max = 2.0F;
        emitter_.config.rotation_min_radians = 0.0F;
        emitter_.config.rotation_max_radians = 0.0F;
        emitter_.config.angular_velocity_min = 0.0F;
        emitter_.config.angular_velocity_max = 0.0F;
        emitter_.config.emission_extent = Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        emitter_.config.emission_radius = 0.0F;
        emitter_.config.emission_axis = DefaultEmissionAxis();
        emitter_.config.cone_half_angle_radians = 0.52359877559829887307F;
        emitter_.config.spread_angle_radians = 0.17453292519943295769F;
        emitter_.config.random_seed = 1U;
        emitter_.config.module_mask = 0U;
        emitter_.config.max_spawn_per_step = 64U;
        emitter_.config.simulation_space = ParticleSimulationSpace::local;
        emitter_.config.emitter_shape = ParticleEmitterShape::point;
        emitter_.config.playing = 1U;
        emitter_.config.looping = 1U;
        emitter_.config.prewarm = 0U;
        emitter_.config.reserved0 = 0U;

        MarkEmitterEdited(particle_);
    }

    static void SetSpawnRate(ParticleType& particle_,
                             ParticleEmitterType& emitter_,
                             float spawn_rate_) noexcept {
        if (emitter_.config.spawn_rate == spawn_rate_) {
            return;
        }
        emitter_.config.spawn_rate = spawn_rate_;
        MarkEmitterEdited(particle_);
    }

    static void SetBurst(ParticleType& particle_,
                         ParticleEmitterType& emitter_,
                         std::uint32_t burst_count_,
                         float burst_interval_s_) noexcept {
        if (emitter_.config.burst_count == burst_count_ &&
            emitter_.config.burst_interval_s == burst_interval_s_) {
            return;
        }
        emitter_.config.burst_count = burst_count_;
        emitter_.config.burst_interval_s = burst_interval_s_;
        MarkEmitterEdited(particle_);
    }

    static void SetLifetimeRange(ParticleType& particle_,
                                 ParticleEmitterType& emitter_,
                                 float lifetime_min_s_,
                                 float lifetime_max_s_) noexcept {
        if (emitter_.config.lifetime_min_s == lifetime_min_s_ &&
            emitter_.config.lifetime_max_s == lifetime_max_s_) {
            return;
        }
        emitter_.config.lifetime_min_s = lifetime_min_s_;
        emitter_.config.lifetime_max_s = lifetime_max_s_;
        MarkEmitterEdited(particle_);
    }

    static void SetSpeedRange(ParticleType& particle_,
                              ParticleEmitterType& emitter_,
                              float speed_min_,
                              float speed_max_) noexcept {
        if (emitter_.config.speed_min == speed_min_ &&
            emitter_.config.speed_max == speed_max_) {
            return;
        }
        emitter_.config.speed_min = speed_min_;
        emitter_.config.speed_max = speed_max_;
        MarkEmitterEdited(particle_);
    }

    static void SetIntegrator(ParticleType& particle_,
                              ParticleEmitterType& emitter_,
                              float drag_coefficient_,
                              float gravity_scale_) noexcept {
        if (emitter_.config.drag_coefficient == drag_coefficient_ &&
            emitter_.config.gravity_scale == gravity_scale_) {
            return;
        }
        emitter_.config.drag_coefficient = drag_coefficient_;
        emitter_.config.gravity_scale = gravity_scale_;
        MarkEmitterEdited(particle_);
    }

    static void SetSizeRange(ParticleType& particle_,
                             ParticleEmitterType& emitter_,
                             float start_size_min_,
                             float start_size_max_,
                             float end_size_min_,
                             float end_size_max_) noexcept {
        if (emitter_.config.start_size_min == start_size_min_ &&
            emitter_.config.start_size_max == start_size_max_ &&
            emitter_.config.end_size_min == end_size_min_ &&
            emitter_.config.end_size_max == end_size_max_) {
            return;
        }
        emitter_.config.start_size_min = start_size_min_;
        emitter_.config.start_size_max = start_size_max_;
        emitter_.config.end_size_min = end_size_min_;
        emitter_.config.end_size_max = end_size_max_;
        MarkEmitterEdited(particle_);
    }

    static void SetRotationRange(ParticleType& particle_,
                                 ParticleEmitterType& emitter_,
                                 float rotation_min_radians_,
                                 float rotation_max_radians_,
                                 float angular_velocity_min_,
                                 float angular_velocity_max_) noexcept {
        if (emitter_.config.rotation_min_radians == rotation_min_radians_ &&
            emitter_.config.rotation_max_radians == rotation_max_radians_ &&
            emitter_.config.angular_velocity_min == angular_velocity_min_ &&
            emitter_.config.angular_velocity_max == angular_velocity_max_) {
            return;
        }
        emitter_.config.rotation_min_radians = rotation_min_radians_;
        emitter_.config.rotation_max_radians = rotation_max_radians_;
        emitter_.config.angular_velocity_min = angular_velocity_min_;
        emitter_.config.angular_velocity_max = angular_velocity_max_;
        MarkEmitterEdited(particle_);
    }

    static void SetEmissionShape(ParticleType& particle_,
                                 ParticleEmitterType& emitter_,
                                 ParticleEmitterShape emitter_shape_,
                                 const Float3& emission_extent_,
                                 float emission_radius_,
                                 const Float3& emission_axis_,
                                 float cone_half_angle_radians_,
                                 float spread_angle_radians_) noexcept {
        emitter_.config.emitter_shape = emitter_shape_;
        emitter_.config.emission_extent = emission_extent_;
        emitter_.config.emission_radius = emission_radius_;
        emitter_.config.emission_axis = emission_axis_;
        emitter_.config.cone_half_angle_radians = cone_half_angle_radians_;
        emitter_.config.spread_angle_radians = spread_angle_radians_;
        MarkEmitterEdited(particle_);
    }

    static void SetRandomSeed(ParticleType& particle_,
                              ParticleEmitterType& emitter_,
                              std::uint32_t random_seed_) noexcept {
        if (emitter_.config.random_seed == random_seed_) {
            return;
        }
        emitter_.config.random_seed = random_seed_;
        MarkEmitterEdited(particle_);
    }

    static void SetModuleMask(ParticleType& particle_,
                              ParticleEmitterType& emitter_,
                              std::uint32_t module_mask_) noexcept {
        if (emitter_.config.module_mask == module_mask_) {
            return;
        }
        emitter_.config.module_mask = module_mask_;
        MarkEmitterEdited(particle_);
    }

    static void SetMaxSpawnPerStep(ParticleType& particle_,
                                   ParticleEmitterType& emitter_,
                                   std::uint16_t max_spawn_per_step_) noexcept {
        if (emitter_.config.max_spawn_per_step == max_spawn_per_step_) {
            return;
        }
        emitter_.config.max_spawn_per_step = max_spawn_per_step_;
        MarkEmitterEdited(particle_);
    }

    static void SetSimulationSpace(ParticleType& particle_,
                                   ParticleEmitterType& emitter_,
                                   ParticleSimulationSpace simulation_space_) noexcept {
        if (emitter_.config.simulation_space == simulation_space_) {
            return;
        }
        emitter_.config.simulation_space = simulation_space_;
        MarkEmitterEdited(particle_);
    }

    static void SetPlayback(ParticleType& particle_,
                            ParticleEmitterType& emitter_,
                            bool playing_,
                            bool looping_,
                            bool prewarm_) noexcept {
        const std::uint8_t playing_value = playing_ ? 1U : 0U;
        const std::uint8_t looping_value = looping_ ? 1U : 0U;
        const std::uint8_t prewarm_value = prewarm_ ? 1U : 0U;
        if (emitter_.config.playing == playing_value &&
            emitter_.config.looping == looping_value &&
            emitter_.config.prewarm == prewarm_value) {
            return;
        }
        emitter_.config.playing = playing_value;
        emitter_.config.looping = looping_value;
        emitter_.config.prewarm = prewarm_value;
        MarkEmitterEdited(particle_);
    }

private:
    static void MarkEmitterEdited(ParticleType& particle_) noexcept {
        particle_.runtime.route.dirty_flags |= particle_dirty_emitter_flag |
                                               particle_dirty_runtime_flag |
                                               particle_dirty_simulation_flag;
        ++particle_.runtime.revision_authoring;
    }

    [[nodiscard]] static Float3 DefaultEmissionAxis() noexcept {
        return Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }
};

} // namespace vr::ecs
