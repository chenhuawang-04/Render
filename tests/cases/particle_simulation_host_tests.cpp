#include "support/test_framework.hpp"
#include "vr/particle/particle_simulation_host.hpp"

#include <type_traits>

VR_TEST_CASE(ParticleSimulationHost_gpu_structs_are_pod,
             "unit;particle;simulation") {
    VR_CHECK(std::is_standard_layout_v<vr::particle::ParticleGpuStateRecord>);
    VR_CHECK(std::is_trivial_v<vr::particle::ParticleGpuStateRecord>);
    VR_CHECK(std::is_standard_layout_v<vr::particle::ParticleGpuSpawnPacket>);
    VR_CHECK(std::is_trivial_v<vr::particle::ParticleGpuSpawnPacket>);
    VR_CHECK(std::is_standard_layout_v<vr::particle::ParticleGpuSortKeyRecord>);
    VR_CHECK(std::is_trivial_v<vr::particle::ParticleGpuSortKeyRecord>);
    VR_CHECK(std::is_standard_layout_v<vr::particle::ParticleSimulationFrameResources>);
    VR_CHECK(std::is_trivially_copyable_v<vr::particle::ParticleSimulationFrameResources>);
    VR_CHECK(std::is_standard_layout_v<vr::particle::ParticleSimulationBufferView>);
    VR_CHECK(std::is_trivially_copyable_v<vr::particle::ParticleSimulationBufferView>);
}

VR_TEST_CASE(ParticleSimulationHost_resolve_simulation_path_respects_capabilities,
             "unit;particle;simulation") {
    vr::particle::ParticleSimulationHostCapabilities cpu_only{};
    VR_CHECK(vr::particle::ParticleSimulationHost::ResolveSimulationPath(
                 vr::ecs::ParticleSimulationMode::cpu,
                 cpu_only) == vr::particle::ParticleSimulationResolvedPath::cpu);
    VR_CHECK(vr::particle::ParticleSimulationHost::ResolveSimulationPath(
                 vr::ecs::ParticleSimulationMode::gpu,
                 cpu_only) == vr::particle::ParticleSimulationResolvedPath::cpu);
    VR_CHECK(vr::particle::ParticleSimulationHost::ResolveSimulationPath(
                 vr::ecs::ParticleSimulationMode::hybrid_gpu,
                 cpu_only) == vr::particle::ParticleSimulationResolvedPath::cpu);

    vr::particle::ParticleSimulationHostCapabilities gpu_caps{};
    gpu_caps.compute_queue_available = true;
    gpu_caps.synchronization2 = true;
    VR_CHECK(vr::particle::ParticleSimulationHost::ResolveSimulationPath(
                 vr::ecs::ParticleSimulationMode::gpu,
                 gpu_caps) == vr::particle::ParticleSimulationResolvedPath::gpu);
    VR_CHECK(vr::particle::ParticleSimulationHost::ResolveSimulationPath(
                 vr::ecs::ParticleSimulationMode::hybrid_gpu,
                 gpu_caps) == vr::particle::ParticleSimulationResolvedPath::hybrid_gpu);
}

