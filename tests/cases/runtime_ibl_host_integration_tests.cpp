#include "support/test_framework.hpp"
#include "vr/render/render_runtime_host.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <stdexcept>
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
    constexpr std::array<std::string_view, 14U> patterns{
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
        "synchronization2",
        "dynamicrendering"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] VkFormat ResolveBrdfLutFormat(const vr::VulkanContext& context_) {
    if (vr::asset::TextureHost::SupportsSampledFormat(const_cast<vr::VulkanContext&>(context_),
                                                      VK_FORMAT_R16G16_SFLOAT)) {
        return VK_FORMAT_R16G16_SFLOAT;
    }
    if (vr::asset::TextureHost::SupportsSampledFormat(const_cast<vr::VulkanContext&>(context_),
                                                      VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if (vr::asset::TextureHost::SupportsSampledFormat(const_cast<vr::VulkanContext&>(context_),
                                                      VK_FORMAT_R8G8B8A8_UNORM)) {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

class IblPrepareRecorder final {
public:
    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        if (prepare_context_.ibl_host == nullptr ||
            prepare_context_.texture_host == nullptr ||
            prepare_context_.upload_host == nullptr ||
            prepare_context_.context == nullptr) {
            throw std::runtime_error("IblPrepareRecorder requires valid IBL/Texture/Upload/Vulkan context pointers");
        }

        if (prepare_count == 0U) {
            prepare_context_.ibl_host->PrepareFrame(prepare_context_);
            fallback_descriptor_set = prepare_context_.ibl_host->ActiveDescriptorSet(prepare_context_.frame_index);
            fallback_brdf_lut = prepare_context_.ibl_host->BrdfLut();
            fallback_specular_texture = prepare_context_.ibl_host->ActiveSpecularTexture();
        } else if (!environment_registered) {
            UploadTextures(prepare_context_);
            prepare_context_.ibl_host->SetBrdfLut(brdf_lut_texture_id);

            vr::render::IblEnvironmentAssetDesc environment_desc{};
            environment_desc.specular_cube = specular_cube_texture_id;
            environment_desc.intensity = 2.5F;
            environment_desc.rotation_y_radians = 0.5F;
            environment_desc.tint_color = {0.75F, 0.80F, 0.90F};
            environment_desc.sh9[0] = {0.1F, 0.2F, 0.3F, 0.0F};
            environment_desc.sh9[1] = {0.4F, 0.5F, 0.6F, 0.0F};

            environment_id = prepare_context_.ibl_host->RegisterEnvironment(*prepare_context_.context,
                                                                             environment_desc);
            prepare_context_.ibl_host->SetActiveEnvironment(environment_id);
            environment_registered = true;
        }

        if (environment_registered) {
            prepare_context_.ibl_host->PrepareFrame(prepare_context_);
            active_descriptor_set = prepare_context_.ibl_host->ActiveDescriptorSet(prepare_context_.frame_index);
            active_params = prepare_context_.ibl_host->ActiveParams();
            active_brdf_lut = prepare_context_.ibl_host->BrdfLut();
            active_specular_texture = prepare_context_.ibl_host->ActiveSpecularTexture();
            active_skybox_texture = prepare_context_.ibl_host->ActiveSkyboxTexture();
        }

        ++prepare_count;
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        (void)record_context_;
    }

    [[nodiscard]] bool HasFallbackCapture() const noexcept {
        return fallback_descriptor_set != VK_NULL_HANDLE &&
               fallback_brdf_lut.IsValid() &&
               fallback_specular_texture.IsValid();
    }

    [[nodiscard]] bool HasActiveCapture() const noexcept {
        return environment_registered &&
               active_descriptor_set != VK_NULL_HANDLE &&
               active_brdf_lut.IsValid() &&
               active_specular_texture.IsValid();
    }

    std::uint32_t prepare_count = 0U;
    bool environment_registered = false;
    vr::render::IblEnvironmentId environment_id{};
    vr::asset::TextureId fallback_brdf_lut{};
    vr::asset::TextureId fallback_specular_texture{};
    VkDescriptorSet fallback_descriptor_set = VK_NULL_HANDLE;
    vr::asset::TextureId active_brdf_lut{};
    vr::asset::TextureId active_specular_texture{};
    vr::asset::TextureId active_skybox_texture{};
    VkDescriptorSet active_descriptor_set = VK_NULL_HANDLE;
    vr::render::IblGpuParams active_params{};

private:
    void UploadTextures(const vr::render::RuntimePrepareContext& prepare_context_) {
        constexpr std::uint32_t cube_width = 2U;
        constexpr std::uint32_t cube_height = 2U;
        constexpr std::uint32_t cube_texel_count = cube_width * cube_height;
        constexpr std::uint32_t cube_channel_count = 4U;
        constexpr std::uint32_t cube_face_byte_count =
            cube_texel_count * cube_channel_count * sizeof(std::uint16_t);

        std::array<std::array<std::uint16_t, cube_texel_count * cube_channel_count>, 6U> cube_faces{};
        for (std::uint32_t face_index = 0U; face_index < cube_faces.size(); ++face_index) {
            auto& face = cube_faces[face_index];
            for (std::uint32_t texel_index = 0U; texel_index < cube_texel_count; ++texel_index) {
                const std::uint32_t base = texel_index * cube_channel_count;
                face[base + 0U] = static_cast<std::uint16_t>(0x2000U + face_index * 0x0400U);
                face[base + 1U] = static_cast<std::uint16_t>(0x1800U + texel_index * 0x0100U);
                face[base + 2U] = static_cast<std::uint16_t>(0x1000U + face_index * 0x0020U);
                face[base + 3U] = 0x3C00U;
            }
        }

        std::array<vr::asset::TextureSubresourceUploadInfo, 6U> cube_subresources{};
        for (std::uint32_t face_index = 0U; face_index < cube_subresources.size(); ++face_index) {
            cube_subresources[face_index].pixels = cube_faces[face_index].data();
            cube_subresources[face_index].size_bytes = cube_face_byte_count;
            cube_subresources[face_index].mip_level = 0U;
            cube_subresources[face_index].base_array_layer = face_index;
            cube_subresources[face_index].layer_count = 1U;
            cube_subresources[face_index].image_extent = VkExtent3D{cube_width, cube_height, 1U};
        }

        vr::asset::TextureUploadInfo cube_upload{};
        cube_upload.create.texture_id = specular_cube_texture_id;
        cube_upload.create.image_flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        cube_upload.create.default_view_type = VK_IMAGE_VIEW_TYPE_CUBE;
        cube_upload.create.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        cube_upload.create.extent = VkExtent3D{cube_width, cube_height, 1U};
        cube_upload.create.mip_levels = 1U;
        cube_upload.create.array_layers = 6U;
        cube_upload.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        cube_upload.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cube_upload.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        cube_upload.subresources = cube_subresources.data();
        cube_upload.subresource_count = static_cast<std::uint32_t>(cube_subresources.size());

        prepare_context_.texture_host->UploadTexture(*prepare_context_.context,
                                                     *prepare_context_.upload_host,
                                                     prepare_context_.frame_index,
                                                     prepare_context_.last_submitted_value,
                                                     prepare_context_.completed_submit_value,
                                                     cube_upload);

        const VkFormat brdf_lut_format = ResolveBrdfLutFormat(*prepare_context_.context);
        if (brdf_lut_format == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("IblPrepareRecorder could not resolve a sampled BRDF LUT format");
        }

        std::array<std::uint16_t, 2U> rg16_pixel{0x3400U, 0x3800U};
        std::array<std::uint16_t, 4U> rgba16_pixel{0x3400U, 0x3800U, 0U, 0x3C00U};
        std::array<std::uint8_t, 4U> rgba8_pixel{64U, 96U, 0U, 255U};

        vr::asset::TextureSubresourceUploadInfo brdf_subresource{};
        brdf_subresource.mip_level = 0U;
        brdf_subresource.base_array_layer = 0U;
        brdf_subresource.layer_count = 1U;
        brdf_subresource.image_extent = VkExtent3D{1U, 1U, 1U};
        switch (brdf_lut_format) {
        case VK_FORMAT_R16G16_SFLOAT:
            brdf_subresource.pixels = rg16_pixel.data();
            brdf_subresource.size_bytes = sizeof(rg16_pixel);
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            brdf_subresource.pixels = rgba16_pixel.data();
            brdf_subresource.size_bytes = sizeof(rgba16_pixel);
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
            brdf_subresource.pixels = rgba8_pixel.data();
            brdf_subresource.size_bytes = sizeof(rgba8_pixel);
            break;
        default:
            throw std::runtime_error("IblPrepareRecorder encountered an unsupported BRDF LUT fallback format");
        }

        vr::asset::TextureUploadInfo brdf_upload{};
        brdf_upload.create.texture_id = brdf_lut_texture_id;
        brdf_upload.create.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        brdf_upload.create.format = brdf_lut_format;
        brdf_upload.create.extent = VkExtent3D{1U, 1U, 1U};
        brdf_upload.create.mip_levels = 1U;
        brdf_upload.create.array_layers = 1U;
        brdf_upload.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        brdf_upload.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        brdf_upload.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        brdf_upload.subresources = &brdf_subresource;
        brdf_upload.subresource_count = 1U;

        prepare_context_.texture_host->UploadTexture(*prepare_context_.context,
                                                     *prepare_context_.upload_host,
                                                     prepare_context_.frame_index,
                                                     prepare_context_.last_submitted_value,
                                                     prepare_context_.completed_submit_value,
                                                     brdf_upload);
    }

    static constexpr vr::asset::TextureId specular_cube_texture_id{7001U};
    static constexpr vr::asset::TextureId brdf_lut_texture_id{7002U};
};

VR_TEST_CASE(RuntimeIntegration_ibl_host_prepares_default_and_explicit_environment,
             "integration;gpu;sdl;runtime;ibl") {
    Runtime runtime{};
    IblPrepareRecorder recorder{};
    bool runtime_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_ibl_host";
        create_info.platform.window.width = 320;
        create_info.platform.window.height = 240;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        VR_REQUIRE(runtime.HasTextureHost());
        VR_REQUIRE(runtime.HasIblHost());
        VR_CHECK(runtime.Ibl().DescriptorLayoutId().IsValid());

        const auto first_tick = runtime.Tick(recorder);
        VR_CHECK(first_tick.running);
        VR_CHECK(recorder.HasFallbackCapture());
        VR_CHECK(!runtime.Ibl().ActiveEnvironment().IsValid());
        VR_CHECK(runtime.Ibl().ActiveParams().tint_intensity[3] == 0.0F);
        VR_CHECK(runtime.Ibl().Stats().default_texture_build_count >= 2U);

        const auto second_tick = runtime.Tick(recorder);
        VR_CHECK(second_tick.running);
        VR_REQUIRE(recorder.environment_id.IsValid());
        VR_CHECK(recorder.HasActiveCapture());
        VR_CHECK(runtime.Ibl().ActiveEnvironment().value == recorder.environment_id.value);
        VR_CHECK(runtime.Ibl().BrdfLut().value == recorder.active_brdf_lut.value);
        VR_CHECK(runtime.Ibl().ActiveSpecularTexture().value == recorder.active_specular_texture.value);
        VR_CHECK(runtime.Ibl().ActiveSkyboxTexture().value == recorder.active_skybox_texture.value);
        VR_CHECK(runtime.Ibl().Stats().environment_count == 1U);
        VR_CHECK(runtime.Ibl().Stats().prepared_frame_count >= 2U);
        VR_CHECK(runtime.Ibl().Stats().descriptor_update_count >= 2U);
        VR_CHECK(recorder.active_params.tint_intensity[0] == 0.75F);
        VR_CHECK(recorder.active_params.tint_intensity[1] == 0.80F);
        VR_CHECK(recorder.active_params.tint_intensity[2] == 0.90F);
        VR_CHECK(recorder.active_params.tint_intensity[3] == 2.5F);
        VR_CHECK(recorder.active_params.rotation_max_lod_flags[2] == 0.0F);
        VR_CHECK(recorder.active_params.rotation_max_lod_flags[3] == 0.0F);
        VR_CHECK(recorder.active_params.sh9[0][0] == 0.1F);
        VR_CHECK(recorder.active_params.sh9[1][2] == 0.6F);

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
