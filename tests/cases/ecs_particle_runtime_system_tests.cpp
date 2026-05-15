#include "support/test_framework.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_runtime_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <cmath>

namespace {

using Particle2D = vr::ecs::Particle<vr::ecs::Dim2>;
using Particle3D = vr::ecs::Particle<vr::ecs::Dim3>;
using ParticleEmitter2D = vr::ecs::ParticleEmitter<vr::ecs::Dim2>;
using ParticleEmitter3D = vr::ecs::ParticleEmitter<vr::ecs::Dim3>;
using ParticleEmitterSystem2D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim2>;
using ParticleEmitterSystem3D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim3>;
using ParticleSystem2D = vr::ecs::ParticleSystem<vr::ecs::Dim2>;
using ParticleSystem3D = vr::ecs::ParticleSystem<vr::ecs::Dim3>;
using ParticleRuntimeSystem2D = vr::ecs::ParticleRuntimeSystem<vr::ecs::Dim2>;
using ParticleRuntimeSystem3D = vr::ecs::ParticleRuntimeSystem<vr::ecs::Dim3>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

VR_TEST_CASE(EcsParticleRuntimeSystem_dim2_build_spawn_and_local_space_transform, "unit;core;ecs;particle;runtime") {
    Particle2D particles[1]{};
    ParticleEmitter2D emitters[1]{};
    Transform2D transforms[1]{};

    ParticleSystem2D::Initialize(particles[0]);
    ParticleEmitterSystem2D::Initialize(particles[0], emitters[0]);
    TransformSystem2D::Initialize(transforms[0]);
    TransformSystem2D::SetLocalPosition(transforms[0], 10.0F, 20.0F);
    TransformSystem2D::UpdateHierarchy(transforms, 1U);

    ParticleEmitterSystem2D::SetSpawnRate(particles[0], emitters[0], 10.0F);
    ParticleEmitterSystem2D::SetLifetimeRange(particles[0], emitters[0], 1.0F, 1.0F);
    ParticleEmitterSystem2D::SetSpeedRange(particles[0], emitters[0], 0.0F, 0.0F);
    ParticleEmitterSystem2D::SetSizeRange(particles[0], emitters[0], 4.0F, 4.0F, 2.0F, 2.0F);
    ParticleEmitterSystem2D::SetEmissionShape(
        particles[0],
        emitters[0],
        vr::ecs::ParticleEmitterShape::point,
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
        0.0F,
        vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
        0.0F,
        0.0F);
    ParticleEmitterSystem2D::SetSimulationSpace(particles[0], emitters[0], vr::ecs::ParticleSimulationSpace::local);
    ParticleSystem2D::SetFacingMode(particles[0], vr::ecs::ParticleFacingMode::local);
    ParticleEmitterSystem2D::SetPlayback(particles[0], emitters[0], true, true, false);

    vr::ecs::Particle2DRuntimeScratch scratch{};
    ParticleRuntimeSystem2D::Reserve(scratch, 1U, 16U);

    vr::ecs::ParticleRuntimeBuildConfig build_config{};
    build_config.delta_time_s = 0.1F;
    build_config.fixed_step_s = 0.1F;
    build_config.max_simulation_steps = 1U;

    const vr::ecs::ParticleRuntimeBuildStats stats =
        ParticleRuntimeSystem2D::Build(particles, emitters, transforms, 1U, scratch, build_config);

    VR_REQUIRE(stats.emitted_instance_count == 1U);
    VR_REQUIRE(stats.active_particle_count == 1U);
    VR_REQUIRE(stats.spawned_particle_count == 1U);
    VR_REQUIRE(scratch.draw_batches.size() == 1U);
    VR_CHECK(std::abs(scratch.instances[0].position_x - 10.0F) < 1e-4F);
    VR_CHECK(std::abs(scratch.instances[0].position_y - 20.0F) < 1e-4F);
    VR_CHECK(std::abs(scratch.instances[0].size_x - 3.8F) < 1e-4F);
    VR_CHECK(scratch.instances[0].component_index == 0U);
}

VR_TEST_CASE(EcsParticleRuntimeSystem_dim3_visible_hint_and_world_space_simulation, "unit;core;ecs;particle;runtime") {
    Particle3D particles[2]{};
    ParticleEmitter3D emitters[2]{};
    Transform3D transforms[2]{};

    for (std::uint32_t i = 0U; i < 2U; ++i) {
        ParticleSystem3D::Initialize(particles[i]);
        ParticleEmitterSystem3D::Initialize(particles[i], emitters[i]);
        TransformSystem3D::Initialize(transforms[i]);
    }

    TransformSystem3D::SetLocalPosition(transforms[0], vr::ecs::Float3{.x = 3.0F, .y = 0.0F, .z = 1.0F});
    TransformSystem3D::SetLocalPosition(transforms[1], vr::ecs::Float3{.x = 7.0F, .y = 0.0F, .z = 2.0F});
    TransformSystem3D::UpdateHierarchy(transforms, 2U);

    for (std::uint32_t i = 0U; i < 2U; ++i) {
        ParticleEmitterSystem3D::SetSpawnRate(particles[i], emitters[i], 10.0F);
        ParticleEmitterSystem3D::SetLifetimeRange(particles[i], emitters[i], 1.0F, 1.0F);
        ParticleEmitterSystem3D::SetSpeedRange(particles[i], emitters[i], 0.0F, 0.0F);
        ParticleEmitterSystem3D::SetSimulationSpace(particles[i], emitters[i], vr::ecs::ParticleSimulationSpace::world);
        ParticleSystem3D::SetFacingMode(particles[i], vr::ecs::ParticleFacingMode::screen);
        ParticleEmitterSystem3D::SetPlayback(particles[i], emitters[i], true, true, false);
        ParticleEmitterSystem3D::SetEmissionShape(
            particles[i],
            emitters[i],
            vr::ecs::ParticleEmitterShape::point,
            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            0.0F,
            vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            0.0F,
            0.0F);
    }

    vr::ecs::Particle3DRuntimeScratch scratch{};
    ParticleRuntimeSystem3D::Reserve(scratch, 2U, 32U);

    vr::ecs::ParticleRuntimeBuildConfig build_config{};
    build_config.delta_time_s = 0.1F;
    build_config.fixed_step_s = 0.1F;
    build_config.max_simulation_steps = 1U;

    const std::uint32_t visible_component_indices[] = {1U};
    vr::ecs::Particle3DRuntimeBuildHint build_hint{};
    build_hint.visible_component_indices = visible_component_indices;
    build_hint.visible_component_count = 1U;
    build_hint.use_visible_component_indices = 1U;

    const vr::ecs::ParticleRuntimeBuildStats stats =
        ParticleRuntimeSystem3D::Build(particles, emitters, transforms, 2U, scratch, build_config, build_hint);

    VR_REQUIRE(stats.active_particle_count == 2U);
    VR_REQUIRE(stats.candidate_emitter_count == 1U);
    VR_REQUIRE(stats.emitted_instance_count == 1U);
    VR_REQUIRE(stats.used_visible_component_indices);
    VR_CHECK(std::abs(scratch.instances[0].position_x - 7.0F) < 1e-4F);
    VR_CHECK(std::abs(scratch.instances[0].position_z - 2.0F) < 1e-4F);
    VR_CHECK(scratch.instances[0].component_index == 1U);
}

VR_TEST_CASE(EcsParticleRuntimeSystem_dim3_linked_appearance_uses_effective_visual_resource_id,
             "unit;core;ecs;particle;runtime") {
    Particle3D particle{};
    ParticleEmitter3D emitter{};
    Transform3D transform{};

    ParticleSystem3D::Initialize(particle);
    ParticleEmitterSystem3D::Initialize(particle, emitter);
    TransformSystem3D::Initialize(transform);
    TransformSystem3D::UpdateHierarchy(&transform, 1U);

    ParticleSystem3D::SetAuthoringVisualResourceId(particle, 19U);
    (void)ParticleSystem3D::SetAppearanceRuntimeLink(particle,
                                                     vr::ecs::AppearanceHandle{.index = 3U, .generation = 1U},
                                                     0ULL,
                                                     0ULL,
                                                     650ULL);
    ParticleEmitterSystem3D::SetSpawnRate(particle, emitter, 10.0F);
    ParticleEmitterSystem3D::SetLifetimeRange(particle, emitter, 1.0F, 1.0F);
    ParticleEmitterSystem3D::SetSpeedRange(particle, emitter, 0.0F, 0.0F);
    ParticleEmitterSystem3D::SetSimulationSpace(particle, emitter, vr::ecs::ParticleSimulationSpace::world);
    ParticleSystem3D::SetFacingMode(particle, vr::ecs::ParticleFacingMode::screen);
    ParticleEmitterSystem3D::SetPlayback(particle, emitter, true, true, false);
    ParticleEmitterSystem3D::SetEmissionShape(
        particle,
        emitter,
        vr::ecs::ParticleEmitterShape::point,
        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
        0.0F,
        vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
        0.0F,
        0.0F);

    vr::ecs::Particle3DRuntimeScratch scratch{};
    ParticleRuntimeSystem3D::Reserve(scratch, 1U, 16U);

    vr::ecs::ParticleRuntimeBuildConfig build_config{};
    build_config.delta_time_s = 0.1F;
    build_config.fixed_step_s = 0.1F;
    build_config.max_simulation_steps = 1U;

    const vr::ecs::ParticleRuntimeBuildStats stats =
        ParticleRuntimeSystem3D::Build(&particle, &emitter, &transform, 1U, scratch, build_config);

    VR_REQUIRE(stats.emitted_instance_count == 1U);
    VR_REQUIRE(scratch.draw_batches.size() == 1U);
    VR_CHECK(particle.runtime.route.authoring_visual_resource_id == 19U);
    VR_CHECK(scratch.draw_batches[0U].effective_visual_resource_id == 650U);
}

VR_TEST_CASE(EcsParticleRuntimeSystem_dim2_batch_split_on_blend_state, "unit;core;ecs;particle;runtime") {
    Particle2D particles[2]{};
    ParticleEmitter2D emitters[2]{};
    Transform2D transforms[2]{};

    for (std::uint32_t i = 0U; i < 2U; ++i) {
        ParticleSystem2D::Initialize(particles[i]);
        ParticleEmitterSystem2D::Initialize(particles[i], emitters[i]);
        TransformSystem2D::Initialize(transforms[i]);

        ParticleEmitterSystem2D::SetSpawnRate(particles[i], emitters[i], 10.0F);
        ParticleEmitterSystem2D::SetLifetimeRange(particles[i], emitters[i], 1.0F, 1.0F);
        ParticleEmitterSystem2D::SetSpeedRange(particles[i], emitters[i], 0.0F, 0.0F);
        ParticleEmitterSystem2D::SetEmissionShape(
            particles[i],
            emitters[i],
            vr::ecs::ParticleEmitterShape::point,
            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            0.0F,
            vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            0.0F,
            0.0F);
        ParticleEmitterSystem2D::SetPlayback(particles[i], emitters[i], true, true, false);
    }
    TransformSystem2D::UpdateHierarchy(transforms, 2U);

    ParticleSystem2D::SetBlendMode(particles[0], vr::ecs::ParticleBlendMode::alpha);
    ParticleSystem2D::SetBlendMode(particles[1], vr::ecs::ParticleBlendMode::additive);

    vr::ecs::Particle2DRuntimeScratch scratch{};
    ParticleRuntimeSystem2D::Reserve(scratch, 2U, 16U);

    vr::ecs::ParticleRuntimeBuildConfig build_config{};
    build_config.delta_time_s = 0.1F;
    build_config.fixed_step_s = 0.1F;
    build_config.max_simulation_steps = 1U;

    const vr::ecs::ParticleRuntimeBuildStats stats =
        ParticleRuntimeSystem2D::Build(particles, emitters, transforms, 2U, scratch, build_config);

    VR_REQUIRE(stats.emitted_instance_count == 2U);
    VR_REQUIRE(scratch.draw_batches.size() == 2U);
    VR_CHECK(scratch.draw_batches[0].pipeline_state != scratch.draw_batches[1].pipeline_state);
}

} // namespace
