#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/scene_recorder_3d.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/surface/surface_renderer_2d.hpp"
#include "vr/surface/surface_renderer_3d.hpp"
#include "vr/surface/surface_upload_host.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

using Surface2D = vr::ecs::Surface<vr::ecs::Dim2>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;

using SurfaceSystem2D = vr::ecs::SurfaceSystem<vr::ecs::Dim2>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

constexpr std::uint32_t k_texture_id_checker = 3101U;
constexpr std::uint32_t k_texture_id_stripes = 3102U;
constexpr std::uint32_t k_texture_id_gradient = 3103U;

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
            const bool odd = ((x >> 3U) ^ (y >> 3U)) & 1U;
            pixels_[index] = odd ? color_a_ : color_b_;
        }
    }
}

void FillStripeTexture(std::uint32_t* pixels_,
                       std::uint32_t width_,
                       std::uint32_t height_) {
    if (pixels_ == nullptr || width_ == 0U || height_ == 0U) {
        return;
    }
    for (std::uint32_t y = 0U; y < height_; ++y) {
        for (std::uint32_t x = 0U; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            const float fx = static_cast<float>(x) / static_cast<float>(width_ - 1U);
            const float wave = 0.5F + 0.5F * std::sin((fx * 10.0F + static_cast<float>(y) * 0.03F) * k_pi);
            const std::uint8_t r = static_cast<std::uint8_t>(40.0F + 140.0F * wave);
            const std::uint8_t g = static_cast<std::uint8_t>(100.0F + 120.0F * (1.0F - wave));
            const std::uint8_t b = static_cast<std::uint8_t>(180.0F + 60.0F * wave);
            pixels_[index] = PackRgba8(r, g, b, 255U);
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
            const float fx = static_cast<float>(x) / static_cast<float>(width_ - 1U);
            const float fy = static_cast<float>(y) / static_cast<float>(height_ - 1U);
            const std::uint8_t r = static_cast<std::uint8_t>(45.0F + 180.0F * fx);
            const std::uint8_t g = static_cast<std::uint8_t>(55.0F + 120.0F * fy);
            const std::uint8_t b = static_cast<std::uint8_t>(95.0F + 120.0F * (1.0F - fx));
            const std::uint8_t a = static_cast<std::uint8_t>(150.0F + 105.0F * fy);
            pixels_[index] = PackRgba8(r, g, b, a);
        }
    }
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

[[nodiscard]] std::uint32_t ParseMaxFrames(int argc_,
                                           char** argv_) {
    if (argc_ <= 1 || argv_ == nullptr) {
        return 0U;
    }

    for (int index = 1; index + 1 < argc_; ++index) {
        if (std::string_view(argv_[index]) != "--frames") {
            continue;
        }
        return static_cast<std::uint32_t>(std::strtoul(argv_[index + 1], nullptr, 10));
    }
    return 0U;
}

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildSurfaceUnifiedRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "SurfaceUnifiedSceneColor";
    create_info.scene_target.depth_debug_name = "SurfaceUnifiedSceneDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.06F, 0.07F, 0.11F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.clear_color = {{0.02F, 0.025F, 0.04F, 1.0F}};
    create_info.bloom.enable_reinhard_tonemap = true;
    create_info.bloom.exposure = 1.0F;
    create_info.bloom.apply_manual_gamma = false;
    create_info.bloom.bloom_threshold = 0.74F;
    create_info.bloom.bloom_knee = 0.42F;
    create_info.bloom.bloom_intensity = 0.90F;
    create_info.bloom.blur_filter_scale = 1.0F;
    create_info.bloom.downsample_scale = 0.5F;
    create_info.reserve_scene_renderer_count = 1U;
    create_info.reserve_overlay_renderer_count = 1U;
    return create_info;
}

} // namespace

int main(int argc_,
         char** argv_) {
    Runtime runtime{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    vr::render::SceneRecorder3D recorder{};
    vr::surface::SurfaceRenderer3D renderer_3d{};
    vr::surface::SurfaceRenderer2D renderer_2d{};
    const std::uint32_t max_frames = ParseMaxFrames(argc_, argv_);

    bool runtime_initialized = false;
    bool upload_host_initialized = false;
    bool image_host_initialized = false;
    bool renderer_3d_initialized = false;
    bool renderer_2d_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "Vulkan SDL3 Surface Unified Demo";
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
        recorder.Initialize(BuildSurfaceUnifiedRecorderCreateInfo());
        recorder.BindRuntime(runtime);

        vr::surface::SurfaceUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_2d_instance_buffer_bytes = 512U * 1024U;
        upload_create_info.initial_3d_instance_buffer_bytes = 1024U * 1024U;
        upload_create_info.patch_merge_gap_bytes = 32U;
        surface_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_host_initialized = true;

        vr::surface::SurfaceImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 64U;
        image_create_info.reserve_retired_image_count = 64U;
        surface_image_host.Initialize(runtime.Context(), runtime.GpuMemory(), image_create_info);
        image_host_initialized = true;

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

        constexpr std::uint32_t texture_width = 64U;
        constexpr std::uint32_t texture_height = 64U;
        std::array<std::uint32_t, texture_width * texture_height> pixels_checker{};
        std::array<std::uint32_t, texture_width * texture_height> pixels_stripes{};
        std::array<std::uint32_t, texture_width * texture_height> pixels_gradient{};

        FillCheckerTexture(pixels_checker.data(),
                           texture_width,
                           texture_height,
                           PackRgba8(245U, 194U, 126U, 255U),
                           PackRgba8(118U, 72U, 46U, 255U));
        FillStripeTexture(pixels_stripes.data(), texture_width, texture_height);
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

            upload_info.image_id = k_texture_id_stripes;
            upload_info.pixels = pixels_stripes.data();
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

        std::array<Surface3D, 2U> surface_3d_components{};
        InitializeSurface3DComponent(surface_3d_components[0U],
                                     k_texture_id_checker,
                                     sampler_linear_repeat_id.value,
                                     32U,
                                     true,
                                     false,
                                     vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                     1.0F);
        InitializeSurface3DComponent(surface_3d_components[1U],
                                     k_texture_id_stripes,
                                     sampler_nearest_clamp_id.value,
                                     44U,
                                     false,
                                     true,
                                     vr::ecs::Rgba8{230U, 250U, 255U, 220U},
                                     0.92F);
        SurfaceSystem3D::SetUvTransform(surface_3d_components[1U], 1.2F, 1.2F, -0.1F, -0.05F);

        std::array<Transform3D, 2U> surface_3d_transforms{};
        std::array<Bounds3D, 2U> surface_3d_bounds{};
        TransformSystem3D::Initialize(surface_3d_transforms[0U]);
        TransformSystem3D::Initialize(surface_3d_transforms[1U]);
        BoundsSystem3D::Initialize(surface_3d_bounds[0U]);
        BoundsSystem3D::Initialize(surface_3d_bounds[1U]);
        TransformSystem3D::SetLocalPosition(surface_3d_transforms[0U],
                                            vr::ecs::Float3{.x = -0.95F, .y = -0.05F, .z = 0.10F});
        TransformSystem3D::SetLocalScale(surface_3d_transforms[0U],
                                         vr::ecs::Float3{.x = 2.10F, .y = 2.10F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(surface_3d_transforms[1U],
                                            vr::ecs::Float3{.x = 1.05F, .y = 0.20F, .z = -0.35F});
        TransformSystem3D::SetLocalScale(surface_3d_transforms[1U],
                                         vr::ecs::Float3{.x = 1.75F, .y = 1.75F, .z = 1.0F});
        BoundsSystem3D::SetLocalCenterExtents(surface_3d_bounds[0U],
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 1.05F, .y = 1.05F, .z = 0.05F});
        BoundsSystem3D::SetLocalCenterExtents(surface_3d_bounds[1U],
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{.x = 0.88F, .y = 0.88F, .z = 0.05F});
        TransformSystem3D::UpdateHierarchy(surface_3d_transforms.data(),
                                           static_cast<std::uint32_t>(surface_3d_transforms.size()));
        (void)BoundsSystem3D::UpdateAligned(surface_3d_bounds.data(),
                                            surface_3d_transforms.data(),
                                            static_cast<std::uint32_t>(surface_3d_bounds.size()));

        Camera3D camera{};
        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetProjectionMode(camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * (k_pi / 180.0F));
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
        CameraSystem3D::SetAspectRatio(camera, 1366.0F / 768.0F);

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.7F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        std::array<Surface2D, 3U> surface_2d_components{};
        InitializeSurface2DComponent(surface_2d_components[0U],
                                     k_texture_id_gradient,
                                     420.0F,
                                     220.0F,
                                     4,
                                     vr::ecs::Rgba8{255U, 255U, 255U, 210U},
                                     vr::ecs::Surface2DBlendMode::alpha);
        InitializeSurface2DComponent(surface_2d_components[1U],
                                     k_texture_id_checker,
                                     240.0F,
                                     240.0F,
                                     6,
                                     vr::ecs::Rgba8{255U, 238U, 220U, 245U},
                                     vr::ecs::Surface2DBlendMode::alpha);
        InitializeSurface2DComponent(surface_2d_components[2U],
                                     k_texture_id_stripes,
                                     520.0F,
                                     110.0F,
                                     8,
                                     vr::ecs::Rgba8{180U, 220U, 255U, 200U},
                                     vr::ecs::Surface2DBlendMode::additive);

        std::array<Transform2D, 3U> surface_2d_transforms{};
        for (Transform2D& transform : surface_2d_transforms) {
            TransformSystem2D::Initialize(transform);
        }
        TransformSystem2D::SetLocalPosition(surface_2d_transforms[0U], 250.0F, 160.0F);
        TransformSystem2D::SetLocalPosition(surface_2d_transforms[1U], 1120.0F, 590.0F);
        TransformSystem2D::SetLocalPosition(surface_2d_transforms[2U], 680.0F, 90.0F);
        TransformSystem2D::UpdateHierarchy(surface_2d_transforms.data(),
                                           static_cast<std::uint32_t>(surface_2d_transforms.size()));

        vr::surface::SurfaceRenderer3DCreateInfo renderer_3d_create_info{};
        renderer_3d_create_info.reserve_component_count =
            static_cast<std::uint32_t>(surface_3d_components.size());
        renderer_3d_create_info.reserve_instance_count = 256U;
        renderer_3d_create_info.enable_depth = true;
        renderer_3d_create_info.clear_depth = true;
        renderer_3d_create_info.clear_swapchain = false;
        renderer_3d_create_info.clear_color = {{0.06F, 0.07F, 0.11F, 1.0F}};
        renderer_3d.Initialize(renderer_3d_create_info);
        renderer_3d_initialized = true;
        renderer_3d.SetHosts(&surface_upload_host, &surface_image_host);
        renderer_3d.SetSceneData(surface_3d_components.data(),
                                 surface_3d_transforms.data(),
                                 static_cast<std::uint32_t>(surface_3d_components.size()),
                                 &camera,
                                 &camera_transform,
                                 surface_3d_bounds.data());
        recorder.RegisterSceneRenderer(renderer_3d, vr::render::SceneRenderPassRole::single);

        vr::surface::SurfaceRenderer2DCreateInfo renderer_2d_create_info{};
        renderer_2d_create_info.reserve_component_count =
            static_cast<std::uint32_t>(surface_2d_components.size());
        renderer_2d_create_info.reserve_instance_count = 1024U;
        renderer_2d_create_info.input_positions_pixel_space = true;
        renderer_2d_create_info.pixel_space_origin_top_left = true;
        renderer_2d_create_info.clear_swapchain = false;
        renderer_2d.Initialize(renderer_2d_create_info);
        renderer_2d_initialized = true;
        renderer_2d.SetHosts(&surface_upload_host, &surface_image_host);
        renderer_2d.SetSceneData(surface_2d_components.data(),
                                 surface_2d_transforms.data(),
                                 static_cast<std::uint32_t>(surface_2d_components.size()));
        recorder.RegisterOverlayRenderer(renderer_2d,
                                         vr::render::SceneRecorder3D::MakePresentOverlayOutputConfig());

        std::cout << "sdl_surface_unified_demo running (3D surface offscreen + bloom post stack + 2D overlay). Close window to exit.\n";

        std::uint64_t fps_window_begin_ticks = SDL_GetTicks();
        std::uint32_t fps_window_frame_count = 0U;
        std::uint64_t frame_index = 0U;
        while (runtime.IsRunning()) {
            const std::uint64_t now_ticks = SDL_GetTicks();
            const float time_seconds = static_cast<float>(now_ticks) * 0.001F;

            const VkExtent2D extent = runtime.Swapchain().Extent();
            if (extent.width > 0U && extent.height > 0U) {
                CameraSystem3D::SetAspectRatio(camera,
                                               static_cast<float>(extent.width) /
                                                   static_cast<float>(extent.height));
            }

            TransformSystem3D::SetLocalRotationEulerXyz(surface_3d_transforms[0U],
                                                        0.0F,
                                                        0.70F * time_seconds,
                                                        0.25F * std::sin(time_seconds));
            TransformSystem3D::SetLocalRotationEulerXyz(surface_3d_transforms[1U],
                                                        0.22F * std::sin(time_seconds * 0.7F),
                                                        -0.45F * time_seconds,
                                                        0.0F);
            TransformSystem3D::UpdateHierarchy(surface_3d_transforms.data(),
                                               static_cast<std::uint32_t>(surface_3d_transforms.size()));
            (void)BoundsSystem3D::UpdateAligned(surface_3d_bounds.data(),
                                                surface_3d_transforms.data(),
                                                static_cast<std::uint32_t>(surface_3d_bounds.size()));
            CameraSystem3D::Update(camera, camera_transform);

            TransformSystem2D::SetLocalRotationRadians(surface_2d_transforms[1U],
                                                       0.40F * std::sin(time_seconds * 1.6F));
            const float ui_scale = 0.9F + 0.15F * (0.5F + 0.5F * std::sin(time_seconds * 2.2F));
            TransformSystem2D::SetLocalScale(surface_2d_transforms[2U], ui_scale, 1.0F);
            TransformSystem2D::UpdateHierarchy(surface_2d_transforms.data(),
                                               static_cast<std::uint32_t>(surface_2d_transforms.size()));

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            (void)tick_result;
            ++fps_window_frame_count;
            ++frame_index;

            const std::uint64_t fps_window_elapsed = now_ticks - fps_window_begin_ticks;
            if (fps_window_elapsed >= 1000U) {
                const float fps = (fps_window_elapsed > 0U)
                    ? (1000.0F * static_cast<float>(fps_window_frame_count) /
                       static_cast<float>(fps_window_elapsed))
                    : 0.0F;
                const vr::surface::SurfaceRenderer3DStats stats_3d = renderer_3d.Stats();
                const vr::surface::SurfaceRenderer2DStats stats_2d = renderer_2d.Stats();
                std::cout << "FPS:" << fps
                          << " Frame:" << frame_index
                          << " | 3D Draw:" << stats_3d.draw_call_count
                          << " Batch:" << stats_3d.draw_batch_count
                          << " DSB:" << stats_3d.descriptor_set_bind_count
                          << " DSU:" << stats_3d.descriptor_set_update_count
                          << " | Bloom P:" << recorder.PostStack().Stats().prefilter_draw_call_count
                          << " B:" << recorder.PostStack().Stats().blur_draw_call_count
                          << " C:" << recorder.PostStack().Stats().combine_draw_call_count
                          << " DSU:" << recorder.PostStack().Stats().descriptor_set_update_count
                          << " | 2D Draw:" << stats_2d.draw_call_count
                          << " Batch:" << stats_2d.draw_batch_count
                          << " DSB:" << stats_2d.descriptor_set_bind_count
                          << " DSU:" << stats_2d.descriptor_set_update_count
                          << '\n';
                fps_window_begin_ticks = now_ticks;
                fps_window_frame_count = 0U;
            }

            if (max_frames > 0U && frame_index >= max_frames) {
                break;
            }
        }

        renderer_2d.Shutdown(runtime.Context());
        renderer_2d_initialized = false;
        recorder.Shutdown(runtime.Context());
        renderer_3d.Shutdown(runtime.Context());
        renderer_3d_initialized = false;
        surface_image_host.Shutdown(runtime.Context());
        image_host_initialized = false;
        surface_upload_host.Shutdown(runtime.Context());
        upload_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        std::cerr << "sdl_surface_unified_demo failed: " << exception_.what() << '\n';

        if (renderer_2d_initialized && runtime_initialized && runtime.IsInitialized()) {
            renderer_2d.Shutdown(runtime.Context());
            renderer_2d_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized() && recorder.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
        }
        if (renderer_3d_initialized && runtime_initialized && runtime.IsInitialized()) {
            renderer_3d.Shutdown(runtime.Context());
            renderer_3d_initialized = false;
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
