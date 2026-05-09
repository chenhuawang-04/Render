#pragma once

#include "vr/particle/particle_simulation_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"
#include "vr/runtime/services/upload_service.hpp"
#include "vr/runtime/services/bound_host_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class ParticleSimulationService final : public detail::BoundHostService<vr::particle::ParticleSimulationHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<
        GpuMemoryService,
        UploadService,
        DescriptorService,
        PipelineService>;
    static constexpr std::string_view Name = "ParticleSimulationService";

    [[nodiscard]] vr::particle::ParticleSimulationHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::particle::ParticleSimulationHost& Host() const {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::particle::ParticleSimulationHostCapabilities& Capabilities() const {
        return Host().Capabilities();
    }

    [[nodiscard]] const vr::particle::ParticleSimulationHostStats& Stats() const noexcept {
        static const vr::particle::ParticleSimulationHostStats empty{};
        return this->HostPtr() != nullptr ? this->HostPtr()->Stats() : empty;
    }

    [[nodiscard]] bool SupportsGpuSimulation() const noexcept {
        return this->HostPtr() != nullptr && this->HostPtr()->Capabilities().SupportsGpuSimulation();
    }

    [[nodiscard]] bool SupportsHybridSimulation() const noexcept {
        return this->HostPtr() != nullptr && this->HostPtr()->Capabilities().SupportsHybridSimulation();
    }

    [[nodiscard]] bool HasComputeTimelineProgress() const noexcept {
        return this->HostPtr() != nullptr && this->HostPtr()->HasComputeTimelineProgress();
    }

    [[nodiscard]] std::uint64_t LastSubmittedValue() const noexcept {
        return this->HostPtr() != nullptr ? this->HostPtr()->LastSubmittedValue() : 0U;
    }

    [[nodiscard]] std::uint64_t CompletedSubmitValue() const noexcept {
        return this->HostPtr() != nullptr ? this->HostPtr()->CompletedSubmitValue() : 0U;
    }

    [[nodiscard]] std::uint64_t NextSignalValue() const noexcept {
        return this->HostPtr() != nullptr ? this->HostPtr()->NextSignalValue() : 1U;
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        if (auto* host_ = this->HostPtr()) {
            host_->BeginFrame(vr::runtime::detail::ResolveDevice(context_),
                              vr::runtime::detail::ResolveFrameIndex(context_),
                              vr::runtime::detail::ResolveGraphicsSubmitted(context_),
                              vr::runtime::detail::ResolveGraphicsCompleted(context_));
        }
    }
};

} // namespace vr::runtime::services
