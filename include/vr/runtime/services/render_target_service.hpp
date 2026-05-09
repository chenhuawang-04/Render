#pragma once

#include "vr/render/render_target_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class RenderTargetService final : public detail::BoundHostService<vr::render::RenderTargetHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService>;
    static constexpr std::string_view Name = "RenderTargetService";

    [[nodiscard]] vr::render::RenderTargetHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::RenderTargetHost& Host() const {
        return this->RequireHost(Name);
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        if (auto* host_ = this->HostPtr()) {
            host_->BeginFrame(vr::runtime::detail::ResolveDevice(context_),
                              vr::runtime::detail::ResolveGraphicsCompleted(context_));
        }
    }
};

} // namespace vr::runtime::services
