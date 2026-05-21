#include "support/test_framework.hpp"
#include "support/render_graph_test_utils.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_3d.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
constexpr vr::asset::TextureId kSkySmokeCubemapTextureId{8101U};
constexpr vr::asset::TextureId kSkySmokeEquirectTextureId{8102U};

enum class SkySmokeScenario : std::uint8_t {
    solid_color = 0U,
    cubemap = 1U,
    hdri = 2U,
    procedural_atmosphere = 3U,
    cubemap_lazy_bake = 4U,
    hdri_lazy_bake = 5U,
};

struct SkySmokeResult final {
    std::uint32_t submitted_frames = 0U;
    vr::render::SkyEnvironmentPassStats pass_stats{};
    vr::render::SceneRecorder3DStats recorder_stats{};
    vr::render_graph::RenderGraphRecordStats graph_record_stats{};
    vr::render::IblHostStats ibl_stats{};
    vr::render::IblBakeHostStats ibl_bake_stats{};
    vr::render::SkyEnvironmentGpuHostStats sky_host_stats{};
    vr::asset::TextureId active_ibl_specular_texture{};
    vr::asset::TextureId active_ibl_skybox_texture{};
    bool graph_only_record_active = false;
};

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


[[nodiscard]] VkFormat SelectSkyTextureFormat(vr::VulkanContext& context_) {
    if (vr::asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if (vr::asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R8G8B8A8_UNORM)) {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

class SkyEnvironmentSmokeRecorder final {
public:
    explicit SkyEnvironmentSmokeRecorder(SkySmokeScenario scenario_) : scenario(scenario_) {}

    void Initialize() {
        recorder.Initialize({});

        TransformSystem3D::Initialize(camera_transform);
        TransformSystem3D::SetLocalPosition(camera_transform,
                                            vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
        TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);

        CameraSystem3D::Initialize(camera);
        CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
        CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);

        environment_state = {};
        environment_state.tint = vr::ecs::Float4{.x = 1.0F, .y = 1.0F, .z = 1.0F, .w = 1.0F};
        environment_state.zenith_color =
            vr::ecs::Float4{.x = 0.14F, .y = 0.22F, .z = 0.40F, .w = 1.0F};
        environment_state.horizon_color =
            vr::ecs::Float4{.x = 0.55F, .y = 0.62F, .z = 0.75F, .w = 1.0F};
        environment_state.ground_color =
            vr::ecs::Float4{.x = 0.04F, .y = 0.05F, .z = 0.06F, .w = 1.0F};
        environment_state.exposure = 1.0F;
        environment_state.sky_intensity = 1.0F;
        environment_state.diffuse_ibl_intensity = 1.0F;
        environment_state.specular_ibl_intensity = 1.0F;
        environment_state.rotation_y = 0.35F;
        environment_state.max_specular_lod = 0.0F;
        environment_state.sun_elevation = 0.45F;
        environment_state.sun_azimuth = -0.8F;
        environment_state.atmosphere_density = 1.35F;
        environment_state.mie_scattering = 2.0F;
        environment_state.rayleigh_scattering = 1.4F;
        environment_state.flags = 0U;
        environment_state.revision = 1U;

        switch (scenario) {
        case SkySmokeScenario::solid_color:
            environment_state.mode = vr::scene::SkyEnvironmentMode::solid_color;
            environment_state.zenith_color =
                vr::ecs::Float4{.x = 0.09F, .y = 0.18F, .z = 0.33F, .w = 1.0F};
            environment_state.horizon_color = environment_state.zenith_color;
            break;
        case SkySmokeScenario::cubemap:
        case SkySmokeScenario::cubemap_lazy_bake:
            environment_state.mode = vr::scene::SkyEnvironmentMode::cubemap;
            environment_state.sky_texture_id = kSkySmokeCubemapTextureId.value;
            break;
        case SkySmokeScenario::hdri:
        case SkySmokeScenario::hdri_lazy_bake:
            environment_state.mode = vr::scene::SkyEnvironmentMode::equirectangular_hdr;
            environment_state.sky_texture_id = kSkySmokeEquirectTextureId.value;
            break;
        case SkySmokeScenario::procedural_atmosphere:
            environment_state.mode = vr::scene::SkyEnvironmentMode::procedural_atmosphere;
            environment_state.zenith_color =
                vr::ecs::Float4{.x = 0.07F, .y = 0.18F, .z = 0.42F, .w = 1.0F};
            environment_state.horizon_color =
                vr::ecs::Float4{.x = 0.76F, .y = 0.52F, .z = 0.26F, .w = 1.0F};
            environment_state.ground_color =
                vr::ecs::Float4{.x = 0.10F, .y = 0.08F, .z = 0.07F, .w = 1.0F};
            environment_state.exposure = 1.15F;
            environment_state.sky_intensity = 1.10F;
            break;
        default:
            throw std::runtime_error("SkyEnvironmentSmokeRecorder encountered an unsupported scenario");
        }
    }

    template<typename RuntimeT>
    void BindRuntime(RuntimeT& runtime_) noexcept {
        recorder.BindRuntime(runtime_);
    }

    void PrepareFrame(const vr::render::SceneRecorder3DPrepareView& prepare_view_) {
        EnsureScenarioAssetsUploaded(prepare_view_);

        vr::render::RefreshExtentBoundWorldSceneSubmission(view,
                                                           packet,
                                                           camera,
                                                           camera_transform,
                                                           prepare_view_.frame.swapchain_extent,
                                                           ++submission_id,
                                                           0U,
                                                           0U);
        packet.extra.environment = environment_state;
        packet.extra.environment_gpu = {};
        packet.extra.ibl_environment_id = 0U;
        vr::render::RefreshRenderScenePacketSignature(packet);

        recorder.PrepareFrame(prepare_view_, packet);
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) {
        recorder.OnSwapchainRecreated(image_count_,
                                      extent_,
                                      format_,
                                      last_submitted_value_,
                                      completed_submit_value_);
    }

    [[nodiscard]] vr::render::SceneRecorder3D& Recorder() noexcept {
        return recorder;
    }

private:
    void EnsureScenarioAssetsUploaded(const vr::render::SceneRecorder3DPrepareView& prepare_view_) {
        if (assets_uploaded ||
            scenario == SkySmokeScenario::solid_color) {
            return;
        }
        if (prepare_view_.texture == nullptr || prepare_view_.upload == nullptr) {
            throw std::runtime_error("SkyEnvironmentSmokeRecorder requires TextureHost and UploadHost");
        }

        const VkFormat texture_format = SelectSkyTextureFormat(prepare_view_.device);
        if (texture_format == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("SkyEnvironmentSmokeRecorder could not resolve a sampled sky texture format");
        }

        switch (scenario) {
        case SkySmokeScenario::cubemap:
        case SkySmokeScenario::cubemap_lazy_bake:
            UploadCubemap(prepare_view_, texture_format);
            break;
        case SkySmokeScenario::hdri:
        case SkySmokeScenario::hdri_lazy_bake:
            UploadEquirect(prepare_view_, texture_format);
            break;
        case SkySmokeScenario::procedural_atmosphere:
        case SkySmokeScenario::solid_color:
        default:
            break;
        }

        assets_uploaded = true;
    }

    void UploadCubemap(const vr::render::SceneRecorder3DPrepareView& prepare_view_,
                       VkFormat texture_format) {
        constexpr std::uint32_t cube_width = 2U;
        constexpr std::uint32_t cube_height = 2U;
        constexpr std::uint32_t cube_texel_count = cube_width * cube_height;
        constexpr std::uint32_t cube_channel_count = 4U;

        std::array<vr::asset::TextureSubresourceUploadInfo, 6U> cube_subresources{};
        vr::asset::TextureUploadInfo cube_upload{};
        cube_upload.create.texture_id = kSkySmokeCubemapTextureId;
        cube_upload.create.image_flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        cube_upload.create.default_view_type = VK_IMAGE_VIEW_TYPE_CUBE;
        cube_upload.create.format = texture_format;
        cube_upload.create.extent = VkExtent3D{cube_width, cube_height, 1U};
        cube_upload.create.mip_levels = 1U;
        cube_upload.create.array_layers = 6U;
        cube_upload.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        cube_upload.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cube_upload.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        cube_upload.create.retain_cpu_upload_data = true;

        if (texture_format == VK_FORMAT_R16G16B16A16_SFLOAT) {
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
                cube_subresources[face_index].pixels = face.data();
                cube_subresources[face_index].size_bytes = sizeof(face);
                cube_subresources[face_index].mip_level = 0U;
                cube_subresources[face_index].base_array_layer = face_index;
                cube_subresources[face_index].layer_count = 1U;
                cube_subresources[face_index].image_extent = VkExtent3D{cube_width, cube_height, 1U};
            }
            cube_upload.subresources = cube_subresources.data();
            cube_upload.subresource_count = static_cast<std::uint32_t>(cube_subresources.size());
            prepare_view_.texture->UploadTexture(prepare_view_.device,
                                                 *prepare_view_.upload,
                                                 prepare_view_.frame.frame_index,
                                                 prepare_view_.progress.last_submitted_value,
                                                 prepare_view_.progress.completed_submit_value,
                                                 cube_upload);
            return;
        }

        {
            std::array<std::array<std::uint8_t, cube_texel_count * cube_channel_count>, 6U> cube_faces{};
            for (std::uint32_t face_index = 0U; face_index < cube_faces.size(); ++face_index) {
                auto& face = cube_faces[face_index];
                for (std::uint32_t texel_index = 0U; texel_index < cube_texel_count; ++texel_index) {
                    const std::uint32_t base = texel_index * cube_channel_count;
                    face[base + 0U] = static_cast<std::uint8_t>(64U + face_index * 24U);
                    face[base + 1U] = static_cast<std::uint8_t>(96U + texel_index * 18U);
                    face[base + 2U] = static_cast<std::uint8_t>(128U + face_index * 8U);
                    face[base + 3U] = 255U;
                }
                cube_subresources[face_index].pixels = face.data();
                cube_subresources[face_index].size_bytes = sizeof(face);
                cube_subresources[face_index].mip_level = 0U;
                cube_subresources[face_index].base_array_layer = face_index;
                cube_subresources[face_index].layer_count = 1U;
                cube_subresources[face_index].image_extent = VkExtent3D{cube_width, cube_height, 1U};
            }
            cube_upload.subresources = cube_subresources.data();
            cube_upload.subresource_count = static_cast<std::uint32_t>(cube_subresources.size());
            prepare_view_.texture->UploadTexture(prepare_view_.device,
                                                 *prepare_view_.upload,
                                                 prepare_view_.frame.frame_index,
                                                 prepare_view_.progress.last_submitted_value,
                                                 prepare_view_.progress.completed_submit_value,
                                                 cube_upload);
        }
    }

    void UploadEquirect(const vr::render::SceneRecorder3DPrepareView& prepare_view_,
                        VkFormat texture_format) {
        constexpr std::uint32_t width = 4U;
        constexpr std::uint32_t height = 2U;
        constexpr std::uint32_t texel_count = width * height;
        constexpr std::uint32_t channel_count = 4U;

        vr::asset::TextureSubresourceUploadInfo subresource{};
        subresource.mip_level = 0U;
        subresource.base_array_layer = 0U;
        subresource.layer_count = 1U;
        subresource.image_extent = VkExtent3D{width, height, 1U};

        vr::asset::TextureUploadInfo upload{};
        upload.create.texture_id = kSkySmokeEquirectTextureId;
        upload.create.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
        upload.create.format = texture_format;
        upload.create.extent = VkExtent3D{width, height, 1U};
        upload.create.mip_levels = 1U;
        upload.create.array_layers = 1U;
        upload.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        upload.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        upload.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        upload.create.retain_cpu_upload_data = true;

        if (texture_format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            std::array<std::uint16_t, texel_count * channel_count> pixels{};
            for (std::uint32_t y = 0U; y < height; ++y) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::uint32_t texel_index = y * width + x;
                    const std::uint32_t base = texel_index * channel_count;
                    pixels[base + 0U] = static_cast<std::uint16_t>(0x1800U + x * 0x0600U);
                    pixels[base + 1U] = static_cast<std::uint16_t>(0x1000U + y * 0x0800U);
                    pixels[base + 2U] = static_cast<std::uint16_t>(0x1400U + (x + y) * 0x0200U);
                    pixels[base + 3U] = 0x3C00U;
                }
            }
            subresource.pixels = pixels.data();
            subresource.size_bytes = sizeof(pixels);
            upload.subresources = &subresource;
            upload.subresource_count = 1U;
            prepare_view_.texture->UploadTexture(prepare_view_.device,
                                                 *prepare_view_.upload,
                                                 prepare_view_.frame.frame_index,
                                                 prepare_view_.progress.last_submitted_value,
                                                 prepare_view_.progress.completed_submit_value,
                                                 upload);
            return;
        }

        std::array<std::uint8_t, texel_count * channel_count> pixels{};
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::uint32_t texel_index = y * width + x;
                const std::uint32_t base = texel_index * channel_count;
                pixels[base + 0U] = static_cast<std::uint8_t>(48U + x * 40U);
                pixels[base + 1U] = static_cast<std::uint8_t>(64U + y * 72U);
                pixels[base + 2U] = static_cast<std::uint8_t>(96U + (x + y) * 24U);
                pixels[base + 3U] = 255U;
            }
        }
        subresource.pixels = pixels.data();
        subresource.size_bytes = sizeof(pixels);
        upload.subresources = &subresource;
        upload.subresource_count = 1U;

        prepare_view_.texture->UploadTexture(prepare_view_.device,
                                             *prepare_view_.upload,
                                             prepare_view_.frame.frame_index,
                                             prepare_view_.progress.last_submitted_value,
                                             prepare_view_.progress.completed_submit_value,
                                             upload);
    }

private:
    SkySmokeScenario scenario = SkySmokeScenario::solid_color;
    vr::render::SceneRecorder3D recorder{};
    vr::render::RenderView3D view{};
    vr::render::RenderScenePacket3D packet{};
    Camera3D camera{};
    Transform3D camera_transform{};
    vr::scene::SkyEnvironmentRenderState environment_state{};
    std::uint64_t submission_id = 0U;
    bool assets_uploaded = false;
};

[[nodiscard]] SkySmokeResult RunSkyEnvironmentSmokeTest(SkySmokeScenario scenario_,
                                                        std::string_view window_title_) {
    Runtime runtime{};
    SkyEnvironmentSmokeRecorder recorder{scenario_};
    bool runtime_initialized = false;
    bool recorder_initialized = false;
    const std::string window_title{window_title_};

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = window_title.c_str();
        create_info.platform.window.width = 320;
        create_info.platform.window.height = 200;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.modules.enable_ibl_bake_host =
            scenario_ == SkySmokeScenario::cubemap_lazy_bake ||
            scenario_ == SkySmokeScenario::hdri_lazy_bake;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        recorder.Initialize();
        recorder_initialized = true;
        recorder.BindRuntime(runtime);

        if (!runtime.HasTextureHost() ||
            !runtime.HasUploadHost() ||
            !runtime.HasIblHost() ||
            !runtime.HasSkyEnvironmentHost()) {
            throw std::runtime_error("Sky environment smoke test runtime is missing required hosts");
        }

        SkySmokeResult result{};
        for (std::uint32_t tick_index = 0U; tick_index < 4U; ++tick_index) {
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++result.submitted_frames;
            }
            SDL_Delay(1U);
        }

        result.pass_stats = recorder.Recorder().EnvironmentPass().Stats();
        result.recorder_stats = recorder.Recorder().Stats();
        if (const auto* service =
                runtime.Services().TryGet<vr::runtime::services::RenderGraphRuntimeService>();
            service != nullptr) {
            result.graph_record_stats = service->LastRecordStats();
        }
        result.graph_only_record_active = vr::test::IsGraphOnlyScene3DRecordActive(runtime);
        result.ibl_stats = runtime.Ibl().Stats();
        if (runtime.HasIblBakeHost()) {
            result.ibl_bake_stats = runtime.IblBake().Stats();
        }
        result.sky_host_stats = runtime.SkyEnvironment().Stats();
        result.active_ibl_specular_texture = runtime.Ibl().ActiveSpecularTexture();
        result.active_ibl_skybox_texture = runtime.Ibl().ActiveSkyboxTexture();

        recorder.Recorder().Shutdown(runtime.Context());
        recorder_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
        return result;
    } catch (...) {
        if (recorder_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.Recorder().Shutdown(runtime.Context());
            recorder_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_sky_environment_3d_solid_color_smoke,
             "integration;gpu;sdl;runtime;scene3d;sky_environment") {
    try {
        const SkySmokeResult result =
            RunSkyEnvironmentSmokeTest(SkySmokeScenario::solid_color,
                                       "vr_tests_runtime_sky_environment_solid");

        VR_REQUIRE(result.submitted_frames > 0U);
        VR_CHECK(result.pass_stats.draw_call_count > 0U);
        VR_CHECK(result.pass_stats.draw_call_count == result.submitted_frames);
        VR_CHECK(result.pass_stats.descriptor_set_bind_count == 0U);
        VR_CHECK(result.pass_stats.skipped_draw_count == 0U);
        VR_CHECK(result.recorder_stats.environment_prepare_count > 0U);
        VR_CHECK(result.graph_only_record_active
                     ? (result.recorder_stats.environment_record_count == 0U)
                     : (result.recorder_stats.environment_record_count > 0U));
        VR_CHECK(result.ibl_stats.prepared_frame_count == 0U);
        VR_CHECK(result.ibl_stats.environment_count > 0U);
        VR_CHECK(result.sky_host_stats.environment_count == 1U);
        VR_CHECK(result.sky_host_stats.ibl_register_count > 0U);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_sky_environment_3d_cubemap_smoke,
             "integration;gpu;sdl;runtime;scene3d;sky_environment;ibl") {
    try {
        const SkySmokeResult result =
            RunSkyEnvironmentSmokeTest(SkySmokeScenario::cubemap,
                                       "vr_tests_runtime_sky_environment_cubemap");

        VR_REQUIRE(result.submitted_frames > 0U);
        VR_CHECK(result.pass_stats.draw_call_count > 0U);
        VR_CHECK(result.pass_stats.draw_call_count == result.submitted_frames);
        VR_CHECK(result.pass_stats.descriptor_set_bind_count > 0U);
        VR_CHECK(result.pass_stats.skipped_draw_count == 0U);
        VR_CHECK(result.recorder_stats.environment_prepare_count > 0U);
        VR_CHECK(result.graph_only_record_active
                     ? (result.recorder_stats.environment_record_count == 0U)
                     : (result.recorder_stats.environment_record_count > 0U));
        VR_CHECK(result.ibl_stats.prepared_frame_count > 0U);
        VR_CHECK(result.ibl_stats.environment_count > 0U);
        VR_CHECK(result.ibl_stats.descriptor_update_count <= result.submitted_frames);
        VR_CHECK(result.sky_host_stats.environment_count == 1U);
        VR_CHECK(result.sky_host_stats.ibl_register_count > 0U);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_sky_environment_3d_hdri_smoke,
             "integration;gpu;sdl;runtime;scene3d;sky_environment;hdri") {
    try {
        const SkySmokeResult result =
            RunSkyEnvironmentSmokeTest(SkySmokeScenario::hdri,
                                       "vr_tests_runtime_sky_environment_hdri");

        VR_REQUIRE(result.submitted_frames > 0U);
        VR_CHECK(result.pass_stats.draw_call_count > 0U);
        VR_CHECK(result.pass_stats.draw_call_count == result.submitted_frames);
        VR_CHECK(result.pass_stats.descriptor_set_bind_count > 0U);
        VR_CHECK(result.pass_stats.skipped_draw_count == 0U);
        VR_CHECK(result.recorder_stats.environment_prepare_count > 0U);
        VR_CHECK(result.graph_only_record_active
                     ? (result.recorder_stats.environment_record_count == 0U)
                     : (result.recorder_stats.environment_record_count > 0U));
        VR_CHECK(result.ibl_stats.prepared_frame_count == 0U);
        VR_CHECK(result.sky_host_stats.environment_count == 1U);
        VR_CHECK(result.sky_host_stats.ibl_register_count == 0U);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_sky_environment_3d_procedural_atmosphere_smoke,
             "integration;gpu;sdl;runtime;scene3d;sky_environment;atmosphere") {
    try {
        const SkySmokeResult result =
            RunSkyEnvironmentSmokeTest(SkySmokeScenario::procedural_atmosphere,
                                       "vr_tests_runtime_sky_environment_procedural_atmosphere");

        VR_REQUIRE(result.submitted_frames > 0U);
        VR_CHECK(result.pass_stats.draw_call_count > 0U);
        VR_CHECK(result.pass_stats.draw_call_count == result.submitted_frames);
        VR_CHECK(result.pass_stats.descriptor_set_bind_count == 0U);
        VR_CHECK(result.pass_stats.skipped_draw_count == 0U);
        VR_CHECK(result.recorder_stats.environment_prepare_count > 0U);
        VR_CHECK(result.graph_only_record_active
                     ? (result.recorder_stats.environment_record_count == 0U)
                     : (result.recorder_stats.environment_record_count > 0U));
        VR_CHECK(result.ibl_stats.environment_count > 0U);
        VR_CHECK(result.sky_host_stats.environment_count == 1U);
        VR_CHECK(result.sky_host_stats.ibl_register_count > 0U);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_sky_environment_3d_cubemap_lazy_bake_ibl_smoke,
             "integration;gpu;sdl;runtime;scene3d;sky_environment;ibl;bake") {
    try {
        const SkySmokeResult result =
            RunSkyEnvironmentSmokeTest(SkySmokeScenario::cubemap_lazy_bake,
                                       "vr_tests_runtime_sky_environment_cubemap_lazy_bake");

        VR_REQUIRE(result.submitted_frames > 0U);
        VR_CHECK(result.pass_stats.draw_call_count > 0U);
        VR_CHECK(result.pass_stats.draw_call_count == result.submitted_frames);
        VR_CHECK(result.pass_stats.descriptor_set_bind_count > 0U);
        VR_CHECK(result.pass_stats.skipped_draw_count == 0U);
        VR_CHECK(result.ibl_stats.environment_count > 0U);
        VR_CHECK(result.ibl_stats.descriptor_update_count <= result.submitted_frames + 2U);
        VR_CHECK(result.ibl_bake_stats.baked_environment_count > 0U);
        VR_CHECK(result.sky_host_stats.bake_apply_count > 0U);
        VR_CHECK(result.sky_host_stats.ibl_register_count > 0U);
        VR_CHECK(result.active_ibl_specular_texture.IsValid());
        VR_CHECK(result.active_ibl_skybox_texture.IsValid());
        VR_CHECK(result.active_ibl_specular_texture.value != kSkySmokeCubemapTextureId.value);
        VR_CHECK(result.active_ibl_skybox_texture.value == kSkySmokeCubemapTextureId.value);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(RuntimeIntegration_sky_environment_3d_hdri_lazy_bake_ibl_smoke,
             "integration;gpu;sdl;runtime;scene3d;sky_environment;hdri;ibl;bake") {
    try {
        const SkySmokeResult result =
            RunSkyEnvironmentSmokeTest(SkySmokeScenario::hdri_lazy_bake,
                                       "vr_tests_runtime_sky_environment_hdri_lazy_bake");

        VR_REQUIRE(result.submitted_frames > 0U);
        VR_CHECK(result.pass_stats.draw_call_count > 0U);
        VR_CHECK(result.pass_stats.draw_call_count == result.submitted_frames);
        VR_CHECK(result.pass_stats.descriptor_set_bind_count > 0U);
        VR_CHECK(result.pass_stats.skipped_draw_count == 0U);
        VR_CHECK(result.ibl_stats.environment_count > 0U);
        VR_CHECK(result.ibl_bake_stats.baked_environment_count > 0U);
        VR_CHECK(result.sky_host_stats.bake_apply_count > 0U);
        VR_CHECK(result.sky_host_stats.ibl_register_count > 0U);
        VR_CHECK(result.ibl_stats.prepared_frame_count == 0U);
    } catch (const std::exception& exception_) {
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace

