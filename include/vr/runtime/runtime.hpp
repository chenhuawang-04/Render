#pragma once

#include "vr/runtime/runtime_kernel.hpp"
#include "vr/runtime/runtime_context.hpp"
#include "vr/runtime/runtime_create_info.hpp"
#include "vr/runtime/runtime_status.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace vr::runtime {

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
class Runtime final {
public:
    using BackendTag = BackendTagT;
    using CreateInfo = RuntimeCreateInfo<BackendTag, frames_in_flight_v>;
    using PipelineWarmupCreateInfo = RuntimePipelineWarmupCreateInfo<BackendTag, frames_in_flight_v>;
    using DefaultProfile = profiles::Runtime3DProfile;
    using RuntimeServicesType = RuntimeServices<DefaultProfile>;
    using KernelType = RuntimeKernel<BackendTag, frames_in_flight_v>;
    using RuntimeFrameType = typename KernelType::FrameType;
    using FrameContext = RuntimeFrameContext<DefaultProfile, BackendTag, frames_in_flight_v>;
    using TickResult = RuntimeTickResult;
    using RuntimeTickResult = TickResult;

private:
    using InternalRuntimeHost = detail::RuntimeHost<BackendTag, frames_in_flight_v>;

public:

#include "vr/runtime/detail/runtime_facade_lifecycle.ipp"

#include "vr/runtime/detail/runtime_facade_accessors.ipp"

private:
#include "vr/runtime/detail/runtime_facade_tick_detail.ipp"

    InternalRuntimeHost host{};
    KernelType kernel{};
    RuntimeExecutionTrace last_execution_trace{};
};

} // namespace vr::runtime

