#include "support/render_graph_test_utils.hpp"
#include "support/test_framework.hpp"
#include "vr/ecs/system/particle_emitter_system.hpp"
#include "vr/ecs/system/particle_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/particle/particle_renderer_2d.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/scene_recorder_2d.hpp"
#include "vr/text/text_renderer_2d.hpp"

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
using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
using Particle2D = vr::ecs::Particle<vr::ecs::Dim2>;
using ParticleEmitter2D = vr::ecs::ParticleEmitter<vr::ecs::Dim2>;
using Transform2D = vr::ecs::Transform<vr::ecs::Dim2>;
using ParticleSystem2D = vr::ecs::ParticleSystem<vr::ecs::Dim2>;
using ParticleEmitterSystem2D = vr::ecs::ParticleEmitterSystem<vr::ecs::Dim2>;
using TransformSystem2D = vr::ecs::TransformSystem<vr::ecs::Dim2>;

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
    constexpr std::array<std::string_view, 20U> patterns{
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

void ConfigureScene2DGraphRuntimeCreateInfo(Runtime::CreateInfo& create_info_,
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
    create_info_.poll_events_each_tick = true;
}

[[nodiscard]] vr::render::RenderView2D MakeFullscreenView(const VkExtent2D extent_,
                                                          const vr::render::RenderViewKind kind_,
                                                          const std::uint32_t view_flags_ = vr::render::render_view_overlay_enabled_flag,
                                                          const std::uint32_t layer_mask_ = 0x1U) {
    vr::render::RenderView2D view{};
    view.kind = kind_;
    view.flags = view_flags_;
    view.layer_mask = layer_mask_;
    view.debug_flags = vr::render::render_view_debug_none_flag;
    view.viewport = vr::render::RenderViewViewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>((extent_.width > 0U) ? extent_.width : 1U),
        .height = static_cast<float>((extent_.height > 0U) ? extent_.height : 1U),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
    };
    view.scissor = vr::render::RenderViewScissor{
        .x = 0,
        .y = 0,
        .width = (extent_.width > 0U) ? extent_.width : 1U,
        .height = (extent_.height > 0U) ? extent_.height : 1U,
    };
    vr::render::RefreshRenderViewSignature(view);
    return view;
}

void BuildMixedWorldOverlayPacket(std::array<vr::render::RenderView2D, 2U>& views_,
                                  vr::render::RenderScenePacket2D& packet_,
                                  const VkExtent2D extent_,
                                  const std::uint64_t submission_id_,
                                  const std::uint32_t layer_mask_ = 0x1U) {
    views_[0U] = MakeFullscreenView(extent_,
                                    vr::render::RenderViewKind::world,
                                    vr::render::render_view_overlay_enabled_flag,
                                    layer_mask_);
    views_[1U] = MakeFullscreenView(extent_,
                                    vr::render::RenderViewKind::ui,
                                    vr::render::render_view_overlay_enabled_flag,
                                    layer_mask_);
    packet_ = vr::render::MakeScenePacketFromViewRange(
        views_.data(),
        static_cast<std::uint32_t>(views_.size()),
        0U,
        submission_id_,
        vr::render::RenderScenePacketKind::mixed);
    packet_.flags = vr::render::render_scene_packet_allow_overlay_flag;
    packet_.render_layer_mask = layer_mask_;
    packet_.debug_flags = vr::render::render_view_debug_none_flag;
    packet_.postprocess_policy = vr::render::RenderPostProcessPolicy::disabled;
    vr::render::RefreshRenderScenePacketSignature(packet_);
}

void InitializeTextComponent(Text2D& component_,
                             const std::uint32_t font_id_,
                             const std::uint32_t appearance_id_,
                             const std::int16_t layer_,
                             std::string_view text_) {
    TextSystem2D::Initialize(component_);
    TextSystem2D::SetRuntimeRoute(component_, font_id_, appearance_id_, 0U, 0U);
    TextSystem2D::SetLayer(component_, layer_);
    TextSystem2D::SetColor(component_, vr::ecs::Rgba8{255U, 255U, 255U, 255U});
    TextSystem2D::SetOutlineEnabled(component_, true);
    TextSystem2D::SetOutlineWidthPx(component_, 1U);
    TextSystem2D::SetOutlineColor(component_, vr::ecs::Rgba8{14U, 16U, 23U, 255U});
    (void)TextSystem2D::SetText(component_, text_);
}

VR_TEST_CASE(RuntimeIntegration_scene_recorder_2d_text_overlay_graph_smoke,
             "integration;gpu;sdl;runtime;text;scene2d;render_graph") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for SceneRecorder2D text graph integration test.");
    }

    Runtime runtime{};
    vr::text::TextRenderer2D text_renderer{};
    vr::render::SceneRecorder2D recorder{};
    bool runtime_initialized = false;
    bool text_renderer_initialized = false;
    bool recorder_initialized = false;

    std::array<Text2D, 3U> text_components{};
    InitializeTextComponent(text_components[0U], 1U, 1U, 0, "SceneRecorder2D Graph Text");
    TextSystem2D::SetPixelSize(text_components[0U], 34.0F);
    InitializeTextComponent(text_components[1U], 1U, 1U, 1, "Overlay pass unified through RenderGraph");
    TextSystem2D::SetPixelSize(text_components[1U], 22.0F);
    InitializeTextComponent(text_components[2U], 1U, 1U, 2, "Frame: 0");
    TextSystem2D::SetPixelSize(text_components[2U], 28.0F);
    TextSystem2D::SetColor(text_components[2U], vr::ecs::Rgba8{190U, 240U, 160U, 255U});

    try {
        Runtime::CreateInfo create_info{};
        ConfigureScene2DGraphRuntimeCreateInfo(create_info, "vr_tests_scene_recorder_2d_text_graph");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId base_face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, base_face_id);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 4096U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 1024U * 1024U;
        text_renderer_create_info.clear_swapchain = false;
        text_renderer.Initialize(text_renderer_create_info);
        text_renderer_initialized = true;
        text_renderer.SetComponents(text_components.data(),
                                    static_cast<std::uint32_t>(text_components.size()));

        recorder.Initialize({});
        recorder_initialized = true;
        recorder.BindRuntime(runtime);
        recorder.RegisterOverlayRenderer(text_renderer, vr::render::SceneRecorder2D::MakePresentOverlayOutputConfig(), 0x1U);

        std::array<vr::render::RenderView2D, 2U> views{};
        vr::render::RenderScenePacket2D packet{};
        BuildMixedWorldOverlayPacket(views, packet, runtime.Swapchain().Extent(), 7301U, 0x1U);
        recorder.SetFramePacket(&packet);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_glyph_quads = 0U;
        std::uint32_t max_draw_batches = 0U;
        std::uint32_t max_draw_calls = 0U;
        bool graph_only_active = false;

        constexpr std::uint32_t max_ticks = 8U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            char frame_text[64]{};
            std::snprintf(frame_text, sizeof(frame_text), "Frame: %u", tick_index);
            (void)TextSystem2D::SetText(text_components[2U], frame_text);

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            graph_only_active =
                graph_only_active || vr::test::IsGraphOnlyScene2DRecordActive(runtime);
            const vr::text::TextRenderer2DStats& stats = text_renderer.Stats();
            max_glyph_quads = std::max(max_glyph_quads, stats.glyph_quad_count);
            max_draw_batches = std::max(max_draw_batches, stats.draw_batch_count);
            max_draw_calls = std::max(max_draw_calls, stats.draw_call_count);
            VR_CHECK(stats.descriptor_set_update_count <= stats.draw_batch_count);
            VR_CHECK(stats.descriptor_set_bind_count <= stats.draw_call_count);
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(graph_only_active);
        VR_CHECK(max_glyph_quads > 0U);
        VR_CHECK(max_draw_batches > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(runtime.GlyphUpload().Stats().uploaded_rect_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_prepare_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_record_count == 0U);
        VR_CHECK(recorder.Stats().frame_view_count == 2U);
        VR_CHECK(recorder.Stats().overlay_enabled == 1U);
        VR_CHECK(recorder.Stats().overlay_view_index == 1U);

        recorder.Shutdown(runtime.Context());
        recorder_initialized = false;
        text_renderer.Shutdown(runtime.Context());
        text_renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (recorder_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
            recorder_initialized = false;
        }
        if (text_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            text_renderer.Shutdown(runtime.Context());
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

VR_TEST_CASE(RuntimeIntegration_scene_recorder_2d_particle_overlay_graph_smoke,
             "integration;gpu;sdl;runtime;particle;scene2d;render_graph") {
    Runtime runtime{};
    vr::particle::ParticleRenderer2D particle_renderer{};
    vr::render::SceneRecorder2D recorder{};
    bool runtime_initialized = false;
    bool particle_renderer_initialized = false;
    bool recorder_initialized = false;

    try {
        Runtime::CreateInfo create_info{};
        ConfigureScene2DGraphRuntimeCreateInfo(create_info, "vr_tests_scene_recorder_2d_particle_graph");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        Particle2D particle{};
        ParticleEmitter2D emitter{};
        ParticleSystem2D::Initialize(particle);
        ParticleEmitterSystem2D::Initialize(particle, emitter);
        ParticleSystem2D::SetRenderPassHint(particle, vr::ecs::ParticleRenderPassHint::transparent);
        ParticleSystem2D::SetSimulationMode(particle, vr::ecs::ParticleSimulationMode::hybrid_gpu);
        ParticleSystem2D::SetRenderMode(particle, vr::ecs::ParticleRenderMode::axis_aligned);
        ParticleSystem2D::SetBlendMode(particle, vr::ecs::ParticleBlendMode::premultiplied_alpha);
        ParticleSystem2D::SetPremultipliedAlpha(particle, true);
        ParticleSystem2D::SetStartEndColor(particle,
                                           vr::ecs::Rgba8{255U, 220U, 120U, 255U},
                                           vr::ecs::Rgba8{255U, 80U, 16U, 0U});
        ParticleSystem2D::SetScalarStyle(particle, 24.0F, 0.0F, 1.0F, 0.0F, 0.0F);

        ParticleEmitterSystem2D::SetBurst(particle, emitter, 12U, 0.0F);
        ParticleEmitterSystem2D::SetSpawnRate(particle, emitter, 48.0F);
        ParticleEmitterSystem2D::SetLifetimeRange(particle, emitter, 0.60F, 1.10F);
        ParticleEmitterSystem2D::SetSpeedRange(particle, emitter, 12.0F, 40.0F);
        ParticleEmitterSystem2D::SetSizeRange(particle, emitter, 16.0F, 28.0F, 4.0F, 10.0F);
        ParticleEmitterSystem2D::SetEmissionShape(particle,
                                                  emitter,
                                                  vr::ecs::ParticleEmitterShape::circle,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                                  8.0F,
                                                  vr::ecs::Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F},
                                                  0.0F,
                                                  0.0F);
        ParticleEmitterSystem2D::SetPlayback(particle, emitter, true, true, true);

        Transform2D transform{};
        TransformSystem2D::Initialize(transform);
        TransformSystem2D::SetLocalPosition(transform, 320.0F, 180.0F);
        TransformSystem2D::UpdateHierarchy(&transform, 1U);

        vr::particle::ParticleRenderer2DCreateInfo renderer_create_info{};
        renderer_create_info.reserve_component_count = 1U;
        renderer_create_info.reserve_particle_count = 256U;
        renderer_create_info.clear_swapchain = false;
        particle_renderer.Initialize(renderer_create_info);
        particle_renderer_initialized = true;
        runtime.Services().Get<vr::runtime::services::ParticleRenderService>().ConfigureRenderer(
            particle_renderer);
        particle_renderer.SetSceneData(&particle, &emitter, &transform, 1U);

        recorder.Initialize({});
        recorder_initialized = true;
        recorder.BindRuntime(runtime);
        recorder.RegisterOverlayRenderer(particle_renderer,
                                         vr::render::SceneRecorder2D::MakePresentOverlayOutputConfig(),
                                         0x1U);

        std::array<vr::render::RenderView2D, 2U> views{};
        vr::render::RenderScenePacket2D packet{};
        BuildMixedWorldOverlayPacket(views, packet, runtime.Swapchain().Extent(), 7303U, 0x1U);
        recorder.SetFramePacket(&packet);

        std::uint32_t submitted_frames = 0U;
        std::uint32_t max_uploaded_instances = 0U;
        std::uint32_t max_draw_calls = 0U;
        std::uint32_t max_indirect_draw_calls = 0U;
        std::uint32_t max_descriptor_binds = 0U;
        std::uint32_t max_descriptor_updates = 0U;
        bool graph_only_active = false;

        constexpr std::uint32_t max_ticks = 10U;
        for (std::uint32_t tick_index = 0U;
             tick_index < max_ticks && runtime.IsRunning();
             ++tick_index) {
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (tick_result.render.code == vr::render::TickCode::Submitted ||
                tick_result.render.code == vr::render::TickCode::RecreateRequested) {
                ++submitted_frames;
            }

            graph_only_active =
                graph_only_active || vr::test::IsGraphOnlyScene2DRecordActive(runtime);
            const auto& renderer_stats = particle_renderer.Stats();
            max_uploaded_instances = std::max(max_uploaded_instances,
                                              renderer_stats.uploaded_instance_count);
            max_draw_calls = std::max(max_draw_calls, renderer_stats.draw_call_count);
            max_indirect_draw_calls = std::max(max_indirect_draw_calls,
                                               renderer_stats.indirect_draw_count);
            max_descriptor_binds = std::max(max_descriptor_binds,
                                            renderer_stats.descriptor_set_bind_count);
            max_descriptor_updates = std::max(max_descriptor_updates,
                                              renderer_stats.descriptor_set_update_count);
            SDL_Delay(1U);
        }

        VR_REQUIRE(submitted_frames > 0U);
        VR_CHECK(graph_only_active);
        VR_CHECK(max_uploaded_instances > 0U);
        VR_CHECK(max_draw_calls > 0U);
        VR_CHECK(max_descriptor_binds > 0U);
        VR_CHECK(max_descriptor_updates == 0U);
        VR_CHECK(recorder.Stats().frame_packet_prepare_count > 0U);
        VR_CHECK(recorder.Stats().frame_packet_record_count == 0U);
        VR_CHECK(recorder.Stats().frame_view_count == 2U);
        VR_CHECK(recorder.Stats().overlay_enabled == 1U);
        const auto& particle_simulation_service =
            runtime.Services().Get<vr::runtime::services::ParticleSimulationService>();
        VR_REQUIRE(particle_simulation_service.Stats().prepared_frame_count > 0U);
        if (particle_simulation_service.Capabilities().SupportsHybridSimulation()) {
            VR_REQUIRE(particle_simulation_service.Stats().gpu_build_prepare_count > 0U);
            VR_REQUIRE(particle_simulation_service.Stats().gpu_build_dispatch_count > 0U);
            VR_REQUIRE(particle_simulation_service.Stats().update_dispatch_count > 0U);
            VR_REQUIRE(max_indirect_draw_calls > 0U);
        }

        recorder.Shutdown(runtime.Context());
        recorder_initialized = false;
        particle_renderer.Shutdown(runtime.Context());
        particle_renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (recorder_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
            recorder_initialized = false;
        }
        if (particle_renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            particle_renderer.Shutdown(runtime.Context());
            particle_renderer_initialized = false;
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
