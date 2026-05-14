#pragma once

#include "vr/render/ibl_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/texture_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class IblService final : public detail::BoundHostService<vr::render::IblHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<TextureService, DescriptorService, SamplerService>;
    static constexpr std::string_view Name = "IblService";

    [[nodiscard]] vr::render::IblHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::IblHost& Host() const {
        return this->RequireHost(Name);
    }
};

} // namespace vr::runtime::services

