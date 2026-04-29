#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/light_system.hpp"
#include "vr/ecs/system/shadow_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/light_frame_coordinator.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/shadow/shadow_renderer_2d.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_2d.hpp"
#include "vr/surface/surface_upload_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <stdexcept>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
using Bounds2D = vr::ecs::Bounds<vr::ecs::Dim2>;
using Camera2D = vr::ecs::Camera<vr::ecs::Dim2>;
using Light2D = vr::ecs::Light<vr::ecs::Dim2>;
using Shadow2D = vr::ecs::Shadow<vr::ecs::Dim2>;

using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
using BoundsSystem2D = vr::ecs::BoundsSystem<vr::ecs::Dim2>;
using CameraSystem2D = vr::ecs::CameraSystem<vr::ecs::Dim2>;
using LightSystem2D = vr::ecs::LightSystem<vr::ecs::Dim2>;
using ShadowSystem2D = vr::ecs::ShadowSystem<vr::ecs::Dim2>;

constexpr std::uint32_t texture_id_background = 4201U;
constexpr std::uint32_t texture_id_panel = 4202U;
constexpr std::uint32_t texture_id_occluder = 4203U;
constexpr bool default_freeze_animation = true;
constexpr bool default_log_every_frame = true;
constexpr std::uint32_t default_frame_interval_ms = 1000U;

constexpr float pi = 3.14159265358979323846F;

struct DemoLaunchOptions final {
    std::uint32_t frame_interval_ms = default_frame_interval_ms;
    std::uint32_t max_frame_count = 0U;
    bool freeze_animation = default_freeze_animation;
    bool log_every_frame = default_log_every_frame;
    bool print_help = false;

    float ambient = 0.08F;
    bool has_ambient = false;

    bool cast_shadow = true;
    bool has_cast_shadow = false;

    float light0_intensity = 1.8F;
    bool has_light0_intensity = false;
    float light0_range = 360.0F;
    bool has_light0_range = false;
    float light0_falloff = 1.8F;
    bool has_light0_falloff = false;
    float light0_height = 74.0F;
    bool has_light0_height = false;

    float light1_intensity = 1.25F;
    bool has_light1_intensity = false;
    float light1_range = 640.0F;
    bool has_light1_range = false;
    float light1_inner = 0.32F;
    bool has_light1_inner = false;
    float light1_outer = 0.78F;
    bool has_light1_outer = false;
    float light1_height = 54.0F;
    bool has_light1_height = false;
};

[[nodiscard]] bool TryParseU32(const char* text_,
                               std::uint32_t& value_out_) noexcept {
    if (text_ == nullptr || text_[0] == '\0') {
        return false;
    }

    char* end_ptr = nullptr;
    const unsigned long parsed = std::strtoul(text_, &end_ptr, 10);
    if (end_ptr == nullptr || *end_ptr != '\0') {
        return false;
    }
    if (parsed > static_cast<unsigned long>((std::numeric_limits<std::uint32_t>::max)())) {
        return false;
    }

    value_out_ = static_cast<std::uint32_t>(parsed);
    return true;
}

[[nodiscard]] bool TryParseF32(const char* text_,
                               float& value_out_) noexcept {
    if (text_ == nullptr || text_[0] == '\0') {
        return false;
    }

    char* end_ptr = nullptr;
    const float parsed = std::strtof(text_, &end_ptr);
    if (end_ptr == nullptr || *end_ptr != '\0') {
        return false;
    }
    if (!std::isfinite(parsed)) {
        return false;
    }

    value_out_ = parsed;
    return true;
}

void PrintUsage() {
    std::cout
        << "Usage: sdl_surface_light_shadow_2d_demo [options]\n"
        << "  --frame-interval-ms <N>  Frame interval in milliseconds (0 = uncapped)\n"
        << "  --fps <N>                Target FPS (N > 0, overrides frame interval)\n"
        << "  --frames <N>             Auto-exit after N frames (0 = run until close)\n"
        << "  --freeze <0|1>           Freeze animation time (default: 1)\n"
        << "  --log-every-frame <0|1>  Print per-frame debug log (default: 1)\n"
        << "  --ambient <F>            Surface ambient light (default: 0.08)\n"
        << "  --shadow <0|1>           Enable cast_shadow for all demo lights\n"
        << "  --light0-intensity <F>   Point light intensity\n"
        << "  --light0-range <F>       Point light range\n"
        << "  --light0-falloff <F>     Point light falloff exponent\n"
        << "  --light0-height <F>      Point light source height\n"
        << "  --light1-intensity <F>   Spot light intensity\n"
        << "  --light1-range <F>       Spot light range\n"
        << "  --light1-inner <F>       Spot inner cone angle (radians)\n"
        << "  --light1-outer <F>       Spot outer cone angle (radians)\n"
        << "  --light1-height <F>      Spot light source height\n"
        << "  --help                   Show this help\n";
}

[[nodiscard]] DemoLaunchOptions ParseLaunchOptions(int argc_,
                                                   char** argv_) {
    DemoLaunchOptions options{};
    if (argc_ <= 1 || argv_ == nullptr) {
        return options;
    }

    for (int i = 1; i < argc_; ++i) {
        const char* arg = argv_[i];
        if (arg == nullptr) {
            continue;
        }

        auto require_value = [&](const char* option_name_) -> const char* {
            if (i + 1 >= argc_) {
                throw std::runtime_error(std::string("Missing value for option: ") + option_name_);
            }
            ++i;
            return argv_[i];
        };

        if (std::strcmp(arg, "--help") == 0) {
            options.print_help = true;
            continue;
        }
        if (std::strcmp(arg, "--frame-interval-ms") == 0) {
            const char* value_text = require_value("--frame-interval-ms");
            std::uint32_t parsed = 0U;
            if (!TryParseU32(value_text, parsed)) {
                throw std::runtime_error("Invalid --frame-interval-ms value");
            }
            options.frame_interval_ms = parsed;
            continue;
        }
        if (std::strcmp(arg, "--fps") == 0) {
            const char* value_text = require_value("--fps");
            std::uint32_t parsed = 0U;
            if (!TryParseU32(value_text, parsed) || parsed == 0U) {
                throw std::runtime_error("Invalid --fps value (must be > 0)");
            }
            const std::uint32_t clamped_fps = std::max<std::uint32_t>(parsed, 1U);
            options.frame_interval_ms = std::max<std::uint32_t>(1U, 1000U / clamped_fps);
            continue;
        }
        if (std::strcmp(arg, "--frames") == 0) {
            const char* value_text = require_value("--frames");
            std::uint32_t parsed = 0U;
            if (!TryParseU32(value_text, parsed)) {
                throw std::runtime_error("Invalid --frames value");
            }
            options.max_frame_count = parsed;
            continue;
        }
        if (std::strcmp(arg, "--freeze") == 0) {
            const char* value_text = require_value("--freeze");
            std::uint32_t parsed = 0U;
            if (!TryParseU32(value_text, parsed) || (parsed != 0U && parsed != 1U)) {
                throw std::runtime_error("Invalid --freeze value (must be 0 or 1)");
            }
            options.freeze_animation = (parsed != 0U);
            continue;
        }
        if (std::strcmp(arg, "--log-every-frame") == 0) {
            const char* value_text = require_value("--log-every-frame");
            std::uint32_t parsed = 0U;
            if (!TryParseU32(value_text, parsed) || (parsed != 0U && parsed != 1U)) {
                throw std::runtime_error("Invalid --log-every-frame value (must be 0 or 1)");
            }
            options.log_every_frame = (parsed != 0U);
            continue;
        }
        if (std::strcmp(arg, "--ambient") == 0) {
            const char* value_text = require_value("--ambient");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --ambient value");
            }
            options.ambient = parsed;
            options.has_ambient = true;
            continue;
        }
        if (std::strcmp(arg, "--shadow") == 0) {
            const char* value_text = require_value("--shadow");
            std::uint32_t parsed = 0U;
            if (!TryParseU32(value_text, parsed) || (parsed != 0U && parsed != 1U)) {
                throw std::runtime_error("Invalid --shadow value (must be 0 or 1)");
            }
            options.cast_shadow = parsed != 0U;
            options.has_cast_shadow = true;
            continue;
        }
        if (std::strcmp(arg, "--light0-intensity") == 0) {
            const char* value_text = require_value("--light0-intensity");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light0-intensity value");
            }
            options.light0_intensity = parsed;
            options.has_light0_intensity = true;
            continue;
        }
        if (std::strcmp(arg, "--light0-range") == 0) {
            const char* value_text = require_value("--light0-range");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light0-range value");
            }
            options.light0_range = parsed;
            options.has_light0_range = true;
            continue;
        }
        if (std::strcmp(arg, "--light0-falloff") == 0) {
            const char* value_text = require_value("--light0-falloff");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light0-falloff value");
            }
            options.light0_falloff = parsed;
            options.has_light0_falloff = true;
            continue;
        }
        if (std::strcmp(arg, "--light0-height") == 0) {
            const char* value_text = require_value("--light0-height");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light0-height value");
            }
            options.light0_height = parsed;
            options.has_light0_height = true;
            continue;
        }
        if (std::strcmp(arg, "--light1-intensity") == 0) {
            const char* value_text = require_value("--light1-intensity");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light1-intensity value");
            }
            options.light1_intensity = parsed;
            options.has_light1_intensity = true;
            continue;
        }
        if (std::strcmp(arg, "--light1-range") == 0) {
            const char* value_text = require_value("--light1-range");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light1-range value");
            }
            options.light1_range = parsed;
            options.has_light1_range = true;
            continue;
        }
        if (std::strcmp(arg, "--light1-inner") == 0) {
            const char* value_text = require_value("--light1-inner");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light1-inner value");
            }
            options.light1_inner = parsed;
            options.has_light1_inner = true;
            continue;
        }
        if (std::strcmp(arg, "--light1-outer") == 0) {
            const char* value_text = require_value("--light1-outer");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light1-outer value");
            }
            options.light1_outer = parsed;
            options.has_light1_outer = true;
            continue;
        }
        if (std::strcmp(arg, "--light1-height") == 0) {
            const char* value_text = require_value("--light1-height");
            float parsed = 0.0F;
            if (!TryParseF32(value_text, parsed)) {
                throw std::runtime_error("Invalid --light1-height value");
            }
            options.light1_height = parsed;
            options.has_light1_height = true;
            continue;
        }

        throw std::runtime_error(std::string("Unknown option: ") + arg);
    }

    return options;
}

[[nodiscard]] constexpr std::uint32_t PackRgba8(std::uint8_t r_,
                                                std::uint8_t g_,
                                                std::uint8_t b_,
                                                std::uint8_t a_) noexcept {
    return static_cast<std::uint32_t>(r_) |
           (static_cast<std::uint32_t>(g_) << 8U) |
           (static_cast<std::uint32_t>(b_) << 16U) |
           (static_cast<std::uint32_t>(a_) << 24U);
}

void FillBackgroundTexture(std::uint32_t* pixels_,
                           std::uint32_t width_,
                           std::uint32_t height_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const float fx = static_cast<float>(x) / static_cast<float>(width_ - 1U);
            const float fy = static_cast<float>(y) / static_cast<float>(height_ - 1U);
            const float wave = 0.5F + 0.5F * std::sin((fx * 6.0F + fy * 4.0F) * pi);
            const std::uint8_t r = static_cast<std::uint8_t>(18.0F + 22.0F * wave);
            const std::uint8_t g = static_cast<std::uint8_t>(22.0F + 26.0F * wave);
            const std::uint8_t b = static_cast<std::uint8_t>(34.0F + 32.0F * wave);
            pixels_[index] = PackRgba8(r, g, b, 255U);
        }
    }
}

void FillPanelTexture(std::uint32_t* pixels_,
                      std::uint32_t width_,
                      std::uint32_t height_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const float fx = static_cast<float>(x) / static_cast<float>(width_ - 1U);
            const float fy = static_cast<float>(y) / static_cast<float>(height_ - 1U);
            const std::uint8_t r = static_cast<std::uint8_t>(60.0F + 70.0F * fx);
            const std::uint8_t g = static_cast<std::uint8_t>(62.0F + 64.0F * fy);
            const std::uint8_t b = static_cast<std::uint8_t>(72.0F + 74.0F * (1.0F - fx));
            pixels_[index] = PackRgba8(r, g, b, 255U);
        }
    }
}

void FillOccluderTexture(std::uint32_t* pixels_,
                         std::uint32_t width_,
                         std::uint32_t height_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const bool checker = (((x >> 4U) ^ (y >> 4U)) & 0x1U) != 0U;
            const std::uint8_t base = checker ? 220U : 180U;
            pixels_[index] = PackRgba8(base, base, base, 255U);
        }
    }
}

void InitializeSurfaceComponent(Surface2D& component_,
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
    SurfaceSystem2D::SetVisible(component_, true);
}

struct SurfaceLightShadow2DRecorder final {
    vr::shadow::ShadowRenderer2D shadow_renderer{};
    vr::surface::SurfaceRenderer2D surface_renderer{};

    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        shadow_renderer.PrepareFrame(prepare_context_);
        surface_renderer.PrepareFrame(prepare_context_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        shadow_renderer.Record(record_context_);
        surface_renderer.Record(record_context_);
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) {
        (void)image_count_;
        (void)last_submitted_value_;
        (void)completed_submit_value_;
        surface_renderer.OnSwapchainRecreated(image_count_,
                                              extent_,
                                              format_,
                                              last_submitted_value_,
                                              completed_submit_value_);
    }
};

} // namespace

int main(int argc_, char** argv_) {
    Runtime runtime{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    SurfaceLightShadow2DRecorder recorder{};
    vr::render::LightFrameCoordinator<vr::ecs::Dim2> light_frame_coordinator{};

    bool runtime_initialized = false;
    bool upload_host_initialized = false;
    bool image_host_initialized = false;
    bool shadow_renderer_initialized = false;
    bool surface_renderer_initialized = false;

    try {
        const DemoLaunchOptions launch_options = ParseLaunchOptions(argc_, argv_);
        if (launch_options.print_help) {
            PrintUsage();
            return 0;
        }

        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Surface Light+Shadow 2D Demo";
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
        runtime_initialized = true;

        vr::surface::SurfaceUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_instance_buffer_bytes = 1024U * 1024U;
        upload_create_info.patch_merge_gap_bytes = 64U;
        surface_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_host_initialized = true;

        vr::surface::SurfaceImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 64U;
        image_create_info.reserve_retired_image_count = 64U;
        surface_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), image_create_info);
        image_host_initialized = true;

        constexpr std::uint32_t texture_width = 128U;
        constexpr std::uint32_t texture_height = 128U;
        std::array<std::uint32_t, texture_width * texture_height> pixels_background{};
        std::array<std::uint32_t, texture_width * texture_height> pixels_panel{};
        std::array<std::uint32_t, texture_width * texture_height> pixels_occluder{};
        FillBackgroundTexture(pixels_background.data(), texture_width, texture_height);
        FillPanelTexture(pixels_panel.data(), texture_width, texture_height);
        FillOccluderTexture(pixels_occluder.data(), texture_width, texture_height);

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

            upload_info.image_id = texture_id_background;
            upload_info.pixels = pixels_background.data();
            surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

            upload_info.image_id = texture_id_panel;
            upload_info.pixels = pixels_panel.data();
            surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

            upload_info.image_id = texture_id_occluder;
            upload_info.pixels = pixels_occluder.data();
            surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);
        }
        const vr::render::UploadEndFrameResult upload_end_result =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end_result.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        surface_image_host.BeginFrame(runtime.Context(), 0U);

        std::array<Surface2D, 4U> surface_components{};
        std::array<Transform2D, 4U> surface_transforms{};
        InitializeSurfaceComponent(surface_components[0U],
                                   texture_id_background,
                                   1600.0F,
                                   900.0F,
                                   0,
                                   vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                   vr::ecs::Surface2DBlendMode::alpha);
        InitializeSurfaceComponent(surface_components[1U],
                                   texture_id_panel,
                                   420.0F,
                                   260.0F,
                                   10,
                                   vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                   vr::ecs::Surface2DBlendMode::alpha);
        InitializeSurfaceComponent(surface_components[2U],
                                   texture_id_occluder,
                                   160.0F,
                                   240.0F,
                                   20,
                                   vr::ecs::Rgba8{255U, 250U, 246U, 255U},
                                   vr::ecs::Surface2DBlendMode::alpha);
        InitializeSurfaceComponent(surface_components[3U],
                                   texture_id_occluder,
                                   220.0F,
                                   120.0F,
                                   22,
                                   vr::ecs::Rgba8{220U, 246U, 255U, 255U},
                                   vr::ecs::Surface2DBlendMode::alpha);

        for (Transform2D& transform : surface_transforms) {
            TransformSystem2D::Initialize(transform);
        }
        TransformSystem2D::SetLocalPosition(surface_transforms[0U], 683.0F, 384.0F);
        TransformSystem2D::SetLocalPosition(surface_transforms[1U], 683.0F, 200.0F);
        TransformSystem2D::SetLocalPosition(surface_transforms[2U], 520.0F, 470.0F);
        TransformSystem2D::SetLocalPosition(surface_transforms[3U], 820.0F, 520.0F);
        TransformSystem2D::UpdateHierarchy(surface_transforms.data(),
                                           static_cast<std::uint32_t>(surface_transforms.size()));

        std::array<Bounds2D, 2U> caster_bounds{};
        std::array<Transform2D, 2U> caster_transforms{};
        for (Bounds2D& bounds : caster_bounds) {
            BoundsSystem2D::Initialize(bounds);
        }
        for (Transform2D& transform : caster_transforms) {
            TransformSystem2D::Initialize(transform);
        }
        BoundsSystem2D::SetLocalCenterExtents(caster_bounds[0U],
                                              vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
                                              vr::ecs::Float2{.x = 80.0F, .y = 120.0F});
        BoundsSystem2D::SetLocalCenterExtents(caster_bounds[1U],
                                              vr::ecs::Float2{.x = 0.0F, .y = 0.0F},
                                              vr::ecs::Float2{.x = 110.0F, .y = 60.0F});
        TransformSystem2D::SetLocalPosition(caster_transforms[0U], 520.0F, 470.0F);
        TransformSystem2D::SetLocalPosition(caster_transforms[1U], 820.0F, 520.0F);
        TransformSystem2D::UpdateHierarchy(caster_transforms.data(),
                                           static_cast<std::uint32_t>(caster_transforms.size()));
        (void)BoundsSystem2D::UpdateAligned(caster_bounds.data(),
                                            caster_transforms.data(),
                                            static_cast<std::uint32_t>(caster_bounds.size()));

        Camera2D light_camera{};
        Transform2D light_camera_transform{};
        CameraSystem2D::Initialize(light_camera);
        TransformSystem2D::Initialize(light_camera_transform);
        CameraSystem2D::SetYDown(light_camera, true);
        CameraSystem2D::SetNearFar(light_camera, 0.0F, 1.0F);
        CameraSystem2D::SetZoom(light_camera, 1.0F);

        std::array<Light2D, 2U> light_components{};
        std::array<Transform2D, 2U> light_transforms{};
        for (Light2D& light : light_components) {
            LightSystem2D::Initialize(light);
        }
        for (Transform2D& transform : light_transforms) {
            TransformSystem2D::Initialize(transform);
        }

        LightSystem2D::SetLightKind(light_components[0U], vr::ecs::LightKind::point);
        LightSystem2D::SetColor(light_components[0U], vr::ecs::Rgba8{255U, 210U, 165U, 255U});
        LightSystem2D::SetIntensity(light_components[0U], 1.8F);
        LightSystem2D::SetRange(light_components[0U], 360.0F);
        LightSystem2D::SetFalloffExponent(light_components[0U], 1.8F);
        LightSystem2D::SetSourceHeight(light_components[0U], 74.0F);
        LightSystem2D::SetCastShadow(light_components[0U], true);
        if (launch_options.has_light0_intensity) {
            LightSystem2D::SetIntensity(light_components[0U], launch_options.light0_intensity);
        }
        if (launch_options.has_light0_range) {
            LightSystem2D::SetRange(light_components[0U], launch_options.light0_range);
        }
        if (launch_options.has_light0_falloff) {
            LightSystem2D::SetFalloffExponent(light_components[0U], launch_options.light0_falloff);
        }
        if (launch_options.has_light0_height) {
            LightSystem2D::SetSourceHeight(light_components[0U], launch_options.light0_height);
        }
        if (launch_options.has_cast_shadow) {
            LightSystem2D::SetCastShadow(light_components[0U], launch_options.cast_shadow);
        }
        TransformSystem2D::SetLocalPosition(light_transforms[0U], 680.0F, 280.0F);

        LightSystem2D::SetLightKind(light_components[1U], vr::ecs::LightKind::spot);
        LightSystem2D::SetColor(light_components[1U], vr::ecs::Rgba8{170U, 220U, 255U, 255U});
        LightSystem2D::SetIntensity(light_components[1U], 1.25F);
        LightSystem2D::SetRange(light_components[1U], 640.0F);
        LightSystem2D::SetConeAngles(light_components[1U], 0.32F, 0.78F);
        LightSystem2D::SetSourceHeight(light_components[1U], 54.0F);
        LightSystem2D::SetCastShadow(light_components[1U], true);
        if (launch_options.has_light1_intensity) {
            LightSystem2D::SetIntensity(light_components[1U], launch_options.light1_intensity);
        }
        if (launch_options.has_light1_range) {
            LightSystem2D::SetRange(light_components[1U], launch_options.light1_range);
        }
        if (launch_options.has_light1_inner || launch_options.has_light1_outer) {
            const float inner = launch_options.has_light1_inner ? launch_options.light1_inner : 0.32F;
            const float outer = launch_options.has_light1_outer ? launch_options.light1_outer : 0.78F;
            LightSystem2D::SetConeAngles(light_components[1U], inner, outer);
        }
        if (launch_options.has_light1_height) {
            LightSystem2D::SetSourceHeight(light_components[1U], launch_options.light1_height);
        }
        if (launch_options.has_cast_shadow) {
            LightSystem2D::SetCastShadow(light_components[1U], launch_options.cast_shadow);
        }
        TransformSystem2D::SetLocalPosition(light_transforms[1U], 1140.0F, 160.0F);
        TransformSystem2D::SetLocalRotationRadians(light_transforms[1U], -0.88F);
        TransformSystem2D::UpdateHierarchy(light_transforms.data(),
                                           static_cast<std::uint32_t>(light_transforms.size()));

        std::array<Shadow2D, 2U> shadow_components{};
        for (Shadow2D& shadow : shadow_components) {
            ShadowSystem2D::Initialize(shadow);
            ShadowSystem2D::SetEnabled(shadow,
                                       launch_options.has_cast_shadow
                                           ? launch_options.cast_shadow
                                           : true);
            ShadowSystem2D::SetVisible(shadow, true);
            ShadowSystem2D::SetAtlasNamespace(shadow, 1U);
            ShadowSystem2D::SetMapResolution(shadow, 1024U, 1024U);
            ShadowSystem2D::SetMaxDistance(shadow, 700.0F);
            ShadowSystem2D::SetBias(shadow, 0.00125F, 0.00025F);
            ShadowSystem2D::SetFilterKernel(shadow, vr::ecs::ShadowFilterKernel::pcf3x3);
            ShadowSystem2D::SetProjectionKind(shadow, vr::ecs::ShadowProjectionKind::directional);
        }
        ShadowSystem2D::SetLightComponentIndex(shadow_components[0U], 0U);
        ShadowSystem2D::SetTransformComponentIndex(shadow_components[0U], 0U);
        ShadowSystem2D::SetOccluderHeight(shadow_components[0U], 74.0F);
        ShadowSystem2D::SetProjectionKind(shadow_components[0U], vr::ecs::ShadowProjectionKind::directional);
        ShadowSystem2D::SetLightComponentIndex(shadow_components[1U], 1U);
        ShadowSystem2D::SetTransformComponentIndex(shadow_components[1U], 1U);
        ShadowSystem2D::SetOccluderHeight(shadow_components[1U], 54.0F);
        ShadowSystem2D::SetProjectionKind(shadow_components[1U], vr::ecs::ShadowProjectionKind::spot);
        ShadowSystem2D::SetBias(shadow_components[1U], 0.0045F, 0.0012F);
        // NOTE:
        // 2D spot-shadow path is still under active refinement. Keep it disabled in the
        // default showcase to avoid temporal striping artifacts while preserving stable output.
        ShadowSystem2D::SetEnabled(shadow_components[1U], false);

        auto light_culling_config = vr::ecs::LightCullingSystem<vr::ecs::Dim2>::DefaultBuildConfig();
        light_culling_config.tile_count_x = 24U;
        light_culling_config.tile_count_y = 14U;
        light_culling_config.max_lights_per_tile = 128U;

        vr::shadow::ShadowRenderer2DCreateInfo shadow_renderer_create_info{};
        shadow_renderer_create_info.reserve_shadow_count =
            static_cast<std::uint32_t>(shadow_components.size());
        shadow_renderer_create_info.reserve_caster_count =
            static_cast<std::uint32_t>(caster_bounds.size());
        shadow_renderer_create_info.runtime_build.atlas_width = 2048U;
        shadow_renderer_create_info.runtime_build.atlas_height = 2048U;
        shadow_renderer_create_info.runtime_build.atlas_layer_count = 4U;
        shadow_renderer_create_info.caster_build.max_casters_per_view = 1024U;
        shadow_renderer_create_info.clear_atlas_each_frame = true;
        recorder.shadow_renderer.Initialize(shadow_renderer_create_info);
        shadow_renderer_initialized = true;
        recorder.shadow_renderer.SetSceneData(shadow_components.data(),
                                              light_transforms.data(),
                                              static_cast<std::uint32_t>(shadow_components.size()),
                                              &light_camera,
                                              caster_bounds.data(),
                                              static_cast<std::uint32_t>(caster_bounds.size()));

        vr::surface::SurfaceRenderer2DCreateInfo surface_renderer_create_info{};
        surface_renderer_create_info.reserve_component_count =
            static_cast<std::uint32_t>(surface_components.size());
        surface_renderer_create_info.reserve_instance_count = 2048U;
        surface_renderer_create_info.input_positions_pixel_space = true;
        surface_renderer_create_info.pixel_space_origin_top_left = true;
        surface_renderer_create_info.clear_swapchain = true;
        surface_renderer_create_info.clear_color = {{0.04F, 0.05F, 0.08F, 1.0F}};
        surface_renderer_create_info.enable_light_shadow = true;
        surface_renderer_create_info.max_fragment_lights = 64U;
        if (launch_options.has_ambient) {
            surface_renderer_create_info.light_ambient = launch_options.ambient;
        }
        surface_renderer_create_info.light_culling_config = light_culling_config;
        recorder.surface_renderer.Initialize(surface_renderer_create_info);
        surface_renderer_initialized = true;
        recorder.surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        recorder.surface_renderer.SetSceneData(surface_components.data(),
                                               surface_transforms.data(),
                                               static_cast<std::uint32_t>(surface_components.size()));
        recorder.surface_renderer.SetLightFrameCoordinator(&light_frame_coordinator);
        recorder.surface_renderer.SetShadowFrameCoordinator(&recorder.shadow_renderer.FrameCoordinatorMutable());
        recorder.surface_renderer.SetShadowAtlasHost(&recorder.shadow_renderer.AtlasHostMutable());

        light_frame_coordinator.SetLightData(light_components.data(),
                                             light_transforms.data(),
                                             static_cast<std::uint32_t>(light_components.size()));
        light_frame_coordinator.SetCamera(&light_camera);
        light_frame_coordinator.Reserve(static_cast<std::uint32_t>(light_components.size()),
                                        light_culling_config);

        std::cout << "sdl_surface_light_shadow_2d_demo running. Close window to exit.\n";

        std::uint64_t fps_window_begin_ticks = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;
        std::uint64_t frame_index = 0U;
        std::uint32_t last_extent_width = 0U;
        std::uint32_t last_extent_height = 0U;
        std::uint64_t next_frame_tick = SDL_GetTicks();
        constexpr std::array<std::uint32_t, 2U> light_dirty_indices{0U, 1U};
        constexpr std::array<std::uint32_t, 2U> surface_dirty_indices{2U, 3U};

        std::cout << "Launch options: frame_interval_ms=" << launch_options.frame_interval_ms
                  << " max_frames=" << launch_options.max_frame_count
                  << " freeze=" << (launch_options.freeze_animation ? 1 : 0)
                  << " log_every_frame=" << (launch_options.log_every_frame ? 1 : 0)
                  << " ambient=" << surface_renderer_create_info.light_ambient
                  << " cast_shadow=" << ((launch_options.has_cast_shadow ? launch_options.cast_shadow : true) ? 1 : 0)
                  << " l0_intensity=" << (launch_options.has_light0_intensity ? launch_options.light0_intensity : 1.8F)
                  << " l0_range=" << (launch_options.has_light0_range ? launch_options.light0_range : 360.0F)
                  << " l0_falloff=" << (launch_options.has_light0_falloff ? launch_options.light0_falloff : 1.8F)
                  << " l1_intensity=" << (launch_options.has_light1_intensity ? launch_options.light1_intensity : 1.25F)
                  << " l1_range=" << (launch_options.has_light1_range ? launch_options.light1_range : 640.0F)
                  << '\n';

        while (runtime.IsRunning()) {
            if (launch_options.frame_interval_ms > 0U) {
                const std::uint64_t sleep_probe_ticks = SDL_GetTicks();
                if (sleep_probe_ticks < next_frame_tick) {
                    const std::uint32_t sleep_ms = static_cast<std::uint32_t>(next_frame_tick - sleep_probe_ticks);
                    if (sleep_ms > 0U) {
                        SDL_Delay(sleep_ms);
                    }
                }
                const std::uint64_t now_ticks = SDL_GetTicks();
                if (next_frame_tick + launch_options.frame_interval_ms < now_ticks) {
                    next_frame_tick = now_ticks + launch_options.frame_interval_ms;
                } else {
                    next_frame_tick += launch_options.frame_interval_ms;
                }
            }
            const std::uint64_t now_ticks = SDL_GetTicks();
            const float time_seconds = launch_options.freeze_animation
                ? 0.0F
                : static_cast<float>(now_ticks) * 0.001F;
            const VkExtent2D extent = runtime.Swapchain().Extent();
            const float width = static_cast<float>(extent.width > 0U ? extent.width : 1U);
            const float height = static_cast<float>(extent.height > 0U ? extent.height : 1U);

            TransformSystem2D::SetLocalPosition(light_camera_transform,
                                                0.5F * width,
                                                0.5F * height);
            TransformSystem2D::UpdateHierarchy(&light_camera_transform, 1U);
            if (extent.width != last_extent_width || extent.height != last_extent_height) {
                CameraSystem2D::SetAspectRatio(light_camera, width / height);
                CameraSystem2D::SetOrthographicHeight(light_camera, height);
                last_extent_width = extent.width;
                last_extent_height = extent.height;
            }
            CameraSystem2D::Update(light_camera, light_camera_transform);

            const float orbit_x = 680.0F + std::cos(time_seconds * 1.1F) * 260.0F;
            const float orbit_y = 360.0F + std::sin(time_seconds * 0.9F) * 180.0F;
            TransformSystem2D::SetLocalPosition(light_transforms[0U], orbit_x, orbit_y);

            const float spot_rotation = -1.15F + 0.25F * std::sin(time_seconds * 0.8F);
            TransformSystem2D::SetLocalRotationRadians(light_transforms[1U], spot_rotation);
            TransformSystem2D::UpdateHierarchy(light_transforms.data(),
                                               static_cast<std::uint32_t>(light_transforms.size()));

            TransformSystem2D::SetLocalRotationRadians(surface_transforms[2U],
                                                       0.25F * std::sin(time_seconds * 0.7F));
            TransformSystem2D::SetLocalRotationRadians(surface_transforms[3U],
                                                       -0.35F * std::sin(time_seconds * 0.55F));
            TransformSystem2D::UpdateHierarchy(surface_transforms.data(),
                                               static_cast<std::uint32_t>(surface_transforms.size()));

            TransformSystem2D::SetLocalPosition(caster_transforms[0U],
                                                surface_transforms[2U].runtime.world_matrix.m02,
                                                surface_transforms[2U].runtime.world_matrix.m12);
            TransformSystem2D::SetLocalRotationRadians(caster_transforms[0U],
                                                       surface_transforms[2U].style.rotation_radians);
            TransformSystem2D::SetLocalPosition(caster_transforms[1U],
                                                surface_transforms[3U].runtime.world_matrix.m02,
                                                surface_transforms[3U].runtime.world_matrix.m12);
            TransformSystem2D::SetLocalRotationRadians(caster_transforms[1U],
                                                       surface_transforms[3U].style.rotation_radians);
            TransformSystem2D::UpdateHierarchy(caster_transforms.data(),
                                               static_cast<std::uint32_t>(caster_transforms.size()));
            (void)BoundsSystem2D::UpdateAligned(caster_bounds.data(),
                                                caster_transforms.data(),
                                                static_cast<std::uint32_t>(caster_bounds.size()));

            recorder.shadow_renderer.SetTransformDirtyHint(light_dirty_indices.data(),
                                                           static_cast<std::uint32_t>(light_dirty_indices.size()));
            recorder.shadow_renderer.SetShadowDirtyHint(light_dirty_indices.data(),
                                                        static_cast<std::uint32_t>(light_dirty_indices.size()));
            recorder.surface_renderer.SetTransformDirtyHint(surface_dirty_indices.data(),
                                                            static_cast<std::uint32_t>(surface_dirty_indices.size()));
            light_frame_coordinator.SetTransformDirtyHint(light_dirty_indices.data(),
                                                          static_cast<std::uint32_t>(light_dirty_indices.size()));

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            ++fps_window_frame_count;
            ++frame_index;

            if (launch_options.log_every_frame) {
                const vr::surface::SurfaceRenderer2DStats surface_stats = recorder.surface_renderer.Stats();
                const vr::shadow::ShadowRenderer2DStats shadow_stats = recorder.shadow_renderer.Stats();
                std::cout << "[FrameDebug] frame=" << frame_index
                          << " fi=" << tick_result.render.frame_index
                          << " ii=" << tick_result.render.image_index
                          << " lights=" << surface_stats.light_count
                          << " visible_lights=" << surface_stats.visible_light_count
                          << " clusters=" << surface_stats.light_cluster_count
                          << " cluster_indices=" << surface_stats.light_cluster_index_count
                          << " shadow_views=" << surface_stats.shadow_view_count
                          << " light_uploads=" << surface_stats.light_buffer_upload_count
                          << " desc_updates=" << surface_stats.descriptor_set_update_count
                          << " shadow_draw=" << shadow_stats.draw_call_count
                          << " atlas_pass=" << shadow_stats.atlas_layer_draw_pass_count
                          << '\n';
            }

            const std::uint64_t fps_window_elapsed = now_ticks - fps_window_begin_ticks;
            if (fps_window_elapsed >= 1000U) {
                const float fps = (fps_window_elapsed > 0U)
                    ? (1000.0F * static_cast<float>(fps_window_frame_count) /
                       static_cast<float>(fps_window_elapsed))
                    : 0.0F;
                const vr::surface::SurfaceRenderer2DStats surface_stats = recorder.surface_renderer.Stats();
                const vr::shadow::ShadowRenderer2DStats shadow_stats = recorder.shadow_renderer.Stats();

                std::cout << "FPS:" << fps
                          << " Frame:" << frame_index
                          << " | 2D Draw:" << surface_stats.draw_call_count
                          << " Batch:" << surface_stats.draw_batch_count
                          << " Lights:" << surface_stats.light_count
                          << " VisibleLights:" << surface_stats.visible_light_count
                          << " ShadowViews:" << surface_stats.shadow_view_count
                          << " | ShadowDraw:" << shadow_stats.draw_call_count
                          << " AtlasPass:" << shadow_stats.atlas_layer_draw_pass_count
                          << '\n';

                fps_window_begin_ticks = now_ticks;
                fps_window_frame_count = 0U;
            }

            if (launch_options.max_frame_count > 0U &&
                frame_index >= launch_options.max_frame_count) {
                runtime.RequestClose();
            }
        }

        recorder.surface_renderer.Shutdown(runtime.Context());
        surface_renderer_initialized = false;
        recorder.shadow_renderer.Shutdown(runtime.Context());
        shadow_renderer_initialized = false;
        surface_image_host.Shutdown(runtime.Context());
        image_host_initialized = false;
        surface_upload_host.Shutdown(runtime.Context());
        upload_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        std::cerr << "sdl_surface_light_shadow_2d_demo failed: " << exception_.what() << '\n';

        if (surface_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.surface_renderer.Shutdown(runtime.Context());
            surface_renderer_initialized = false;
        }
        if (shadow_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.shadow_renderer.Shutdown(runtime.Context());
            shadow_renderer_initialized = false;
        }
        if (image_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_image_host.Shutdown(runtime.Context());
            image_host_initialized = false;
        }
        if (upload_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_upload_host.Shutdown(runtime.Context());
            upload_host_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }
        return 1;
    }

    return 0;
}
