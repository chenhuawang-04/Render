#include "vr/ecs/system/text_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/text/text_renderer_2d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

constexpr std::uint8_t k_page_any = 255U;
constexpr std::uint32_t k_page_count = 4U;
constexpr std::uint32_t k_component_count = 13U;
constexpr std::uint32_t k_page_duration_ms = 7000U;

enum DemoComponentIndex : std::uint32_t {
    header = 0U,
    footer_stats = 1U,
    p0_title = 2U,
    p0_primary = 3U,
    p0_secondary = 4U,
    p1_title = 5U,
    p1_sdf_outline = 6U,
    p1_bitmap = 7U,
    p2_title = 8U,
    p2_kerning = 9U,
    p2_multiline = 10U,
    p3_title = 11U,
    p3_runtime = 12U
};

struct DemoFontPaths final {
    std::string primary{};
    std::string secondary{};
};

[[nodiscard]] std::string FileNameOnly(const std::string& path_) {
    namespace fs = std::filesystem;
    if (path_.empty()) {
        return {};
    }
    return fs::path(path_).filename().string();
}

[[nodiscard]] DemoFontPaths PickDemoFonts() {
    namespace fs = std::filesystem;

    constexpr std::array<const char*, 8U> candidates{
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/verdana.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf"
    };

    DemoFontPaths out{};
    for (const char* candidate : candidates) {
        const fs::path font_path(candidate);
        if (!fs::exists(font_path) || !fs::is_regular_file(font_path)) {
            continue;
        }

        if (out.primary.empty()) {
            out.primary = font_path.string();
            continue;
        }
        if (out.secondary.empty() && out.primary != font_path.string()) {
            out.secondary = font_path.string();
            break;
        }
    }

    if (!out.primary.empty() && out.secondary.empty()) {
        out.secondary = out.primary;
    }
    return out;
}

void InitializeTextComponent(Text2D& component_,
                             std::uint32_t font_id_,
                             std::uint32_t material_id_,
                             std::int16_t layer_,
                             float pixel_size_,
                             vr::ecs::Rgba8 color_,
                             bool sdf_enabled_,
                             bool outline_enabled_,
                             std::uint8_t outline_width_px_,
                             vr::ecs::Rgba8 outline_color_,
                             vr::ecs::TextHorizontalAlign horizontal_align_,
                             vr::ecs::TextVerticalAlign vertical_align_,
                             float line_spacing_,
                             float letter_spacing_,
                             std::string_view text_) {
    TextSystem2D::Initialize(component_);
    TextSystem2D::SetRuntimeRoute(component_, font_id_, material_id_, 0U, 0U);
    TextSystem2D::SetLayer(component_, layer_);
    TextSystem2D::SetPixelSize(component_, pixel_size_);
    TextSystem2D::SetColor(component_, color_);
    TextSystem2D::SetSdfEnabled(component_, sdf_enabled_);
    TextSystem2D::SetOutlineEnabled(component_, outline_enabled_);
    TextSystem2D::SetOutlineWidthPx(component_, outline_width_px_);
    TextSystem2D::SetOutlineColor(component_, outline_color_);
    TextSystem2D::SetHorizontalAlign(component_, horizontal_align_);
    TextSystem2D::SetVerticalAlign(component_, vertical_align_);
    TextSystem2D::SetLineSpacing(component_, line_spacing_);
    TextSystem2D::SetLetterSpacing(component_, letter_spacing_);
    (void)TextSystem2D::SetText(component_, text_);
}

} // namespace

int main() {
    try {
        const DemoFontPaths fonts = PickDemoFonts();
        if (fonts.primary.empty()) {
            throw std::runtime_error("No usable system font found (tried common Windows font paths).");
        }

        Runtime runtime{};
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Text 2D Showcase";
        create_info.platform.window.width = 1280;
        create_info.platform.window.height = 720;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = true;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = true;
        create_info.render_loop.swapchain.preferred_image_count = 3U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;

        runtime.Initialize(create_info);

        vr::text::FontFaceCreateInfo face_create_info_primary{};
        face_create_info_primary.file_path = fonts.primary;
        face_create_info_primary.pixel_height = 34U;
        const vr::text::FontFaceId face_primary = runtime.FreeType().RegisterFace(face_create_info_primary);

        vr::text::FontFaceCreateInfo face_create_info_secondary{};
        face_create_info_secondary.file_path = fonts.secondary;
        face_create_info_secondary.pixel_height = 34U;
        const vr::text::FontFaceId face_secondary = runtime.FreeType().RegisterFace(face_create_info_secondary);

        runtime.GlyphAtlas().MapFont(1U, face_primary);
        runtime.GlyphAtlas().MapFont(2U, face_secondary.IsValid() ? face_secondary : face_primary);

        std::array<Text2D, k_component_count> components{};
        const std::array<std::uint8_t, k_component_count> component_pages{
            k_page_any, // header
            k_page_any, // footer_stats
            0U, 0U, 0U,
            1U, 1U, 1U,
            2U, 2U, 2U,
            3U, 3U
        };

        InitializeTextComponent(components[header],
                                1U,
                                1U,
                                -100,
                                22.0F,
                                vr::ecs::Rgba8{225U, 240U, 255U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "Melosyne 2D Text Showcase");

        InitializeTextComponent(components[footer_stats],
                                1U,
                                1U,
                                100,
                                18.0F,
                                vr::ecs::Rgba8{182U, 220U, 190U, 255U},
                                false,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nStats");

        InitializeTextComponent(components[p0_title],
                                1U,
                                1U,
                                0,
                                30.0F,
                                vr::ecs::Rgba8{255U, 236U, 176U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\nPage 1/4 - Font routes + size tiers");

        InitializeTextComponent(components[p0_primary],
                                1U,
                                1U,
                                1,
                                56.0F,
                                vr::ecs::Rgba8{245U, 245U, 245U, 255U},
                                true,
                                true,
                                1U,
                                vr::ecs::Rgba8{20U, 24U, 38U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\n\nMelosyne Vulkan Text");

        InitializeTextComponent(components[p0_secondary],
                                2U,
                                2U,
                                2,
                                34.0F,
                                vr::ecs::Rgba8{200U, 232U, 255U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\n\n\n\nFont#2 route | ABC xyz 123 | 中文样例");

        InitializeTextComponent(components[p1_title],
                                1U,
                                1U,
                                10,
                                30.0F,
                                vr::ecs::Rgba8{255U, 236U, 176U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\nPage 2/4 - SDF, outline and bitmap");

        InitializeTextComponent(components[p1_sdf_outline],
                                1U,
                                1U,
                                11,
                                48.0F,
                                vr::ecs::Rgba8{255U, 248U, 240U, 255U},
                                true,
                                true,
                                2U,
                                vr::ecs::Rgba8{18U, 24U, 48U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\n\nSDF + Outline (crisp edges)");

        InitializeTextComponent(components[p1_bitmap],
                                1U,
                                1U,
                                12,
                                40.0F,
                                vr::ecs::Rgba8{188U, 255U, 216U, 255U},
                                false,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\n\n\n\nBitmap glyph mode (SDF off)");

        InitializeTextComponent(components[p2_title],
                                1U,
                                1U,
                                20,
                                30.0F,
                                vr::ecs::Rgba8{255U, 236U, 176U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\nPage 3/4 - Kerning, spacing, multiline");

        InitializeTextComponent(components[p2_kerning],
                                1U,
                                1U,
                                21,
                                38.0F,
                                vr::ecs::Rgba8{246U, 246U, 246U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\n\nKerning pairs: AVA WA To Fo ffi fl");

        InitializeTextComponent(components[p2_multiline],
                                1U,
                                1U,
                                22,
                                26.0F,
                                vr::ecs::Rgba8{184U, 214U, 255U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.22F,
                                0.0F,
                                "\n\n\n\nMultiline layout\nLine spacing demo\nTab:\tA\tB\tC");

        InitializeTextComponent(components[p3_title],
                                1U,
                                1U,
                                30,
                                30.0F,
                                vr::ecs::Rgba8{255U, 236U, 176U, 255U},
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.0F,
                                0.0F,
                                "\nPage 4/4 - Runtime knobs / sort route");

        InitializeTextComponent(components[p3_runtime],
                                1U,
                                2U,
                                31,
                                24.0F,
                                vr::ecs::Rgba8{214U, 255U, 206U, 255U},
                                true,
                                true,
                                1U,
                                vr::ecs::Rgba8{16U, 24U, 34U, 255U},
                                vr::ecs::TextHorizontalAlign::left,
                                vr::ecs::TextVerticalAlign::top,
                                1.15F,
                                0.0F,
                                "\n\nmaterial/font route + batch/depth bins\nruntime text update each frame");

        for (std::uint32_t i = 0U; i < components.size(); ++i) {
            const bool visible = component_pages[i] == k_page_any || component_pages[i] == 0U;
            TextSystem2D::SetVisible(components[i], visible);
        }

        vr::text::TextRenderer2D text_renderer{};
        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.runtime_build.bitmap_render_mode = vr::text::GlyphRenderMode::light;
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(components.size());
        text_renderer_create_info.reserve_glyph_count = 32768U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 4U * 1024U * 1024U;
        text_renderer_create_info.bitmap_gamma = 1.0F;
        text_renderer_create_info.bitmap_edge_sharpness = 1.18F;
        text_renderer_create_info.enable_pixel_snap = true;
        text_renderer_create_info.pixel_snap_step = 1.0F;
        text_renderer_create_info.clear_swapchain = true;
        text_renderer_create_info.clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
        text_renderer.Initialize(text_renderer_create_info);
        text_renderer.SetComponents(components.data(),
                                    static_cast<std::uint32_t>(components.size()));

        const std::string primary_name = FileNameOnly(fonts.primary);
        const std::string secondary_name = FileNameOnly(fonts.secondary);

        std::uint64_t frame_counter = 0U;
        std::uint32_t current_page = std::numeric_limits<std::uint32_t>::max();
        std::uint64_t fps_window_begin = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;
        float fps = 0.0F;
        const std::uint64_t demo_start_ticks = SDL_GetTicks();

        std::cout << "sdl_text_demo running (2D showcase). Close window to exit.\n";
        while (runtime.IsRunning()) {
            const std::uint64_t now_ticks = SDL_GetTicks();
            const std::uint32_t elapsed_ms = static_cast<std::uint32_t>(now_ticks - demo_start_ticks);
            const std::uint32_t page = (elapsed_ms / k_page_duration_ms) % k_page_count;

            if (page != current_page) {
                current_page = page;
                for (std::uint32_t i = 0U; i < components.size(); ++i) {
                    const bool visible = component_pages[i] == k_page_any ||
                                         component_pages[i] == current_page;
                    TextSystem2D::SetVisible(components[i], visible);
                }
            }

            const float time_seconds = static_cast<float>(elapsed_ms) * 0.001F;
            const float spacing_anim = std::sin(time_seconds * 1.7F) * 1.6F;
            TextSystem2D::SetLetterSpacing(components[p2_kerning], spacing_anim);

            const std::uint8_t pulse =
                static_cast<std::uint8_t>(150.0F + 105.0F * (0.5F + 0.5F * std::sin(time_seconds * 2.1F)));
            TextSystem2D::SetColor(components[p1_bitmap],
                                   vr::ecs::Rgba8{static_cast<std::uint8_t>(120U + pulse / 2U), pulse, 190U, 255U});

            char header_text[240]{};
            std::snprintf(header_text,
                          sizeof(header_text),
                          "Melosyne 2D Showcase | Page %u/%u | Font1:%s | Font2:%s",
                          current_page + 1U,
                          k_page_count,
                          primary_name.c_str(),
                          secondary_name.c_str());
            (void)TextSystem2D::SetText(components[header], header_text);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(text_renderer);
            (void)tick_result;

            ++fps_window_frame_count;
            const std::uint64_t fps_window_now = SDL_GetTicks();
            const std::uint64_t fps_window_span = fps_window_now - fps_window_begin;
            if (fps_window_span >= 400U) {
                fps = static_cast<float>(fps_window_frame_count) * 1000.0F /
                      static_cast<float>(fps_window_span);
                fps_window_begin = fps_window_now;
                fps_window_frame_count = 0U;
            }

            const vr::text::TextRenderer2DStats stats = text_renderer.Stats();
            char footer_text[240]{};
            std::snprintf(footer_text,
                          sizeof(footer_text),
                          "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nFPS: %.1f | Frame: %llu | Visible:%u Built:%u\nGlyphs:%u Batches:%u Draws:%u Upload:%llu KB",
                          fps,
                          static_cast<unsigned long long>(frame_counter),
                          stats.visible_component_count,
                          stats.built_component_count,
                          stats.glyph_quad_count,
                          stats.draw_batch_count,
                          stats.draw_call_count,
                          static_cast<unsigned long long>(stats.uploaded_bytes / 1024U));
            (void)TextSystem2D::SetText(components[footer_stats], footer_text);

            SDL_Delay(8U);
            ++frame_counter;
        }

        text_renderer.Shutdown(runtime.Context());
        runtime.Shutdown();
        return 0;
    } catch (const std::exception& exception_) {
        std::cerr << "sdl_text_demo failed: " << exception_.what() << '\n';
        return 1;
    }
}
