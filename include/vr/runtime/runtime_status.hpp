#pragma once

#include "vr/render/render_loop_host.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/runtime/runtime_execution.hpp"

#include <cstdint>

namespace vr::runtime {

enum class RuntimeStatusCode : std::uint8_t {
    Ok = 0U,
    WindowHidden = 1U,
    SwapchainRecreated = 2U,
    DeviceLost = 3U,
    OutOfMemory = 4U,
    InvalidState = 5U,
    ServiceDependencyMissing = 6U,
    UnsupportedCapability = 7U,
};

struct RuntimeTickResult final {
    RuntimeStatusCode code = RuntimeStatusCode::Ok;
    std::uint64_t frame_id = 0U;
    std::uint32_t frame_index = 0U;
    std::uint32_t image_index = 0U;
    bool running = true;

    std::uint32_t compiled_pipeline_count = 0U;
    std::uint32_t pending_graphics_compile_count = 0U;
    std::uint32_t pending_compute_compile_count = 0U;
    bool upload_submitted = false;
    bool upload_cross_queue_wait = false;
    bool events_polled = false;

    vr::render::TickResult render{};
    RuntimeFrameDiagnosticsV2 diagnostics{};
    RuntimeExecutionTrace execution{};
};

[[nodiscard]] constexpr RuntimeStatusCode ToRuntimeStatusCode(
    const vr::render::TickCode code_) noexcept {
    switch (code_) {
    case vr::render::TickCode::Submitted:
        return RuntimeStatusCode::Ok;
    case vr::render::TickCode::SkippedWindowHidden:
        return RuntimeStatusCode::WindowHidden;
    case vr::render::TickCode::RecreateRequested:
        return RuntimeStatusCode::SwapchainRecreated;
    default:
        return RuntimeStatusCode::InvalidState;
    }
}

} // namespace vr::runtime
