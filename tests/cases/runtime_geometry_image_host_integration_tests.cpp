#include "support/test_framework.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/render/render_runtime_host.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

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

VR_TEST_CASE(RuntimeIntegration_geometry_image_host_upload_and_lifecycle, "integration;gpu;sdl;runtime;geometry") {
    Runtime runtime{};
    vr::geometry::GeometryImageHost image_host{};
    bool runtime_initialized = false;
    bool image_host_initialized = false;

    constexpr std::uint32_t width = 8U;
    constexpr std::uint32_t height = 8U;
    std::array<std::uint32_t, width * height> pixels_a{};
    std::array<std::uint32_t, width * height> pixels_b{};
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const bool checker = ((x ^ y) & 1U) != 0U;
            pixels_a[index] = checker ? 0xFF2A7CDBU : 0xFFF0E0CCU;
            pixels_b[index] = checker ? 0xFF93E4A6U : 0xFF324A78U;
        }
    }

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_geometry_image_host";
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

        vr::geometry::GeometryImageHostCreateInfo image_create_info{};
        image_create_info.reserve_image_count = 32U;
        image_create_info.reserve_retired_image_count = 32U;
        image_host.Initialize(runtime.Context(), runtime.GpuMemory(), image_create_info);
        image_host_initialized = true;

        vr::geometry::GeometryImageUploadInfo upload_info{};
        upload_info.image_id = 1U;
        upload_info.pixels = pixels_a.data();
        upload_info.width = width;
        upload_info.height = height;
        upload_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        upload_info.bytes_per_pixel = 4U;
        upload_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        upload_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        upload_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        image_host.UploadImage(runtime.Context(),
                               runtime.Upload(),
                               0U,
                               0U,
                               0U,
                               upload_info);
        const vr::render::UploadEndFrameResult first_upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (first_upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        image_host.BeginFrame(runtime.Context(), 0U);

        const vr::geometry::GeometryImageHost::ImageRecord* first_record = image_host.FindImage(1U);
        VR_REQUIRE(first_record != nullptr);
        VR_CHECK(first_record->resource.image != VK_NULL_HANDLE);
        VR_CHECK(first_record->resource.default_view != VK_NULL_HANDLE);
        VR_CHECK(first_record->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        upload_info.pixels = pixels_b.data();
        runtime.Upload().BeginFrame(runtime.Context(), 1U);
        image_host.UploadImage(runtime.Context(),
                               runtime.Upload(),
                               1U,
                               0U,
                               0U,
                               upload_info);
        const vr::render::UploadEndFrameResult second_upload_end =
            runtime.Upload().EndFrameAndSubmit(runtime.Context(), 1U);
        if (second_upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 1U);
        }
        image_host.BeginFrame(runtime.Context(), 0U);

        const vr::geometry::GeometryImageHost::ImageRecord* second_record = image_host.FindImage(1U);
        VR_REQUIRE(second_record != nullptr);
        VR_CHECK(second_record->revision >= 2U);
        VR_CHECK(second_record->resource.image != VK_NULL_HANDLE);
        VR_CHECK(second_record->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VR_REQUIRE(image_host.RemoveImage(runtime.Context(), 1U, 0U, 0U));
        image_host.BeginFrame(runtime.Context(), 0U);
        VR_CHECK(image_host.FindImage(1U) == nullptr);

        const vr::geometry::GeometryImageHostStats stats = image_host.Stats();
        VR_CHECK(stats.uploaded_image_count > 0U);
        VR_CHECK(stats.updated_image_count > 0U);
        VR_CHECK(stats.removed_image_count > 0U);
        VR_CHECK(stats.uploaded_bytes >= static_cast<std::uint64_t>(width * height * 4U) * 2U);
        VR_CHECK(stats.barrier_count >= 2U);

        image_host.Shutdown(runtime.Context());
        image_host_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (image_host_initialized && runtime_initialized && runtime.IsInitialized()) {
            image_host.Shutdown(runtime.Context());
            image_host_initialized = false;
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


