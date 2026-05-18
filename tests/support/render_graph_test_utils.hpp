#pragma once

#include "vr/runtime/services/render_graph_runtime_service.hpp"

namespace vr::test {

template<typename RuntimeT>
[[nodiscard]] inline bool IsGraphOnlyScene3DRecordActive(RuntimeT& runtime_) {
    using Service = vr::runtime::services::RenderGraphRuntimeService;
    const auto* service = runtime_.Services().template TryGet<Service>();
    return service != nullptr &&
           service->GraphOnlyRecordPathEnabled() &&
           service->CanExecuteGraphRecord(runtime_.Context()) &&
           service->LastRecordStats().pass_count > 0U;
}

template<typename RuntimeT>
[[nodiscard]] inline bool IsGraphOnlyScene2DRecordActive(RuntimeT& runtime_) {
    using Service = vr::runtime::services::RenderGraphRuntimeService;
    const auto* service = runtime_.Services().template TryGet<Service>();
    return service != nullptr &&
           service->GraphOnlyRecordPathEnabled() &&
           service->CanExecuteGraphRecord(runtime_.Context()) &&
           service->LastRecordStats().pass_count > 0U;
}

} // namespace vr::test
