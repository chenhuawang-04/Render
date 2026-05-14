#pragma once

#include "vr/render/ibl_bake_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/ibl_service.hpp"
#include "vr/runtime/services/texture_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class IblBakeService final : public detail::BoundHostService<vr::render::IblBakeHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<TextureService, IblService>;
    static constexpr std::string_view Name = "IblBakeService";

    [[nodiscard]] vr::render::IblBakeHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::IblBakeHost& Host() const {
        return this->RequireHost(Name);
    }
};

} // namespace vr::runtime::services

