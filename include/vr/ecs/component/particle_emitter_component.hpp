#pragma once

#include "vr/ecs/component/particle_component.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

struct ParticleEmitterCommon final {
    float spawn_rate;
    std::uint32_t burst_count;
    float burst_interval_s;
    float lifetime_min_s;
    float lifetime_max_s;
    float speed_min;
    float speed_max;
    float drag_coefficient;
    float gravity_scale;
    float start_size_min;
    float start_size_max;
    float end_size_min;
    float end_size_max;
    float rotation_min_radians;
    float rotation_max_radians;
    float angular_velocity_min;
    float angular_velocity_max;
    Float3 emission_extent;
    float emission_radius;
    Float3 emission_axis;
    float cone_half_angle_radians;
    float spread_angle_radians;
    std::uint32_t random_seed;
    std::uint32_t module_mask;
    std::uint16_t max_spawn_per_step;
    ParticleSimulationSpace simulation_space;
    ParticleEmitterShape emitter_shape;
    std::uint8_t playing;
    std::uint8_t looping;
    std::uint8_t prewarm;
    std::uint8_t reserved0;
};

template<DimensionTag DimensionT>
struct ParticleEmitterComponent;

template<>
struct ParticleEmitterComponent<Dim2> final {
    using ConfigType = ParticleEmitterCommon;

    ConfigType config;
};

template<>
struct ParticleEmitterComponent<Dim3> final {
    using ConfigType = ParticleEmitterCommon;

    ConfigType config;
};

template<DimensionTag DimensionT>
using ParticleEmitter = ParticleEmitterComponent<DimensionT>;

template<typename T>
concept PurePodParticleEmitterComponent = std::is_standard_layout_v<T> &&
                                          std::is_trivial_v<T>;

static_assert(PurePodParticleEmitterComponent<ParticleEmitterCommon>);
static_assert(PurePodParticleEmitterComponent<ParticleEmitter<Dim2>>);
static_assert(PurePodParticleEmitterComponent<ParticleEmitter<Dim3>>);

} // namespace vr::ecs
