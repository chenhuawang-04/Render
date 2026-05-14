#include "support/test_framework.hpp"
#include "vr/render/frame_composer_host.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

[[nodiscard]] std::string ToLower(std::string_view value_) {
    std::string lowered{};
    lowered.reserve(value_.size());
    for (const unsigned char ch : value_) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view text_,
                                           std::string_view needle_) {
    const std::string lowered_text = ToLower(text_);
    const std::string lowered_needle = ToLower(needle_);
    return lowered_text.find(lowered_needle) != std::string::npos;
}

[[nodiscard]] bool IsEnvironmentSkipError(std::string_view message_) {
    constexpr std::array<std::string_view, 14U> patterns{
        "sdl_initsubsystem",
        "sdl_createwindow",
        "sdl_vulkan_getinstanceextensions",
        "sdl_vulkan_createsurface",
        "vkcreateinstance",
        "vkenumeratephysicaldevices",
        "no vulkan physical devices found",
        "no suitable vulkan physical device found",
        "missing required instance extension",
        "vkcreatedevice",
        "vkgetphysicaldevicesurfacesupportkhr",
        "vkgetphysicaldevicesurfaceformatskhr",
        "dynamicrendering",
        "synchronization2"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

class FrameComposerRecorder final {
public:
    void PrepareFrame(const vr::render::FrameComposerPrepareView& prepare_view_) {
        prepared = composer->PrepareFrame(prepare_view_);
        if (prepared) {
            captured_targets = composer->Targets(prepare_view_.frame.frame_index);
        }
        ++prepare_count;
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        if (composer == nullptr) {
            throw std::runtime_error("FrameComposerRecorder::Record called before binding composer");
        }
        composer->RecordTonemapPass(record_context_);
        ++record_count;
    }

    void Bind(vr::render::FrameComposerHost& composer_) noexcept {
        composer = &composer_;
    }

    vr::render::FrameComposerHost* composer = nullptr;
    vr::render::FrameComposerTargets captured_targets{};
    std::uint32_t prepare_count = 0U;
    std::uint32_t record_count = 0U;
    bool prepared = false;
};

VR_TEST_CASE(RuntimeIntegration_frame_composer_prepare_and_tonemap_smoke,
             "integration;gpu;sdl;runtime;frame_composer") {
    Runtime runtime{};
    FrameComposerRecorder recorder{};
    bool runtime_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_frame_composer";
        create_info.platform.window.width = 320;
        create_info.platform.window.height = 240;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::CountersOnly;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        VR_REQUIRE(runtime.HasFrameComposerHost());
        recorder.Bind(runtime.FrameComposer());

        const auto first_tick = runtime.Tick(recorder);
        VR_CHECK(first_tick.running);
        VR_CHECK(recorder.prepared);
        VR_CHECK(recorder.prepare_count == 1U);
        VR_CHECK(recorder.record_count == 1U);
        VR_CHECK(recorder.captured_targets.ready);
        VR_CHECK(vr::render::IsValidRenderTargetHandle(recorder.captured_targets.hdr_color_target));
        VR_CHECK(vr::render::IsValidRenderTargetHandle(recorder.captured_targets.depth_target));
        VR_CHECK(first_tick.diagnostics.collected);
        VR_CHECK(first_tick.diagnostics.frame_composer.prepared_frame_count >= 1U);
        VR_CHECK(first_tick.diagnostics.frame_composer.tonemap_record_count >= 1U);

        const auto second_tick = runtime.Tick(recorder);
        VR_CHECK(second_tick.running);
        VR_CHECK(second_tick.diagnostics.frame_composer.prepared_frame_count >= 2U);
        VR_CHECK(second_tick.diagnostics.frame_composer.ready_frame_count >= 2U);
        VR_CHECK(second_tick.diagnostics.frame_composer.tonemap_record_count >= 2U);

        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace

