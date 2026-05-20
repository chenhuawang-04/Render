#pragma once

#include "vr/runtime/services/render_graph_runtime_service.hpp"

namespace vr::test {

template<typename RuntimeT>
[[nodiscard]] inline bool IsGraphOnlyScene3DRecordActive(RuntimeT& runtime_) {
    using Service = vr::runtime::services::RenderGraphRuntimeService;
    const auto* service = runtime_.Services().template TryGet<Service>();
    return service != nullptr &&
           service->LastDiagnostics().graph_only_active;
}

template<typename RuntimeT>
[[nodiscard]] inline bool IsGraphOnlyScene2DRecordActive(RuntimeT& runtime_) {
    using Service = vr::runtime::services::RenderGraphRuntimeService;
    const auto* service = runtime_.Services().template TryGet<Service>();
    return service != nullptr &&
           service->LastDiagnostics().graph_only_active;
}

} // namespace vr::test
