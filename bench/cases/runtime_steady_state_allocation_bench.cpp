#include "support/bench_framework.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/render/render_view_submission_utils.hpp"
#include "vr/render/scene_recorder_2d.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/text/text_renderer_2d.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Runtime = vr::render::RenderRuntimeHost<vr::platform::ActiveBackendTag, 2U>;
using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

[[nodiscard]] std::string FindBenchFontPath() {
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

void InitializeTextComponent(Text2D& component_,
                             std::uint32_t font_id_,
                             std::uint32_t appearance_id_,
                             std::int16_t layer_,
                             std::string_view text_) {
    TextSystem2D::Initialize(component_);
    TextSystem2D::SetRuntimeRoute(component_, font_id_, appearance_id_, 0U, 0U);
    TextSystem2D::SetLayer(component_, layer_);
    TextSystem2D::SetColor(component_, vr::ecs::Rgba8{255U, 255U, 255U, 255U});
    TextSystem2D::SetOutlineEnabled(component_, true);
    TextSystem2D::SetOutlineWidthPx(component_, 1U);
    TextSystem2D::SetOutlineColor(component_, vr::ecs::Rgba8{16U, 20U, 28U, 255U});
    (void)TextSystem2D::SetText(component_, text_);
}

[[nodiscard]] bool IsSubmitted(vr::render::TickCode code_) noexcept {
    return code_ == vr::render::TickCode::Submitted ||
           code_ == vr::render::TickCode::RecreateRequested;
}

void ConfigureScene2DGraphRuntimeCreateInfo(Runtime::CreateInfo& create_info_,
                                            const char* window_title_) {
    create_info_.platform.window.title = window_title_;
    create_info_.platform.window.width = 640;
    create_info_.platform.window.height = 360;
    create_info_.platform.window.resizable = false;
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

[[nodiscard]] vr::render::RenderView2D MakeFullscreenView(
    const VkExtent2D extent_,
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

template<typename RuntimeT>
[[nodiscard]] bool IsGraphOnlyScene2DRecordActive(RuntimeT& runtime_) {
    using Service = vr::runtime::services::RenderGraphRuntimeService;
    const auto* service = runtime_.Services().template TryGet<Service>();
    return service != nullptr &&
           service->GraphOnlyRecordPathEnabled() &&
           service->CanExecuteGraphRecord(runtime_.Context()) &&
           service->LastRecordStats().pass_count > 0U;
}

struct RenderGraphSteadyStateSnapshot final {
    bool render_graph_available = false;
    bool graph_only_active = false;
    std::uint32_t compiled_pass_count = 0U;
    std::uint32_t recorded_pass_count = 0U;
    std::uint64_t transient_logical_total_bytes = 0U;
    std::uint64_t transient_physical_total_bytes = 0U;
    std::uint64_t transient_peak_live_bytes = 0U;
    std::uint64_t transient_saved_bytes = 0U;
    std::uint32_t transient_page_count = 0U;
    std::uint32_t alias_barrier_count = 0U;
    std::uint32_t lazy_memory_requested_count = 0U;
    std::uint32_t glyph_page_count = 0U;
    std::uint32_t glyph_entry_count = 0U;
};

[[nodiscard]] RenderGraphSteadyStateSnapshot CaptureRenderGraphSteadyStateSnapshot(
    const Runtime::RuntimeTickResult& tick_result_,
    Runtime& runtime_) noexcept {
    RenderGraphSteadyStateSnapshot snapshot{};
    snapshot.render_graph_available = tick_result_.diagnostics.render_graph.available;
    snapshot.graph_only_active = IsGraphOnlyScene2DRecordActive(runtime_);
    snapshot.compiled_pass_count = tick_result_.diagnostics.render_graph.compiled_pass_count;
    snapshot.recorded_pass_count = tick_result_.diagnostics.render_graph.recorded_pass_count;
    snapshot.transient_logical_total_bytes =
        tick_result_.diagnostics.render_graph.transient_logical_total_bytes;
    snapshot.transient_physical_total_bytes =
        tick_result_.diagnostics.render_graph.transient_physical_total_bytes;
    snapshot.transient_peak_live_bytes =
        tick_result_.diagnostics.render_graph.transient_peak_live_bytes;
    snapshot.transient_saved_bytes =
        tick_result_.diagnostics.render_graph.transient_saved_bytes;
    snapshot.transient_page_count =
        tick_result_.diagnostics.render_graph.transient_page_count;
    snapshot.alias_barrier_count =
        tick_result_.diagnostics.render_graph.alias_barrier_count;
    snapshot.lazy_memory_requested_count =
        tick_result_.diagnostics.render_graph.lazy_memory_requested_count;
    snapshot.glyph_page_count = runtime_.GlyphAtlas().Stats().page_count;
    snapshot.glyph_entry_count = runtime_.GlyphAtlas().Stats().glyph_entry_count;
    return snapshot;
}

[[nodiscard]] bool SameSnapshot(const RenderGraphSteadyStateSnapshot& lhs_,
                                const RenderGraphSteadyStateSnapshot& rhs_) noexcept {
    return lhs_.render_graph_available == rhs_.render_graph_available &&
           lhs_.graph_only_active == rhs_.graph_only_active &&
           lhs_.compiled_pass_count == rhs_.compiled_pass_count &&
           lhs_.recorded_pass_count == rhs_.recorded_pass_count &&
           lhs_.transient_logical_total_bytes == rhs_.transient_logical_total_bytes &&
           lhs_.transient_physical_total_bytes == rhs_.transient_physical_total_bytes &&
           lhs_.transient_peak_live_bytes == rhs_.transient_peak_live_bytes &&
           lhs_.transient_saved_bytes == rhs_.transient_saved_bytes &&
           lhs_.transient_page_count == rhs_.transient_page_count &&
           lhs_.alias_barrier_count == rhs_.alias_barrier_count &&
           lhs_.lazy_memory_requested_count == rhs_.lazy_memory_requested_count &&
           lhs_.glyph_page_count == rhs_.glyph_page_count &&
           lhs_.glyph_entry_count == rhs_.glyph_entry_count;
}

[[noreturn]] void ThrowSnapshotMismatch(const RenderGraphSteadyStateSnapshot& expected_,
                                        const RenderGraphSteadyStateSnapshot& observed_) {
    throw std::runtime_error(
        "Runtime render-graph steady-state gate failed: expected snapshot [available=" +
        std::to_string(expected_.render_graph_available ? 1U : 0U) +
        ", graph_only=" + std::to_string(expected_.graph_only_active ? 1U : 0U) +
        ", compiled_passes=" + std::to_string(expected_.compiled_pass_count) +
        ", recorded_passes=" + std::to_string(expected_.recorded_pass_count) +
        ", logical_bytes=" + std::to_string(expected_.transient_logical_total_bytes) +
        ", physical_bytes=" + std::to_string(expected_.transient_physical_total_bytes) +
        ", peak_live=" + std::to_string(expected_.transient_peak_live_bytes) +
        ", saved=" + std::to_string(expected_.transient_saved_bytes) +
        ", pages=" + std::to_string(expected_.transient_page_count) +
        ", alias_barriers=" + std::to_string(expected_.alias_barrier_count) +
        ", lazy_requested=" + std::to_string(expected_.lazy_memory_requested_count) +
        ", glyph_pages=" + std::to_string(expected_.glyph_page_count) +
        ", glyph_entries=" + std::to_string(expected_.glyph_entry_count) +
        "] but observed [available=" + std::to_string(observed_.render_graph_available ? 1U : 0U) +
        ", graph_only=" + std::to_string(observed_.graph_only_active ? 1U : 0U) +
        ", compiled_passes=" + std::to_string(observed_.compiled_pass_count) +
        ", recorded_passes=" + std::to_string(observed_.recorded_pass_count) +
        ", logical_bytes=" + std::to_string(observed_.transient_logical_total_bytes) +
        ", physical_bytes=" + std::to_string(observed_.transient_physical_total_bytes) +
        ", peak_live=" + std::to_string(observed_.transient_peak_live_bytes) +
        ", saved=" + std::to_string(observed_.transient_saved_bytes) +
        ", pages=" + std::to_string(observed_.transient_page_count) +
        ", alias_barriers=" + std::to_string(observed_.alias_barrier_count) +
        ", lazy_requested=" + std::to_string(observed_.lazy_memory_requested_count) +
        ", glyph_pages=" + std::to_string(observed_.glyph_page_count) +
        ", glyph_entries=" + std::to_string(observed_.glyph_entry_count) + "]");
}

struct SteadyStateSnapshot final {
    std::uint64_t upload_capacity_bytes = 0U;
    std::uint32_t upload_staging_page_growth_count = 0U;
    std::uint32_t descriptor_total_pool_count = 0U;
    std::uint32_t descriptor_total_allocated_set_count = 0U;
    std::uint32_t pipeline_graphics_pipeline_count = 0U;
    std::uint32_t pipeline_layout_count = 0U;
    std::uint32_t pipeline_shader_module_count = 0U;
    std::uint32_t glyph_page_count = 0U;
    std::uint32_t glyph_entry_count = 0U;
};

[[nodiscard]] SteadyStateSnapshot CaptureSteadyStateSnapshot(
    const Runtime::RuntimeTickResult& tick_result_,
    Runtime& runtime_) noexcept {
    SteadyStateSnapshot snapshot{};
    snapshot.upload_capacity_bytes = tick_result_.diagnostics.allocations.upload_capacity_bytes;
    snapshot.upload_staging_page_growth_count =
        tick_result_.diagnostics.allocations.upload_staging_page_growth_count;
    snapshot.descriptor_total_pool_count =
        tick_result_.diagnostics.allocations.descriptor_total_pool_count;
    snapshot.descriptor_total_allocated_set_count =
        tick_result_.diagnostics.descriptor.total_allocated_set_count;
    snapshot.pipeline_graphics_pipeline_count =
        tick_result_.diagnostics.pipeline.graphics_pipeline_count;
    snapshot.pipeline_layout_count = tick_result_.diagnostics.pipeline.pipeline_layout_count;
    snapshot.pipeline_shader_module_count = tick_result_.diagnostics.pipeline.shader_module_count;
    snapshot.glyph_page_count = runtime_.GlyphAtlas().Stats().page_count;
    snapshot.glyph_entry_count = runtime_.GlyphAtlas().Stats().glyph_entry_count;
    return snapshot;
}

[[nodiscard]] bool SameSnapshot(const SteadyStateSnapshot& lhs_,
                                const SteadyStateSnapshot& rhs_) noexcept {
    return lhs_.upload_capacity_bytes == rhs_.upload_capacity_bytes &&
           lhs_.upload_staging_page_growth_count == rhs_.upload_staging_page_growth_count &&
           lhs_.descriptor_total_pool_count == rhs_.descriptor_total_pool_count &&
           lhs_.descriptor_total_allocated_set_count == rhs_.descriptor_total_allocated_set_count &&
           lhs_.pipeline_graphics_pipeline_count == rhs_.pipeline_graphics_pipeline_count &&
           lhs_.pipeline_layout_count == rhs_.pipeline_layout_count &&
           lhs_.pipeline_shader_module_count == rhs_.pipeline_shader_module_count &&
           lhs_.glyph_page_count == rhs_.glyph_page_count &&
           lhs_.glyph_entry_count == rhs_.glyph_entry_count;
}

[[noreturn]] void ThrowSnapshotMismatch(const SteadyStateSnapshot& expected_,
                                        const SteadyStateSnapshot& observed_) {
    throw std::runtime_error(
        "Runtime steady-state allocation gate failed: expected snapshot [upload_capacity=" +
        std::to_string(expected_.upload_capacity_bytes) +
        ", upload_growth=" + std::to_string(expected_.upload_staging_page_growth_count) +
        ", descriptor_pools=" + std::to_string(expected_.descriptor_total_pool_count) +
        ", descriptor_sets=" + std::to_string(expected_.descriptor_total_allocated_set_count) +
        ", pipelines=" + std::to_string(expected_.pipeline_graphics_pipeline_count) +
        ", layouts=" + std::to_string(expected_.pipeline_layout_count) +
        ", shader_modules=" + std::to_string(expected_.pipeline_shader_module_count) +
        ", glyph_pages=" + std::to_string(expected_.glyph_page_count) +
        ", glyph_entries=" + std::to_string(expected_.glyph_entry_count) +
        "] but observed [upload_capacity=" + std::to_string(observed_.upload_capacity_bytes) +
        ", upload_growth=" + std::to_string(observed_.upload_staging_page_growth_count) +
        ", descriptor_pools=" + std::to_string(observed_.descriptor_total_pool_count) +
        ", descriptor_sets=" + std::to_string(observed_.descriptor_total_allocated_set_count) +
        ", pipelines=" + std::to_string(observed_.pipeline_graphics_pipeline_count) +
        ", layouts=" + std::to_string(observed_.pipeline_layout_count) +
        ", shader_modules=" + std::to_string(observed_.pipeline_shader_module_count) +
        ", glyph_pages=" + std::to_string(observed_.glyph_page_count) +
        ", glyph_entries=" + std::to_string(observed_.glyph_entry_count) + "]");
}

VR_BENCHMARK_CASE(RuntimeDiagnostics_steady_state_text_renderer_2d_allocation_gate,
                  "integration;gpu;sdl;runtime;allocation") {
    const std::string font_path = FindBenchFontPath();
    if (font_path.empty()) {
        VR_BENCH_SKIP("No usable system font found for steady-state allocation benchmark.");
    }

    Runtime runtime{};
    vr::text::TextRenderer2D text_renderer{};
    bool runtime_initialized = false;
    bool renderer_initialized = false;

    std::array<Text2D, 3U> text_components{};
    InitializeTextComponent(text_components[0U], 1U, 1U, 0, "Steady-state allocation gate");
    TextSystem2D::SetPixelSize(text_components[0U], 34.0F);
    InitializeTextComponent(text_components[1U], 1U, 1U, 1, "Warmup then verify zero growth");
    TextSystem2D::SetPixelSize(text_components[1U], 24.0F);
    InitializeTextComponent(text_components[2U], 1U, 1U, 2, "Static text to avoid atlas churn");
    TextSystem2D::SetPixelSize(text_components[2U], 26.0F);

    try {
        Runtime::CreateInfo create_info{};
        create_info.platform.window.title = "vr_bench_runtime_steady_state_allocation";
        create_info.platform.window.width = 640;
        create_info.platform.window.height = 360;
        create_info.platform.window.resizable = false;
        create_info.platform.window.high_pixel_density = true;
        create_info.platform.instance.enable_validation = false;
        create_info.platform.device.required_vulkan13_features.dynamicRendering = VK_TRUE;
        create_info.platform.device.required_vulkan13_features.synchronization2 = VK_TRUE;
        create_info.render_loop.swapchain.enable_vsync = false;
        create_info.render_loop.swapchain.preferred_image_count = 2U;
        create_info.render_loop.commands.initial_primary_per_frame = 2U;
        create_info.render_loop.commands.primary_growth_chunk = 2U;
        create_info.diagnostics.level = vr::runtime::DiagnosticsLevel::Detailed;
        create_info.poll_events_each_tick = true;
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, face_id);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.reserve_component_count = static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 8192U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 2U * 1024U * 1024U;
        text_renderer_create_info.clear_swapchain = true;
        text_renderer.Initialize(text_renderer_create_info);
        renderer_initialized = true;
        text_renderer.SetComponents(text_components.data(), static_cast<std::uint32_t>(text_components.size()));

        constexpr std::uint32_t warmup_frame_limit = 24U;
        constexpr std::uint32_t required_stable_submitted_frames = 3U;
        std::uint32_t stable_submitted_frames = 0U;
        bool baseline_captured = false;
        SteadyStateSnapshot baseline_snapshot{};

        for (std::uint32_t warmup_index = 0U;
             warmup_index < warmup_frame_limit && runtime.IsRunning() && stable_submitted_frames < required_stable_submitted_frames;
             ++warmup_index) {
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(text_renderer);
            if (!IsSubmitted(tick_result.render.code)) {
                continue;
            }
            if (!tick_result.diagnostics.collected) {
                throw std::runtime_error("Runtime steady-state allocation warmup did not collect diagnostics.");
            }

            const SteadyStateSnapshot snapshot = CaptureSteadyStateSnapshot(tick_result, runtime);
            if (!baseline_captured) {
                baseline_snapshot = snapshot;
                baseline_captured = true;
                stable_submitted_frames = 1U;
                continue;
            }

            if (SameSnapshot(baseline_snapshot, snapshot)) {
                ++stable_submitted_frames;
            } else {
                baseline_snapshot = snapshot;
                stable_submitted_frames = 1U;
            }
        }

        if (!baseline_captured || stable_submitted_frames < required_stable_submitted_frames) {
            throw std::runtime_error("Runtime steady-state allocation gate could not reach a stable warmup baseline.");
        }

        const std::uint64_t iterations = bench_context_.Iterations();
        std::uint64_t measured_frames = 0U;
        std::uint64_t uploaded_bytes = 0U;
        vr::render::TickCode last_tick_code = vr::render::TickCode::SkippedWindowHidden;
        for (std::uint64_t i = 0U; i < iterations; ++i) {
            if (!runtime.IsRunning()) {
                break;
            }

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(text_renderer);
            last_tick_code = tick_result.render.code;
            if (!IsSubmitted(tick_result.render.code)) {
                continue;
            }
            if (!tick_result.diagnostics.collected) {
                throw std::runtime_error("Runtime steady-state allocation gate lost diagnostics collection during measured frames.");
            }

            const SteadyStateSnapshot snapshot = CaptureSteadyStateSnapshot(tick_result, runtime);
            if (!SameSnapshot(baseline_snapshot, snapshot)) {
                ThrowSnapshotMismatch(baseline_snapshot, snapshot);
            }

            ++measured_frames;
            uploaded_bytes += text_renderer.Stats().uploaded_bytes;
        }

        if (measured_frames == 0U) {
            throw std::runtime_error("Runtime steady-state allocation gate submitted zero measured frames.");
        }

        bench_context_.AddItems(measured_frames);
        bench_context_.AddBytes(uploaded_bytes);
        vr::bench::BenchmarkContext::DoNotOptimize(last_tick_code);
        vr::bench::BenchmarkContext::DoNotOptimize(text_renderer.Stats().draw_call_count);
        vr::bench::BenchmarkContext::ClobberMemory();

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
            VR_BENCH_SKIP(exception_.what());
        }
        throw;
    }
}

VR_BENCHMARK_CASE(RuntimeDiagnostics_steady_state_scene_recorder_2d_text_graph_allocation_gate,
                  "integration;gpu;sdl;runtime;allocation;render_graph") {
    const std::string font_path = FindBenchFontPath();
    if (font_path.empty()) {
        VR_BENCH_SKIP("No usable system font found for render-graph steady-state benchmark.");
    }

    Runtime runtime{};
    vr::text::TextRenderer2D text_renderer{};
    vr::render::SceneRecorder2D recorder{};
    bool runtime_initialized = false;
    bool renderer_initialized = false;
    bool recorder_initialized = false;

    std::array<Text2D, 3U> text_components{};
    InitializeTextComponent(text_components[0U], 1U, 1U, 0, "RenderGraph steady-state gate");
    TextSystem2D::SetPixelSize(text_components[0U], 34.0F);
    InitializeTextComponent(text_components[1U], 1U, 1U, 1, "Graph-only overlay path");
    TextSystem2D::SetPixelSize(text_components[1U], 22.0F);
    InitializeTextComponent(text_components[2U], 1U, 1U, 2, "Static text for stable diagnostics");
    TextSystem2D::SetPixelSize(text_components[2U], 28.0F);
    TextSystem2D::SetColor(text_components[2U], vr::ecs::Rgba8{190U, 240U, 160U, 255U});

    try {
        Runtime::CreateInfo create_info{};
        ConfigureScene2DGraphRuntimeCreateInfo(create_info,
                                               "vr_bench_runtime_scene_recorder_2d_text_graph");
        runtime.Initialize(create_info);
        runtime_initialized = true;

        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 32U;
        const vr::text::FontFaceId face_id = runtime.FreeType().RegisterFace(face_create_info);
        runtime.GlyphAtlas().MapFont(1U, face_id);

        vr::text::TextRenderer2DCreateInfo text_renderer_create_info{};
        text_renderer_create_info.runtime_build.pixel_size_quantization = 1.0F;
        text_renderer_create_info.runtime_build.enable_kerning = true;
        text_renderer_create_info.reserve_component_count =
            static_cast<std::uint32_t>(text_components.size());
        text_renderer_create_info.reserve_glyph_count = 4096U;
        text_renderer_create_info.initial_vertex_buffer_bytes = 1024U * 1024U;
        text_renderer_create_info.clear_swapchain = false;
        text_renderer.Initialize(text_renderer_create_info);
        renderer_initialized = true;
        text_renderer.SetComponents(text_components.data(),
                                    static_cast<std::uint32_t>(text_components.size()));

        recorder.Initialize({});
        recorder_initialized = true;
        recorder.BindRuntime(runtime);
        recorder.RegisterOverlayRenderer(text_renderer,
                                         vr::render::SceneRecorder2D::MakePresentOverlayOutputConfig(),
                                         0x1U);

        std::array<vr::render::RenderView2D, 2U> views{};
        vr::render::RenderScenePacket2D packet{};
        BuildMixedWorldOverlayPacket(views, packet, runtime.Swapchain().Extent(), 8101U, 0x1U);
        recorder.SetFramePacket(&packet);

        constexpr std::uint32_t warmup_frame_limit = 24U;
        constexpr std::uint32_t required_stable_submitted_frames = 3U;
        std::uint32_t stable_submitted_frames = 0U;
        bool baseline_captured = false;
        RenderGraphSteadyStateSnapshot baseline_snapshot{};

        for (std::uint32_t warmup_index = 0U;
             warmup_index < warmup_frame_limit && runtime.IsRunning() && stable_submitted_frames < required_stable_submitted_frames;
             ++warmup_index) {
            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            if (!IsSubmitted(tick_result.render.code)) {
                continue;
            }
            if (!tick_result.diagnostics.collected) {
                throw std::runtime_error("Runtime render-graph steady-state warmup did not collect diagnostics.");
            }

            const RenderGraphSteadyStateSnapshot snapshot =
                CaptureRenderGraphSteadyStateSnapshot(tick_result, runtime);
            if (!snapshot.render_graph_available ||
                !snapshot.graph_only_active ||
                snapshot.recorded_pass_count == 0U) {
                throw std::runtime_error("Runtime render-graph steady-state warmup did not activate graph-only diagnostics.");
            }

            if (!baseline_captured) {
                baseline_snapshot = snapshot;
                baseline_captured = true;
                stable_submitted_frames = 1U;
                continue;
            }

            if (SameSnapshot(baseline_snapshot, snapshot)) {
                ++stable_submitted_frames;
            } else {
                baseline_snapshot = snapshot;
                stable_submitted_frames = 1U;
            }
        }

        if (!baseline_captured || stable_submitted_frames < required_stable_submitted_frames) {
            throw std::runtime_error(
                "Runtime render-graph steady-state gate could not reach a stable warmup baseline.");
        }

        const std::uint64_t iterations = bench_context_.Iterations();
        std::uint64_t measured_frames = 0U;
        std::uint64_t measured_bytes = 0U;
        vr::render::TickCode last_tick_code = vr::render::TickCode::SkippedWindowHidden;
        for (std::uint64_t i = 0U; i < iterations; ++i) {
            if (!runtime.IsRunning()) {
                break;
            }

            const Runtime::RuntimeTickResult tick_result = runtime.Tick(recorder);
            last_tick_code = tick_result.render.code;
            if (!IsSubmitted(tick_result.render.code)) {
                continue;
            }
            if (!tick_result.diagnostics.collected) {
                throw std::runtime_error(
                    "Runtime render-graph steady-state gate lost diagnostics collection during measured frames.");
            }

            const RenderGraphSteadyStateSnapshot snapshot =
                CaptureRenderGraphSteadyStateSnapshot(tick_result, runtime);
            if (!SameSnapshot(baseline_snapshot, snapshot)) {
                ThrowSnapshotMismatch(baseline_snapshot, snapshot);
            }

            ++measured_frames;
            measured_bytes += text_renderer.Stats().uploaded_bytes;
            measured_bytes += snapshot.transient_physical_total_bytes;
        }

        if (measured_frames == 0U) {
            throw std::runtime_error(
                "Runtime render-graph steady-state gate submitted zero measured frames.");
        }

        bench_context_.AddItems(measured_frames);
        bench_context_.AddBytes(measured_bytes);
        vr::bench::BenchmarkContext::DoNotOptimize(last_tick_code);
        vr::bench::BenchmarkContext::DoNotOptimize(baseline_snapshot.recorded_pass_count);
        vr::bench::BenchmarkContext::DoNotOptimize(baseline_snapshot.transient_saved_bytes);
        vr::bench::BenchmarkContext::ClobberMemory();

        recorder.Shutdown(runtime.Context());
        recorder_initialized = false;
        text_renderer.Shutdown(runtime.Context());
        renderer_initialized = false;
        runtime.Shutdown();
        runtime_initialized = false;
    } catch (const std::exception& exception_) {
        if (recorder_initialized && runtime_initialized && runtime.IsInitialized()) {
            recorder.Shutdown(runtime.Context());
            recorder_initialized = false;
        }
        if (renderer_initialized && runtime_initialized && runtime.IsInitialized()) {
            text_renderer.Shutdown(runtime.Context());
            renderer_initialized = false;
        }
        if (runtime_initialized && runtime.IsInitialized()) {
            runtime.Shutdown();
            runtime_initialized = false;
        }

        if (IsEnvironmentSkipError(exception_.what())) {
            VR_BENCH_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace


