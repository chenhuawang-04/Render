#include "Center/Memory/Container/Vector/McVector.hpp"
#include "support/bench_framework.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_runtime_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <cstdint>

namespace {

using Particle3D = vr::ecs::Particle<vr::ecs::Dim3>;
using ParticleEmitter3D = vr::ecs::ParticleEmitter<vr::ecs::Dim3>;
using ParticleEmitterSystem3D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim3>;
using ParticleRuntimeSystem3D = vr::ecs::ParticleRuntimeSystem<vr::ecs::Dim3>;
using ParticleSystem3D = vr::ecs::ParticleSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

template<typename T>
using ParticleBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

constexpr std::uint32_t k_emitter_count = 128U;

void InitializeParticleScene(ParticleBenchMcVector<Particle3D>& particles_,
                             ParticleBenchMcVector<ParticleEmitter3D>& emitters_,
                             ParticleBenchMcVector<Transform3D>& transforms_) {
    particles_.resize(k_emitter_count);
    emitters_.resize(k_emitter_count);
    transforms_.resize(k_emitter_count);

    for (std::uint32_t i = 0U; i < k_emitter_count; ++i) {
        ParticleSystem3D::Initialize(particles_[i]);
        ParticleEmitterSystem3D::Initialize(particles_[i], emitters_[i]);
        TransformSystem3D::Initialize(transforms_[i]);
        TransformSystem3D::SetLocalPosition(
            transforms_[i],
            vr::ecs::Float3{
                .x = static_cast<float>(i % 16U) * 1.5F,
                .y = static_cast<float>(i / 16U) * 0.75F,
                .z = static_cast<float>(i % 7U) * 0.5F});

        ParticleEmitterSystem3D::SetSpawnRate(particles_[i], emitters_[i], 60.0F);
        ParticleEmitterSystem3D::SetLifetimeRange(particles_[i], emitters_[i], 1.0F, 2.0F);
        ParticleEmitterSystem3D::SetSpeedRange(particles_[i], emitters_[i], 0.5F, 2.0F);
        ParticleEmitterSystem3D::SetSizeRange(particles_[i], emitters_[i], 0.1F, 0.3F, 0.05F, 0.2F);
        ParticleSystem3D::SetCapacity(particles_[i], 256U, 32U);
        ParticleEmitterSystem3D::SetSimulationSpace(particles_[i], emitters_[i], vr::ecs::ParticleSimulationSpace::world);
        ParticleSystem3D::SetFacingMode(particles_[i], vr::ecs::ParticleFacingMode::screen);
        ParticleEmitterSystem3D::SetPlayback(particles_[i], emitters_[i], true, true, false);
        ParticleEmitterSystem3D::SetEmissionShape(
            particles_[i],
            emitters_[i],
            vr::ecs::ParticleEmitterShape::sphere,
            vr::ecs::Float3{.x = 0.25F, .y = 0.25F, .z = 0.25F},
            0.5F,
            vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            0.35F,
            0.15F);
        ParticleSystem3D::SetSurfaceId(particles_[i], 1U + (i % 8U));
        ParticleSystem3D::SetAuthoringVisualResourceId(particles_[i], 1U + (i % 4U));
        ParticleSystem3D::SetBatchTag(particles_[i], i % 3U);
        ParticleSystem3D::SetDepthBin(particles_[i], static_cast<std::uint16_t>(i % 1024U));
    }

    TransformSystem3D::UpdateHierarchy(transforms_.data(), k_emitter_count);
}

VR_BENCHMARK_CASE(EcsParticleRuntimeSystem_dim3_simulate_and_build_128_emitters, "core;ecs;particle;runtime;cpu") {
    ParticleBenchMcVector<Particle3D> particles{};
    ParticleBenchMcVector<ParticleEmitter3D> emitters{};
    ParticleBenchMcVector<Transform3D> transforms{};
    InitializeParticleScene(particles, emitters, transforms);

    vr::ecs::Particle3DRuntimeScratch scratch{};
    ParticleRuntimeSystem3D::Reserve(scratch, k_emitter_count, k_emitter_count * 256U);

    vr::ecs::ParticleRuntimeBuildConfig build_config{};
    build_config.delta_time_s = 1.0F / 60.0F;
    build_config.fixed_step_s = 1.0F / 60.0F;
    build_config.max_simulation_steps = 1U;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_emitter_count - 1U);
        TransformSystem3D::SetLocalPosition(
            transforms[hot_index],
            vr::ecs::Float3{
                .x = static_cast<float>(hot_index % 16U) * 1.5F,
                .y = static_cast<float>(hot_index / 16U) * 0.75F,
                .z = static_cast<float>((hot_index + i) % 13U) * 0.25F});
        TransformSystem3D::UpdateHierarchy(transforms.data(), k_emitter_count);

        const vr::ecs::ParticleRuntimeBuildStats stats =
            ParticleRuntimeSystem3D::Build(particles.data(),
                                           emitters.data(),
                                           transforms.data(),
                                           k_emitter_count,
                                           scratch,
                                           build_config);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.emitted_instance_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.spawned_particle_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.visible_particle_count);
    }

    bench_context_.AddItems(iterations * k_emitter_count);
    bench_context_.AddBytes(
        iterations * static_cast<std::uint64_t>(scratch.instances.size()) * sizeof(vr::ecs::Particle3DGpuInstance));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace
