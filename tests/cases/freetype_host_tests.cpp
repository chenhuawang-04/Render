#include "support/test_framework.hpp"
#include "vr/text/freetype_host.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace {

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

VR_TEST_CASE(FreeTypeHost_initialize_and_shutdown, "unit;core;text;freetype") {
    vr::text::FreeTypeHost host{};
    host.Initialize();
    VR_CHECK(host.IsInitialized());
    host.Shutdown();
    VR_CHECK(!host.IsInitialized());
}

VR_TEST_CASE(FreeTypeHost_supports_sdf_query_is_stable, "unit;core;text;freetype") {
    vr::text::FreeTypeHost host{};
    const bool support_before_initialize = host.SupportsSdfRasterization();
    host.Initialize();
    const bool support_after_initialize = host.SupportsSdfRasterization();
    VR_CHECK(support_before_initialize == support_after_initialize);
    host.Shutdown();
}

VR_TEST_CASE(FreeTypeHost_register_face_and_rasterize_ascii, "unit;core;text;freetype") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for FreeTypeHost test.");
    }

    vr::text::FreeTypeHost host{};
    host.Initialize();

    vr::text::FontFaceCreateInfo create_info{};
    create_info.file_path = font_path;
    create_info.pixel_height = 32U;

    const vr::text::FontFaceId face_id = host.RegisterFace(create_info);
    VR_REQUIRE(face_id.IsValid());

    const vr::text::FontFaceMetrics metrics = host.FaceMetrics(face_id);
    VR_CHECK(metrics.y_ppem == 32U);
    VR_CHECK(metrics.height_26_6 != 0);

    VR_CHECK(host.HasGlyph(face_id, static_cast<std::uint32_t>('A')));

    vr::text::GlyphRasterRequest request{};
    request.face_id = face_id;
    request.codepoint = static_cast<std::uint32_t>('A');
    request.render_mode = vr::text::GlyphRenderMode::normal;

    const vr::text::GlyphBitmapView glyph_a_first = host.RasterizeGlyph(request);
    VR_CHECK(glyph_a_first.glyph_index != 0U);
    VR_CHECK(glyph_a_first.width > 0U);
    VR_CHECK(glyph_a_first.rows > 0U);
    VR_CHECK(glyph_a_first.size_bytes > 0U);
    VR_CHECK(glyph_a_first.pixels != nullptr);

    const std::uint32_t hits_before = host.Stats().glyph_cache_hits;
    const vr::text::GlyphBitmapView glyph_a_second = host.RasterizeGlyph(request);
    VR_CHECK(glyph_a_second.glyph_index == glyph_a_first.glyph_index);
    VR_CHECK(glyph_a_second.size_bytes == glyph_a_first.size_bytes);
    VR_CHECK(host.Stats().glyph_cache_hits >= hits_before + 1U);

    host.Shutdown();
}

VR_TEST_CASE(FreeTypeHost_set_face_pixel_size_updates_metrics, "unit;core;text;freetype") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for FreeTypeHost test.");
    }

    vr::text::FreeTypeHost host{};
    host.Initialize();

    vr::text::FontFaceCreateInfo create_info{};
    create_info.file_path = font_path;
    create_info.pixel_height = 20U;

    const vr::text::FontFaceId face_id = host.RegisterFace(create_info);
    VR_REQUIRE(face_id.IsValid());
    VR_CHECK(host.FaceMetrics(face_id).y_ppem == 20U);

    host.SetFacePixelSize(face_id, 44U, 0U);
    VR_CHECK(host.FaceMetrics(face_id).y_ppem == 44U);

    vr::text::GlyphRasterRequest request{};
    request.face_id = face_id;
    request.codepoint = static_cast<std::uint32_t>('M');
    const vr::text::GlyphBitmapView glyph = host.RasterizeGlyph(request);
    VR_CHECK(glyph.glyph_index != 0U);
    VR_CHECK(glyph.rows > 0U);
    VR_CHECK(glyph.width > 0U);
}

} // namespace

