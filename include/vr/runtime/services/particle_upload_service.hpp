#pragma once

#include "vr/particle/particle_upload_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/upload_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class ParticleUploadService final : public detail::BoundHostService<vr::particle::ParticleUploadHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService, UploadService>;
    static constexpr std::string_view Name = "ParticleUploadService";

    [[nodiscard]] vr::particle::ParticleUploadHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::particle::ParticleUploadHost& Host() const {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::particle::ParticleUploadHostStats& Stats() const noexcept {
        static const vr::particle::ParticleUploadHostStats empty{};
        return this->HostPtr() != nullptr ? this->HostPtr()->Stats() : empty;
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

