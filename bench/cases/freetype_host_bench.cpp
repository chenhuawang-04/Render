#include "support/bench_framework.hpp"
#include "vr/text/freetype_host.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace {

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

VR_BENCHMARK_CASE(FreeTypeHost_rasterize_ascii_hot_cache, "core;text;freetype;cpu") {
    const std::string font_path = FindBenchFontPath();
    if (font_path.empty()) {
        VR_BENCH_SKIP("No usable system font found for FreeTypeHost benchmark.");
    }

    vr::text::FreeTypeHost host{};
    host.Initialize();

    vr::text::FontFaceCreateInfo create_info{};
    create_info.file_path = font_path;
    create_info.pixel_height = 32U;

    const vr::text::FontFaceId face_id = host.RegisterFace(create_info);

    vr::text::GlyphRasterRequest request{};
    request.face_id = face_id;
    request.render_mode = vr::text::GlyphRenderMode::normal;

    for (std::uint32_t codepoint = 32U; codepoint <= 126U; ++codepoint) {
        request.codepoint = codepoint;
        (void)host.RasterizeGlyph(request);
    }

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        request.codepoint = static_cast<std::uint32_t>(32U + (i % 95U));
        const vr::text::GlyphBitmapView glyph = host.RasterizeGlyph(request);
        vr::bench::BenchmarkContext::DoNotOptimize(glyph.size_bytes);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * 64U);
    vr::bench::BenchmarkContext::DoNotOptimize(host.Stats().glyph_cache_hits);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(FreeTypeHost_rasterize_ascii_cold_cache_refill, "core;text;freetype;cpu") {
    const std::string font_path = FindBenchFontPath();
    if (font_path.empty()) {
        VR_BENCH_SKIP("No usable system font found for FreeTypeHost benchmark.");
    }

    vr::text::FreeTypeHost host{};
    host.Initialize();

    vr::text::FontFaceCreateInfo create_info{};
    create_info.file_path = font_path;
    create_info.pixel_height = 32U;

    const vr::text::FontFaceId face_id = host.RegisterFace(create_info);

    vr::text::GlyphRasterRequest request{};
    request.face_id = face_id;
    request.render_mode = vr::text::GlyphRenderMode::normal;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        host.ClearGlyphCache();
        for (std::uint32_t codepoint = 32U; codepoint <= 126U; ++codepoint) {
            request.codepoint = codepoint;
            const vr::text::GlyphBitmapView glyph = host.RasterizeGlyph(request);
            vr::bench::BenchmarkContext::DoNotOptimize(glyph.size_bytes);
        }
    }

    bench_context_.AddItems(iterations * 95U);
    bench_context_.AddBytes(iterations * 95U * 64U);
    vr::bench::BenchmarkContext::DoNotOptimize(host.Stats().glyph_cache_misses);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace

