#include "support/test_framework.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/geometry_system.hpp"
#include "vr/geometry/geometry_renderer_2d.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_2d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Appearance2D = vr::ecs::Appearance<vr::ecs::Dim2>;
using AppearanceSystem2D = vr::ecs::AppearanceSystem<vr::ecs::Dim2>;
using Geometry2D = vr::ecs::Geometry<vr::ecs::Dim2>;
using GeometrySystem2D = vr::ecs::GeometrySystem<vr::ecs::Dim2>;

[[nodiscard]] std::string ToLower(std::string_view value_) {
    std::string lowered{};
    lowered.reserve(value_.size());
    for (const unsigned char ch : value_) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view text_,
                                           std::string_view needle_) {
    const std::string lowered_text = ToLower(text_);
    const std::string lowered_needle = ToLower(needle_);
    return lowered_text.find(lowered_needle) != std::string::npos;
}

[[nodiscard]] bool IsEnvironmentSkipError(std::string_view message_) {
    constexpr std::array<std::string_view, 15U> patterns{
        "sdl_initsubsystem",
        "sdl_createwindow",
        "sdl_vulkan_getinstanceextensions",
        "sdl_vulkan_createsurface",
        "vkcreateinstance",
        "vkenumeratephysicaldevices",
        "no vulkan physical devices found",
        "no suitable vulkan physical device found",
        "missing required instance extension",
        "vkcreatedevice",
        "vkgetphysicaldevicesurfacesupportkhr",
        "vkgetphysicaldevicesurfaceformatskhr",
        "vkgetphysicaldevicesurfacepresentmodeskhr",
        "dynamicrendering",
        "synchronization2"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

void ApplyGeometryAppearance(Geometry2D& component_,
                             vr::ecs::Rgba8 fill_color_,
                             vr::ecs::Rgba8 stroke_color_,
                             float opacity_ = 1.0F,
                             std::int16_t layer_ = 0) {
    Appearance2D appearance{};
    AppearanceSystem2D::Initialize(appearance);
    AppearanceSystem2D::SetFillColor(appearance, fill_color_);
    AppearanceSystem2D::SetStrokeColor(appearance, stroke_color_);
    AppearanceSystem2D::SetOpacity(appearance, opacity_);
    AppearanceSystem2D::SetLayer(appearance, layer_);
    (void)GeometrySystem2D::ApplyAppearanceRuntimeState(component_, appearance.style);
}

void InitializePathComponent(Geometry2D& component_,
                             std::uint32_t geometry_id_,
                             std::uint32_t appearance_id_,
                             vr::ecs::Rgba8 stroke_color_,
                             float stroke_width_px_,
                             std::array<vr::ecs::Float2, 4U> points_) {
    GeometrySystem2D::Initialize(component_);
    GeometrySystem2D::SetRuntimeRoute(component_, geometry_id_, appearance_id_, 0U);
    component_.style.topology = vr::ecs::Geometry2DTopology::stroke;
    component_.style.stroke_width_px = stroke_width_px_;
    ApplyGeometryAppearance(component_, stroke_color_, stroke_color_);

    (void)vr::ecs::GeometryPathSystem::AppendMoveTo(component_, points_[0U].x, points_[0U].y);
    (void)vr::ecs::GeometryPathSystem::AppendLineTo(component_, points_[1U].x, points_[1U].y);
    (void)vr::ecs::GeometryPathSystem::AppendLineTo(component_, points_[2U].x, points_[2U].y);
    (void)vr::ecs::GeometryPathSystem::AppendLineTo(component_, points_[3U].x, points_[3U].y);
    (void)vr::ecs::GeometryPathSystem::AppendClose(component_);
}

VR_TEST_CASE(RuntimeIntegration_geometry_renderer_2d_end_to_end_smoke, "integration;gpu;sdl;runtime;geometry") {
    Runtime runtime{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryRenderer2D geometry_renderer{};

    bool runtime_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_renderer_initialized = false;

    std::array<Geometry2D, 2U> geometry_components{};
    InitializePathComponent(geometry_components[0U],
                            1U,
                            101U,
                            vr::ecs::Rgba8{245U, 224U, 180U, 255U},
                            6.0F,
                            std::array<vr::ecs::Float2, 4U>{
                                vr::ecs::Float2{.x = 80.0F, .y = 70.0F},
                                vr::ecs::Float2{.x = 320.0F, .y = 70.0F},
                                vr::ecs::Float2{.x = 320.0F, .y = 210.0F},
                                vr::ecs::Float2{.x = 80.0F, .y = 210.0F}
                            });
    InitializePathComponent(geometry_components[1U],
                            2U,
                            202U,
                            vr::ecs::Rgba8{170U, 225U, 255U, 220U},
                            4.0F,
                            std::array<vr::ecs::Float2, 4U>{
                                vr::ecs::Float2{.x = 360.0F, .y = 120.0F},
                                vr::ecs::Float2{.x = 560.0F, .y = 80.0F},
                                vr::ecs::Float2{.x = 540.0F, .y = 260.0F},
                                vr::ecs::Float2{.x = 380.0F, .y = 280.0F}
                            });

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_geometry_2d_smoke";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::geometry::GeometryUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_primitive_buffer_bytes = 256U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        geometry_upload_host_initialized = true;

        vr::geometry::GeometryRenderer2DCreateInfo renderer_create_info{};
        renderer_create_info.runtime_build.quad_subdivision = 8U;
        renderer_create_info.runtime_build.cubic_subdivision = 12U;
        renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(geometry_components.size());
        renderer_create_info.reserve_primitive_count = 1024U;
        renderer_create_info.input_positions_pixel_space = true;
        renderer_create_info.pixel_space_origin_top_left = true;
        renderer_create_info.clear_swapchain = true;
        renderer_create_info.clear_color = {{0.06F, 0.08F, 0.11F, 1.0F}};
        geometry_renderer.Initialize(renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHost(&geometry_upload_host);
        geometry_renderer.SetSceneData(geometry_components.data(),
                                       static_cast<std::uint32_t>(geometry_components.size()));

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_primitives = 0U;
        std::uint64_t max_uploaded_bytes = 0U;

        constexpr std::uint32_t max_ticks = 16U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const std::uint8_t pulse =
                static_cast<std::uint8_t>(110U + ((tick_index * 21U) % 120U));
            const vr::ecs::Rgba8 animated_stroke = vr::ecs::Rgba8{
                static_cast<std::uint8_t>(120U + pulse / 3U),
                static_cast<std::uint8_t>(180U + pulse / 6U),
                static_cast<std::uint8_t>(220U + pulse / 8U),
                220U
            };
            ApplyGeometryAppearance(geometry_components[1U], animated_stroke, animated_stroke);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(geometry_renderer);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const vr::geometry::GeometryRenderer2DStats renderer_stats = geometry_renderer.Stats();
            max_draw_calls = std::max(max_draw_calls, renderer_stats.draw_call_count);
            max_draw_batches = std::max(max_draw_batches, renderer_stats.draw_batch_count);
            max_primitives = std::max(max_primitives, renderer_stats.primitive_count);
            max_uploaded_bytes = std::max(max_uploaded_bytes, renderer_stats.uploaded_bytes);
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_primitives > 0U);
        VR_CHECK(max_uploaded_bytes > 0U);
        VR_CHECK(geometry_upload_host.Stats().upload_count > 0U);

        geometry_renderer.Shutdown(runtime.Context());
        geometry_renderer_initialized = false;
        geometry_upload_host.Shutdown(runtime.Context());
        geometry_upload_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (geometry_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_renderer.Shutdown(runtime.Context());
            geometry_renderer_initialized = false;
        }
        if (geometry_upload_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_upload_host.Shutdown(runtime.Context());
            geometry_upload_host_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_scene_recorder_2d_geometry_scene_packet_smoke,
             "integration;gpu;sdl;runtime;geometry;scene2d") {
    Runtime runtime{};
    vr::geometry::GeometryUploadHost geometry_upload_host{};
    vr::geometry::GeometryRenderer2D geometry_renderer{};
    vr::render::SceneRecorder2D recorder{};

    bool runtime_initialized = false;
    bool geometry_upload_host_initialized = false;
    bool geometry_renderer_initialized = false;

    std::array<Geometry2D, 1U> geometry_components{};
    InitializePathComponent(geometry_components[0U],
                            11U,
                            111U,
                            vr::ecs::Rgba8{245U, 224U, 180U, 255U},
                            6.0F,
                            std::array<vr::ecs::Float2, 4U>{
                                vr::ecs::Float2{.x = 80.0F, .y = 70.0F},
                                vr::ecs::Float2{.x = 320.0F, .y = 70.0F},
                                vr::ecs::Float2{.x = 320.0F, .y = 210.0F},
                                vr::ecs::Float2{.x = 80.0F, .y = 210.0F}
                            });

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_scene_recorder_2d_geometry";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = true;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::geometry::GeometryUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_primitive_buffer_bytes = 256U * 1024U;
        geometry_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        geometry_upload_host_initialized = true;

        vr::geometry::GeometryRenderer2DCreateInfo renderer_create_info{};
        renderer_create_info.runtime_build.quad_subdivision = 8U;
        renderer_create_info.runtime_build.cubic_subdivision = 12U;
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_primitive_count = 512U;
        renderer_create_info.input_positions_pixel_space = true;
        renderer_create_info.pixel_space_origin_top_left = true;
        renderer_create_info.clear_swapchain = true;
        renderer_create_info.clear_color = {{0.06F, 0.08F, 0.11F, 1.0F}};
        geometry_renderer.Initialize(renderer_create_info);
        geometry_renderer_initialized = true;
        geometry_renderer.SetHost(&geometry_upload_host);
        geometry_renderer.SetSceneData(geometry_components.data(), 1U);

        recorder.Initialize({});
        recorder.BindRuntime(runtime);
        recorder.RegisterSceneRenderer(geometry_renderer, vr::render::SceneRenderPassRole::single, 0x1U);

        vr::render::RenderView2D main_view{};
        vr::render::RenderScenePacket2D main_scene_packet{};
        vr::render::RefreshExtentBoundScreenSceneSubmission(main_view,
                                                            main_scene_packet,
                                                            runtime.Swapchain().Extent(),
                                                            0U,
                                                            vr::render::RenderViewKind::world,
                                                            vr::render::render_view_overlay_enabled_flag,
                                                            vr::render::render_scene_packet_none_flag,
                                                            0x1U);
        recorder.SetFramePacket(&main_scene_packet);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_primitives = 0U;

        for (std::uint32_t tick_index = 0U;
             tick_index < 8U && runtime.IsRunning();
             ++tick_index) {
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }
            max_draw_calls = std::max(max_draw_calls, geometry_renderer.Stats().draw_call_count);
            max_primitives = std::max(max_primitives, geometry_renderer.Stats().primitive_count);
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_primitives > 0U);
        VR_CHECK(recorder.Stats().frame_packet_prepare_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_record_count > 0U);
        VR_CHECK(recorder.Stats().frame_view_count == 1U);
        VR_CHECK(recorder.Stats().effective_layer_mask == 0x1U);
        VR_CHECK(recorder.Stats().overlay_enabled == 0U);

        recorder.Shutdown(runtime.Context());
        geometry_renderer.Shutdown(runtime.Context());
        geometry_renderer_initialized = false;
        geometry_upload_host.Shutdown(runtime.Context());
        geometry_upload_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (geometry_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_renderer.Shutdown(runtime.Context());
            geometry_renderer_initialized = false;
        }
        if (geometry_upload_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            geometry_upload_host.Shutdown(runtime.Context());
            geometry_upload_host_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace

