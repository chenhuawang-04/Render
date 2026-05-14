#include "support/bench_framework.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/render/render_runtime_host.hpp"
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
using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

[[nodiscard]] std::string FindBenchFontPath() {
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
                             std::uint32_t appearance_id_,
                             std::int16_t layer_,
                             std::string_view text_) {
    TextSystem2D::Initialize(component_);
    TextSystem2D::SetRuntimeRoute(component_, font_id_, appearance_id_, 0U, 0U);
    TextSystem2D::SetLayer(component_, layer_);
    TextSystem2D::SetColor(component_, vr::ecs::Rgba8{255U, 255U, 255U, 255U});
    TextSystem2D::SetOutlineEnabled(component_, true);
    TextSystem2D::SetOutlineWidthPx(component_, 1U);
    TextSystem2D::SetOutlineColor(component_, vr::ecs::Rgba8{16U, 20U, 28U, 255U});
    (void)TextSystem2D::SetText(component_, text_);
}

VR_BENCHMARK_CASE(Runtime_text_renderer_2d_tick_loop, "integration;gpu;sdl;runtime;text") {
    const std::string font_path = FindBenchFontPath();
    if (font_path.empty()) {
        VR_BENCH_SKIP("No usable system font found for runtime text benchmark.");
    }

    Runtime runtime{};
    vr::text::TextRenderer2D text_renderer{};
    bool runtime_initialized = false;
    bool renderer_initialized = false;

    std::array<Text2D, 3U> text_components{};
    InitializeTextComponent(text_components[0U], 1U, 1U, 0, "Runtime text benchmark");
    TextSystem2D::SetPixelSize(text_components[0U], 34.0F);
    InitializeTextComponent(text_components[1U], 1U, 1U, 1, "Upload + descriptor + draw");
    TextSystem2D::SetPixelSize(text_components[1U], 24.0F);
    InitializeTextComponent(text_components[2U], 1U, 1U, 2, "Frame: 0");
    TextSystem2D::SetPixelSize(text_components[2U], 26.0F);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_bench_runtime_text";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = false;
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
        const vr::text::FontFaceId face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, face_id);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 8192U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 2U * 1024U * 1024U;
        text_renderer_create_info.clear_swapchain = true;
        text_renderer.Initialize(text_renderer_create_info);
        renderer_initialized = true;
        text_renderer.SetComponents(text_components.data(),
                                    static_cast<std::uint32_t>(text_components.size()));

        const std::uint64_t iterations = bench_context_.Iterations();
        std::uint64_t submitted_frames = 0U;
        std::uint64_t rendered_glyph_quads = 0U;
        for (std::uint64_t i = 0U; i < iterations; ++i) {
            if (!runtime.IsRunning()) {
                break;
            }

            char frame_text[64]{};
            std::snprintf(frame_text, sizeof(frame_text), "Frame: %llu",
                          static_cast<unsigned long long>(i));
            (void)TextSystem2D::SetText(text_components[2U], frame_text);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(text_renderer);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            rendered_glyph_quads += text_renderer.Stats().glyph_quad_count;
            SDL_Delay(0U);
        }

        bench_context_.AddItems(submitted_frames);
        bench_context_.AddBytes(rendered_glyph_quads * sizeof(float) * 4U);

        vr::bench::BenchmarkContext::DoNotOptimize(text_renderer.Stats().draw_call_count);
        vr::bench::BenchmarkContext::DoNotOptimize(runtime.GlyphUpload().Stats().uploaded_rect_count);
        vr::bench::BenchmarkContext::ClobberMemory();

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
            VR_BENCH_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace

