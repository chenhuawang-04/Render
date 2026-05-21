#include "support/bench_framework.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/runtime/services/frame_composer_service.hpp"
#include "vr/runtime/services/particle_render_service.hpp"
#include "vr/runtime/services/particle_simulation_service.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeGraphicsFallbackQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.graphics_fallback_active = true;
    diagnostics.effective_queue_batch_count = 1U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 0U,
            .queue_name = "graphics",
            .pass_indices = {0U, 1U},
            .pass_debug_names = {"upload_payload", "prepare_present_target"},
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeTransferEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.transfer_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 2U;
    diagnostics.effective_queue_dependency_count = 1U;
    diagnostics.graphics_submit_wait_count = 1U;
    diagnostics.non_graphics_submit_batch_count = 1U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 0U,
            .queue_name = "transfer",
            .pass_indices = {0U},
            .pass_debug_names = {"text_2d_upload_instances"},
            .signal_dependency_indices = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 1U,
            .queue_name = "graphics",
            .pass_indices = {1U},
            .pass_debug_names = {"overlay_pass"},
            .wait_dependency_indices = {0U},
            .submit_wait_count = 1U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_index = 0U,
            .source_queue_name = "transfer",
            .target_queue_name = "graphics",
            .source_batch_index = 0U,
            .target_batch_index = 1U,
            .source_pass_index = 0U,
            .target_pass_index = 1U,
            .source_pass_debug_name = "text_2d_upload_instances",
            .target_pass_debug_name = "overlay_pass",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeComputeEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.compute_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 2U;
    diagnostics.effective_queue_dependency_count = 1U;
    diagnostics.graphics_submit_wait_count = 1U;
    diagnostics.non_graphics_submit_batch_count = 1U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 0U,
            .queue_name = "compute",
            .pass_indices = {0U},
            .pass_debug_names = {"particle_2d_gpu_build"},
            .signal_dependency_indices = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 1U,
            .queue_name = "graphics",
            .pass_indices = {1U},
            .pass_debug_names = {"overlay_pass"},
            .wait_dependency_indices = {0U},
            .submit_wait_count = 1U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_index = 0U,
            .source_queue_name = "compute",
            .target_queue_name = "graphics",
            .source_batch_index = 0U,
            .target_batch_index = 1U,
            .source_pass_index = 0U,
            .target_pass_index = 1U,
            .source_pass_debug_name = "particle_2d_gpu_build",
            .target_pass_debug_name = "overlay_pass",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeTransferComputeEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.transfer_queue_enabled = true;
    diagnostics.compute_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 3U;
    diagnostics.effective_queue_dependency_count = 2U;
    diagnostics.graphics_submit_wait_count = 2U;
    diagnostics.non_graphics_submit_batch_count = 2U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 0U,
            .queue_name = "transfer",
            .pass_indices = {0U},
            .pass_debug_names = {"upload_payload"},
            .signal_dependency_indices = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 1U,
            .queue_name = "compute",
            .pass_indices = {1U},
            .pass_debug_names = {"simulate_payload"},
            .wait_dependency_indices = {0U},
            .signal_dependency_indices = {1U},
            .submit_wait_count = 1U,
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 2U,
            .queue_name = "graphics",
            .pass_indices = {2U},
            .pass_debug_names = {"prepare_present_target"},
            .wait_dependency_indices = {1U},
            .submit_wait_count = 2U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_index = 0U,
            .source_queue_name = "transfer",
            .target_queue_name = "compute",
            .source_batch_index = 0U,
            .target_batch_index = 1U,
            .source_pass_index = 0U,
            .target_pass_index = 1U,
            .source_pass_debug_name = "upload_payload",
            .target_pass_debug_name = "simulate_payload",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_index = 1U,
            .source_queue_name = "compute",
            .target_queue_name = "graphics",
            .source_batch_index = 1U,
            .target_batch_index = 2U,
            .source_pass_index = 1U,
            .target_pass_index = 2U,
            .source_pass_debug_name = "simulate_payload",
            .target_pass_debug_name = "prepare_present_target",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeMultiGraphicsComputeEnabledQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.compute_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 3U;
    diagnostics.effective_queue_dependency_count = 2U;
    diagnostics.graphics_submit_wait_count = 1U;
    diagnostics.non_graphics_submit_batch_count = 2U;
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 0U,
            .queue_name = "graphics",
            .pass_indices = {0U},
            .pass_debug_names = {"scene_prepare"},
            .signal_dependency_indices = {0U},
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 1U,
            .queue_name = "compute",
            .pass_indices = {1U},
            .pass_debug_names = {"simulate_payload"},
            .wait_dependency_indices = {0U},
            .signal_dependency_indices = {1U},
            .submit_wait_count = 1U,
            .submit_signal_count = 1U,
            .submitted_on_owned_queue = true,
        });
    diagnostics.effective_queue_batches.push_back(
        vr::runtime::RenderGraphQueueBatchDiagnostics{
            .batch_index = 2U,
            .queue_name = "graphics",
            .pass_indices = {2U, 3U},
            .pass_debug_names = {"prepare_present_target", "present_transition"},
            .wait_dependency_indices = {1U},
            .submit_wait_count = 1U,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_index = 0U,
            .source_queue_name = "graphics",
            .target_queue_name = "compute",
            .source_batch_index = 0U,
            .target_batch_index = 1U,
            .source_pass_index = 0U,
            .target_pass_index = 1U,
            .source_pass_debug_name = "scene_prepare",
            .target_pass_debug_name = "simulate_payload",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    diagnostics.effective_queue_dependencies.push_back(
        vr::runtime::RenderGraphQueueDependencyDiagnostics{
            .dependency_index = 1U,
            .source_queue_name = "compute",
            .target_queue_name = "graphics",
            .source_batch_index = 1U,
            .target_batch_index = 2U,
            .source_pass_index = 1U,
            .target_pass_index = 2U,
            .source_pass_debug_name = "simulate_payload",
            .target_pass_debug_name = "prepare_present_target",
            .resource_count = 1U,
            .queue_transfer = true,
        });
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeFusedNativePassDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.dynamic_rendering_local_read_supported = true;
    diagnostics.dynamic_rendering_local_read_requested = true;
    diagnostics.dynamic_rendering_local_read_enabled = false;
    diagnostics.compiled_pass_count = 6U;
    diagnostics.executable_pass_count = 6U;
    diagnostics.logical_raster_pass_count = 5U;
    diagnostics.native_pass_group_count = 4U;
    diagnostics.fused_raster_pass_count = 1U;
    diagnostics.recorded_pass_count = 6U;
    diagnostics.recorded_rendering_scope_count = 4U;
    diagnostics.store_elision_count = 1U;
    diagnostics.load_inference_count = 2U;
    diagnostics.effective_clear_attachment_count = 2U;
    diagnostics.local_read_candidate_count = 1U;
    diagnostics.dynamic_rendering_local_read_status = "disabled";
    diagnostics.dynamic_rendering_local_read_reason =
        "device_feature_not_enabled";
    return diagnostics;
}

void RunQueueTimelineConsumerBenchmark(
    vr::bench::BenchmarkContext& bench_context_,
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
    const vr::runtime::RenderGraphQueueTimelineMode expected_mode_,
    const std::uint32_t expected_graphics_batches_,
    const std::uint32_t expected_transfer_batches_,
    const std::uint32_t expected_compute_batches_,
    const std::uint32_t expected_cross_queue_dependencies_) {
    std::uint64_t checksum = 0U;
    std::uint64_t bytes_processed = 0U;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const auto view = vr::runtime::BuildRenderGraphQueueTimelineView(diagnostics_);
        const std::string json = vr::runtime::BuildRenderGraphQueueTimelineJson(view);
        if (!view.available ||
            view.mode != expected_mode_ ||
            view.graphics_batch_count != expected_graphics_batches_ ||
            view.transfer_batch_count != expected_transfer_batches_ ||
            view.compute_batch_count != expected_compute_batches_ ||
            view.cross_queue_dependency_count != expected_cross_queue_dependencies_ ||
            json.empty()) {
            throw std::runtime_error("RenderGraph queue timeline consumer benchmark validation failed");
        }

        checksum += static_cast<std::uint64_t>(json.size()) +
                    view.batch_count +
                    view.dependency_count +
                    view.total_submit_wait_count +
                    view.total_submit_signal_count;
        bytes_processed += static_cast<std::uint64_t>(json.size());
        vr::bench::BenchmarkContext::DoNotOptimize(checksum);
        vr::bench::BenchmarkContext::DoNotOptimize(json);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(bytes_processed);
    vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    vr::bench::BenchmarkContext::ClobberMemory();
}

void RunNativePassDiagnosticsConsumerBenchmark(
    vr::bench::BenchmarkContext& bench_context_,
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) {
    std::uint64_t checksum = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        if (!diagnostics_.available ||
            !diagnostics_.frame_compiled ||
            diagnostics_.native_pass_group_count >= diagnostics_.logical_raster_pass_count ||
            diagnostics_.recorded_rendering_scope_count >=
                diagnostics_.logical_raster_pass_count ||
            diagnostics_.fused_raster_pass_count == 0U ||
            diagnostics_.store_elision_count == 0U ||
            diagnostics_.local_read_candidate_count == 0U ||
            diagnostics_.dynamic_rendering_local_read_status.empty() ||
            diagnostics_.dynamic_rendering_local_read_reason.empty()) {
            throw std::runtime_error(
                "RenderGraph native-pass diagnostics benchmark validation failed");
        }

        checksum += diagnostics_.compiled_pass_count;
        checksum += diagnostics_.logical_raster_pass_count;
        checksum += diagnostics_.native_pass_group_count;
        checksum += diagnostics_.fused_raster_pass_count;
        checksum += diagnostics_.recorded_rendering_scope_count;
        checksum += diagnostics_.store_elision_count;
        checksum += diagnostics_.load_inference_count;
        checksum += diagnostics_.effective_clear_attachment_count;
        checksum += diagnostics_.local_read_candidate_count;
        checksum += diagnostics_.dynamic_rendering_local_read_supported ? 1U : 0U;
        checksum += diagnostics_.dynamic_rendering_local_read_requested ? 1U : 0U;
        checksum += diagnostics_.dynamic_rendering_local_read_enabled ? 1U : 0U;
        checksum += static_cast<std::uint64_t>(
            diagnostics_.dynamic_rendering_local_read_status.size() +
            diagnostics_.dynamic_rendering_local_read_reason.size());
        vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::runtime::RenderGraphRuntimeDiagnostics));
    vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RuntimeDiagnostics_level_predicates, "core;runtime;cpu") {
    constexpr std::array<vr::runtime::DiagnosticsLevel, 5U> k_levels{
        vr::runtime::DiagnosticsLevel::Off,
        vr::runtime::DiagnosticsLevel::CountersOnly,
        vr::runtime::DiagnosticsLevel::Detailed,
        vr::runtime::DiagnosticsLevel::GpuTiming,
        vr::runtime::DiagnosticsLevel::Capture,
    };

    std::uint64_t score = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const auto level = k_levels[static_cast<std::size_t>(i % k_levels.size())];
        score += vr::runtime::DiagnosticsCollectsFrameData(level) ? 1U : 0U;
        score += vr::runtime::DiagnosticsCollectsServiceCounters(level) ? 1U : 0U;
        score += vr::runtime::DiagnosticsCollectsDetailedData(level) ? 1U : 0U;
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::runtime::DiagnosticsLevel));
    vr::bench::BenchmarkContext::DoNotOptimize(score);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RuntimeServices_profiled_try_get_particle_services, "core;runtime;cpu") {
    Runtime runtime{};
    auto& services = runtime.Services();

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        auto* particle_render =
            services.TryGet<vr::runtime::services::ParticleRenderService>();
        auto* particle_simulation =
            services.TryGet<vr::runtime::services::ParticleSimulationService>();
        auto* frame_composer =
            services.TryGet<vr::runtime::services::FrameComposerService>();
        vr::bench::BenchmarkContext::DoNotOptimize(particle_render);
        vr::bench::BenchmarkContext::DoNotOptimize(particle_simulation);
        vr::bench::BenchmarkContext::DoNotOptimize(frame_composer);
    }

    bench_context_.AddItems(iterations * 3U);
    bench_context_.AddBytes(iterations * sizeof(void*) * 3U);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RenderGraphQueueTimelineConsumer_graphics_fallback,
                  "core;runtime;cpu;render_graph;queue;diagnostics;phase10") {
    RunQueueTimelineConsumerBenchmark(
        bench_context_,
        MakeGraphicsFallbackQueueTimelineDiagnostics(),
        vr::runtime::RenderGraphQueueTimelineMode::graphics_fallback,
        1U,
        0U,
        0U,
        0U);
}

VR_BENCHMARK_CASE(RenderGraphQueueTimelineConsumer_transfer_enabled,
                  "core;runtime;cpu;render_graph;queue;diagnostics;phase10") {
    RunQueueTimelineConsumerBenchmark(
        bench_context_,
        MakeTransferEnabledQueueTimelineDiagnostics(),
        vr::runtime::RenderGraphQueueTimelineMode::transfer_enabled,
        1U,
        1U,
        0U,
        1U);
}

VR_BENCHMARK_CASE(RenderGraphQueueTimelineConsumer_compute_enabled,
                  "core;runtime;cpu;render_graph;queue;diagnostics;phase10") {
    RunQueueTimelineConsumerBenchmark(
        bench_context_,
        MakeComputeEnabledQueueTimelineDiagnostics(),
        vr::runtime::RenderGraphQueueTimelineMode::compute_enabled,
        1U,
        0U,
        1U,
        1U);
}

VR_BENCHMARK_CASE(RenderGraphQueueTimelineConsumer_transfer_compute_enabled,
                  "core;runtime;cpu;render_graph;queue;diagnostics;phase10") {
    RunQueueTimelineConsumerBenchmark(
        bench_context_,
        MakeTransferComputeEnabledQueueTimelineDiagnostics(),
        vr::runtime::RenderGraphQueueTimelineMode::transfer_compute_enabled,
        1U,
        1U,
        1U,
        2U);
}

VR_BENCHMARK_CASE(RenderGraphQueueTimelineConsumer_multi_graphics_compute_enabled,
                  "core;runtime;cpu;render_graph;queue;diagnostics;phase10") {
    RunQueueTimelineConsumerBenchmark(
        bench_context_,
        MakeMultiGraphicsComputeEnabledQueueTimelineDiagnostics(),
        vr::runtime::RenderGraphQueueTimelineMode::compute_enabled,
        2U,
        0U,
        1U,
        2U);
}

VR_BENCHMARK_CASE(RenderGraphNativePassDiagnosticsConsumer_fused_scene,
                  "core;runtime;cpu;render_graph;native_pass;diagnostics;phase11") {
    RunNativePassDiagnosticsConsumerBenchmark(
        bench_context_,
        MakeFusedNativePassDiagnostics());
}

} // namespace

