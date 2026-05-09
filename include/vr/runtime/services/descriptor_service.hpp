#pragma once

#include "vr/render/descriptor_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class DescriptorService final : public detail::BoundHostService<vr::render::DescriptorHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "DescriptorService";

    [[nodiscard]] vr::render::DescriptorHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::DescriptorHost& Host() const {
        return this->RequireHost(Name);
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        if (auto* host_ = this->HostPtr()) {
            host_->BeginFrame(vr::runtime::detail::ResolveDevice(context_),
                              vr::runtime::detail::ResolveFrameIndex(context_));
        }
    }
};

} // namespace vr::runtime::services
