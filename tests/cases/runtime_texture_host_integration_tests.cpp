#include "support/test_framework.hpp"
#include "vr/asset/texture_host.hpp"
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

VR_TEST_CASE(RuntimeIntegration_texture_host_upload_cube_and_remove,
             "integration;gpu;sdl;runtime;texture") {
    Runtime runtime{};
    bool runtime_initialized = false;

    constexpr std::uint32_t face_width = 4U;
    constexpr std::uint32_t face_height = 4U;
    constexpr std::uint32_t face_texel_count = face_width * face_height;
    constexpr std::uint32_t channel_count = 4U;
    constexpr std::uint32_t bytes_per_channel = 2U;
    constexpr std::uint32_t face_byte_count =
        face_texel_count * channel_count * bytes_per_channel;

    std::array<std::array<std::uint16_t, face_texel_count * channel_count>, 6U> cube_faces{};
    for (std::uint32_t face_index = 0U; face_index < cube_faces.size(); ++face_index) {
        auto& face = cube_faces[face_index];
        for (std::uint32_t texel_index = 0U; texel_index < face_texel_count; ++texel_index) {
            const std::uint32_t base = texel_index * channel_count;
            face[base + 0U] = static_cast<std::uint16_t>(0x2000U + face_index * 0x0800U);
            face[base + 1U] = static_cast<std::uint16_t>(0x1800U + texel_index * 0x0010U);
            face[base + 2U] = static_cast<std::uint16_t>(0x1000U + face_index * 0x0040U);
            face[base + 3U] = static_cast<std::uint16_t>(0x3C00U);
        }
    }

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_texture_host";
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

        VR_REQUIRE(runtime.HasTextureHost());
        VR_CHECK(vr::asset::TextureHost::SupportsHdrEnvironmentFormat(runtime.Context(),
                                                                      VK_FORMAT_R16G16B16A16_SFLOAT));
        const VkFormat hdr_environment_format =
            vr::asset::TextureHost::ResolveHdrEnvironmentFormat(runtime.Context(), true);
        VR_CHECK(hdr_environment_format == VK_FORMAT_BC6H_UFLOAT_BLOCK ||
                 hdr_environment_format == VK_FORMAT_R16G16B16A16_SFLOAT ||
                 hdr_environment_format == VK_FORMAT_BC6H_SFLOAT_BLOCK);

        vr::asset::TextureCreateInfo texture_create_info{};
        texture_create_info.texture_id = vr::asset::TextureId{1U};
        texture_create_info.image_flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        texture_create_info.default_view_type = VK_IMAGE_VIEW_TYPE_CUBE;
        texture_create_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        texture_create_info.extent = VkExtent3D{face_width, face_height, 1U};
        texture_create_info.mip_levels = 1U;
        texture_create_info.array_layers = 6U;
        texture_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        texture_create_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        texture_create_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;

        std::array<vr::asset::TextureSubresourceUploadInfo, 6U> subresources{};
        for (std::uint32_t face_index = 0U; face_index < subresources.size(); ++face_index) {
            subresources[face_index].pixels = cube_faces[face_index].data();
            subresources[face_index].size_bytes = face_byte_count;
            subresources[face_index].mip_level = 0U;
            subresources[face_index].base_array_layer = face_index;
            subresources[face_index].layer_count = 1U;
            subresources[face_index].image_extent = VkExtent3D{face_width, face_height, 1U};
        }

        vr::asset::TextureUploadInfo upload_info{};
        upload_info.create = texture_create_info;
        upload_info.subresources = subresources.data();
        upload_info.subresource_count = static_cast<std::uint32_t>(subresources.size());

        runtime.Upload().BeginFrame(runtime.Context(), 0U);
        runtime.Texture().UploadTexture(runtime.Context(),
                                        runtime.Upload(),
                                        0U,
                                        0U,
                                        0U,
                                        upload_info);
        const auto upload_end = runtime.Upload().EndFrameAndSubmit(runtime.Context(), 0U);
        if (upload_end.submitted) {
            runtime.Upload().WaitFrame(runtime.Context(), 0U);
        }
        runtime.Texture().BeginFrame(runtime.Context(), 0U);

        const vr::asset::TextureHost::TextureRecord* record =
            runtime.Texture().FindTexture(vr::asset::TextureId{1U});
        VR_REQUIRE(record != nullptr);
        VR_CHECK(record->resource.image != VK_NULL_HANDLE);
        VR_CHECK(record->resource.default_view != VK_NULL_HANDLE);
        VR_CHECK(record->default_view_type == VK_IMAGE_VIEW_TYPE_CUBE);
        VR_CHECK(record->array_layers == 6U);
        VR_CHECK(record->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        const vr::asset::TextureHostStats stats_after_upload = runtime.Texture().Stats();
        VR_CHECK(stats_after_upload.uploaded_texture_count > 0U);
        VR_CHECK(stats_after_upload.uploaded_bytes >= static_cast<std::uint64_t>(face_byte_count) * 6U);
        VR_CHECK(stats_after_upload.barrier_count >= 2U);

        VR_REQUIRE(runtime.Texture().RemoveTexture(runtime.Context(),
                                                   vr::asset::TextureId{1U},
                                                   0U,
                                                   0U));
        runtime.Texture().BeginFrame(runtime.Context(), 0U);
        VR_CHECK(runtime.Texture().FindTexture(vr::asset::TextureId{1U}) == nullptr);

        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
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
