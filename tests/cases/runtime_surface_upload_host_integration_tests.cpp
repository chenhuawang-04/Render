#include "support/test_framework.hpp"
#include "vr/ecs/system/surface_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/surface/surface_upload_host.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Surface3D = vr::ecs::Surface<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using SurfaceSystem3D = vr::ecs::SurfaceSystem<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

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
    constexpr std::array<std::string_view, 13U> patterns{
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
        "synchronization2"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

VR_TEST_CASE(RuntimeIntegration_surface_upload_host_prepare_runtime_and_incremental_upload, "integration;gpu;sdl;runtime;surface") {
    Runtime runtime{};
    vr::surface::SurfaceUploadHost surface_upload_host{};
    bool runtime_initialized = false;
    bool surface_upload_host_initialized = false;

    constexpr std::uint32_t component_count = 64U;
    std::array<Surface3D, component_count> components{};
    std::array<Transform3D, component_count> transforms{};

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        SurfaceSystem3D::Initialize(components[i]);
        SurfaceSystem3D::SetTextureRoute(components[i], 1000U + i, 1U, 0U, 0U);
        SurfaceSystem3D::SetRuntimeRoute(components[i], 1000U + i, 1U, 0U);
        SurfaceSystem3D::SetDepthBin(components[i], static_cast<std::uint16_t>(i & 0x3FU));

        TransformSystem3D::Initialize(transforms[i]);
        TransformSystem3D::SetLocalPosition(transforms[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i & 7U) * 0.25F,
                                                .y = static_cast<float>((i >> 3U) & 7U) * 0.25F,
                                                .z = -0.25F * static_cast<float>(i & 3U)});
    }
    TransformSystem3D::UpdateHierarchy(transforms.data(), component_count);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_surface_upload_host";
        create_info.platform.window.width = 320;
        create_info.platform.window.height = 240;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::surface::SurfaceUploadHostCreateInfo upload_host_create_info{};
        upload_host_create_info.frames_in_flight = 2U;
        upload_host_create_info.patch_merge_gap_bytes = 0U;
        upload_host_create_info.patch_fallback_coverage_percent = 95U;
        upload_host_create_info.patch_fallback_copy_count = 1024U;
        surface_upload_host.Initialize(runtime.Context(), runtime.GpuMemory(), upload_host_create_info);
        surface_upload_host_initialized = true;

        vr::ecs::Surface3DRuntimeScratch runtime_scratch{};
        vr::ecs::SurfaceUploadPlanScratch<vr::ecs::Dim3> plan_scratch{};
        vr::surface::Surface3DRuntimeUploadOptions upload_options{};
        upload_options.enable_partial_upload = true;
        upload_options.require_dirty_hint_for_partial = true;
        upload_options.min_partial_dirty_component_count = 1U;
        upload_options.plan_build.merge_gap_instances = 0U;

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        const auto first_result = surface_upload_host.PrepareRuntimeAndUpload3D(
            runtime.Context(),
            runtime.Upload(),
            0U,
            components.data(),
            transforms.data(),
            component_count,
            runtime_scratch,
            plan_scratch,
            {},
            upload_options);
        VR_CHECK(first_result.runtime.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::miss);
        VR_CHECK(first_result.runtime.emitted_instance_count == component_count);
        VR_CHECK(first_result.upload.uploaded);
        VR_CHECK(!first_result.upload.partial);
        VR_CHECK(!first_result.skipped_upload);
        const vr::render::UploadEndFrameResult first_upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (first_upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }

        std::array<std::uint32_t, 8U> dirty_components{};
        for (std::uint32_t i = 0U; i < dirty_components.size(); ++i) {
            dirty_components[i] = 5U + i * 3U;
            TransformSystem3D::SetLocalPosition(
                transforms[dirty_components[i]],
                vr::ecs::Float3{
                    .x = 1.5F + static_cast<float>(i) * 0.1F,
                    .y = 0.3F * static_cast<float>(i),
                    .z = -1.0F - 0.05F * static_cast<float>(i)});
        }
        TransformSystem3D::UpdateHierarchy(transforms.data(), component_count);

        vr::ecs::Surface3DRuntimeBuildHint partial_hint{};
        partial_hint.transform_dirty_component_indices = dirty_components.data();
        partial_hint.transform_dirty_component_count = static_cast<std::uint32_t>(dirty_components.size());

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        const auto partial_result = surface_upload_host.PrepareRuntimeAndUpload3D(
            runtime.Context(),
            runtime.Upload(),
            0U,
            components.data(),
            transforms.data(),
            component_count,
            runtime_scratch,
            plan_scratch,
            partial_hint,
            upload_options);
        VR_CHECK(partial_result.runtime.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::hit_partial_update);
        VR_CHECK(partial_result.plan.range_count > 0U);
        VR_CHECK(partial_result.upload.uploaded);
        VR_CHECK(partial_result.upload.partial);
        VR_CHECK(partial_result.used_partial_upload);
        VR_CHECK(!partial_result.skipped_upload);
        const vr::render::UploadEndFrameResult second_upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (second_upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }

        runtime.Upload().BeginFrame(runtime.Context(), 1U);
        const auto reused_result = surface_upload_host.PrepareRuntimeAndUpload3D(
            runtime.Context(),
            runtime.Upload(),
            1U,
            components.data(),
            transforms.data(),
            component_count,
            runtime_scratch,
            plan_scratch,
            {},
            upload_options);
        VR_CHECK(reused_result.runtime.cache_status == vr::ecs::SurfaceRuntimeCacheStatus::hit_reused);
        VR_CHECK(reused_result.skipped_upload);
        VR_CHECK(!reused_result.upload.uploaded);
        (void)runtime.Upload().EndFrameAndSubmit(runtime.Context(), 1U);

        const auto stats = surface_upload_host.Stats();
        VR_CHECK(stats.upload_count >= 2U);
        VR_CHECK(stats.partial_upload_count >= 1U);
        VR_CHECK(stats.patch_copy_count >= 1U);
        VR_CHECK(stats.uploaded_bytes > 0U);

        surface_upload_host.Shutdown(runtime.Context());
        surface_upload_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (surface_upload_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            surface_upload_host.Shutdown(runtime.Context());
            surface_upload_host_initialized = false;
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
