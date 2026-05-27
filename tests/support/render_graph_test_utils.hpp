#pragma once

#include "vr/render_graph/compiled_render_graph.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <string_view>

namespace vr::test {

template<typename RuntimeT>
[[nodiscard]] inline bool IsGraphOnlyScene3DRecordActive(RuntimeT& runtime_) {
    using Service = vr::runtime::services::RenderGraphRuntimeService;
    const auto* service = runtime_.Services().template TryGet<Service>();
    return service != nullptr &&
           service->IsGraphOnlyRecordActive(runtime_.Context());
}

template<typename RuntimeT>
[[nodiscard]] inline bool IsGraphOnlyScene2DRecordActive(RuntimeT& runtime_) {
    using Service = vr::runtime::services::RenderGraphRuntimeService;
    const auto* service = runtime_.Services().template TryGet<Service>();
    return service != nullptr &&
           service->IsGraphOnlyRecordActive(runtime_.Context());
}

[[nodiscard]] inline const render_graph::CompiledPass* FindCompiledPassByName(
    const render_graph::CompiledRenderGraph& graph_,
    std::string_view debug_name_) noexcept {
    for (const auto& pass_ : graph_.Passes()) {
        if (pass_.debug_name == debug_name_) {
            return &pass_;
        }
    }
    return nullptr;
}

} // namespace vr::test
