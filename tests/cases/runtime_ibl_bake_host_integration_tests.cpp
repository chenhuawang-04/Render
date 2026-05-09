#include "support/test_framework.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"

#include <array>
#include <cctype>
#include <cmath>
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

class IblBakeRecorder final {
public:
    IblBakeRecorder() {
        for (std::uint32_t y = 0U; y < kEquirectHeight; ++y) {
            const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(kEquirectHeight);
            for (std::uint32_t x = 0U; x < kEquirectWidth; ++x) {
                const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(kEquirectWidth);
                auto& pixel = equirect_pixels[static_cast<std::size_t>(y) * kEquirectWidth + x];
                pixel.x = 0.35F + 1.55F * u;
                pixel.y = 0.20F + 0.95F * (1.0F - v);
                pixel.z = 0.12F + 2.10F * (0.25F + 0.75F * u * v);
                pixel.w = 1.0F;
            }
        }
    }

    void PrepareFrame(const vr::render::SceneRecorder3DPrepareView& prepare_view_) {
        if (prepare_view_.ibl_bake == nullptr ||
            prepare_view_.ibl == nullptr ||
            prepare_view_.texture == nullptr ||
            prepare_view_.upload == nullptr) {
            throw std::runtime_error(
                "IblBakeRecorder requires IBL bake/runtime dependencies in SceneRecorder3DPrepareView");
        }

        if (!baked_once) {
            vr::render::IblBakeRequest request{};
            request.source.kind = vr::render::IblBakeSourceKind::equirectangular;
            request.source.equirect.pixels = equirect_pixels.data();
            request.source.equirect.width = kEquirectWidth;
            request.source.equirect.height = kEquirectHeight;
            request.skybox_cube_size = 8U;
            request.specular_cube_size = 8U;
            request.specular_sample_count = 64U;
            request.sh_sample_count = 512U;
            request.brdf_lut_size = 16U;
            request.brdf_sample_count = 128U;
            request.intensity = 1.75F;
            request.rotation_y_radians = 0.65F;
            request.tint_color = {0.85F, 0.90F, 1.05F};
            bake_result = prepare_view_.ibl_bake->BakeEnvironment(
                vr::render::MakeIblBakeHostPrepareView(prepare_view_),
                request);
            baked_once = true;
        }

        prepare_view_.ibl->PrepareEnvironmentFrame(vr::render::MakeIblHostPrepareView(prepare_view_),
                                                   bake_result.environment_id,
                                                   bake_result.brdf_lut);
        active_descriptor_set = prepare_view_.ibl->ActiveDescriptorSet(prepare_view_.frame.frame_index);
        active_params = prepare_view_.ibl->ActiveParams();
        active_brdf_lut = prepare_view_.ibl->BrdfLut();
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        (void)record_context_;
    }

    static constexpr std::uint32_t kEquirectWidth = 16U;
    static constexpr std::uint32_t kEquirectHeight = 8U;

    std::array<vr::ecs::Float4, static_cast<std::size_t>(kEquirectWidth) * kEquirectHeight> equirect_pixels{};
    bool baked_once = false;
    vr::render::IblBakeResult bake_result{};
    VkDescriptorSet active_descriptor_set = VK_NULL_HANDLE;
    vr::render::IblGpuParams active_params{};
    vr::asset::TextureId active_brdf_lut{};
};

VR_TEST_CASE(RuntimeIntegration_ibl_bake_host_bakes_environment_and_registers_runtime,
             "integration;gpu;sdl;runtime;ibl") {
    Runtime runtime{};
    IblBakeRecorder recorder{};
    bool runtime_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_ibl_bake_host";
        create_info.platform.window.width = 320;
        create_info.platform.window.height = 240;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = false;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.modules.enable_ibl_bake_host = true;
        create_info.render_loop.swapchain.enable_vsync = false;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        VR_REQUIRE(runtime.HasTextureHost());
        VR_REQUIRE(runtime.HasIblHost());
        VR_REQUIRE(runtime.HasIblBakeHost());

        const auto tick_result = runtime.Tick(recorder);
        VR_CHECK(tick_result.running);
        VR_REQUIRE(recorder.baked_once);
        VR_REQUIRE(recorder.bake_result.specular_cube.IsValid());
        VR_REQUIRE(recorder.bake_result.skybox_cube.IsValid());
        VR_REQUIRE(recorder.bake_result.brdf_lut.IsValid());
        VR_REQUIRE(recorder.bake_result.environment_id.IsValid());
        VR_CHECK(recorder.bake_result.registered_with_ibl_host);
        VR_CHECK(recorder.active_descriptor_set != VK_NULL_HANDLE);
        VR_CHECK(recorder.active_brdf_lut.value == recorder.bake_result.brdf_lut.value);

        const auto* specular_record = runtime.Texture().FindTexture(recorder.bake_result.specular_cube);
        const auto* skybox_record = runtime.Texture().FindTexture(recorder.bake_result.skybox_cube);
        const auto* brdf_record = runtime.Texture().FindTexture(recorder.bake_result.brdf_lut);
        VR_REQUIRE(specular_record != nullptr);
        VR_REQUIRE(skybox_record != nullptr);
        VR_REQUIRE(brdf_record != nullptr);
        VR_CHECK(specular_record->default_view_type == VK_IMAGE_VIEW_TYPE_CUBE);
        VR_CHECK(specular_record->array_layers == 6U);
        VR_CHECK(specular_record->mip_levels > 1U);
        VR_CHECK(skybox_record->default_view_type == VK_IMAGE_VIEW_TYPE_CUBE);
        VR_CHECK(skybox_record->mip_levels == 1U);
        VR_CHECK(brdf_record->default_view_type == VK_IMAGE_VIEW_TYPE_2D);
        VR_CHECK(runtime.Ibl().Stats().environment_count == 1U);
        VR_CHECK(runtime.IblBake().Stats().baked_environment_count == 1U);
        VR_CHECK(runtime.IblBake().Stats().baked_brdf_lut_count >= 1U);
        VR_CHECK(runtime.IblBake().Stats().generated_texture_count >= 3U);
        VR_CHECK(std::abs(recorder.active_params.tint_intensity[3] - 1.75F) < 1e-4F);
        VR_CHECK(std::abs(recorder.active_params.rotation_max_lod_flags[2] -
                          static_cast<float>(specular_record->mip_levels - 1U)) < 1e-4F);
        VR_CHECK(std::abs(recorder.active_params.sh9[0][0]) > 1e-5F);
        VR_CHECK(std::abs(recorder.active_params.sh9[1][1]) > 1e-5F);

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
