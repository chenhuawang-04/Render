#include "support/bench_framework.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"

#include <array>
#include <cstdint>
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

VR_BENCHMARK_CASE(GlyphAtlasHost_resolve_ascii_hot_cache, "core;text;atlas;cpu") {
    const std::string font_path = FindBenchFontPath();
    if (font_path.empty()) {
        VR_BENCH_SKIP("No usable system font found for GlyphAtlasHost benchmark.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 32U;
    const vr::text::FontFaceId face_id = freetype_host.RegisterFace(face_create_info);

    vr::text::GlyphAtlasHost atlas_host{};
    vr::text::GlyphAtlasCreateInfo atlas_create_info{};
    atlas_create_info.page_width = 1024U;
    atlas_create_info.page_height = 1024U;
    atlas_create_info.max_page_count = 8U;
    atlas_host.Initialize(freetype_host, atlas_create_info);
    atlas_host.MapFont(1U, face_id);

    vr::text::GlyphAtlasResolveRequest request{};
    request.font_id = 1U;

    for (std::uint32_t codepoint = 32U; codepoint <= 126U; ++codepoint) {
        request.codepoint = codepoint;
        (void)atlas_host.ResolveGlyph(request);
    }
    atlas_host.ClearAllDirtyRects();

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        request.codepoint = static_cast<std::uint32_t>(32U + (i % 95U));
        const auto entry = atlas_host.ResolveGlyph(request);
        vr::bench::BenchmarkContext::DoNotOptimize(entry.glyph_index);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * 16U);
    vr::bench::BenchmarkContext::DoNotOptimize(atlas_host.Stats().cache_hits);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace


