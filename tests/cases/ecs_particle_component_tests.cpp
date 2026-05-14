#include "support/test_framework.hpp"
#include "vr/ecs/component/particle_component.hpp"
#include "vr/ecs/component/particle_emitter_component.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_system.hpp"

#include <type_traits>

namespace {

using Particle2D = vr::ecs::Particle<vr::ecs::Dim2>;
using Particle3D = vr::ecs::Particle<vr::ecs::Dim3>;
using ParticleEmitter2D = vr::ecs::ParticleEmitter<vr::ecs::Dim2>;
using ParticleEmitter3D = vr::ecs::ParticleEmitter<vr::ecs::Dim3>;
using ParticleEmitterSystem2D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim2>;
using ParticleSystem2D = vr::ecs::ParticleSystem<vr::ecs::Dim2>;
using ParticleSystem3D = vr::ecs::ParticleSystem<vr::ecs::Dim3>;

VR_TEST_CASE(EcsParticleComponent_is_pure_pod, "unit;core;ecs;particle") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::ParticleRuntimeRoute>);
    VR_CHECK(std::is_trivial_v<vr::ecs::ParticleRuntimeRoute>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::ParticleEmitterCommon>);
    VR_CHECK(std::is_trivial_v<vr::ecs::ParticleEmitterCommon>);
    VR_CHECK(std::is_standard_layout_v<ParticleEmitter2D>);
    VR_CHECK(std::is_trivial_v<ParticleEmitter2D>);
    VR_CHECK(std::is_standard_layout_v<Particle2D>);
    VR_CHECK(std::is_trivial_v<Particle2D>);
    VR_CHECK(std::is_standard_layout_v<Particle3D>);
    VR_CHECK(std::is_trivial_v<Particle3D>);
}

VR_TEST_CASE(EcsParticleSystem_dim2_defaults_and_emitter_setters_mark_dirty, "unit;core;ecs;particle") {
    Particle2D component{};
    ParticleEmitter2D emitter{};
    ParticleSystem2D::Initialize(component);
    ParticleEmitterSystem2D::Initialize(component, emitter);

    VR_CHECK(component.style.layer == 0);
    VR_CHECK(component.style.render_mode == vr::ecs::ParticleRenderMode::axis_aligned);
    VR_CHECK(component.style.max_particles == 256U);
    VR_CHECK(component.runtime.route.pass_hint == vr::ecs::ParticleRenderPassHint::transparent);
    VR_CHECK(vr::ecs::ParticleSystem<vr::ecs::Dim2>::HasDirtyFlags(
        component,
        vr::ecs::particle_dirty_style_flag |
            vr::ecs::particle_dirty_emitter_flag |
            vr::ecs::particle_dirty_runtime_flag |
            vr::ecs::particle_dirty_simulation_flag));

    const std::uint32_t authoring_revision_before = component.runtime.revision_authoring;
    ParticleSystem2D::ClearDirtyFlags(component, 0xFFFFFFFFU);
    ParticleEmitterSystem2D::SetSpawnRate(component, emitter, 128.0F);
    ParticleEmitterSystem2D::SetLifetimeRange(component, emitter, 0.25F, 1.25F);
    ParticleSystem2D::SetCapacity(component, 512U, 32U);
    ParticleSystem2D::SetLayer(component, 42);
    ParticleSystem2D::SetTextureId(component, 77U);

    VR_CHECK(emitter.config.spawn_rate == 128.0F);
    VR_CHECK(emitter.config.lifetime_min_s == 0.25F);
    VR_CHECK(emitter.config.lifetime_max_s == 1.25F);
    VR_CHECK(component.style.max_particles == 512U);
    VR_CHECK(component.style.max_alive_per_frame == 32U);
    VR_CHECK(component.style.layer == 42);
    VR_CHECK(component.runtime.route.texture_id == 77U);
    VR_CHECK(component.runtime.revision_authoring > authoring_revision_before);
    VR_CHECK(ParticleSystem2D::HasDirtyFlags(
        component,
        vr::ecs::particle_dirty_emitter_flag |
            vr::ecs::particle_dirty_runtime_flag |
            vr::ecs::particle_dirty_simulation_flag));
}

VR_TEST_CASE(EcsParticleSystem_dim3_sort_key_and_shadow_flags, "unit;core;ecs;particle") {
    Particle3D component{};
    ParticleSystem3D::Initialize(component);
    ParticleSystem3D::ClearDirtyFlags(component, 0xFFFFFFFFU);

    ParticleSystem3D::SetVisualResourceId(component, 9U);
    ParticleSystem3D::SetTextureId(component, 17U);
    ParticleSystem3D::SetBatchTag(component, 3U);
    ParticleSystem3D::SetDepthBin(component, 100U);
    ParticleSystem3D::SetShadowFlags(component, true, true);
    ParticleSystem3D::RebuildSortKey(component);

    VR_CHECK(component.style.receive_shadow == 1U);
    VR_CHECK(component.runtime.route.cast_shadow == 1U);
    VR_CHECK(component.runtime.route.sort_key != 0U);
    VR_CHECK(ParticleSystem3D::ExtractVisualResourceBucket(component.runtime.route.sort_key) == 9U);
    VR_CHECK(ParticleSystem3D::ExtractTextureBucket(component.runtime.route.sort_key) == 17U);
}

} // namespace

