#pragma once

#include "vr/text/freetype_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class FreeTypeService final : public detail::BoundHostService<vr::text::FreeTypeHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "FreeTypeService";

    [[nodiscard]] vr::text::FreeTypeHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::text::FreeTypeHost& Host() const {
        return this->RequireHost(Name);
    }
};

} // namespace vr::runtime::services
