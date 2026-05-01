#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/geometry/geometry_renderer_2d.hpp"
#include "vr/geometry/geometry_upload_host.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_2d.hpp"
#include "vr/surface/surface_upload_host.hpp"
#include "vr/text/text_renderer_2d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;

using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;
using GeometryPathSystem = vr::ecs::GeometryPathSystem;
using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

constexpr std::uint32_t k_texture_id_checker = 4101U;
constexpr std::uint32_t k_texture_id_gradient = 4102U;
constexpr float k_pi = 3.14159265358979323846F;

[[nodiscard]] constexpr std::uint32_t PackRgba8(std::uint8_t r_,
                                                std::uint8_t g_,
                                                std::uint8_t b_,
                                                std::uint8_t a_) noexcept {
    return static_cast<std::uint32_t>(r_) |
           (static_cast<std::uint32_t>(g_) << 8U) |
           (static_cast<std::uint32_t>(b_) << 16U) |
           (static_cast<std::uint32_t>(a_) << 24U);
}

void FillCheckerTexture(std::uint32_t* pixels_,
                        std::uint32_t width_,
                        std::uint32_t height_,
                        std::uint32_t color_a_,
                        std::uint32_t color_b_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            pixels_[index] = (((x >> 3U) ^ (y >> 3U)) & 1U) != 0U ? color_a_ : color_b_;
        }
    }
}

void FillGradientTexture(std::uint32_t* pixels_,
                         std::uint32_t width_,
                         std::uint32_t height_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const float fx = static_cast<float>(x) / static_cast<float>(std::max(width_, 1U) - 1U);
            const float fy = static_cast<float>(y) / static_cast<float>(std::max(height_, 1U) - 1U);
            const std::uint8_t r = static_cast<std::uint8_t>(70.0F + 185.0F * fx);
            const std::uint8_t g = static_cast<std::uint8_t>(40.0F + 165.0F * (1.0F - fy));
            const std::uint8_t b = static_cast<std::uint8_t>(160.0F + 80.0F * fy);
            pixels_[index] = PackRgba8(r, g, b, 255U);
        }
    }
}

[[nodiscard]] std::string PickDemoFontPath() {
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
    for (const char* candidate : candidates) {
        const fs::path font_path(candidate);
        if (fs::exists(font_path) && fs::is_regular_file(font_path)) {
            return font_path.string();
        }
    }
    return {};
}

void InitializeRectPath(Geometry2D& component_,
                        std::uint32_t geometry_id_,
                        std::uint32_t material_id_,
                        std::int16_t layer_,
                        vr::ecs::Rgba8 fill_color_,
                        vr::ecs::Rgba8 stroke_color_,
                        float stroke_width_,
                        float min_x_,
                        float min_y_,
                        float max_x_,
                        float max_y_) {
    GeometryPathSystem::Initialize(component_);
    GeometrySystem2D::SetRuntimeRoute(component_, geometry_id_, material_id_, 0U);
    GeometrySystem2D::SetLayer(component_, layer_);
    component_.style.topology = vr::ecs::Geometry2DTopology::fill_and_stroke;
    component_.style.fill_color = fill_color_;
    component_.style.stroke_color = stroke_color_;
    component_.style.stroke_width_px = stroke_width_;
    (void)GeometryPathSystem::AppendMoveTo(component_, min_x_, min_y_);
    (void)GeometryPathSystem::AppendLineTo(component_, max_x_, min_y_);
    (void)GeometryPathSystem::AppendLineTo(component_, max_x_, max_y_);
    (void)GeometryPathSystem::AppendLineTo(component_, min_x_, max_y_);
    (void)GeometryPathSystem::AppendClose(component_);
}

void InitializeCurvePath(Geometry2D& component_,
                         std::uint32_t geometry_id_,
                         std::uint32_t material_id_,
                         std::int16_t layer_) {
    GeometryPathSystem::Initialize(component_);
    GeometrySystem2D::SetRuntimeRoute(component_, geometry_id_, material_id_, 0U);
    GeometrySystem2D::SetLayer(component_, layer_);
    component_.style.topology = vr::ecs::Geometry2DTopology::stroke;
    component_.style.stroke_color = vr::ecs::Rgba8{208U, 230U, 255U, 255U};
    component_.style.stroke_width_px = 5.0F;
    component_.style.line_cap = vr::ecs::Geometry2DLineCap::round;
    component_.style.line_join = vr::ecs::Geometry2DLineJoin::round;
    (void)GeometryPathSystem::AppendMoveTo(component_, 90.0F, 620.0F);
    (void)GeometryPathSystem::AppendCubicTo(component_,
                                            240.0F,
                                            420.0F,
                                            760.0F,
                                            780.0F,
                                            1160.0F,
                                            520.0F);
}

void InitializeSurface2DComponent(Surface2D& component_,
                                  std::uint32_t image_id_,
                                  float width_,
                                  float height_,
                                  std::int16_t layer_,
                                  vr::ecs::Rgba8 tint_color_,
                                  vr::ecs::Surface2DBlendMode blend_mode_) {
    SurfaceSystem2D::Initialize(component_);
    SurfaceSystem2D::SetImageId(component_, image_id_, 0U);
    SurfaceSystem2D::SetSize(component_, vr::ecs::Float2{.x = width_, .y = height_});
    SurfaceSystem2D::SetPivot(component_, vr::ecs::Float2{.x = 0.5F, .y = 0.5F});
    SurfaceSystem2D::SetLayer(component_, layer_);
    SurfaceSystem2D::SetTintColor(component_, tint_color_);
    SurfaceSystem2D::SetOpacity(component_, 1.0F);
    SurfaceSystem2D::SetBlendMode(component_, blend_mode_);
}

void InitializeTextComponent(Text2D& component_,
                             std::uint32_t font_id_,
                             std::uint32_t material_id_,
                             std::int16_t layer_,
                             float pixel_size_,
                             vr::ecs::Rgba8 color_,
                             std::string_view text_) {
    TextSystem2D::Initialize(component_);
    TextSystem2D::SetRuntimeRoute(component_, font_id_, material_id_, 0U, 0U);
    TextSystem2D::SetLayer(component_, layer_);
    TextSystem2D::SetPixelSize(component_, pixel_size_);
    TextSystem2D::SetColor(component_, color_);
    TextSystem2D::SetSdfEnabled(component_, true);
    TextSystem2D::SetOutlineEnabled(component_, false);
    TextSystem2D::SetHorizontalAlign(component_, vr::ecs::TextHorizontalAlign::left);
    TextSystem2D::SetVerticalAlign(component_, vr::ecs::TextVerticalAlign::top);
    (void)TextSystem2D::SetText(component_, text_);
}

struct OffscreenPostProcessRecorder final {
    Runtime* runtime = nullptr;
    vr::geometry::GeometryRenderer2D geometry_renderer{};
    vr::surface::SurfaceRenderer2D surface_renderer{};
    vr::text::TextRenderer2D text_renderer{};
    vr::render::RenderTargetCompositeRenderer composite_renderer{};
    vr::render::SceneRenderTargetSet scene_targets{};

    void InitializeSceneTargets() {
        vr::render::SceneRenderTargetSetCreateInfo create_info{};
        create_info.color_debug_name = "OffscreenSceneColor";
        create_info.enable_depth = false;
        create_info.color_lifetime = vr::render::RenderTargetLifetime::transient;
        create_info.additional_color_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        create_info.clear_color = VkClearColorValue{{0.025F, 0.030F, 0.060F, 1.0F}};
        scene_targets.Initialize(create_info);
    }

    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        (void)scene_targets.PrepareFrameAndConfigure(
            prepare_context_,
            &composite_renderer,
            vr::render::BindSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::first),
            vr::render::BindSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::middle),
            vr::render::BindSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::last));
        geometry_renderer.PrepareFrame(prepare_context_);
        surface_renderer.PrepareFrame(prepare_context_);
        text_renderer.PrepareFrame(prepare_context_);
        composite_renderer.PrepareFrame(prepare_context_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        geometry_renderer.Record(record_context_);
        surface_renderer.Record(record_context_);
        text_renderer.Record(record_context_);
        composite_renderer.Record(record_context_);
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) {
        geometry_renderer.OnSwapchainRecreated(image_count_,
                                               extent_,
                                               format_,
                                               last_submitted_value_,
                                               completed_submit_value_);
        surface_renderer.OnSwapchainRecreated(image_count_,
                                              extent_,
                                              format_,
                                              last_submitted_value_,
                                              completed_submit_value_);
        text_renderer.OnSwapchainRecreated(image_count_, extent_, format_);
        composite_renderer.OnSwapchainRecreated(image_count_, extent_, format_);

        if (runtime == nullptr) {
            return;
        }

        (void)scene_targets.OnSwapchainRecreatedAndConfigure(
            runtime->Context(),
            runtime->RenderTarget(),
            runtime->HasRenderTargetPool() ? &runtime->TargetPool() : nullptr,
            extent_,
            last_submitted_value_,
            completed_submit_value_,
            &composite_renderer,
            vr::render::BindSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::first),
            vr::render::BindSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::middle),
            vr::render::BindSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::last));
    }
};

} // namespace

int main(int argc_, char** argv_) {
    try {
        std::uint32_t max_frames = 0U;
        for (int arg_index = 1; arg_index + 1 < argc_; ++arg_index) {
            if (std::string_view(argv_[arg_index]) == "--frames") {
                max_frames = static_cast<std::uint32_t>(std::strtoul(argv_[arg_index + 1], nullptr, 10));
            }
        }

        const std::string font_path = PickDemoFontPath();
        if (font_path.empty()) {
            throw std::runtime_error("No usable Windows font found for offscreen text overlay demo");
        }

        Runtime runtime{};
        vr::surface::SurfaceUploadHost surface_upload_host{};
        vr::surface::SurfaceImageHost surface_image_host{};
        vr::geometry::GeometryUploadHost geometry_upload_host{};
        OffscreenPostProcessRecorder recorder{};

        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Offscreen PostProcess Demo";
        create_info.platform.window.width = 1366;
        create_info.platform.window.height = 768;
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

        recorder.runtime = &runtime;
        recorder.InitializeSceneTargets();

        vr::surface::SurfaceUploadHostCreateInfo surface_upload_create_info{};
        surface_upload_create_info.frames_in_flight = 2U;
        surface_upload_create_info.initial_2d_instance_buffer_bytes = 512U * 1024U;
        surface_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), surface_upload_create_info);

        vr::surface::SurfaceImageHostCreateInfo surface_image_create_info{};
        surface_image_create_info.reserve_image_count = 32U;
        surface_image_create_info.reserve_retired_image_count = 32U;
        surface_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), surface_image_create_info);

        vr::geometry::GeometryUploadHostCreateInfo geometry_upload_create_info{};
        geometry_upload_create_info.frames_in_flight = 2U;
        geometry_upload_create_info.initial_2d_primitive_buffer_bytes = 512U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(),
                                        runtime.GpuMemory(),
                                        geometry_upload_create_info);

        vr::text::FontFaceCreateInfo font_create_info{};
        font_create_info.file_path = font_path;
        font_create_info.pixel_height = 34U;
        const vr::text::FontFaceId font_face_id = runtime.FreeType().RegisterFace(font_create_info);
        runtime.GlyphAtlas().MapFont(1U, font_face_id);

        constexpr std::uint32_t texture_width = 64U;
        constexpr std::uint32_t texture_height = 64U;
        std::array<std::uint32_t, texture_width * texture_height> pixels_checker{};
        std::array<std::uint32_t, texture_width * texture_height> pixels_gradient{};
        FillCheckerTexture(pixels_checker.data(),
                           texture_width,
                           texture_height,
                           PackRgba8(255U, 232U, 180U, 255U),
                           PackRgba8(120U, 84U, 52U, 255U));
        FillGradientTexture(pixels_gradient.data(), texture_width, texture_height);

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        {
            vr::surface::SurfaceImageUploadInfo upload_info{};
            upload_info.width = texture_width;
            upload_info.height = texture_height;
            upload_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            upload_info.bytes_per_pixel = 4U;
            upload_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            upload_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            upload_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;

            upload_info.image_id = k_texture_id_checker;
            upload_info.pixels = pixels_checker.data();
            surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

            upload_info.image_id = k_texture_id_gradient;
            upload_info.pixels = pixels_gradient.data();
            surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);
        }
        const vr::render::UploadEndFrameResult upload_end_result =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end_result.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        surface_image_host.BeginFrame(runtime.Context(), 0U);

        std::array<Geometry2D, 2U> geometry_components{};
        InitializeRectPath(geometry_components[0U],
                           1U,
                           1U,
                           -20,
                           vr::ecs::Rgba8{42U, 52U, 82U, 255U},
                           vr::ecs::Rgba8{114U, 142U, 188U, 255U},
                           3.0F,
                           120.0F,
                           110.0F,
                           1210.0F,
                           660.0F);
        InitializeCurvePath(geometry_components[1U], 2U, 2U, -10);

        std::array<Surface2D, 2U> surface_components{};
        std::array<Transform2D, 2U> surface_transforms{};
        for (auto& transform : surface_transforms) {
            TransformSystem2D::Initialize(transform);
        }
        InitializeSurface2DComponent(surface_components[0U],
                                     k_texture_id_checker,
                                     280.0F,
                                     210.0F,
                                     0,
                                     vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                     vr::ecs::Surface2DBlendMode::alpha);
        InitializeSurface2DComponent(surface_components[1U],
                                     k_texture_id_gradient,
                                     360.0F,
                                     200.0F,
                                     1,
                                     vr::ecs::Rgba8{255U, 255U, 255U, 220U},
                                     vr::ecs::Surface2DBlendMode::alpha);
        TransformSystem2D::SetLocalPosition(surface_transforms[0U], 360.0F, 290.0F);
        TransformSystem2D::SetLocalPosition(surface_transforms[1U], 915.0F, 365.0F);
        TransformSystem2D::UpdateHierarchy(surface_transforms.data(),
                                           static_cast<std::uint32_t>(surface_transforms.size()));

        std::array<Text2D, 2U> text_components{};
        InitializeTextComponent(text_components[0U],
                                1U,
                                1U,
                                40,
                                24.0F,
                                vr::ecs::Rgba8{235U, 244U, 255U, 255U},
                                "Offscreen Scene -> Tone Map -> Present");
        InitializeTextComponent(text_components[1U],
                                1U,
                                1U,
                                41,
                                18.0F,
                                vr::ecs::Rgba8{188U, 226U, 196U, 255U},
                                "\n\nFPS: -- | Geometry+Surface+Text offscreen composite");

        vr::geometry::GeometryRenderer2DCreateInfo geometry_renderer_create_info{};
        geometry_renderer_create_info.clear_swapchain = false;
        recorder.geometry_renderer.Initialize(geometry_renderer_create_info);
        recorder.geometry_renderer.SetHost(&geometry_upload_host);
        recorder.geometry_renderer.SetSceneData(geometry_components.data(),
                                               static_cast<std::uint32_t>(geometry_components.size()));

        vr::surface::SurfaceRenderer2DCreateInfo surface_renderer_create_info{};
        surface_renderer_create_info.clear_swapchain = false;
        surface_renderer_create_info.enable_light_shadow = false;
        recorder.surface_renderer.Initialize(surface_renderer_create_info);
        recorder.surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        recorder.surface_renderer.SetSceneData(surface_components.data(),
                                              surface_transforms.data(),
                                              static_cast<std::uint32_t>(surface_components.size()));

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.clear_swapchain = false;
        recorder.text_renderer.Initialize(text_renderer_create_info);
        recorder.text_renderer.SetComponents(text_components.data(),
                                            static_cast<std::uint32_t>(text_components.size()));

        vr::render::RenderTargetCompositeRendererCreateInfo composite_create_info{};
        composite_create_info.clear_swapchain = true;
        composite_create_info.enable_reinhard_tonemap = true;
        composite_create_info.exposure = 1.15F;
        composite_create_info.apply_manual_gamma = false;
        recorder.composite_renderer.Initialize(composite_create_info);

        std::cout << "sdl_offscreen_postprocess_demo running. Close window to exit.\n";

        std::uint32_t frame_counter = 0U;
        std::uint64_t stats_last_tick = SDL_GetTicks();
        std::uint32_t stats_frame_counter = 0U;
        while (runtime.IsRunning()) {
            const float time_seconds = static_cast<float>(SDL_GetTicks()) * 0.001F;
            TransformSystem2D::SetLocalRotationRadians(surface_transforms[0U],
                                                       0.35F * std::sin(time_seconds * 1.3F));
            TransformSystem2D::SetLocalRotationRadians(surface_transforms[1U],
                                                       -0.20F * std::sin(time_seconds * 0.9F));
            const float pulse_scale = 0.88F + 0.18F * (0.5F + 0.5F * std::sin(time_seconds * 1.7F));
            TransformSystem2D::SetLocalScale(surface_transforms[1U], pulse_scale, 1.0F);
            TransformSystem2D::UpdateHierarchy(surface_transforms.data(),
                                               static_cast<std::uint32_t>(surface_transforms.size()));

            ++frame_counter;
            ++stats_frame_counter;
            const std::uint64_t now_ticks = SDL_GetTicks();
            if (now_ticks - stats_last_tick >= 1000U) {
                const double seconds = static_cast<double>(now_ticks - stats_last_tick) / 1000.0;
                const double fps = seconds > 0.0 ? static_cast<double>(stats_frame_counter) / seconds : 0.0;
                stats_last_tick = now_ticks;
                stats_frame_counter = 0U;

                std::string stats_text =
                    "\n\nFPS: " + std::to_string(static_cast<int>(std::round(fps))) +
                    " | Draw(surface/text/comp): " +
                    std::to_string(recorder.surface_renderer.Stats().draw_call_count) + "/" +
                    std::to_string(recorder.text_renderer.Stats().draw_call_count) + "/" +
                    std::to_string(recorder.composite_renderer.Stats().draw_call_count);
                (void)TextSystem2D::SetText(text_components[1U], stats_text);
            }

            const auto tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::SkippedWindowHidden) {
                SDL_Delay(8);
                continue;
            }

            if (max_frames > 0U && frame_counter >= max_frames) {
                break;
            }
            SDL_Delay(8);
        }

        recorder.composite_renderer.Shutdown(runtime.Context());
        recorder.text_renderer.Shutdown(runtime.Context());
        recorder.surface_renderer.Shutdown(runtime.Context());
        recorder.geometry_renderer.Shutdown(runtime.Context());
        geometry_upload_host.Shutdown(runtime.Context());
        surface_image_host.Shutdown(runtime.Context());
        surface_upload_host.Shutdown(runtime.Context());
        runtime.Shutdown();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "sdl_offscreen_postprocess_demo failed: " << ex.what() << '\n';
        return 1;
    }
}
