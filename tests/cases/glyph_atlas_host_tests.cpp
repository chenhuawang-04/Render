#include "support/test_framework.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"

#include <array>
#include <cstdint>
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

VR_TEST_CASE(GlyphAtlasHost_map_resolve_and_cache_hit, "unit;core;text;freetype;atlas") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for GlyphAtlasHost test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 40U;
    const vr::text::FontFaceId face_id = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(face_id.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    vr::text::GlyphAtlasCreateInfo atlas_create_info{};
    atlas_create_info.page_width = 256U;
    atlas_create_info.page_height = 256U;
    atlas_create_info.max_page_count = 4U;
    atlas_host.Initialize(freetype_host, atlas_create_info);

    atlas_host.MapFont(7U, face_id);

    vr::text::GlyphAtlasResolveRequest request{};
    request.font_id = 7U;
    request.codepoint = static_cast<std::uint32_t>('A');
    const auto first = atlas_host.ResolveGlyph(request);
    VR_CHECK(first.has_glyph);
    VR_CHECK(first.glyph_index != 0U);

    if (first.region.HasAtlasPixels()) {
        VR_CHECK(atlas_host.PageCount() > 0U);
        const auto& dirty_rects = atlas_host.PageDirtyRects(first.region.page_index);
        VR_CHECK(!dirty_rects.empty());
        atlas_host.ClearPageDirtyRects(first.region.page_index);
    }

    const std::uint32_t hits_before = atlas_host.Stats().cache_hits;
    const auto second = atlas_host.ResolveGlyph(request);
    VR_CHECK(second.glyph_index == first.glyph_index);
    VR_CHECK(second.region.page_index == first.region.page_index);
    VR_CHECK(atlas_host.Stats().cache_hits >= hits_before + 1U);

    if (second.region.HasAtlasPixels()) {
        VR_CHECK(atlas_host.PageDirtyRects(second.region.page_index).empty());
    }
}

VR_TEST_CASE(GlyphAtlasHost_space_glyph_can_be_empty_bitmap, "unit;core;text;freetype;atlas") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for GlyphAtlasHost test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 32U;
    const vr::text::FontFaceId face_id = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(face_id.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(1U, face_id);

    vr::text::GlyphAtlasResolveRequest request{};
    request.font_id = 1U;
    request.codepoint = static_cast<std::uint32_t>(' ');
    const auto resolved = atlas_host.ResolveGlyph(request);
    VR_CHECK(resolved.has_glyph);
    VR_CHECK(resolved.glyph_index != 0U);
    if (!resolved.region.HasAtlasPixels()) {
        VR_CHECK(resolved.region.pixel_rect.width == 0U);
        VR_CHECK(resolved.region.pixel_rect.height == 0U);
    }
}

VR_TEST_CASE(GlyphAtlasHost_small_page_grows_multiple_pages, "unit;core;text;freetype;atlas") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for GlyphAtlasHost test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 28U;
    const vr::text::FontFaceId face_id = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(face_id.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    vr::text::GlyphAtlasCreateInfo atlas_create_info{};
    atlas_create_info.page_width = 64U;
    atlas_create_info.page_height = 64U;
    atlas_create_info.max_page_count = 32U;
    atlas_create_info.glyph_padding = 1U;
    atlas_host.Initialize(freetype_host, atlas_create_info);
    atlas_host.MapFont(9U, face_id);

    vr::text::GlyphAtlasResolveRequest request{};
    request.font_id = 9U;

    for (std::uint32_t codepoint = 33U; codepoint < 128U; ++codepoint) {
        request.codepoint = codepoint;
        (void)atlas_host.ResolveGlyph(request);
    }

    VR_CHECK(atlas_host.PageCount() >= 2U);
    VR_CHECK(atlas_host.Stats().glyph_entry_count >= 30U);
}

} // namespace

