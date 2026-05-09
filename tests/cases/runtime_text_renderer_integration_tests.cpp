#include "support/test_framework.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/text/text_renderer_2d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using RuntimeRoot = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

[[nodiscard]] std::string FindTestFontPath() {
    namespace fs = std::filesystem;

    constexpr std::array<const char*, 6U> candidate_paths{
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/msyh.ttc"
    };

    for (const char* path : candidate_paths) {
        const fs::path candidate(path);
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
    }
    return {};
}

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
    constexpr std::array<std::string_view, 17U> patterns{
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
        "vkgetphysicaldevicesurfacepresentmodeskhr",
        "dynamicrendering",
        "synchronization2",
        "ft_new_face",
        "freetypehost::registerface"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

void InitializeTextComponent(Text2D& component_,
                             std::uint32_t font_id_,
                             std::uint32_t material_id_,
                             std::int16_t layer_,
                             std::string_view text_) {
    TextSystem2D::Initialize(component_);
    TextSystem2D::SetRuntimeRoute(component_, font_id_, material_id_, 0U, 0U);
    TextSystem2D::SetLayer(component_, layer_);
    TextSystem2D::SetColor(component_, vr::ecs::Rgba8{255U, 255U, 255U, 255U});
    TextSystem2D::SetOutlineEnabled(component_, true);
    TextSystem2D::SetOutlineWidthPx(component_, 1U);
    TextSystem2D::SetOutlineColor(component_, vr::ecs::Rgba8{14U, 16U, 23U, 255U});
    (void)TextSystem2D::SetText(component_, text_);
}

VR_TEST_CASE(RuntimeIntegration_text_renderer_2d_end_to_end_smoke, "integration;gpu;sdl;runtime;text") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for runtime text renderer integration test.");
    }

    Runtime runtime{};
    vr::text::TextRenderer2D text_renderer{};
    bool runtime_initialized = false;
    bool renderer_initialized = false;

    std::array<Text2D, 3U> text_components{};
    InitializeTextComponent(text_components[0U], 1U, 1U, 0, "Melosyne Runtime Text Integration");
    TextSystem2D::SetPixelSize(text_components[0U], 36.0F);

    InitializeTextComponent(text_components[1U], 1U, 1U, 1, "Glyph atlas + upload + dynamic rendering");
    TextSystem2D::SetPixelSize(text_components[1U], 22.0F);

    InitializeTextComponent(text_components[2U], 1U, 1U, 2, "Frame: 0");
    TextSystem2D::SetPixelSize(text_components[2U], 28.0F);
    TextSystem2D::SetColor(text_components[2U], vr::ecs::Rgba8{190U, 240U, 160U, 255U});

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_text_smoke";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;

        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, base_face_id);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 8192U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 2U * 1024U * 1024U;
        text_renderer_create_info.clear_swapchain = true;
        text_renderer.Initialize(text_renderer_create_info);
        renderer_initialized = true;
        text_renderer.SetComponents(text_components.data(),
                                    static_cast<std::uint32_t>(text_components.size()));

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_glyph_quads = 0U;
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_draw_calls = 0U;

        constexpr std::uint32_t max_ticks = 18U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            char frame_text[64]{};
            std::snprintf(frame_text, sizeof(frame_text), "Frame: %u", tick_index);
            (void)TextSystem2D::SetText(text_components[2U], frame_text);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(text_renderer);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const vr::text::TextRenderer2DStats& stats = text_renderer.Stats();
            max_glyph_quads = std::max(max_glyph_quads, stats.glyph_quad_count);
            max_draw_batches = std::max(max_draw_batches, stats.draw_batch_count);
            max_draw_calls = std::max(max_draw_calls, stats.draw_call_count);
            VR_CHECK(stats.descriptor_set_update_count <= stats.draw_batch_count);
            VR_CHECK(stats.descriptor_set_bind_count <= stats.draw_call_count);

            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_glyph_quads > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(runtime.GlyphUpload().Stats().uploaded_rect_count > 0U);

        text_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            text_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
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

VR_TEST_CASE(RuntimeIntegration_runtime_root_text_renderer_2d_smoke,
             "integration;gpu;sdl;runtime;text") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for runtime root 2D text integration test.");
    }

    RuntimeRoot runtime{};
    vr::text::TextRenderer2D text_renderer{};
    bool runtime_initialized = false;
    bool renderer_initialized = false;

    Text2D text_component{};
    InitializeTextComponent(text_component, 1U, 1U, 0, "Runtime root text smoke");
    TextSystem2D::SetPixelSize(text_component, 32.0F);

    try {
        RuntimeRoot::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_root_text_smoke";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, base_face_id);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.reserve_component_count = 1U;
        text_renderer_create_info.reserve_glyph_count = 2048U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 512U * 1024U;
        text_renderer_create_info.clear_swapchain = false;
        text_renderer.Initialize(text_renderer_create_info);
        renderer_initialized = true;
        text_renderer.SetComponents(&text_component, 1U);

        const RuntimeRoot::TickResult tick_result = runtime.Tick(text_renderer);
        VR_REQUIRE(tick_result.code == vr::runtime::RuntimeStatusCode::Ok ||
                   tick_result.code == vr::runtime::RuntimeStatusCode::SwapchainRecreated);
        VR_CHECK(tick_result.frame_id > 0U);
        VR_CHECK(tick_result.frame_index < 2U);
        VR_CHECK(tick_result.diagnostics.level == vr::runtime::DiagnosticsLevel::Off);
        VR_CHECK(runtime.Kernel().Clock().frame_id == tick_result.frame_id);
        VR_CHECK(runtime.Frames().CurrentFrameIndex() < runtime.Frames().FramesInFlight());
        VR_CHECK(runtime.Commands().Stats(tick_result.frame_index).frame_slot_count == 2U);
        VR_CHECK(tick_result.execution.last_completed_stage ==
                 vr::runtime::RuntimeExecutionStage::Diagnostics);
        VR_CHECK(tick_result.execution.completed_stage_count >= 2U);
        VR_CHECK(runtime.LastExecutionTrace().last_completed_stage ==
                 vr::runtime::RuntimeExecutionStage::Diagnostics);

        const RuntimeRoot::FrameContext frame_context = runtime.BuildFrameContext();
        const bool has_cross_queue_upload_timeline =
            runtime.Upload().UsesCrossQueueSubmit() && tick_result.upload_cross_queue_wait;
        VR_CHECK(frame_context.frame.frame_id == tick_result.frame_id);
        VR_CHECK(frame_context.frame.frame_index == tick_result.frame_index);
        VR_CHECK(frame_context.frame.image_index == tick_result.image_index);
        VR_CHECK(frame_context.progress.graphics_submitted == runtime.Frames().LastSubmittedValue());
        VR_CHECK(frame_context.progress.graphics_completed == runtime.Frames().CompletedSubmitValue());
        if (has_cross_queue_upload_timeline) {
            VR_CHECK(frame_context.progress.transfer_submitted == runtime.Upload().LastSubmittedValue());
            VR_CHECK(frame_context.progress.transfer_completed == runtime.Upload().CompletedSubmitValue());
        } else {
            VR_CHECK(frame_context.progress.transfer_submitted == 0U);
            VR_CHECK(frame_context.progress.transfer_completed == 0U);
        }
        VR_CHECK(frame_context.progress.compute_submitted == 0U);
        VR_CHECK(frame_context.progress.compute_completed == 0U);
        VR_CHECK(frame_context.timelines.graphics.IsAvailable());
        VR_CHECK(frame_context.timelines.graphics.submitted_value ==
                 frame_context.progress.graphics_submitted);
        VR_CHECK(frame_context.timelines.graphics.completed_value ==
                 frame_context.progress.graphics_completed);
        VR_CHECK(frame_context.timelines.Get(vr::runtime::QueueKind::graphics).submitted_value ==
                 frame_context.progress.graphics_submitted);
        if (has_cross_queue_upload_timeline) {
            VR_CHECK(frame_context.timelines.transfer.IsAvailable());
            VR_CHECK(frame_context.timelines.transfer.submitted_value ==
                     frame_context.progress.transfer_submitted);
            VR_CHECK(frame_context.timelines.transfer.completed_value ==
                     frame_context.progress.transfer_completed);
        } else {
            VR_CHECK(frame_context.timelines.Get(vr::runtime::QueueKind::transfer).submitted_value == 0U);
        }
        VR_CHECK(frame_context.timelines.Get(vr::runtime::QueueKind::compute).submitted_value == 0U);
        VR_CHECK(!runtime.ParticleSimulationService().HasComputeTimelineProgress());
        VR_CHECK(runtime.ParticleSimulationService().LastSubmittedValue() == 0U);
        VR_CHECK(runtime.ParticleSimulationService().CompletedSubmitValue() == 0U);
        const auto graphics_dependency = runtime.Kernel().BuildGraphicsDependency();
        VR_CHECK(graphics_dependency.source_queue == vr::runtime::QueueKind::graphics);
        VR_CHECK(graphics_dependency.value == frame_context.timelines.graphics.submitted_value);
        const auto transfer_dependency = runtime.Kernel().BuildTransferDependency();
        VR_CHECK(transfer_dependency.source_queue == vr::runtime::QueueKind::transfer);
        if (has_cross_queue_upload_timeline) {
            VR_CHECK(transfer_dependency.value == frame_context.timelines.transfer.submitted_value);
        } else {
            VR_CHECK(transfer_dependency.value == 0U);
        }
        const auto compute_dependency = runtime.Kernel().BuildComputeDependency();
        VR_CHECK(compute_dependency.source_queue == vr::runtime::QueueKind::compute);
        VR_CHECK(compute_dependency.value == 0U);
        VR_CHECK(&frame_context.services == &runtime.Services());
        VR_CHECK(&frame_context.kernel == &runtime.Kernel());
        VR_CHECK(&frame_context.commands == &runtime.Commands());
        VR_CHECK(frame_context.commands.Stats(tick_result.frame_index).frame_slot_count == 2U);
        VR_REQUIRE(text_renderer.Stats().draw_call_count > 0U);
        VR_REQUIRE(runtime.Pipeline().Stats().graphics_pipeline_count > 0U);

        text_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            text_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
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

VR_TEST_CASE(RuntimeIntegration_pipeline_cache_save_load_smoke, "integration;gpu;sdl;runtime;pipeline") {
    namespace fs = std::filesystem;

    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for pipeline cache smoke test.");
    }

    const fs::path cache_path = fs::path("build") / "test_artifacts" / "runtime_pipeline_cache_smoke.bin";
    std::error_code filesystem_error{};
    fs::create_directories(cache_path.parent_path(), filesystem_error);
    fs::remove(cache_path, filesystem_error);

    auto run_text_frame_and_save = [&](Runtime& runtime_,
                                       vr::text::TextRenderer2D& text_renderer_,
                                       bool& runtime_initialized_,
                                       bool& renderer_initialized_) {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_pipeline_cache_smoke";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.pipeline.enable_pipeline_cache = true;
        runtime_.Initialize(create_info);
        runtime_initialized_ = true;

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime_.FreeType().RegisterFace(face_create_info);
        runtime_.GlyphAtlas().MapFont(1U, base_face_id);

        Text2D text_component{};
        InitializeTextComponent(text_component, 1U, 1U, 0, "Pipeline cache smoke");
        TextSystem2D::SetPixelSize(text_component, 34.0F);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.reserve_component_count = 1U;
        text_renderer_create_info.reserve_glyph_count = 2048U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 512U * 1024U;
        text_renderer_create_info.clear_swapchain = false;
        text_renderer_.Initialize(text_renderer_create_info);
        renderer_initialized_ = true;
        text_renderer_.SetComponents(&text_component, 1U);

        const Runtime::RuntimeTickResult tick_result = runtime_.Tick(text_renderer_);
        VR_REQUIRE(tick_result.render.code == vr::render::TickCode::Submitted ||
                   tick_result.render.code == vr::render::TickCode::RecreateRequested);
        VR_REQUIRE(text_renderer_.Stats().draw_call_count > 0U);
        VR_REQUIRE(runtime_.Pipeline().Stats().graphics_pipeline_count > 0U);
    };

    Runtime runtime_save{};
    vr::text::TextRenderer2D text_renderer_save{};
    bool runtime_save_initialized = false;
    bool renderer_save_initialized = false;

    Runtime runtime_load{};
    vr::text::TextRenderer2D text_renderer_load{};
    bool runtime_load_initialized = false;
    bool renderer_load_initialized = false;

    try {
        run_text_frame_and_save(runtime_save,
                                text_renderer_save,
                                runtime_save_initialized,
                                renderer_save_initialized);

        VR_REQUIRE(runtime_save.Pipeline().SavePipelineCacheToFile(runtime_save.Context(),
                                                                   cache_path.string().c_str()));
        VR_REQUIRE(fs::exists(cache_path));
        VR_REQUIRE(fs::file_size(cache_path) > 0U);

        text_renderer_save.Shutdown(runtime_save.Context());
        renderer_save_initialized = false;
        runtime_save.Shutdown();
        runtime_save_initialized = false;

        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_pipeline_cache_load";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.pipeline.enable_pipeline_cache = true;
        runtime_load.Initialize(create_info);
        runtime_load_initialized = true;

        VR_REQUIRE(runtime_load.Pipeline().LoadPipelineCacheFromFile(runtime_load.Context(),
                                                                     cache_path.string().c_str()));

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime_load.FreeType().RegisterFace(face_create_info);
        runtime_load.GlyphAtlas().MapFont(1U, base_face_id);

        Text2D text_component{};
        InitializeTextComponent(text_component, 1U, 1U, 0, "Pipeline cache reload");
        TextSystem2D::SetPixelSize(text_component, 34.0F);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.reserve_component_count = 1U;
        text_renderer_create_info.reserve_glyph_count = 2048U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 512U * 1024U;
        text_renderer_create_info.clear_swapchain = false;
        text_renderer_load.Initialize(text_renderer_create_info);
        renderer_load_initialized = true;
        text_renderer_load.SetComponents(&text_component, 1U);

        const Runtime::RuntimeTickResult tick_result = runtime_load.Tick(text_renderer_load);
        VR_REQUIRE(tick_result.render.code == vr::render::TickCode::Submitted ||
                   tick_result.render.code == vr::render::TickCode::RecreateRequested);
        VR_REQUIRE(text_renderer_load.Stats().draw_call_count > 0U);
        VR_REQUIRE(runtime_load.Pipeline().Stats().graphics_pipeline_count > 0U);

        text_renderer_load.Shutdown(runtime_load.Context());
        renderer_load_initialized = false;
        runtime_load.Shutdown();
        runtime_load_initialized = false;

        fs::remove(cache_path, filesystem_error);
    } catch (const std::exception& exception_) {
        if (renderer_load_initialized && runtime_load_initialized && runtime_load.IsInitialized()) {
            text_renderer_load.Shutdown(runtime_load.Context());
            renderer_load_initialized = false;
        }
        if (runtime_load_initialized && runtime_load.IsInitialized()) {
            runtime_load.Shutdown();
            runtime_load_initialized = false;
        }
        if (renderer_save_initialized && runtime_save_initialized && runtime_save.IsInitialized()) {
            text_renderer_save.Shutdown(runtime_save.Context());
            renderer_save_initialized = false;
        }
        if (runtime_save_initialized && runtime_save.IsInitialized()) {
            runtime_save.Shutdown();
            runtime_save_initialized = false;
        }

        fs::remove(cache_path, filesystem_error);

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace
