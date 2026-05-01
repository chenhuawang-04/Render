#include "support/test_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_target_composite_renderer.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_3d.hpp"
#include "vr/surface/surface_upload_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

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
            const std::uint8_t r = static_cast<std::uint8_t>(55.0F + 180.0F * fx);
            const std::uint8_t g = static_cast<std::uint8_t>(45.0F + 170.0F * (1.0F - fy));
            const std::uint8_t b = static_cast<std::uint8_t>(120.0F + 100.0F * fy);
            pixels_[index] = PackRgba8(r, g, b, 255U);
        }
    }
}

void InitializeSurface3DComponent(Surface3D& component_,
                                  std::uint32_t texture_id_,
                                  std::uint32_t sampler_id_,
                                  std::uint16_t depth_bin_,
                                  bool depth_write_,
                                  bool double_sided_,
                                  vr::ecs::Rgba8 tint_color_,
                                  float opacity_) {
    SurfaceSystem3D::Initialize(component_);
    SurfaceSystem3D::SetTextureRoute(component_, texture_id_, sampler_id_, 0U, 0U);
    SurfaceSystem3D::SetDepthBin(component_, depth_bin_);
    SurfaceSystem3D::SetDepthTest(component_, true);
    SurfaceSystem3D::SetDepthWrite(component_, depth_write_);
    SurfaceSystem3D::SetDoubleSided(component_, double_sided_);
    SurfaceSystem3D::SetTintColor(component_, tint_color_);
    SurfaceSystem3D::SetOpacity(component_, opacity_);
}

struct Surface3DOffscreenRecorder final {
    Runtime* runtime = nullptr;
    vr::surface::SurfaceRenderer3D surface_renderer{};
    vr::render::RenderTargetCompositeRenderer composite_renderer{};
    vr::render::SceneRenderTargetSet scene_targets{};

    void InitializeSceneTargets() {
        vr::render::SceneRenderTargetSetCreateInfo create_info{};
        create_info.color_debug_name = "RuntimeSurface3DSceneColor";
        create_info.depth_debug_name = "RuntimeSurface3DSceneDepth";
        create_info.enable_depth = true;
        create_info.color_lifetime = vr::render::RenderTargetLifetime::transient;
        create_info.depth_lifetime = vr::render::RenderTargetLifetime::transient;
        create_info.clear_color = VkClearColorValue{{0.05F, 0.07F, 0.10F, 1.0F}};
        scene_targets.Initialize(create_info);
    }

    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        (void)scene_targets.PrepareFrameAndConfigure(
            prepare_context_,
            &composite_renderer,
            vr::render::BindSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single));
        surface_renderer.PrepareFrame(prepare_context_);
        composite_renderer.PrepareFrame(prepare_context_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        surface_renderer.Record(record_context_);
        composite_renderer.Record(record_context_);
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) {
        surface_renderer.OnSwapchainRecreated(image_count_,
                                              extent_,
                                              format_,
                                              last_submitted_value_,
                                              completed_submit_value_);
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
            vr::render::BindSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single));
    }
};

VR_TEST_CASE(RuntimeIntegration_surface_renderer_3d_offscreen_composite_smoke,
             "integration;gpu;sdl;runtime;surface;render_target") {
    Runtime runtime{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    Surface3DOffscreenRecorder recorder{};

    bool runtime_initialized = false;
    bool upload_initialized = false;
    bool image_initialized = false;
    bool surface_renderer_initialized = false;
    bool composite_renderer_initialized = false;

    constexpr std::uint32_t texture_width = 64U;
    constexpr std::uint32_t texture_height = 64U;
    std::array<std::uint32_t, texture_width * texture_height> pixels_checker{};
    std::array<std::uint32_t, texture_width * texture_height> pixels_gradient{};
    FillCheckerTexture(pixels_checker.data(),
                       texture_width,
                       texture_height,
                       PackRgba8(245U, 194U, 126U, 255U),
                       PackRgba8(118U, 72U, 46U, 255U));
    FillGradientTexture(pixels_gradient.data(), texture_width, texture_height);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_surface_3d_offscreen";
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

        recorder.runtime = &runtime;
        recorder.InitializeSceneTargets();

        vr::surface::SurfaceUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_3d_instance_buffer_bytes = 512U * 1024U;
        surface_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_initialized = true;

        vr::surface::SurfaceImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 16U;
        image_create_info.reserve_retired_image_count = 16U;
        surface_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), image_create_info);
        image_initialized = true;

        vr::resource::SamplerId sampler_linear_repeat_id{};
        {
            vr::resource::SamplerDesc sampler_desc{};
            sampler_desc.mag_filter = VK_FILTER_LINEAR;
            sampler_desc.min_filter = VK_FILTER_LINEAR;
            sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_desc.min_lod = 0.0F;
            sampler_desc.max_lod = 0.0F;
            sampler_linear_repeat_id = runtime.Sampler().RegisterSampler(runtime.Context(), sampler_desc);
        }

        vr::resource::SamplerId sampler_nearest_clamp_id{};
        {
            vr::resource::SamplerDesc sampler_desc{};
            sampler_desc.mag_filter = VK_FILTER_NEAREST;
            sampler_desc.min_filter = VK_FILTER_NEAREST;
            sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_desc.min_lod = 0.0F;
            sampler_desc.max_lod = 0.0F;
            sampler_nearest_clamp_id = runtime.Sampler().RegisterSampler(runtime.Context(), sampler_desc);
        }

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        vr::surface::SurfaceImageUploadInfo upload_info{};
        upload_info.width = texture_width;
        upload_info.height = texture_height;
        upload_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        upload_info.bytes_per_pixel = 4U;
        upload_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        upload_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        upload_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;

        upload_info.image_id = 6101U;
        upload_info.pixels = pixels_checker.data();
        surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

        upload_info.image_id = 6102U;
        upload_info.pixels = pixels_gradient.data();
        surface_image_host.UploadImage(runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

        const auto upload_end = runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        surface_image_host.BeginFrame(runtime.Context(), 0U);

        std::array<Surface3D, 2U> components{};
        InitializeSurface3DComponent(components[0U],
                                     6101U,
                                     sampler_linear_repeat_id.value,
                                     32U,
                                     true,
                                     false,
                                     vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                     1.0F);
        InitializeSurface3DComponent(components[1U],
                                     6102U,
                                     sampler_nearest_clamp_id.value,
                                     48U,
                                     false,
                                     true,
                                     vr::ecs::Rgba8{220U, 240U, 255U, 220U},
                                     0.90F);
        SurfaceSystem3D::SetUvTransform(components[1U], 1.15F, 1.15F, -0.08F, -0.04F);

        std::array<Transform3D, 2U> transforms{};
        std::array<Bounds3D, 2U> bounds{};
        for (std::uint32_t i = 0U; i < transforms.size(); ++i) {
            TransformSystem3D::Initialize(transforms[i]);
            BoundsSystem3D::Initialize(bounds[i]);
        }
        TransformSystem3D::SetLocalPosition(transforms[0U],
                                            vr::ecs::Float3{.x = -0.90F, .y = -0.05F, .z = 0.12F});
        TransformSystem3D::SetLocalScale(transforms[0U],
                                         vr::ecs::Float3{.x = 2.05F, .y = 2.05F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(transforms[1U],
                                            vr::ecs::Float3{.x = 0.95F, .y = 0.15F, .z = -0.28F});
        TransformSystem3D::SetLocalScale(transforms[1U],
                                         vr::ecs::Float3{.x = 1.70F, .y = 1.70F, .z = 1.0F});
        BoundsSystem3D::SetLocalCenterExtents(bounds[0U],
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 1.05F, .y = 1.05F, .z = 0.05F});
        BoundsSystem3D::SetLocalCenterExtents(bounds[1U],
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.88F, .y = 0.88F, .z = 0.05F});
        TransformSystem3D::UpdateHierarchy(transforms.data(),
                                           static_cast<std::uint32_t>(transforms.size()));
        (void)BoundsSystem3D::UpdateAligned(bounds.data(),
                                            transforms.data(),
                                            static_cast<std::uint32_t>(bounds.size()));

        Camera3D camera{};
        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
        CameraSystem3D::SetAspectRatio(camera, 640.0F / 360.0F);

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.5F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::surface::SurfaceRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(components.size());
        renderer_create_info.reserve_instance_count = 128U;
        renderer_create_info.enable_depth = true;
        renderer_create_info.clear_depth = true;
        renderer_create_info.clear_swapchain = false;
        recorder.surface_renderer.Initialize(renderer_create_info);
        surface_renderer_initialized = true;
        recorder.surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        recorder.surface_renderer.SetSceneData(components.data(),
                                               transforms.data(),
                                               static_cast<std::uint32_t>(components.size()),
                                               &camera,
                                               &camera_transform,
                                               bounds.data());

        vr::render::RenderTargetCompositeRendererCreateInfo composite_create_info{};
        composite_create_info.clear_swapchain = true;
        composite_create_info.enable_reinhard_tonemap = true;
        composite_create_info.exposure = 1.05F;
        composite_create_info.apply_manual_gamma = false;
        recorder.composite_renderer.Initialize(composite_create_info);
        composite_renderer_initialized = true;

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_surface_draw_calls = 0U;
        std::uint32_t max_surface_draw_batches = 0U;
        std::uint32_t max_surface_instances = 0U;
        std::uint32_t max_surface_descriptor_updates = 0U;
        std::uint32_t max_surface_depth_test_batches = 0U;
        std::uint32_t max_surface_depth_write_batches = 0U;
        std::uint32_t max_surface_culling_visible_count = 0U;
        std::uint32_t max_composite_draw_calls = 0U;
        std::uint32_t max_composite_descriptor_updates = 0U;
        bool observed_bounds_culling = false;

        constexpr std::uint32_t max_ticks = 18U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const float t = static_cast<float>(tick_index);
            TransformSystem3D::SetLocalRotationEulerXyz(transforms[0U],
                                                        0.0F,
                                                        0.25F * t,
                                                        0.10F * std::sin(t * 0.35F));
            TransformSystem3D::SetLocalRotationEulerXyz(transforms[1U],
                                                        0.12F * std::sin(t * 0.25F),
                                                        -0.18F * t,
                                                        0.0F);
            TransformSystem3D::UpdateHierarchy(transforms.data(),
                                               static_cast<std::uint32_t>(transforms.size()));
            (void)BoundsSystem3D::UpdateAligned(bounds.data(),
                                                transforms.data(),
                                                static_cast<std::uint32_t>(bounds.size()));
            CameraSystem3D::Update(camera, camera_transform);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const auto surface_stats = recorder.surface_renderer.Stats();
            const auto composite_stats = recorder.composite_renderer.Stats();
            max_surface_draw_calls = std::max(max_surface_draw_calls, surface_stats.draw_call_count);
            max_surface_draw_batches = std::max(max_surface_draw_batches, surface_stats.draw_batch_count);
            max_surface_instances = std::max(max_surface_instances, surface_stats.instance_count);
            max_surface_descriptor_updates =
                std::max(max_surface_descriptor_updates, surface_stats.descriptor_set_update_count);
            max_surface_depth_test_batches =
                std::max(max_surface_depth_test_batches, surface_stats.depth_test_batch_count);
            max_surface_depth_write_batches =
                std::max(max_surface_depth_write_batches, surface_stats.depth_write_batch_count);
            max_surface_culling_visible_count =
                std::max(max_surface_culling_visible_count, surface_stats.culling_visible_count);
            max_composite_draw_calls = std::max(max_composite_draw_calls, composite_stats.draw_call_count);
            max_composite_descriptor_updates =
                std::max(max_composite_descriptor_updates, composite_stats.descriptor_set_update_count);
            observed_bounds_culling = observed_bounds_culling || surface_stats.used_bounds_culling;
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_surface_draw_calls > 0U);
        VR_CHECK(max_surface_draw_batches > 0U);
        VR_CHECK(max_surface_instances > 0U);
        VR_CHECK(max_surface_descriptor_updates > 0U);
        VR_CHECK(max_surface_depth_test_batches > 0U);
        VR_CHECK(max_surface_depth_write_batches > 0U);
        VR_CHECK(max_surface_culling_visible_count > 0U);
        VR_CHECK(observed_bounds_culling);
        VR_CHECK(max_composite_draw_calls > 0U);
        VR_CHECK(max_composite_descriptor_updates > 0U);
        VR_CHECK(runtime.RenderTarget().ResolveView(recorder.scene_targets.ColorTarget()).state ==
                 vr::render::RenderTargetStateKind::shader_read);
        VR_CHECK(runtime.RenderTarget().ResolveView(recorder.scene_targets.DepthTarget()).state ==
                 vr::render::RenderTargetStateKind::depth_attachment);
        VR_CHECK(surface_image_host.Stats().image_count >= 2U);
        VR_CHECK(runtime.TargetPool().Stats().acquire_count > 0U);
        VR_CHECK(runtime.TargetPool().Stats().reuse_hit_count > 0U);

        recorder.composite_renderer.Shutdown(runtime.Context());
        composite_renderer_initialized = false;
        recorder.surface_renderer.Shutdown(runtime.Context());
        surface_renderer_initialized = false;
        surface_image_host.Shutdown(runtime.Context());
        image_initialized = false;
        surface_upload_host.Shutdown(runtime.Context());
        upload_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (composite_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.composite_renderer.Shutdown(runtime.Context());
            composite_renderer_initialized = false;
        }
        if (surface_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.surface_renderer.Shutdown(runtime.Context());
            surface_renderer_initialized = false;
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
