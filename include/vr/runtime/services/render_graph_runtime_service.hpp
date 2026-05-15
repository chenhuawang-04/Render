#pragma once

#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"

#include <string_view>

namespace vr::runtime::services {

class RenderGraphRuntimeService final {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "RenderGraphRuntimeService";

    template<typename ContextT>
    void BeginFrame(ContextT&) noexcept {}

    template<typename ContextT>
    void PrepareFrame(ContextT&) noexcept {}

    template<typename ContextT>
    void PreRecord(ContextT&) noexcept {}

    template<typename ContextT>
    void Retire(ContextT&) noexcept {}
};

} // namespace vr::runtime::services
