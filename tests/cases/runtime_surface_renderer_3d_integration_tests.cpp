#include "support/test_framework.hpp"
#include "support/render_graph_test_utils.hpp"
#include "vr/ecs/system/appearance_system.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"
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
#include <utility>

namespace vr::surface {

struct SurfaceRenderer3DTestAccess final {
    static void BindPrepareFrameRuntime(
        SurfaceRenderer3D& renderer_,
        const render::SurfaceRenderer3DPrepareView& prepare_view_) {
        renderer_.BindPrepareFrameRuntime(prepare_view_);
    }

    [[nodiscard]] static auto BuildCpuRuntimeFrameStage(
        SurfaceRenderer3D& renderer_,
        const render::SurfaceRenderer3DPrepareView& prepare_view_)
        -> SurfaceRenderer3D::CpuRuntimeFrameBuildResult {
        return renderer_.BuildCpuRuntimeFrameStage(prepare_view_);
    }

    static void ApplyPreparedFrameState(
        SurfaceRenderer3D& renderer_,
        const render::SurfaceRenderer3DPrepareView& prepare_view_,
        SurfaceRenderer3D::CpuRuntimeFrameBuildResult cpu_stage_) {
        renderer_.ApplyPreparedFrameState(prepare_view_, std::move(cpu_stage_));
    }

    [[nodiscard]] static bool CpuPayloadUsesFullUpload(
        const SurfaceRenderer3D::CpuRuntimeFrameBuildResult& cpu_stage_) noexcept {
        return cpu_stage_.dispatch_payload.upload_mode ==
               SurfaceRenderer3D::UploadDispatchMode::full;
    }

    [[nodiscard]] static bool CpuPayloadUsesPartialUpload(
        const SurfaceRenderer3D::CpuRuntimeFrameBuildResult& cpu_stage_) noexcept {
        return cpu_stage_.dispatch_payload.upload_mode ==
               SurfaceRenderer3D::UploadDispatchMode::partial;
    }

    [[nodiscard]] static std::uint32_t PreparedInstanceCount(
        const SurfaceRenderer3D::CpuRuntimeFrameBuildResult& cpu_stage_) noexcept {
        return static_cast<std::uint32_t>(
            cpu_stage_.prepared_artifacts.instance_upload_source.size());
    }

    [[nodiscard]] static std::uint32_t PreparedDrawBatchCount(
        const SurfaceRenderer3D::CpuRuntimeFrameBuildResult& cpu_stage_) noexcept {
        return static_cast<std::uint32_t>(
            cpu_stage_.prepared_artifacts.draw_batches.size());
    }

    [[nodiscard]] static std::uint32_t PreparedAppearanceRecordCount(
        const SurfaceRenderer3D::CpuRuntimeFrameBuildResult& cpu_stage_) noexcept {
        return static_cast<std::uint32_t>(
            cpu_stage_.prepared_artifacts.appearance_source_records.size());
    }

    [[nodiscard]] static std::uint32_t PreparedPatchCount(
        const SurfaceRenderer3D::CpuRuntimeFrameBuildResult& cpu_stage_) noexcept {
        return static_cast<std::uint32_t>(
            cpu_stage_.prepared_artifacts.upload_patch_ranges.size());
    }

    [[nodiscard]] static VkDescriptorSet CurrentAppearanceDescriptorSet(
        const SurfaceRenderer3D& renderer_) noexcept {
        if (renderer_.active_frame_index >= renderer_.frame_appearance_resources.size()) {
            return VK_NULL_HANDLE;
        }
        return renderer_.frame_appearance_resources[renderer_.active_frame_index]
            .descriptor_set;
    }

    [[nodiscard]] static std::uint32_t UploadedInstanceCount(
        const SurfaceRenderer3D& renderer_) noexcept {
        return renderer_.last_upload_result.upload.element_count;
    }

    [[nodiscard]] static std::uint32_t ScratchDrawBatchCount(
        const SurfaceRenderer3D& renderer_) noexcept {
        return static_cast<std::uint32_t>(renderer_.runtime_scratch.draw_batches.size());
    }

    [[nodiscard]] static std::uint32_t ScratchAppearanceSourceRecordCount(
        const SurfaceRenderer3D& renderer_) noexcept {
        return static_cast<std::uint32_t>(renderer_.appearance_source_record_scratch.size());
    }

    [[nodiscard]] static std::uint32_t ActivePreparedDrawBatchCount(
        const SurfaceRenderer3D& renderer_) noexcept {
        return static_cast<std::uint32_t>(
            renderer_.active_prepared_frame_state.artifacts.draw_batches.size());
    }

    [[nodiscard]] static std::uint32_t ActivePreparedAppearanceRecordCount(
        const SurfaceRenderer3D& renderer_) noexcept {
        return static_cast<std::uint32_t>(renderer_.active_prepared_frame_state
                                              .artifacts.appearance_source_records
                                              .size());
    }

    [[nodiscard]] static std::uint32_t ActivePreparedInstanceCount(
        const SurfaceRenderer3D& renderer_) noexcept {
        return static_cast<std::uint32_t>(
            renderer_.active_prepared_frame_state.artifacts.instance_upload_source
                .size());
    }

    [[nodiscard]] static bool ActivePreparedHasSceneData(
        const SurfaceRenderer3D& renderer_) noexcept {
        return renderer_.active_prepared_frame_state.artifacts.has_scene_data;
    }

    [[nodiscard]] static bool ActiveRuntimeHasBuffer(
        const SurfaceRenderer3D& renderer_) noexcept {
        return renderer_.active_frame_runtime_truth.instance_upload_range.buffer !=
               VK_NULL_HANDLE;
    }

    [[nodiscard]] static std::uint32_t ActiveRuntimeUploadedInstanceCount(
        const SurfaceRenderer3D& renderer_) noexcept {
        return renderer_.active_frame_runtime_truth.instance_upload_range
            .element_count;
    }
};

} // namespace vr::surface

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using Appearance3D = vr::ecs::Appearance<vr::ecs::Dim3>;
using AppearanceSystem3D = vr::ecs::AppearanceSystem<vr::ecs::Dim3>;
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


void ConfigureSurface3DRuntimeCreateInfo(Runtime::CreateInfo& create_info_,
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
    create_info_.diagnostics.level = vr::runtime::DiagnosticsLevel::Detailed;
    create_info_.poll_events_each_tick = true;
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
    SurfaceSystem3D::SetSource(component_, vr::ecs::SurfaceSampledSource3DDesc{.surface_id = texture_id_, .sampler_id = sampler_id_, .uv_set = 0U, .flags = 0U});
    SurfaceSystem3D::SetDepthBin(component_, depth_bin_);
    SurfaceSystem3D::SetRenderPassHint(component_, vr::ecs::SurfaceRenderPassHint::transparent);

    Appearance3D appearance{};
    AppearanceSystem3D::Initialize(appearance);
    AppearanceSystem3D::SetBaseColor(appearance, tint_color_);
    AppearanceSystem3D::SetOpacity(appearance, opacity_);
    AppearanceSystem3D::SetDepthTest(appearance, true);
    AppearanceSystem3D::SetDepthWrite(appearance, depth_write_);
    AppearanceSystem3D::SetDoubleSided(appearance, double_sided_);
    AppearanceSystem3D::SetBlendMode(appearance, vr::ecs::AppearanceBlendMode::alpha);
    AppearanceSystem3D::SetAlphaMode(appearance, vr::ecs::AppearanceAlphaMode::blend);
    (void)SurfaceSystem3D::ApplyAppearanceRuntimeState(component_, appearance.style);
}

[[nodiscard]] vr::render::SceneRecorder3DCreateInfo BuildSurfaceRecorderCreateInfo() noexcept {
    vr::render::SceneRecorder3DCreateInfo create_info{};
    create_info.scene_target.color_debug_name = "RuntimeSurface3DSceneColor";
    create_info.scene_target.depth_debug_name = "RuntimeSurface3DSceneDepth";
    create_info.scene_target.enable_depth = true;
    create_info.scene_target.color_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.depth_lifetime = vr::render::RenderTargetLifetime::transient;
    create_info.scene_target.clear_color = VkClearColorValue{{0.05F, 0.07F, 0.10F, 1.0F}};
    create_info.bloom.clear_swapchain = true;
    create_info.bloom.enable_reinhard_tonemap = true;
    create_info.bloom.exposure = 1.0F;
    create_info.bloom.apply_manual_gamma = false;
    create_info.bloom.bloom_threshold = 0.66F;
    create_info.bloom.bloom_knee = 0.40F;
    create_info.bloom.bloom_intensity = 0.90F;
    create_info.bloom.blur_filter_scale = 1.05F;
    create_info.bloom.downsample_scale = 0.5F;
    create_info.reserve_scene_renderer_count = 1U;
    create_info.reserve_overlay_renderer_count = 0U;
    return create_info;
}

VR_TEST_CASE(RuntimeIntegration_surface_renderer_3d_prepare_cpu_seam_regression,
             "integration;gpu;sdl;runtime;surface;prepare;seam;regression") {
    Runtime runtime{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    vr::surface::SurfaceRenderer3D surface_renderer{};

    bool runtime_initialized = false;
    bool upload_initialized = false;
    bool image_initialized = false;
    bool surface_renderer_initialized = false;

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
        ConfigureSurface3DRuntimeCreateInfo(
            create_info, "vr_tests_runtime_surface_3d_prepare_cpu_seam");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::surface::SurfaceUploadHostCreateInfo upload_create_info{};
        upload_create_info.frames_in_flight = 2U;
        upload_create_info.initial_3d_instance_buffer_bytes = 512U * 1024U;
        surface_upload_host.Initialize(
            runtime.Context(), runtime.GpuMemory(), upload_create_info);
        upload_initialized = true;

        vr::surface::SurfaceImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 16U;
        image_create_info.reserve_retired_image_count = 16U;
        surface_image_host.Initialize(
            runtime.Context(), runtime.GpuMemory(), image_create_info);
        image_initialized = true;
        runtime.BindlessResources().ConfigureSurfaceImageHost(surface_image_host);

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
            sampler_linear_repeat_id =
                runtime.Sampler().RegisterSampler(runtime.Context(), sampler_desc);
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
            sampler_nearest_clamp_id =
                runtime.Sampler().RegisterSampler(runtime.Context(), sampler_desc);
        }

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        vr::surface::SurfaceImageUploadInfo upload_info{};
        upload_info.width = texture_width;
        upload_info.height = texture_height;
        upload_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        upload_info.bytes_per_pixel = 4U;
        upload_info.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        upload_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        upload_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;

        upload_info.image_id = 7101U;
        upload_info.pixels = pixels_checker.data();
        surface_image_host.UploadImage(
            runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

        upload_info.image_id = 7102U;
        upload_info.pixels = pixels_gradient.data();
        surface_image_host.UploadImage(
            runtime.Context(), runtime.Upload(), 0U, 0U, 0U, upload_info);

        const auto upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        surface_image_host.BeginFrame(runtime.Context(), 0U);

        std::array<Surface3D, 2U> components{};
        InitializeSurface3DComponent(components[0U],
                                     7101U,
                                     sampler_linear_repeat_id.value,
                                     24U,
                                     true,
                                     false,
                                     vr::ecs::Rgba8{255U, 255U, 255U, 255U},
                                     1.0F);
        InitializeSurface3DComponent(components[1U],
                                     7102U,
                                     sampler_nearest_clamp_id.value,
                                     40U,
                                     false,
                                     true,
                                     vr::ecs::Rgba8{220U, 240U, 255U, 220U},
                                     0.90F);
        SurfaceSystem3D::SetUvTransform(
            components[1U], 1.12F, 1.12F, -0.08F, -0.04F);

        std::array<Transform3D, 2U> transforms{};
        std::array<Bounds3D, 2U> bounds{};
        for (std::uint32_t i = 0U; i < transforms.size(); ++i) {
            TransformSystem3D::Initialize(transforms[i]);
            BoundsSystem3D::Initialize(bounds[i]);
        }
        TransformSystem3D::SetLocalPosition(
            transforms[0U], vr::ecs::Float3{.x = -0.85F, .y = -0.10F, .z = 0.12F});
        TransformSystem3D::SetLocalScale(
            transforms[0U], vr::ecs::Float3{.x = 2.00F, .y = 2.00F, .z = 1.0F});
        TransformSystem3D::SetLocalPosition(
            transforms[1U], vr::ecs::Float3{.x = 0.95F, .y = 0.18F, .z = -0.25F});
        TransformSystem3D::SetLocalScale(
            transforms[1U], vr::ecs::Float3{.x = 1.65F, .y = 1.65F, .z = 1.0F});
        BoundsSystem3D::SetLocalCenterExtents(
            bounds[0U],
            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            vr::ecs::Float3{.x = 1.00F, .y = 1.00F, .z = 0.05F});
        BoundsSystem3D::SetLocalCenterExtents(
            bounds[1U],
            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
            vr::ecs::Float3{.x = 0.88F, .y = 0.88F, .z = 0.05F});
        TransformSystem3D::UpdateHierarchy(
            transforms.data(), static_cast<std::uint32_t>(transforms.size()));
        (void)BoundsSystem3D::UpdateAligned(
            bounds.data(), transforms.data(), static_cast<std::uint32_t>(bounds.size()));

        Camera3D camera{};
        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetProjectionMode(
            camera, vr::ecs::CameraProjectionMode::perspective);
        CameraSystem3D::SetVerticalFovRadians(
            camera, 60.0F * 0.01745329251994329577F);
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
        CameraSystem3D::SetAspectRatio(camera, 640.0F / 360.0F);

        Transform3D camera_transform{};
        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(
            camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.5F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
        CameraSystem3D::MarkViewDirty(camera);
        CameraSystem3D::Update(camera, camera_transform);

        vr::surface::SurfaceRenderer3DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count =
            static_cast<std::uint32_t>(components.size());
        renderer_create_info.reserve_instance_count = 128U;
        renderer_create_info.enable_depth = true;
        surface_renderer.Initialize(renderer_create_info);
        surface_renderer_initialized = true;
        surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        surface_renderer.SetSceneData(components.data(),
                                      transforms.data(),
                                      static_cast<std::uint32_t>(components.size()),
                                      &camera,
                                      &camera_transform,
                                      bounds.data());

        const vr::render::SceneRecorder3DPrepareView prepare_view{
            .device = runtime.Context(),
            .gpu_memory = &runtime.GpuMemory(),
            .texture = &runtime.Texture(),
            .bindless = &runtime.BindlessResources(),
            .upload = &runtime.Upload(),
            .descriptor = &runtime.Descriptor(),
            .ibl = &runtime.Ibl(),
            .pipeline = &runtime.Pipeline(),
            .render_target = runtime.RenderTarget(),
            .sampler = &runtime.Sampler(),
            .frame =
                vr::render::FrameStaticContext{
                    .frame_index = 0U,
                    .swapchain_extent = runtime.Swapchain().Extent(),
                    .swapchain_format = runtime.Swapchain().Format(),
                },
            .progress = {},
        };
        const auto surface_prepare_view =
            vr::render::MakeSurfaceRenderer3DPrepareView(prepare_view);

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        vr::surface::SurfaceRenderer3DTestAccess::BindPrepareFrameRuntime(
            surface_renderer, surface_prepare_view);
        const std::uint32_t upload_count_before_cpu_stage =
            surface_upload_host.Stats().upload_count;
        const std::uint64_t descriptor_updates_before_cpu_stage =
            runtime.Descriptor().Stats().transient_update_call_count;
        const auto cpu_build_result =
            vr::surface::SurfaceRenderer3DTestAccess::BuildCpuRuntimeFrameStage(
                surface_renderer, surface_prepare_view);
        const auto& cpu_payload = cpu_build_result.dispatch_payload;

        VR_CHECK(cpu_payload.has_scene_data);
        VR_CHECK(cpu_payload.instance_upload_revision != 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     CpuPayloadUsesFullUpload(cpu_build_result));
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     CpuPayloadUsesPartialUpload(cpu_build_result));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedPatchCount(cpu_build_result) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedInstanceCount(cpu_build_result) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedDrawBatchCount(cpu_build_result) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedAppearanceRecordCount(cpu_build_result) > 0U);
        VR_CHECK(surface_upload_host.Stats().upload_count ==
                 upload_count_before_cpu_stage);
        VR_CHECK(runtime.Descriptor().Stats().transient_update_call_count ==
                 descriptor_updates_before_cpu_stage);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     UploadedInstanceCount(surface_renderer) == 0U);
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ScratchDrawBatchCount(surface_renderer) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ScratchAppearanceSourceRecordCount(surface_renderer) == 0U);
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedHasSceneData(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedDrawBatchCount(surface_renderer) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedAppearanceRecordCount(surface_renderer) ==
                 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     CurrentAppearanceDescriptorSet(surface_renderer) ==
                 VK_NULL_HANDLE);

        vr::surface::SurfaceRenderer3DTestAccess::ApplyPreparedFrameState(
            surface_renderer, surface_prepare_view, cpu_build_result);
        VR_CHECK(surface_upload_host.Stats().upload_count >
                 upload_count_before_cpu_stage);
        VR_CHECK(runtime.Descriptor().Stats().transient_update_call_count >
                 descriptor_updates_before_cpu_stage);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     UploadedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ScratchDrawBatchCount(surface_renderer) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ScratchAppearanceSourceRecordCount(surface_renderer) ==
                 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedHasSceneData(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedDrawBatchCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedAppearanceRecordCount(surface_renderer) >
                 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     CurrentAppearanceDescriptorSet(surface_renderer) !=
                 VK_NULL_HANDLE);
        VR_CHECK(!surface_renderer.Stats().used_partial_upload);
        const auto first_apply_upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (first_apply_upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        vr::surface::SurfaceRenderer3DTestAccess::BindPrepareFrameRuntime(
            surface_renderer, surface_prepare_view);
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) == 0U);
        const std::uint32_t upload_count_before_second_cpu_stage =
            surface_upload_host.Stats().upload_count;
        const std::uint32_t reuse_hits_before_second_apply =
            surface_upload_host.Stats().reuse_hit_count;
        const std::uint64_t descriptor_updates_before_second_cpu_stage =
            runtime.Descriptor().Stats().transient_update_call_count;
        const auto second_cpu_build_result =
            vr::surface::SurfaceRenderer3DTestAccess::BuildCpuRuntimeFrameStage(
                surface_renderer, surface_prepare_view);
        const auto& second_cpu_payload = second_cpu_build_result.dispatch_payload;

        VR_CHECK(second_cpu_payload.has_scene_data);
        VR_CHECK(second_cpu_payload.instance_upload_revision != 0U);
        VR_CHECK(second_cpu_payload.runtime_stats.cache_status ==
                 vr::ecs::SurfaceRuntimeCacheStatus::hit_reused);
        VR_CHECK(second_cpu_payload.runtime_stats.cache_reused);
        VR_CHECK(!second_cpu_payload.runtime_stats.transform_only_update);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     CpuPayloadUsesFullUpload(second_cpu_build_result));
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     CpuPayloadUsesPartialUpload(second_cpu_build_result));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedPatchCount(second_cpu_build_result) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedInstanceCount(second_cpu_build_result) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedDrawBatchCount(second_cpu_build_result) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     PreparedAppearanceRecordCount(second_cpu_build_result) >
                 0U);
        VR_CHECK(surface_upload_host.Stats().upload_count ==
                 upload_count_before_second_cpu_stage);
        VR_CHECK(runtime.Descriptor().Stats().transient_update_call_count ==
                 descriptor_updates_before_second_cpu_stage);
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) == 0U);

        vr::surface::SurfaceRenderer3DTestAccess::ApplyPreparedFrameState(
            surface_renderer, surface_prepare_view, second_cpu_build_result);
        VR_CHECK(surface_upload_host.Stats().upload_count ==
                 upload_count_before_second_cpu_stage);
        VR_CHECK(surface_upload_host.Stats().reuse_hit_count >
                 reuse_hits_before_second_apply);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     UploadedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedHasSceneData(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(surface_renderer.Stats().cache_reused);
        VR_CHECK(surface_renderer.Stats().skipped_upload);
        VR_CHECK(!surface_renderer.Stats().used_partial_upload);
        const auto second_apply_upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (second_apply_upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }

        std::array<Transform3D, 2U> alternate_transforms = transforms;
        surface_renderer.SetSceneData(
            components.data(),
            alternate_transforms.data(),
            static_cast<std::uint32_t>(components.size()),
            &camera,
            &camera_transform,
            bounds.data());
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedHasSceneData(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedDrawBatchCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedAppearanceRecordCount(surface_renderer) >
                 0U);

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        vr::surface::SurfaceRenderer3DTestAccess::BindPrepareFrameRuntime(
            surface_renderer, surface_prepare_view);
        const auto third_cpu_build_result =
            vr::surface::SurfaceRenderer3DTestAccess::BuildCpuRuntimeFrameStage(
                surface_renderer, surface_prepare_view);
        vr::surface::SurfaceRenderer3DTestAccess::ApplyPreparedFrameState(
            surface_renderer, surface_prepare_view, third_cpu_build_result);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) > 0U);
        const auto third_apply_upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (third_apply_upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }

        surface_renderer.OnSwapchainRecreated(
            runtime.Swapchain().ImageCount(),
            runtime.Swapchain().Extent(),
            runtime.Swapchain().Format());
        VR_CHECK(!vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeHasBuffer(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActiveRuntimeUploadedInstanceCount(surface_renderer) == 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedHasSceneData(surface_renderer));
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedInstanceCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedDrawBatchCount(surface_renderer) > 0U);
        VR_CHECK(vr::surface::SurfaceRenderer3DTestAccess::
                     ActivePreparedAppearanceRecordCount(surface_renderer) >
                 0U);

        surface_renderer.Shutdown(runtime.Context());
        surface_renderer_initialized = false;
        surface_image_host.Shutdown(runtime.Context());
        image_initialized = false;
        surface_upload_host.Shutdown(runtime.Context());
        upload_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (surface_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_renderer.Shutdown(runtime.Context());
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

VR_TEST_CASE(RuntimeIntegration_surface_renderer_3d_bloom_post_stack_smoke,
             "integration;gpu;sdl;runtime;surface;render_target;postprocess") {
    Runtime runtime{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    vr::surface::SurfaceImageHost surface_image_host{};
    vr::render::SceneRecorder3D recorder{};
    vr::surface::SurfaceRenderer3D surface_renderer{};

    bool runtime_initialized = false;
    bool upload_initialized = false;
    bool image_initialized = false;
    bool surface_renderer_initialized = false;

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
        ConfigureSurface3DRuntimeCreateInfo(create_info, "vr_tests_runtime_surface_3d_offscreen");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        recorder.Initialize(BuildSurfaceRecorderCreateInfo());
        recorder.BindRuntime(runtime);

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
        runtime.BindlessResources().ConfigureSurfaceImageHost(surface_image_host);

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
        surface_renderer.Initialize(renderer_create_info);
        surface_renderer_initialized = true;
        surface_renderer.SetHosts(&surface_upload_host, &surface_image_host);
        surface_renderer.SetSceneData(components.data(),
                                      transforms.data(),
                                      static_cast<std::uint32_t>(components.size()),
                                      &camera,
                                      &camera_transform,
                                      bounds.data());
        recorder.RegisterTransparentSceneRenderer(surface_renderer, vr::render::SceneRenderPassRole::single);

        vr::render::RenderView3D main_view{};
        vr::render::RenderScenePacket3D main_scene_packet{};
        vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                           main_scene_packet,
                                                           camera,
                                                           camera_transform,
                                                           runtime.Swapchain().Extent(),
                                                           0U);
        recorder.SetFramePacket(&main_scene_packet);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_surface_draw_calls = 0U;
        std::uint32_t max_surface_draw_batches = 0U;
        std::uint32_t max_surface_instances = 0U;
        std::uint32_t max_surface_bindless_set_binds = 0U;
        std::uint32_t max_surface_ibl_descriptor_binds = 0U;
        std::uint32_t max_surface_depth_test_batches = 0U;
        std::uint32_t max_surface_depth_write_batches = 0U;
        std::uint32_t max_surface_culling_visible_count = 0U;
        std::uint32_t max_prefilter_draw_calls = 0U;
        std::uint32_t max_blur_draw_calls = 0U;
        std::uint32_t max_combine_draw_calls = 0U;
        std::uint32_t max_bloom_descriptor_updates = 0U;
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
            vr::render::RefreshExtentBoundWorldSceneSubmission(main_view,
                                                               main_scene_packet,
                                                               camera,
                                                               camera_transform,
                                                               runtime.Swapchain().Extent(),
                                                               tick_index);
            recorder.SetFramePacket(&main_scene_packet);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const auto surface_stats = surface_renderer.Stats();
            const auto bloom_stats = recorder.BloomStats();
            max_surface_draw_calls = std::max(max_surface_draw_calls, surface_stats.draw_call_count);
            max_surface_draw_batches = std::max(max_surface_draw_batches, surface_stats.draw_batch_count);
            max_surface_instances = std::max(max_surface_instances, surface_stats.instance_count);
            max_surface_bindless_set_binds =
                std::max(max_surface_bindless_set_binds, surface_stats.descriptor_set_bind_count);
            max_surface_ibl_descriptor_binds =
                std::max(max_surface_ibl_descriptor_binds, surface_stats.ibl_descriptor_set_bind_count);
            max_surface_depth_test_batches =
                std::max(max_surface_depth_test_batches, surface_stats.depth_test_batch_count);
            max_surface_depth_write_batches =
                std::max(max_surface_depth_write_batches, surface_stats.depth_write_batch_count);
            max_surface_culling_visible_count =
                std::max(max_surface_culling_visible_count, surface_stats.culling_visible_count);
            max_prefilter_draw_calls =
                std::max(max_prefilter_draw_calls, bloom_stats.prefilter_draw_call_count);
            max_blur_draw_calls =
                std::max(max_blur_draw_calls, bloom_stats.blur_draw_call_count);
            max_combine_draw_calls =
                std::max(max_combine_draw_calls, bloom_stats.combine_draw_call_count);
            max_bloom_descriptor_updates =
                std::max(max_bloom_descriptor_updates, bloom_stats.descriptor_set_update_count);
            observed_bounds_culling = observed_bounds_culling || surface_stats.used_bounds_culling;
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_surface_draw_calls > 0U);
        VR_CHECK(max_surface_draw_batches > 0U);
        VR_CHECK(max_surface_instances > 0U);
        VR_CHECK(max_surface_bindless_set_binds > 0U);
        VR_CHECK(max_surface_ibl_descriptor_binds > 0U);
        VR_CHECK(max_surface_depth_test_batches > 0U);
        VR_CHECK(max_surface_depth_write_batches == 0U);
        VR_CHECK(max_surface_culling_visible_count > 0U);
        VR_CHECK(observed_bounds_culling);
        VR_CHECK(max_prefilter_draw_calls > 0U);
        VR_CHECK(max_blur_draw_calls > 0U);
        VR_CHECK(max_combine_draw_calls > 0U);
        const bool graph_only_record_active = vr::test::IsGraphOnlyScene3DRecordActive(runtime);
        VR_CHECK(graph_only_record_active);
        VR_CHECK(max_bloom_descriptor_updates == 0U);
        VR_CHECK(recorder.Stats().frame_packet_prepare_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_record_count == 0U);
        VR_CHECK(recorder.ActiveView() == &main_view);
        VR_CHECK(recorder.ActiveView() != nullptr);
        VR_CHECK(recorder.ActiveView()->camera == &camera);
        VR_CHECK(surface_image_host.Stats().image_count >= 2U);
        VR_CHECK(runtime.Ibl().Stats().prepared_frame_count > 0U);
        VR_CHECK(surface_image_host.ResolveBindlessImageSlot(6101U).IsValid());
        VR_CHECK(surface_image_host.ResolveBindlessImageSlot(6102U).IsValid());

        recorder.Shutdown(runtime.Context());
        surface_renderer.Shutdown(runtime.Context());
        surface_renderer_initialized = false;
        surface_image_host.Shutdown(runtime.Context());
        image_initialized = false;
        surface_upload_host.Shutdown(runtime.Context());
        upload_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (runtime_initialized && runtime.IsInitialized() && recorder.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
        }
        if (surface_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_renderer.Shutdown(runtime.Context());
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

