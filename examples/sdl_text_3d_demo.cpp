#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/text/text_renderer_3d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

constexpr std::uint32_t k_component_count = 10U;

enum DemoComponentIndex : std::uint32_t {
    rotating_title = 0U,
    billboard_subtitle = 1U,
    depth_back = 2U,
    depth_front_write = 3U,
    no_depth_overlay = 4U,
    sdf_outline = 5U,
    bitmap_label = 6U,
    max_screen_clamped = 7U,
    runtime_modes = 8U,
    runtime_stats = 9U
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

void InitializeTextComponent(Text3D& component_,
                             std::uint32_t font_id_,
                             std::uint32_t material_id_,
                             float world_size_,
                             float max_screen_size_px_,
                             vr::ecs::Rgba8 color_,
                             bool billboard_,
                             bool depth_test_,
                             bool depth_write_,
                             bool sdf_enabled_,
                             bool outline_enabled_,
                             std::uint8_t outline_width_px_,
                             vr::ecs::Rgba8 outline_color_,
                             std::uint16_t depth_bin_,
                             std::string_view text_) {
    TextSystem3D::Initialize(component_);
    TextSystem3D::SetRuntimeRoute(component_, font_id_, material_id_, 0U, 0U);
    TextSystem3D::SetColor(component_, color_);
    TextSystem3D::SetWorldSize(component_, world_size_);
    TextSystem3D::SetMaxScreenSizePx(component_, max_screen_size_px_);
    TextSystem3D::SetBillboard(component_, billboard_);
    TextSystem3D::SetDepthTest(component_, depth_test_);
    TextSystem3D::SetDepthWrite(component_, depth_write_);
    TextSystem3D::SetSdfEnabled(component_, sdf_enabled_);
    TextSystem3D::SetOutlineEnabled(component_, outline_enabled_);
    TextSystem3D::SetOutlineWidthPx(component_, outline_width_px_);
    TextSystem3D::SetOutlineColor(component_, outline_color_);
    TextSystem3D::SetDepthBin(component_, depth_bin_);
    (void)TextSystem3D::SetText(component_, text_);
}

void InitializeTransform(Transform3D& transform_,
                         vr::ecs::Float3 position_,
                         vr::ecs::Float3 euler_radians_,
                         vr::ecs::Float3 scale_) {
    TransformSystem3D::Initialize(transform_);
    TransformSystem3D::SetLocalPosition(transform_, position_);
    TransformSystem3D::SetLocalRotationEulerXyz(transform_,
                                                euler_radians_.x,
                                                euler_radians_.y,
                                                euler_radians_.z);
    TransformSystem3D::SetLocalScale(transform_, scale_);
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
        create_info.platform.window.title = "Vulkan SDL3 Text 3D Showcase";
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
        face_create_info_primary.pixel_height = 36U;
        const vr::text::FontFaceId face_primary = runtime.FreeType().RegisterFace(face_create_info_primary);

        vr::text::FontFaceCreateInfo face_create_info_secondary{};
        face_create_info_secondary.file_path = fonts.secondary;
        face_create_info_secondary.pixel_height = 36U;
        const vr::text::FontFaceId face_secondary = runtime.FreeType().RegisterFace(face_create_info_secondary);

        runtime.GlyphAtlas().MapFont(1U, face_primary);
        runtime.GlyphAtlas().MapFont(2U, face_secondary.IsValid() ? face_secondary : face_primary);

        std::array<Text3D, k_component_count> components{};
        InitializeTextComponent(components[rotating_title],
                                1U,
                                1U,
                                0.78F,
                                280.0F,
                                vr::ecs::Rgba8{245U, 245U, 245U, 255U},
                                false,
                                true,
                                true,
                                true,
                                true,
                                1U,
                                vr::ecs::Rgba8{16U, 22U, 36U, 255U},
                                40U,
                                "Melosyne 3D Text");

        InitializeTextComponent(components[billboard_subtitle],
                                1U,
                                1U,
                                0.44F,
                                220.0F,
                                vr::ecs::Rgba8{196U, 228U, 255U, 255U},
                                true,
                                true,
                                false,
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                50U,
                                "Billboard text + camera-facing basis");

        InitializeTextComponent(components[depth_back],
                                1U,
                                2U,
                                0.46F,
                                240.0F,
                                vr::ecs::Rgba8{190U, 220U, 255U, 255U},
                                false,
                                true,
                                false,
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                70U,
                                "Depth back (test on, write off)");

        InitializeTextComponent(components[depth_front_write],
                                1U,
                                2U,
                                0.50F,
                                250.0F,
                                vr::ecs::Rgba8{255U, 206U, 156U, 255U},
                                false,
                                true,
                                true,
                                true,
                                true,
                                1U,
                                vr::ecs::Rgba8{36U, 18U, 6U, 255U},
                                80U,
                                "Depth front (test+write)");

        InitializeTextComponent(components[no_depth_overlay],
                                1U,
                                1U,
                                0.42F,
                                220.0F,
                                vr::ecs::Rgba8{170U, 255U, 176U, 255U},
                                true,
                                false,
                                false,
                                false,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                10U,
                                "NoDepth label (test off)");

        InitializeTextComponent(components[sdf_outline],
                                2U,
                                1U,
                                0.40F,
                                200.0F,
                                vr::ecs::Rgba8{255U, 250U, 230U, 255U},
                                true,
                                true,
                                false,
                                true,
                                true,
                                2U,
                                vr::ecs::Rgba8{16U, 26U, 58U, 255U},
                                55U,
                                "SDF + outline + Font#2");

        InitializeTextComponent(components[bitmap_label],
                                2U,
                                1U,
                                0.36F,
                                170.0F,
                                vr::ecs::Rgba8{188U, 255U, 216U, 255U},
                                true,
                                true,
                                false,
                                false,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                56U,
                                "Bitmap raster path (SDF off)");

        InitializeTextComponent(components[max_screen_clamped],
                                1U,
                                1U,
                                1.10F,
                                58.0F,
                                vr::ecs::Rgba8{255U, 236U, 182U, 255U},
                                true,
                                true,
                                false,
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                58U,
                                "World-size big + max_screen_size clamp");

        InitializeTextComponent(components[runtime_modes],
                                1U,
                                1U,
                                0.30F,
                                190.0F,
                                vr::ecs::Rgba8{224U, 228U, 255U, 255U},
                                true,
                                false,
                                false,
                                true,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                5U,
                                "Mode info");

        InitializeTextComponent(components[runtime_stats],
                                1U,
                                1U,
                                0.27F,
                                180.0F,
                                vr::ecs::Rgba8{206U, 240U, 186U, 255U},
                                true,
                                false,
                                false,
                                false,
                                false,
                                0U,
                                vr::ecs::Rgba8{0U, 0U, 0U, 255U},
                                6U,
                                "Stats");

        std::array<Transform3D, k_component_count> transforms{};
        InitializeTransform(transforms[rotating_title],
                            vr::ecs::Float3{.x = -2.1F, .y = 1.25F, .z = 0.30F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.30F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[billboard_subtitle],
                            vr::ecs::Float3{.x = -2.3F, .y = 0.62F, .z = 0.00F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[depth_back],
                            vr::ecs::Float3{.x = -1.7F, .y = -0.20F, .z = 0.45F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[depth_front_write],
                            vr::ecs::Float3{.x = -1.72F, .y = -0.22F, .z = 0.16F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.12F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[no_depth_overlay],
                            vr::ecs::Float3{.x = -1.6F, .y = -0.86F, .z = -0.20F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[sdf_outline],
                            vr::ecs::Float3{.x = 1.12F, .y = 0.95F, .z = 0.00F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[bitmap_label],
                            vr::ecs::Float3{.x = 1.08F, .y = 0.34F, .z = 0.00F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[max_screen_clamped],
                            vr::ecs::Float3{.x = 1.08F, .y = -0.26F, .z = 0.00F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[runtime_modes],
                            vr::ecs::Float3{.x = -2.3F, .y = -1.45F, .z = 0.00F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        InitializeTransform(transforms[runtime_stats],
                            vr::ecs::Float3{.x = -2.3F, .y = -1.86F, .z = 0.00F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

        Camera3D camera{};
        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetReverseZ(camera, false);

        Transform3D camera_transform{};
        InitializeTransform(camera_transform,
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 6.0F},
                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                            vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

        TransformSystem3D::UpdateHierarchy(transforms.data(),
                                           static_cast<std::uint32_t>(transforms.size()));
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::text::TextRenderer3D text_renderer{};
        vr::text::TextRenderer3DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.runtime_build.bitmap_render_mode = vr::text::GlyphRenderMode::light;
        text_renderer_create_info.reserve_component_count =
            static_cast<std::uint32_t>(components.size());
        text_renderer_create_info.reserve_glyph_count = 32768U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 4U * 1024U * 1024U;
        text_renderer_create_info.enable_depth = true;
        text_renderer_create_info.clear_depth = true;
        text_renderer_create_info.clear_depth_value = 1.0F;
        text_renderer_create_info.bitmap_gamma = 1.0F;
        text_renderer_create_info.bitmap_edge_sharpness = 1.12F;
        text_renderer_create_info.clear_swapchain = true;
        text_renderer_create_info.clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
        text_renderer.Initialize(text_renderer_create_info);
        text_renderer.SetSceneData(components.data(),
                                   transforms.data(),
                                   static_cast<std::uint32_t>(components.size()),
                                   &camera,
                                   &camera_transform);

        const std::string primary_name = FileNameOnly(fonts.primary);
        const std::string secondary_name = FileNameOnly(fonts.secondary);

        std::uint64_t frame_counter = 0U;
        std::uint64_t fps_window_begin = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;
        float fps = 0.0F;
        const std::uint64_t demo_start_ticks = SDL_GetTicks();

        bool reverse_z_enabled = false;
        vr::ecs::CameraProjectionMode projection_mode = vr::ecs::CameraProjectionMode::perspective;
        float cached_aspect_ratio = 1280.0F / 720.0F;

        std::cout << "sdl_text_3d_demo running (3D showcase). Close window to exit.\n";
        while (runtime.IsRunning()) {
            const std::uint64_t now_ticks = SDL_GetTicks();
            const std::uint32_t elapsed_ms = static_cast<std::uint32_t>(now_ticks - demo_start_ticks);
            const float time_seconds = static_cast<float>(elapsed_ms) * 0.001F;

            const bool target_reverse_z = ((elapsed_ms / 9000U) % 2U) != 0U;
            if (target_reverse_z != reverse_z_enabled) {
                reverse_z_enabled = target_reverse_z;
                CameraSystem3D::SetReverseZ(camera, reverse_z_enabled);
            }

            const vr::ecs::CameraProjectionMode target_projection_mode =
                ((elapsed_ms / 18000U) % 2U) == 0U
                    ? vr::ecs::CameraProjectionMode::perspective
                    : vr::ecs::CameraProjectionMode::orthographic;
            if (target_projection_mode != projection_mode) {
                projection_mode = target_projection_mode;
                CameraSystem3D::SetProjectionMode(camera, projection_mode);
                if (projection_mode == vr::ecs::CameraProjectionMode::orthographic) {
                    CameraSystem3D::SetOrthographicHeight(camera, 4.8F);
                }
            }

            const VkExtent2D extent = runtime.Swapchain().Extent();
            if (extent.width > 0U && extent.height > 0U) {
                const float aspect_ratio = static_cast<float>(extent.width) /
                                           static_cast<float>(extent.height);
                if (std::abs(aspect_ratio - cached_aspect_ratio) > 1e-4F) {
                    cached_aspect_ratio = aspect_ratio;
                    CameraSystem3D::SetAspectRatio(camera, cached_aspect_ratio);
                }
            }

            TransformSystem3D::SetLocalRotationEulerXyz(transforms[rotating_title],
                                                        0.0F,
                                                        0.0F,
                                                        0.30F + 0.55F * std::sin(time_seconds * 0.8F));
            TransformSystem3D::SetLocalPosition(transforms[billboard_subtitle],
                                                vr::ecs::Float3{
                                                    .x = -2.3F,
                                                    .y = 0.62F + 0.15F * std::sin(time_seconds * 1.1F),
                                                    .z = 0.00F});

            const float depth_front_z = 0.14F + 0.34F * std::sin(time_seconds * 0.9F);
            TransformSystem3D::SetLocalPosition(transforms[depth_front_write],
                                                vr::ecs::Float3{
                                                    .x = -1.72F,
                                                    .y = -0.22F,
                                                    .z = depth_front_z});
            TransformSystem3D::SetLocalRotationEulerXyz(transforms[depth_front_write],
                                                        0.0F,
                                                        0.0F,
                                                        0.10F + 0.20F * std::sin(time_seconds * 1.2F));

            const std::uint8_t pulse =
                static_cast<std::uint8_t>(120.0F + 120.0F * (0.5F + 0.5F * std::sin(time_seconds * 2.3F)));
            TextSystem3D::SetColor(components[no_depth_overlay],
                                   vr::ecs::Rgba8{100U, static_cast<std::uint8_t>(130U + pulse / 2U), 100U, 255U});

            TransformSystem3D::UpdateHierarchy(transforms.data(),
                                               static_cast<std::uint32_t>(transforms.size()));
            CameraSystem3D::Update(camera, camera_transform);

            const char* projection_name =
                (projection_mode == vr::ecs::CameraProjectionMode::perspective)
                    ? "Perspective"
                    : "Orthographic";

            char mode_text[240]{};
            std::snprintf(mode_text,
                          sizeof(mode_text),
                          "Mode: %s | ReverseZ: %s | Font1:%s | Font2:%s",
                          projection_name,
                          reverse_z_enabled ? "ON" : "OFF",
                          primary_name.c_str(),
                          secondary_name.c_str());
            (void)TextSystem3D::SetText(components[runtime_modes], mode_text);

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

            const vr::text::TextRenderer3DStats stats = text_renderer.Stats();
            char stats_text[240]{};
            std::snprintf(stats_text,
                          sizeof(stats_text),
                          "FPS: %.1f Frame:%llu Draw:%u Batch:%u Inst:%u DT:%u DW:%u",
                          fps,
                          static_cast<unsigned long long>(frame_counter),
                          stats.draw_call_count,
                          stats.draw_batch_count,
                          stats.instance_count,
                          stats.depth_test_batch_count,
                          stats.depth_write_batch_count);
            (void)TextSystem3D::SetText(components[runtime_stats], stats_text);

            SDL_Delay(8U);
            ++frame_counter;
        }

        text_renderer.Shutdown(runtime.Context());
        runtime.Shutdown();
        return 0;
    } catch (const std::exception& exception_) {
        std::cerr << "sdl_text_3d_demo failed: " << exception_.what() << '\n';
        return 1;
    }
}
