#include "support/test_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/render/render_target_bloom_renderer.hpp"
#include "vr/render/render_target_format_utils.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/scene_render_target_set.hpp"
#include "vr/text/text_renderer_3d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

[[nodiscard]] VkFormat ResolveDepthTargetFormat(vr::VulkanContext& context_) {
    constexpr std::array<VkFormat, 3U> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT
    };
    return vr::render::ResolveFirstSupportedDepthStencilFormat(context_, candidates);
}

[[nodiscard]] std::string FindTestFontPath() {
    namespace fs = std::filesystem;

    constexpr std::array<const char*, 6U> candidate_paths{
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/msyh.ttc"
    };

    for (const char* path : candidate_paths) {
        const fs::path candidate(path);
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
    }
    return {};
}

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
    constexpr std::array<std::string_view, 17U> patterns{
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
        "synchronization2",
        "ft_new_face",
        "freetypehost::registerface"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

void InitializeTextComponent(Text3D& component_,
                             std::uint32_t font_id_,
                             std::uint32_t material_id_,
                             std::string_view text_) {
    TextSystem3D::Initialize(component_);
    TextSystem3D::SetRuntimeRoute(component_, font_id_, material_id_, 0U, 0U);
    TextSystem3D::SetColor(component_, vr::ecs::Rgba8{255U, 255U, 255U, 255U});
    TextSystem3D::SetOutlineEnabled(component_, true);
    TextSystem3D::SetOutlineWidthPx(component_, 1U);
    TextSystem3D::SetOutlineColor(component_, vr::ecs::Rgba8{12U, 15U, 22U, 255U});
    TextSystem3D::SetBillboard(component_, true);
    TextSystem3D::SetWorldSize(component_, 0.45F);
    (void)TextSystem3D::SetText(component_, text_);
}

struct Text3DOffscreenRecorder final {
    Runtime* runtime = nullptr;
    vr::text::TextRenderer3D text_renderer{};
    vr::render::RenderTargetBloomRenderer bloom_renderer{};
    vr::render::SceneRenderTargetSet scene_targets{};

    void InitializeSceneTargets() {
        vr::render::SceneRenderTargetSetCreateInfo create_info{};
        create_info.color_debug_name = "RuntimeText3DSceneColor";
        create_info.depth_debug_name = "RuntimeText3DSceneDepth";
        create_info.enable_depth = true;
        create_info.color_lifetime = vr::render::RenderTargetLifetime::transient;
        create_info.depth_lifetime = vr::render::RenderTargetLifetime::transient;
        create_info.clear_color = VkClearColorValue{{0.07F, 0.08F, 0.11F, 1.0F}};
        scene_targets.Initialize(create_info);
    }

    void PrepareFrame(const vr::render::RuntimePrepareContext& prepare_context_) {
        (void)scene_targets.PrepareFrameAndConfigure(
            prepare_context_,
            &bloom_renderer,
            vr::render::BindSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::single));
        text_renderer.PrepareFrame(prepare_context_);
        bloom_renderer.PrepareFrame(prepare_context_);
    }

    void Record(const vr::render::FrameRecordContext& record_context_) {
        text_renderer.Record(record_context_);
        bloom_renderer.Record(record_context_);
    }

    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_) {
        text_renderer.OnSwapchainRecreated(image_count_,
                                           extent_,
                                           format_,
                                           last_submitted_value_,
                                           completed_submit_value_);
        bloom_renderer.OnSwapchainRecreated(image_count_, extent_, format_);

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
            &bloom_renderer,
            vr::render::BindSceneRenderer(text_renderer, vr::render::SceneRenderPassRole::single));
    }
};

VR_TEST_CASE(RuntimeIntegration_text_renderer_3d_end_to_end_smoke, "integration;gpu;sdl;runtime;text") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for runtime 3D text renderer integration test.");
    }

    Runtime runtime{};
    vr::text::TextRenderer3D text_renderer{};
    bool runtime_initialized = false;
    bool renderer_initialized = false;

    std::array<Text3D, 3U> text_components{};
    InitializeTextComponent(text_components[0U], 1U, 3U, "Melosyne 3D Text");
    TextSystem3D::SetWorldSize(text_components[0U], 0.7F);
    TextSystem3D::SetBillboard(text_components[0U], false);
    TextSystem3D::SetDepthWrite(text_components[0U], true);

    InitializeTextComponent(text_components[1U], 1U, 3U, "Billboard + Camera Driven");
    TextSystem3D::SetWorldSize(text_components[1U], 0.45F);
    TextSystem3D::SetBillboard(text_components[1U], true);

    InitializeTextComponent(text_components[2U], 1U, 3U, "Frame: 0");
    TextSystem3D::SetWorldSize(text_components[2U], 0.5F);
    TextSystem3D::SetColor(text_components[2U], vr::ecs::Rgba8{200U, 245U, 170U, 255U});
    TextSystem3D::SetBillboard(text_components[2U], true);

    std::array<Transform3D, 3U> text_transforms{};
    for (auto& transform : text_transforms) {
        TransformSystem3D::Initialize(transform);
    }

    std::array<Bounds3D, 3U> bounds_components{};
    for (auto& bounds : bounds_components) {
        BoundsSystem3D::Initialize(bounds);
    }

    TransformSystem3D::SetLocalPosition(text_transforms[0U],
                                        vr::ecs::Float3{.x = -1.25F, .y = 0.4F, .z = 0.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(text_transforms[0U],
                                                 0.0F,
                                                 0.0F,
                                                 0.35F);
    TransformSystem3D::SetLocalScale(text_transforms[0U],
                                     vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

    TransformSystem3D::SetLocalPosition(text_transforms[1U],
                                        vr::ecs::Float3{.x = -1.8F, .y = -0.4F, .z = 0.0F});
    TransformSystem3D::SetLocalScale(text_transforms[1U],
                                     vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

    TransformSystem3D::SetLocalPosition(text_transforms[2U],
                                        vr::ecs::Float3{.x = -1.6F, .y = -1.1F, .z = 0.0F});
    TransformSystem3D::SetLocalScale(text_transforms[2U],
                                     vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

    BoundsSystem3D::SetLocalCenterExtents(bounds_components[0U],
                                          vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                          vr::ecs::Float3{.x = 2.4F, .y = 0.6F, .z = 0.12F});
    BoundsSystem3D::SetLocalCenterExtents(bounds_components[1U],
                                          vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                          vr::ecs::Float3{.x = 2.9F, .y = 0.6F, .z = 0.12F});
    BoundsSystem3D::SetLocalCenterExtents(bounds_components[2U],
                                          vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                          vr::ecs::Float3{.x = 1.4F, .y = 0.55F, .z = 0.12F});

    TransformSystem3D::UpdateHierarchy(text_transforms.data(),
                                       static_cast<std::uint32_t>(text_transforms.size()));
    (void)BoundsSystem3D::UpdateAligned(bounds_components.data(),
                                        text_transforms.data(),
                                        static_cast<std::uint32_t>(bounds_components.size()));

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);
    CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
    CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform,
                                                 0.0F,
                                                 0.0F,
                                                 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_text_3d_smoke";
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

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, base_face_id);

        vr::text::TextRenderer3DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 8192U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 2U * 1024U * 1024U;
        text_renderer_create_info.clear_swapchain = true;
        text_renderer.Initialize(text_renderer_create_info);
        renderer_initialized = true;
        const VkFormat external_depth_format = ResolveDepthTargetFormat(runtime.Context());
        vr::render::RenderTargetDesc external_depth_desc{};
        external_depth_desc.debug_name = "RuntimeText3DExternalDepth";
        external_depth_desc.dimension = vr::render::RenderTargetDimension::image_2d;
        external_depth_desc.lifetime = vr::render::RenderTargetLifetime::persistent;
        external_depth_desc.scale_mode = vr::render::RenderTargetScaleMode::absolute;
        external_depth_desc.width = runtime.Swapchain().Extent().width;
        external_depth_desc.height = runtime.Swapchain().Extent().height;
        external_depth_desc.depth = 1U;
        external_depth_desc.format = external_depth_format;
        external_depth_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        external_depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        external_depth_desc.aspect = vr::render::DepthStencilAspectMask(external_depth_format);
        const vr::render::RenderTargetHandle external_depth_target =
            runtime.RenderTarget().CreatePersistentTarget(runtime.Context(),
                                                          external_depth_desc,
                                                          runtime.Swapchain().Extent());
        vr::render::RenderTargetDepthOutputConfig depth_output_config{};
        depth_output_config.depth_target = external_depth_target;
        depth_output_config.final_state = vr::render::RenderTargetStateKind::depth_attachment;
        depth_output_config.use_explicit_load_op = true;
        depth_output_config.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_output_config.store_op = VK_ATTACHMENT_STORE_OP_STORE;
        depth_output_config.clear_depth_stencil = VkClearDepthStencilValue{1.0F, 0U};
        text_renderer.SetDepthTargetConfig(depth_output_config);
        text_renderer.SetSceneData(text_components.data(),
                                   text_transforms.data(),
                                   static_cast<std::uint32_t>(text_components.size()),
                                   &camera,
                                   &camera_transform,
                                   bounds_components.data());

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_instance_count = 0U;
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_billboard_instances = 0U;
        std::uint32_t max_depth_test_batches = 0U;
        std::uint32_t max_depth_write_batches = 0U;
        std::uint32_t max_culling_input_count = 0U;
        std::uint32_t max_culling_visible_count = 0U;
        bool observed_bounds_culling = false;

        constexpr std::uint32_t max_ticks = 18U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            char frame_text[64]{};
            std::snprintf(frame_text, sizeof(frame_text), "Frame: %u", tick_index);
            (void)TextSystem3D::SetText(text_components[2U], frame_text);

            TransformSystem3D::SetLocalRotationEulerXyz(text_transforms[0U],
                                                         0.0F,
                                                         0.0F,
                                                         0.35F + 0.015F * static_cast<float>(tick_index));
            TransformSystem3D::UpdateHierarchy(text_transforms.data(),
                                               static_cast<std::uint32_t>(text_transforms.size()));
            (void)BoundsSystem3D::UpdateAligned(bounds_components.data(),
                                                text_transforms.data(),
                                                static_cast<std::uint32_t>(bounds_components.size()));

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(text_renderer);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const vr::text::TextRenderer3DStats& stats = text_renderer.Stats();
            max_instance_count = std::max(max_instance_count, stats.instance_count);
            max_draw_batches = std::max(max_draw_batches, stats.draw_batch_count);
            max_draw_calls = std::max(max_draw_calls, stats.draw_call_count);
            max_billboard_instances = std::max(max_billboard_instances, stats.billboard_instance_count);
            max_depth_test_batches = std::max(max_depth_test_batches, stats.depth_test_batch_count);
            max_depth_write_batches = std::max(max_depth_write_batches, stats.depth_write_batch_count);
            max_culling_input_count = std::max(max_culling_input_count, stats.culling_input_count);
            max_culling_visible_count = std::max(max_culling_visible_count, stats.culling_visible_count);
            observed_bounds_culling = observed_bounds_culling || stats.used_bounds_culling;
            VR_CHECK(stats.descriptor_set_update_count <= stats.draw_batch_count);
            VR_CHECK(stats.descriptor_set_bind_count <= stats.draw_call_count);

            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_instance_count > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_billboard_instances > 0U);
        VR_CHECK(max_depth_test_batches > 0U);
        VR_CHECK(max_depth_write_batches > 0U);
        VR_CHECK(observed_bounds_culling);
        VR_CHECK(max_culling_input_count == static_cast<std::uint32_t>(text_components.size()));
        VR_CHECK(max_culling_visible_count > 0U);
        VR_CHECK(runtime.GlyphUpload().Stats().uploaded_rect_count > 0U);
        VR_CHECK(runtime.RenderTarget().ResolveView(external_depth_target).format == external_depth_format);

        text_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            text_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
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

VR_TEST_CASE(RuntimeIntegration_text_renderer_3d_bloom_post_stack_smoke,
             "integration;gpu;sdl;runtime;text;render_target;postprocess") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for runtime 3D text offscreen integration test.");
    }

    Runtime runtime{};
    Text3DOffscreenRecorder recorder{};
    bool runtime_initialized = false;
    bool text_renderer_initialized = false;
    bool bloom_renderer_initialized = false;

    std::array<Text3D, 3U> text_components{};
    InitializeTextComponent(text_components[0U], 1U, 3U, "Melosyne 3D Text");
    TextSystem3D::SetWorldSize(text_components[0U], 0.7F);
    TextSystem3D::SetBillboard(text_components[0U], false);
    TextSystem3D::SetDepthWrite(text_components[0U], true);

    InitializeTextComponent(text_components[1U], 1U, 3U, "Billboard + Camera Driven");
    TextSystem3D::SetWorldSize(text_components[1U], 0.45F);
    TextSystem3D::SetBillboard(text_components[1U], true);

    InitializeTextComponent(text_components[2U], 1U, 3U, "Frame: 0");
    TextSystem3D::SetWorldSize(text_components[2U], 0.5F);
    TextSystem3D::SetColor(text_components[2U], vr::ecs::Rgba8{200U, 245U, 170U, 255U});
    TextSystem3D::SetBillboard(text_components[2U], true);

    std::array<Transform3D, 3U> text_transforms{};
    for (auto& transform : text_transforms) {
        TransformSystem3D::Initialize(transform);
    }

    std::array<Bounds3D, 3U> bounds_components{};
    for (auto& bounds : bounds_components) {
        BoundsSystem3D::Initialize(bounds);
    }

    TransformSystem3D::SetLocalPosition(text_transforms[0U],
                                        vr::ecs::Float3{.x = -1.25F, .y = 0.4F, .z = 0.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(text_transforms[0U], 0.0F, 0.0F, 0.35F);
    TransformSystem3D::SetLocalScale(text_transforms[0U],
                                     vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

    TransformSystem3D::SetLocalPosition(text_transforms[1U],
                                        vr::ecs::Float3{.x = -1.8F, .y = -0.4F, .z = 0.0F});
    TransformSystem3D::SetLocalScale(text_transforms[1U],
                                     vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

    TransformSystem3D::SetLocalPosition(text_transforms[2U],
                                        vr::ecs::Float3{.x = -1.6F, .y = -1.1F, .z = 0.0F});
    TransformSystem3D::SetLocalScale(text_transforms[2U],
                                     vr::ecs::Float3{.x = 1.0F, .y = 1.0F, .z = 1.0F});

    BoundsSystem3D::SetLocalCenterExtents(bounds_components[0U],
                                          vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                          vr::ecs::Float3{.x = 2.4F, .y = 0.6F, .z = 0.12F});
    BoundsSystem3D::SetLocalCenterExtents(bounds_components[1U],
                                          vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                          vr::ecs::Float3{.x = 2.9F, .y = 0.6F, .z = 0.12F});
    BoundsSystem3D::SetLocalCenterExtents(bounds_components[2U],
                                          vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                          vr::ecs::Float3{.x = 1.4F, .y = 0.55F, .z = 0.12F});

    TransformSystem3D::UpdateHierarchy(text_transforms.data(),
                                       static_cast<std::uint32_t>(text_transforms.size()));
    (void)BoundsSystem3D::UpdateAligned(bounds_components.data(),
                                        text_transforms.data(),
                                        static_cast<std::uint32_t>(bounds_components.size()));

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::SetAspectRatio(camera, 1280.0F / 720.0F);
    CameraSystem3D::SetNearFar(camera, 0.05F, 256.0F);
    CameraSystem3D::SetVerticalFovRadians(camera, 60.0F * 0.01745329251994329577F);

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 4.0F});
    TransformSystem3D::SetLocalRotationEulerXyz(camera_transform, 0.0F, 0.0F, 0.0F);
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_tests_runtime_text_3d_offscreen";
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

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, base_face_id);

        vr::text::TextRenderer3DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 8192U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 2U * 1024U * 1024U;
        text_renderer_create_info.enable_depth = true;
        text_renderer_create_info.clear_depth = true;
        text_renderer_create_info.clear_depth_value = 1.0F;
        text_renderer_create_info.clear_swapchain = false;
        text_renderer_create_info.clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
        recorder.text_renderer.Initialize(text_renderer_create_info);
        text_renderer_initialized = true;
        recorder.text_renderer.SetSceneData(text_components.data(),
                                            text_transforms.data(),
                                            static_cast<std::uint32_t>(text_components.size()),
                                            &camera,
                                            &camera_transform,
                                            bounds_components.data());

        vr::render::RenderTargetBloomRendererCreateInfo bloom_create_info{};
        bloom_create_info.clear_swapchain = true;
        bloom_create_info.clear_color = {{0.02F, 0.025F, 0.04F, 1.0F}};
        bloom_create_info.enable_reinhard_tonemap = true;
        bloom_create_info.exposure = 1.0F;
        bloom_create_info.apply_manual_gamma = false;
        bloom_create_info.bloom_threshold = 0.68F;
        bloom_create_info.bloom_knee = 0.42F;
        bloom_create_info.bloom_intensity = 0.92F;
        bloom_create_info.blur_filter_scale = 1.05F;
        bloom_create_info.downsample_scale = 0.5F;
        recorder.bloom_renderer.Initialize(bloom_create_info);
        bloom_renderer_initialized = true;

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_instance_count = 0U;
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_billboard_instances = 0U;
        std::uint32_t max_depth_test_batches = 0U;
        std::uint32_t max_depth_write_batches = 0U;
        std::uint32_t max_culling_input_count = 0U;
        std::uint32_t max_culling_visible_count = 0U;
        std::uint32_t max_prefilter_draw_calls = 0U;
        std::uint32_t max_blur_draw_calls = 0U;
        std::uint32_t max_combine_draw_calls = 0U;
        std::uint32_t max_bloom_descriptor_updates = 0U;
        bool observed_bounds_culling = false;

        constexpr std::uint32_t max_ticks = 18U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            char frame_text[64]{};
            std::snprintf(frame_text, sizeof(frame_text), "Frame: %u", tick_index);
            (void)TextSystem3D::SetText(text_components[2U], frame_text);

            TransformSystem3D::SetLocalRotationEulerXyz(text_transforms[0U],
                                                        0.0F,
                                                        0.0F,
                                                        0.35F + 0.015F * static_cast<float>(tick_index));
            TransformSystem3D::UpdateHierarchy(text_transforms.data(),
                                               static_cast<std::uint32_t>(text_transforms.size()));
            (void)BoundsSystem3D::UpdateAligned(bounds_components.data(),
                                                text_transforms.data(),
                                                static_cast<std::uint32_t>(bounds_components.size()));

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            const vr::text::TextRenderer3DStats& text_stats = recorder.text_renderer.Stats();
            const vr::render::RenderTargetBloomRendererStats& bloom_stats =
                recorder.bloom_renderer.Stats();
            max_instance_count = std::max(max_instance_count, text_stats.instance_count);
            max_draw_batches = std::max(max_draw_batches, text_stats.draw_batch_count);
            max_draw_calls = std::max(max_draw_calls, text_stats.draw_call_count);
            max_billboard_instances = std::max(max_billboard_instances, text_stats.billboard_instance_count);
            max_depth_test_batches = std::max(max_depth_test_batches, text_stats.depth_test_batch_count);
            max_depth_write_batches = std::max(max_depth_write_batches, text_stats.depth_write_batch_count);
            max_culling_input_count = std::max(max_culling_input_count, text_stats.culling_input_count);
            max_culling_visible_count = std::max(max_culling_visible_count, text_stats.culling_visible_count);
            max_prefilter_draw_calls = std::max(max_prefilter_draw_calls,
                                                bloom_stats.prefilter_draw_call_count);
            max_blur_draw_calls = std::max(max_blur_draw_calls,
                                           bloom_stats.blur_draw_call_count);
            max_combine_draw_calls = std::max(max_combine_draw_calls,
                                              bloom_stats.combine_draw_call_count);
            max_bloom_descriptor_updates = std::max(max_bloom_descriptor_updates,
                                                    bloom_stats.descriptor_set_update_count);
            observed_bounds_culling = observed_bounds_culling || text_stats.used_bounds_culling;
            VR_CHECK(text_stats.descriptor_set_update_count <= text_stats.draw_batch_count);
            VR_CHECK(text_stats.descriptor_set_bind_count <= text_stats.draw_call_count);

            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(max_instance_count > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_billboard_instances > 0U);
        VR_CHECK(max_depth_test_batches > 0U);
        VR_CHECK(max_depth_write_batches > 0U);
        VR_CHECK(max_prefilter_draw_calls > 0U);
        VR_CHECK(max_blur_draw_calls > 0U);
        VR_CHECK(max_combine_draw_calls > 0U);
        VR_CHECK(max_bloom_descriptor_updates > 0U);
        VR_CHECK(observed_bounds_culling);
        VR_CHECK(max_culling_input_count == static_cast<std::uint32_t>(text_components.size()));
        VR_CHECK(max_culling_visible_count > 0U);
        VR_CHECK(runtime.GlyphUpload().Stats().uploaded_rect_count > 0U);
        VR_CHECK(runtime.TargetPool().Stats().acquire_count > 0U);
        VR_CHECK(runtime.TargetPool().Stats().reuse_hit_count > 0U);
        VR_CHECK(runtime.RenderTarget().ResolveView(recorder.scene_targets.ColorTarget()).state ==
                 vr::render::RenderTargetStateKind::shader_read);
        VR_CHECK(runtime.RenderTarget().ResolveView(recorder.scene_targets.DepthTarget()).state ==
                 vr::render::RenderTargetStateKind::depth_attachment);

        recorder.bloom_renderer.Shutdown(runtime.Context());
        bloom_renderer_initialized = false;
        recorder.text_renderer.Shutdown(runtime.Context());
        text_renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (bloom_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.bloom_renderer.Shutdown(runtime.Context());
            bloom_renderer_initialized = false;
        }
        if (text_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.text_renderer.Shutdown(runtime.Context());
            text_renderer_initialized = false;
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
