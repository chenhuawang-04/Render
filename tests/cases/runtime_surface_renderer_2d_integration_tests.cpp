#include "support/test_framework.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_2d.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_2d.hpp"
#include "vr/surface/surface_upload_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

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
    constexpr std::array<std::string_view, 18U> patterns{
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
        "bindlessresourcesystem",
        "descriptor indexing",
        "runtime descriptor array",
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
            const bool odd = (((x >> 3U) ^ (y >> 3U)) & 1U) != 0U;
            pixels_[index] = odd ? color_a_ : color_b_;
        }
    }
}

void ConfigureSurface2DRuntimeCreateInfo(Runtime::CreateInfo& create_info_,
                                         const char* window_title_) {
    create_info_.platform.window.title = window_title_;
    create_info_.platform.window.width = 640;
    create_info_.platform.window.height = 360;
    create_info_.platform.window.resizable = true;
    create_info_.platform.window.high_pixel_density = true;
    create_info_.platform.instance.enable_validation = false;
    create_info_.platform.device.required_vulkan12_features.runtimeDescriptorArray = VK_TRUE;
    create_info_.platform.device.required_vulkan12_features.descriptorBindingPartiallyBound = VK_TRUE;
    create_info_.platform.device.required_vulkan12_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    create_info_.platform.device.required_vulkan12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    create_info_.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
    create_info_.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
    create_info_.render_loop.swapchain.enable_vsync = false;
    create_info_.render_loop.swapchain.preferred_image_count = 2U;
    create_info_.render_loop.commands.initial_primary_per_frame = 2U;
    create_info_.render_loop.commands.primary_growth_chunk = 2U;
    create_info_.poll_events_each_tick = true;
}

void InitializeSurfaceComponent(Surface2D& component_,
                                std::uint32_t image_id_,
                                vr::ecs::Float2 size_,
                                vr::ecs::Float2 pivot_,
                                vr::ecs::Rgba8 tint_color_,
                                float opacity_,
                                vr::ecs::Surface2DBlendMode blend_mode_,
                                bool premultiplied_alpha_,
                                std::int16_t layer_) {
    SurfaceSystem2D::Initialize(component_);
    SurfaceSystem2D::SetImageId(component_, image_id_);
    SurfaceSystem2D::SetMaterialId(component_, 1U);
    SurfaceSystem2D::SetBatchTag(component_, 0U);
    SurfaceSystem2D::SetRenderPassHint(component_, vr::ecs::SurfaceRenderPassHint::transparent);
    SurfaceSystem2D::SetSize(component_, size_);
    SurfaceSystem2D::SetPivot(component_, pivot_);
    SurfaceSystem2D::SetTintColor(component_, tint_color_);
    SurfaceSystem2D::SetOpacity(component_, opacity_);
    SurfaceSystem2D::SetBlendMode(component_, blend_mode_);
    SurfaceSystem2D::SetPremultipliedAlpha(component_, premultiplied_alpha_);
    SurfaceSystem2D::SetLayer(component_, layer_);
}

VR_TEST_CASE(RuntimeIntegration_surface_renderer_2d_bindless_scene_packet_smoke,
             "integration;gpu;sdl;runtime;surface;render2d") {
    Runtime runtime{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    vr::surface::SurfaceRenderer2D surface_renderer{};
    vr::render::SceneRecorder2D recorder{};

    bool runtime_initialized = false;
    bool upload_initialized = false;
    bool image_initialized = false;
    bool renderer_initialized = false;
    bool recorder_initialized = false;

    constexpr std::uint32_t texture_width = 64U;
    constexpr std::uint32_t texture_height = 64U;
    std::array<std::uint32_t, texture_width * texture_height> pixels_a{};
    std::array<std::uint32_t, texture_width * texture_height> pixels_b{};
    FillCheckerTexture(pixels_a.data(),
                       texture_width,
                       texture_height,
                       PackRgba8(244U, 194U, 120U, 255U),
                       PackRgba8(125U, 76U, 45U, 255U));
    FillCheckerTexture(pixels_b.data(),
                       texture_width,
                       texture_height,
                       PackRgba8(118U, 214U, 255U, 255U),
                       PackRgba8(26U, 78U, 132U, 255U));

    try {
        Runtime::CreateInfo create_info{};
        ConfigureSurface2DRuntimeCreateInfo(create_info, "vr_tests_runtime_surface_2d_bindless");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::surface::SurfaceUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_instance_buffer_bytes = 256U * 1024U;
        surface_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_initialized = true;

        vr::surface::SurfaceImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 16U;
        image_create_info.reserve_retired_image_count = 16U;
        surface_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), image_create_info);
        image_initialized = true;
        runtime.BindlessResources().ConfigureSurfaceImageHost(surface_image_host);

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        vr::surface::SurfaceImageUploadInfo upload_info{};
        upload_info.width = texture_width;
        upload_info.height = texture_height;
        upload_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        upload_info.bytes_per_pixel = 4U;
        upload_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        upload_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        upload_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;

        upload_info.image_id = 7101U;
        upload_info.pixels = pixels_a.data();
        surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

        upload_info.image_id = 7102U;
        upload_info.pixels = pixels_b.data();
        surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

        const auto upload_end = runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        surface_image_host.BeginFrame(runtime.Context(), 0U);

        std::array<Surface2D, 2U> components{};
        InitializeSurfaceComponent(components[0U],
                                   7101U,
                                   vr::ecs::Float2{.x = 180.0F, .y = 128.0F},
                                   vr::ecs::Float2{.x = 0.5F, .y = 0.5F},
                                   vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                   1.0F,
                                   vr::ecs::Surface2DBlendMode::alpha,
                                   false,
                                   0);
        InitializeSurfaceComponent(components[1U],
                                   7102U,
                                   vr::ecs::Float2{.x = 156.0F, .y = 116.0F},
                                   vr::ecs::Float2{.x = 0.5F, .y = 0.5F},
                                   vr::ecs::Rgba8{220U, 240U, 255U, 224U},
                                   0.90F,
                                   vr::ecs::Surface2DBlendMode::alpha,
                                   true,
                                   2);
        SurfaceSystem2D::SetUvRect(components[1U], 0.05F, 0.08F, 0.95F, 0.92F);

        std::array<Transform2D, 2U> transforms{};
        for (auto& transform : transforms) {
            TransformSystem2D::Initialize(transform);
        }
        TransformSystem2D::SetLocalPosition(transforms[0U], 200.0F, 180.0F);
        TransformSystem2D::SetLocalPosition(transforms[1U], 430.0F, 170.0F);
        TransformSystem2D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));

        vr::surface::SurfaceRenderer2DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(components.size());
        renderer_create_info.reserve_instance_count = 32U;
        renderer_create_info.reserve_dirty_component_count = 8U;
        renderer_create_info.input_positions_pixel_space = true;
        renderer_create_info.pixel_space_origin_top_left = true;
        renderer_create_info.clear_swapchain = true;
        renderer_create_info.clear_color = {{0.05F, 0.07F, 0.10F, 1.0F}};
        surface_renderer.Initialize(renderer_create_info);
        renderer_initialized = true;
        surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        surface_renderer.SetSceneData(components.data(),
                                      transforms.data(),
                                      static_cast<std::uint32_t>(components.size()));

        recorder.Initialize({});
        recorder_initialized = true;
        recorder.BindRuntime(runtime);
        recorder.RegisterSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single, 0x1U);

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
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_instances = 0U;
        std::uint32_t max_descriptor_binds = 0U;
        std::uint32_t max_light_descriptor_binds = 0U;
        std::uint32_t max_descriptor_updates = 0U;
        std::uint32_t min_bindless_slot_index = std::numeric_limits<std::uint32_t>::max();
        std::array<std::uint32_t, 1U> dirty_indices{1U};

        constexpr std::uint32_t max_ticks = 10U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            TransformSystem2D::SetLocalPosition(transforms[1U],
                                                430.0F - 10.0F * static_cast<float>(tick_index),
                                                170.0F + 4.0F * static_cast<float>(tick_index & 1U));
            TransformSystem2D::UpdateHierarchy(transforms.data(), static_cast<std::uint32_t>(transforms.size()));
            surface_renderer.SetTransformDirtyHint(dirty_indices.data(),
                                                   static_cast<std::uint32_t>(dirty_indices.size()));
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const auto stats = surface_renderer.Stats();
            max_draw_calls = std::max(max_draw_calls, stats.draw_call_count);
            max_draw_batches = std::max(max_draw_batches, stats.draw_batch_count);
            max_instances = std::max(max_instances, stats.instance_count);
            max_descriptor_binds = std::max(max_descriptor_binds, stats.descriptor_set_bind_count);
            max_light_descriptor_binds = std::max(max_light_descriptor_binds, stats.light_descriptor_set_bind_count);
            max_descriptor_updates = std::max(max_descriptor_updates, stats.descriptor_set_update_count);
            if (!surface_renderer.Stats().skipped_upload && !surface_renderer.Stats().cache_reused) {
                const auto slot_a = surface_image_host.ResolveBindlessImageSlot(7101U);
                const auto slot_b = surface_image_host.ResolveBindlessImageSlot(7102U);
                if (slot_a.IsValid()) {
                    min_bindless_slot_index = std::min(min_bindless_slot_index, slot_a.index);
                }
                if (slot_b.IsValid()) {
                    min_bindless_slot_index = std::min(min_bindless_slot_index, slot_b.index);
                }
            }
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_instances > 0U);
        VR_CHECK(max_descriptor_binds > 0U);
        VR_CHECK(max_light_descriptor_binds > 0U);
        VR_CHECK(max_descriptor_updates > 0U);
        VR_CHECK(max_descriptor_binds <= max_draw_batches);
        VR_CHECK(surface_image_host.Stats().image_count >= 2U);
        VR_CHECK(surface_image_host.ResolveBindlessImageSlot(7101U).IsValid());
        VR_CHECK(surface_image_host.ResolveBindlessImageSlot(7102U).IsValid());
        VR_CHECK(min_bindless_slot_index != std::numeric_limits<std::uint32_t>::max());
        VR_CHECK(recorder.Stats().frame_packet_prepare_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_record_count > 0U);
        VR_CHECK(recorder.Stats().frame_view_count == 1U);

        recorder.Shutdown(runtime.Context());
        recorder_initialized = false;
        surface_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        surface_image_host.Shutdown(runtime.Context());
        image_initialized = false;
        surface_upload_host.Shutdown(runtime.Context());
        upload_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (recorder_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
            recorder_initialized = false;
        }
        if (renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
        if (image_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_image_host.Shutdown(runtime.Context());
            image_initialized = false;
        }
        if (upload_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_upload_host.Shutdown(runtime.Context());
            upload_initialized = false;
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
