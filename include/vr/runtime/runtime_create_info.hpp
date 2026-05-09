#pragma once

#include "vr/platform/render_host.hpp"
#include "vr/render/render_runtime_host.hpp"

#include <cstdint>

namespace vr::runtime {

using RuntimeModulesCreateInfo = vr::render::RuntimeModulesCreateInfo;

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimeCreateInfo =
    typename vr::render::RenderRuntimeHost<BackendTagT, frames_in_flight_v>::CreateInfo;

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimePipelineWarmupCreateInfo =
    typename vr::render::RenderRuntimeHost<BackendTagT, frames_in_flight_v>::PipelineWarmupCreateInfo;

} // namespace vr::runtime
