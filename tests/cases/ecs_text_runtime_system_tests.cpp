#include "support/test_framework.hpp"
#include "vr/ecs/system/text_runtime_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"

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

VR_TEST_CASE(EcsTextRuntimeSystem_dim2_build_quads_and_batches, "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRuntimeSystem test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 24U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    vr::text::GlyphAtlasCreateInfo atlas_create_info{};
    atlas_create_info.page_width = 512U;
    atlas_create_info.page_height = 512U;
    atlas_create_info.max_page_count = 8U;
    atlas_host.Initialize(freetype_host, atlas_create_info);
    atlas_host.MapFont(1U, base_face);

    std::array<vr::ecs::Text<vr::ecs::Dim2>, 2U> components{};
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
    for (auto& component : components) {
        TextSystem2D::Initialize(component);
        TextSystem2D::SetRuntimeRoute(component, 1U, 5U, 0U, 0U);
    }

    VR_REQUIRE(TextSystem2D::SetText(components[0U], "Hello Vulkan"));
    TextSystem2D::SetHorizontalAlign(components[0U], vr::ecs::TextHorizontalAlign::left);

    VR_REQUIRE(TextSystem2D::SetText(components[1U], "AB"));
    TextSystem2D::SetHorizontalAlign(components[1U], vr::ecs::TextHorizontalAlign::center);
    TextSystem2D::SetPixelSize(components[1U], 28.0F);

    vr::ecs::TextRuntimeScratch<vr::ecs::Dim2> scratch{};
    vr::ecs::TextRuntimeSystem<vr::ecs::Dim2>::Reserve(scratch,
                                                       static_cast<std::uint32_t>(components.size()),
                                                       128U);

    const auto stats = vr::ecs::TextRuntimeSystem<vr::ecs::Dim2>::Build(components.data(),
                                                                         static_cast<std::uint32_t>(components.size()),
                                                                         atlas_host,
                                                                         freetype_host,
                                                                         scratch);

    VR_CHECK(stats.visible_component_count == 2U);
    VR_CHECK(stats.built_component_count == 2U);
    VR_CHECK(stats.emitted_glyph_quad_count > 0U);
    VR_CHECK(!scratch.glyph_quads.empty());
    VR_CHECK(!scratch.draw_batches.empty());
    VR_CHECK(atlas_host.PageCount() > 0U);

    VR_CHECK(components[0U].runtime.glyph_count > 0U);
    VR_CHECK(components[1U].runtime.glyph_count > 0U);
    VR_CHECK(components[0U].runtime.dirty_flags == 0U);
    VR_CHECK(components[1U].runtime.dirty_flags == 0U);

    const auto& centered_component = components[1U];
    const std::uint32_t centered_begin = centered_component.runtime.glyph_begin;
    VR_REQUIRE(centered_begin < scratch.glyph_quads.size());
    const vr::ecs::TextGlyphQuad& centered_quad = scratch.glyph_quads[centered_begin];
    VR_CHECK(centered_quad.x0 < centered_quad.x1);
    VR_CHECK(centered_quad.x0 < 0.0F);
}

VR_TEST_CASE(EcsTextRuntimeSystem_font_size_variant_cache_reuse, "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRuntimeSystem test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 16U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(7U, base_face);

    std::array<vr::ecs::Text<vr::ecs::Dim2>, 2U> components{};
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
    for (auto& component : components) {
        TextSystem2D::Initialize(component);
        TextSystem2D::SetRuntimeRoute(component, 7U, 1U, 0U, 0U);
        VR_REQUIRE(TextSystem2D::SetText(component, "Scale"));
    }
    TextSystem2D::SetPixelSize(components[0U], 18.0F);
    TextSystem2D::SetPixelSize(components[1U], 42.0F);

    vr::ecs::TextRuntimeScratch<vr::ecs::Dim2> scratch{};
    const auto stats_first = vr::ecs::TextRuntimeSystem<vr::ecs::Dim2>::Build(components.data(),
                                                                               static_cast<std::uint32_t>(components.size()),
                                                                               atlas_host,
                                                                               freetype_host,
                                                                               scratch);
    VR_CHECK(stats_first.face_variant_cache_entries >= 2U);
    VR_CHECK(components[0U].runtime.glyph_count > 0U);
    VR_CHECK(components[1U].runtime.glyph_count > 0U);

    const std::uint32_t face_count_after_first_build = freetype_host.FaceCount();
    const auto stats_second = vr::ecs::TextRuntimeSystem<vr::ecs::Dim2>::Build(components.data(),
                                                                                static_cast<std::uint32_t>(components.size()),
                                                                                atlas_host,
                                                                                freetype_host,
                                                                                scratch);
    VR_CHECK(stats_second.face_variant_cache_entries >= stats_first.face_variant_cache_entries);
    VR_CHECK(freetype_host.FaceCount() == face_count_after_first_build);
}

VR_TEST_CASE(EcsTextRuntimeSystem_sdf_flags_match_actual_raster_mode, "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRuntimeSystem test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 24U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(9U, base_face);

    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
    vr::ecs::Text<vr::ecs::Dim2> component{};
    TextSystem2D::Initialize(component);
    TextSystem2D::SetRuntimeRoute(component, 9U, 1U, 0U, 0U);
    TextSystem2D::SetSdfEnabled(component, true);
    TextSystem2D::SetOutlineEnabled(component, true);
    TextSystem2D::SetOutlineWidthPx(component, 2U);
    VR_REQUIRE(TextSystem2D::SetText(component, "SDF Probe"));

    vr::ecs::TextRuntimeScratch<vr::ecs::Dim2> scratch{};
    const auto stats = vr::ecs::TextRuntimeSystem<vr::ecs::Dim2>::Build(&component,
                                                                         1U,
                                                                         atlas_host,
                                                                         freetype_host,
                                                                         scratch);
    VR_REQUIRE(stats.built_component_count == 1U);
    VR_REQUIRE(!scratch.glyph_quads.empty());

    bool any_sdf_quad = false;
    for (const auto& quad : scratch.glyph_quads) {
        if (quad.sdf_enabled != 0U) {
            any_sdf_quad = true;
            VR_CHECK(quad.outline_enabled != 0U);
            VR_CHECK(quad.outline_width_px > 0U);
        } else {
            VR_CHECK(quad.outline_enabled == 0U);
            VR_CHECK(quad.outline_width_px == 0U);
        }
    }

    if (freetype_host.SupportsSdfRasterization()) {
        VR_CHECK(any_sdf_quad);
    } else {
        VR_CHECK(!any_sdf_quad);
    }
}

VR_TEST_CASE(EcsTextRuntimeSystem_dim3_world_size_build, "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRuntimeSystem test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 20U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(3U, base_face);

    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
    vr::ecs::Text<vr::ecs::Dim3> component{};
    TextSystem3D::Initialize(component);
    TextSystem3D::SetRuntimeRoute(component, 3U, 11U, 0U, 0U);
    TextSystem3D::SetWorldSize(component, 0.5F);
    VR_REQUIRE(TextSystem3D::SetText(component, "Beacon-3D"));

    vr::ecs::TextRuntimeScratch<vr::ecs::Dim3> scratch{};
    vr::ecs::TextRuntimeBuildConfig build_config{};
    build_config.dim3_pixels_per_world_unit = 96.0F;

    const auto stats = vr::ecs::TextRuntimeSystem<vr::ecs::Dim3>::Build(&component,
                                                                         1U,
                                                                         atlas_host,
                                                                         freetype_host,
                                                                         scratch,
                                                                         build_config);
    VR_CHECK(stats.built_component_count == 1U);
    VR_CHECK(component.runtime.glyph_count > 0U);
    VR_CHECK(!scratch.glyph_quads.empty());
    VR_CHECK(!scratch.draw_batches.empty());
}

VR_TEST_CASE(EcsTextRuntimeSystem_dim3_world_size_scales_glyph_geometry, "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRuntimeSystem test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 20U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(13U, base_face);

    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
    std::array<vr::ecs::Text<vr::ecs::Dim3>, 2U> components{};
    for (auto& component : components) {
        TextSystem3D::Initialize(component);
        TextSystem3D::SetRuntimeRoute(component, 13U, 11U, 0U, 0U);
        VR_REQUIRE(TextSystem3D::SetText(component, "Scale3D"));
    }
    TextSystem3D::SetWorldSize(components[0U], 0.5F);
    TextSystem3D::SetWorldSize(components[1U], 1.0F);

    vr::ecs::TextRuntimeScratch<vr::ecs::Dim3> scratch{};
    vr::ecs::TextRuntimeBuildConfig build_config{};
    build_config.dim3_pixels_per_world_unit = 96.0F;

    const auto stats = vr::ecs::TextRuntimeSystem<vr::ecs::Dim3>::Build(components.data(),
                                                                         static_cast<std::uint32_t>(components.size()),
                                                                         atlas_host,
                                                                         freetype_host,
                                                                         scratch,
                                                                         build_config);
    VR_REQUIRE(stats.built_component_count == 2U);
    VR_REQUIRE(components[0U].runtime.glyph_count > 0U);
    VR_REQUIRE(components[1U].runtime.glyph_count > 0U);

    const vr::ecs::TextGlyphQuad& quad_small =
        scratch.glyph_quads[components[0U].runtime.glyph_begin];
    const vr::ecs::TextGlyphQuad& quad_large =
        scratch.glyph_quads[components[1U].runtime.glyph_begin];

    const float width_small = quad_small.x1 - quad_small.x0;
    const float width_large = quad_large.x1 - quad_large.x0;
    VR_CHECK(width_small > 0.0F);
    VR_CHECK(width_large > width_small);
    VR_CHECK(width_large / width_small > 1.5F);
}

VR_TEST_CASE(EcsTextRuntimeSystem_dim3_candidate_visibility_hint_limits_build_scope, "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRuntimeSystem candidate visibility test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 24U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(21U, base_face);

    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
    std::array<vr::ecs::Text<vr::ecs::Dim3>, 3U> components{};
    for (auto& component : components) {
        TextSystem3D::Initialize(component);
        TextSystem3D::SetRuntimeRoute(component, 21U, 3U, 0U, 0U);
        TextSystem3D::SetWorldSize(component, 0.45F);
    }
    VR_REQUIRE(TextSystem3D::SetText(components[0U], "Candidate-A"));
    VR_REQUIRE(TextSystem3D::SetText(components[1U], "Candidate-B"));
    VR_REQUIRE(TextSystem3D::SetText(components[2U], "Candidate-C"));

    vr::ecs::TextRuntimeScratch<vr::ecs::Dim3> scratch{};
    const std::array<std::uint32_t, 2U> visible_indices{
        0U,
        2U
    };
    vr::ecs::TextRuntimeBuildHint build_hint{};
    build_hint.visible_component_indices = visible_indices.data();
    build_hint.visible_component_count = static_cast<std::uint32_t>(visible_indices.size());
    build_hint.use_visible_component_indices = 1U;

    const auto stats = vr::ecs::TextRuntimeSystem<vr::ecs::Dim3>::Build(components.data(),
                                                                         static_cast<std::uint32_t>(components.size()),
                                                                         atlas_host,
                                                                         freetype_host,
                                                                         scratch,
                                                                         {},
                                                                         build_hint);
    VR_CHECK(stats.total_component_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.candidate_component_count == 2U);
    VR_CHECK(stats.used_visible_component_indices);
    VR_CHECK(stats.visible_component_count == 2U);
    VR_CHECK(stats.built_component_count == 2U);
    VR_CHECK(stats.visible_set_signature != 0U);
    VR_CHECK(components[0U].runtime.glyph_count > 0U);
    VR_CHECK(components[2U].runtime.glyph_count > 0U);
    VR_CHECK(components[1U].runtime.glyph_count == 0U);

    vr::ecs::TextRuntimeBuildHint hinted_signature = build_hint;
    hinted_signature.external_visible_set_signature = stats.visible_set_signature + 7U;
    hinted_signature.use_external_visible_set_signature = 1U;
    const auto stats_with_external_signature =
        vr::ecs::TextRuntimeSystem<vr::ecs::Dim3>::Build(components.data(),
                                                         static_cast<std::uint32_t>(components.size()),
                                                         atlas_host,
                                                         freetype_host,
                                                         scratch,
                                                         {},
                                                         hinted_signature);
    VR_CHECK(stats_with_external_signature.visible_set_signature_from_hint);
    VR_CHECK(stats_with_external_signature.visible_set_signature ==
             hinted_signature.external_visible_set_signature);
    VR_CHECK(stats_with_external_signature.candidate_component_count == 2U);
}

} // namespace
