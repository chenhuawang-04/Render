#pragma once

#include "vr/render/render_target_pool.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class RenderTargetPoolService final : public detail::BoundHostService<vr::render::RenderTargetPool> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService, RenderTargetService>;
    static constexpr std::string_view Name = "RenderTargetPoolService";

    [[nodiscard]] vr::render::RenderTargetPool& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::RenderTargetPool& Host() const {
        return this->RequireHost(Name);
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        if (auto* host_ = this->HostPtr()) {
            host_->BeginFrame(vr::runtime::detail::ResolveFrameIndex(context_),
                              vr::runtime::detail::ResolveGraphicsCompleted(context_));
        }
    }

    template<typename ContextT>
    void EndFrame(ContextT& context_) {
        if (auto* host_ = this->HostPtr()) {
            host_->EndFrame(vr::runtime::detail::ResolveFrameIndex(context_),
                            vr::runtime::detail::ResolveGraphicsSubmitted(context_));
        }
    }
};

} // namespace vr::runtime::services
