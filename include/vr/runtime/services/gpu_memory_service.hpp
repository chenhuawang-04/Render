#pragma once

#include "vr/resource/gpu_memory_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class GpuMemoryService final : public detail::BoundHostService<vr::resource::GpuMemoryHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "GpuMemoryService";

    [[nodiscard]] vr::resource::GpuMemoryHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::resource::GpuMemoryHost& Host() const {
        return this->RequireHost(Name);
    }
};

} // namespace vr::runtime::services
