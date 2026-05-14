#include "support/bench_framework.hpp"
#include "vr/ecs/system/text_runtime_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"

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

VR_BENCHMARK_CASE(EcsTextRuntimeSystem_dim2_build_1k, "core;ecs;text;runtime;cpu") {
    const std::string font_path = FindBenchFontPath();
    if (font_path.empty()) {
        VR_BENCH_SKIP("No usable system font found for TextRuntimeSystem benchmark.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 24U;
    const vr::text::FontFaceId face_id = freetype_host.RegisterFace(face_create_info);

    vr::text::GlyphAtlasHost atlas_host{};
    vr::text::GlyphAtlasCreateInfo atlas_create_info{};
    atlas_create_info.page_width = 2048U;
    atlas_create_info.page_height = 2048U;
    atlas_create_info.max_page_count = 16U;
    atlas_host.Initialize(freetype_host, atlas_create_info);
    atlas_host.MapFont(1U, face_id);

    using TextType = vr::ecs::Text<vr::ecs::Dim2>;
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
    using RuntimeSystem2D = vr::ecs::TextRuntimeSystem<vr::ecs::Dim2>;

    constexpr std::uint32_t component_count = 1024U;
    std::array<const char*, 8U> labels{
        "HP",
        "MP",
        "EXP",
        "Quest",
        "Target",
        "Marker",
        "Status",
        "Cooldown"
    };

    Center::Memory::mc_vector<TextType, Center::Memory::Tags::Container> components{};
    components.resize(component_count);
    for (std::uint32_t i = 0U; i < component_count; ++i) {
        TextSystem2D::Initialize(components[i]);
        TextSystem2D::SetRuntimeRoute(components[i], 1U, i & 63U, 0U, i & 31U);
        TextSystem2D::SetPixelSize(components[i], 16.0F + static_cast<float>(i % 3U) * 6.0F);
        (void)TextSystem2D::SetText(components[i], labels[i % labels.size()]);
    }

    vr::ecs::TextRuntimeScratch<vr::ecs::Dim2> scratch{};
    RuntimeSystem2D::Reserve(scratch, component_count, component_count * 12U);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        (void)TextSystem2D::SetText(components[hot_index], labels[(i + 3U) % labels.size()]);

        const auto stats = RuntimeSystem2D::Build(components.data(),
                                                  component_count,
                                                  atlas_host,
                                                  freetype_host,
                                                  scratch);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.emitted_glyph_quad_count);
    }

    bench_context_.AddItems(iterations * component_count);
    bench_context_.AddBytes(iterations * static_cast<std::uint64_t>(scratch.glyph_quads.size()) *
                            sizeof(vr::ecs::TextGlyphQuad));
    vr::bench::BenchmarkContext::DoNotOptimize(scratch.draw_batches.size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace


