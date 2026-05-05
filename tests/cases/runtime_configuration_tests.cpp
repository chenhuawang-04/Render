#include "support/test_framework.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/text/text_runtime_contract.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;

class NoopRecorder final {
public:
    void Record(const vr::render::FrameRecordContext& record_context_) {
        (void)record_context_;
    }
};

template<typename FnT>
[[nodiscard]] bool ThrowsAnyException(FnT&& function_) {
    try {
        std::invoke(std::forward<FnT>(function_));
    } catch (...) {
        return true;
    }
    return false;
}

VR_TEST_CASE(RuntimeConfig_modules_default_to_enabled, "unit;core;runtime") {
    vr::render::RuntimeModulesCreateInfo modules{};
    VR_CHECK(modules.enable_texture_host);
    VR_CHECK(modules.enable_frame_composer_host);
    VR_CHECK(modules.enable_ibl_host);
    VR_CHECK(!modules.enable_ibl_bake_host);
    VR_CHECK(modules.enable_upload_host);
    VR_CHECK(modules.enable_descriptor_host);
    VR_CHECK(modules.enable_pipeline_host);
    VR_CHECK(modules.enable_render_target_host);
    VR_CHECK(modules.enable_render_target_pool);
    VR_CHECK(modules.enable_sampler_host);
    VR_CHECK(modules.enable_freetype_host);
    VR_CHECK(modules.enable_glyph_atlas_host);
    VR_CHECK(modules.enable_glyph_upload_host);
}

VR_TEST_CASE(RuntimeConfig_default_state_before_initialize_is_safe, "unit;core;runtime") {
    Runtime runtime{};

    VR_CHECK(!runtime.IsInitialized());
    VR_CHECK(!runtime.IsRunning());
    VR_CHECK(!runtime.HasTextureHost());
    VR_CHECK(!runtime.HasFrameComposerHost());
    VR_CHECK(!runtime.HasIblHost());
    VR_CHECK(!runtime.HasIblBakeHost());
    VR_CHECK(!runtime.HasUploadHost());
    VR_CHECK(!runtime.HasDescriptorHost());
    VR_CHECK(!runtime.HasPipelineHost());
    VR_CHECK(!runtime.HasRenderTargetHost());
    VR_CHECK(!runtime.HasRenderTargetPool());
    VR_CHECK(!runtime.HasSamplerHost());
    VR_CHECK(!runtime.HasFreeTypeHost());
    VR_CHECK(!runtime.HasGlyphAtlasHost());
    VR_CHECK(!runtime.HasGlyphUploadHost());

    const Runtime::CreateInfo& config = runtime.Config();
    VR_CHECK(config.modules.enable_upload_host);
    VR_CHECK(config.modules.enable_texture_host);
    VR_CHECK(config.modules.enable_frame_composer_host);
    VR_CHECK(config.modules.enable_ibl_host);
    VR_CHECK(!config.modules.enable_ibl_bake_host);
    VR_CHECK(config.modules.enable_descriptor_host);
    VR_CHECK(config.modules.enable_pipeline_host);
    VR_CHECK(config.modules.enable_render_target_host);
    VR_CHECK(config.modules.enable_render_target_pool);
    VR_CHECK(config.modules.enable_sampler_host);
    VR_CHECK(config.modules.enable_freetype_host);
    VR_CHECK(config.modules.enable_glyph_atlas_host);
    VR_CHECK(config.modules.enable_glyph_upload_host);
    VR_CHECK(!config.diagnostics.enable_frame_diagnostics);
}

VR_TEST_CASE(RuntimeConfig_text_runtime_feature_contract_enables_dynamic_rendering_and_sync2,
             "unit;core;runtime;text") {
    Runtime::CreateInfo create_info{};
    vr::text::ApplyTextRuntimeFeatureContract(create_info);

    VR_CHECK(create_info.platform.device.required_vulkan13_features.dynamicRendering == VK_TRUE);
    VR_CHECK(create_info.platform.device.required_vulkan13_features.synchronization2 == VK_TRUE);

    const auto default_text_create_info =
        vr::text::MakeDefaultTextRuntimeCreateInfo<Runtime::CreateInfo>();
    VR_CHECK(default_text_create_info.platform.device.required_vulkan13_features.dynamicRendering == VK_TRUE);
    VR_CHECK(default_text_create_info.platform.device.required_vulkan13_features.synchronization2 == VK_TRUE);
}

VR_TEST_CASE(RuntimeConfig_unavailable_modules_throw_before_initialize, "unit;core;runtime") {
    Runtime runtime{};

    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GpuMemory(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Texture(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.FrameComposer(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Ibl(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.IblBake(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Upload(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Descriptor(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Pipeline(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.RenderTarget(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.TargetPool(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Sampler(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.FreeType(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GlyphAtlas(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GlyphUpload(); }));
}

VR_TEST_CASE(RuntimeConfig_tick_requires_initialize, "unit;core;runtime") {
    Runtime runtime{};
    NoopRecorder recorder{};

    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Tick(recorder); }));
}

VR_TEST_CASE(RuntimeConfig_resource_creation_requires_initialize, "unit;core;runtime") {
    Runtime runtime{};

    vr::resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = 1024U;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    vr::resource::ImageCreateInfo image_create_info{};
    image_create_info.extent = VkExtent3D{64U, 64U, 1U};
    image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.CreateBuffer(buffer_create_info); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.CreateImage(image_create_info); }));
}

} // namespace
