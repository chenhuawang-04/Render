#include "support/bench_framework.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/runtime/services/frame_composer_service.hpp"
#include "vr/runtime/services/particle_render_service.hpp"
#include "vr/runtime/services/particle_simulation_service.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
using QueueClass = vr::render_graph::QueueClass;

template<typename IdT>
[[nodiscard]] std::vector<IdT> MakeDiagnosticIdVector(
    std::initializer_list<typename IdT::underlying_type> values_) {
    std::vector<IdT> values{};
    values.reserve(values_.size());
    for (const auto value_ : values_) {
        values.emplace_back(value_);
    }
    return values;
}

[[nodiscard]] vr::runtime::RenderGraphQueueBatchDiagnostics MakeQueueBatchDiagnostics(
    const vr::runtime::RenderGraphQueueBatchDiagnosticId::underlying_type batch_id_,
    const QueueClass queue_,
    std::initializer_list<
        vr::runtime::RenderGraphPassDiagnosticId::underlying_type> pass_ids_,
    std::initializer_list<const char*> pass_debug_names_,
    std::initializer_list<
        vr::runtime::RenderGraphQueueDependencyDiagnosticId::underlying_type>
        wait_dependency_ids_ = {},
    std::initializer_list<
        vr::runtime::RenderGraphQueueDependencyDiagnosticId::underlying_type>
        signal_dependency_ids_ = {},
    std::initializer_list<
        vr::runtime::RenderGraphBarrierBatchDiagnosticId::underlying_type>
        barrier_batch_ids_ = {},
    const std::uint32_t submit_wait_count_ = 0U,
    const std::uint32_t submit_signal_count_ = 0U,
    const bool contains_host_boundary_ = false,
    const bool submitted_on_owned_queue_ = false) {
    vr::runtime::RenderGraphQueueBatchDiagnostics diagnostics{};
    diagnostics.batch_id = batch_id_;
    diagnostics.queue = queue_;
    diagnostics.pass_ids =
        MakeDiagnosticIdVector<vr::runtime::RenderGraphPassDiagnosticId>(
            pass_ids_);
    diagnostics.pass_debug_names.assign(pass_debug_names_.begin(),
                                        pass_debug_names_.end());
    diagnostics.wait_dependency_ids =
        MakeDiagnosticIdVector<
            vr::runtime::RenderGraphQueueDependencyDiagnosticId>(
            wait_dependency_ids_);
    diagnostics.signal_dependency_ids =
        MakeDiagnosticIdVector<
            vr::runtime::RenderGraphQueueDependencyDiagnosticId>(
            signal_dependency_ids_);
    diagnostics.barrier_batch_ids =
        MakeDiagnosticIdVector<vr::runtime::RenderGraphBarrierBatchDiagnosticId>(
            barrier_batch_ids_);
    diagnostics.submit_wait_count = submit_wait_count_;
    diagnostics.submit_signal_count = submit_signal_count_;
    diagnostics.contains_host_boundary = contains_host_boundary_;
    diagnostics.submitted_on_owned_queue = submitted_on_owned_queue_;
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphQueueDependencyDiagnostics
MakeQueueDependencyDiagnostics(
    const vr::runtime::RenderGraphQueueDependencyDiagnosticId::underlying_type
        dependency_id_,
    const QueueClass source_queue_,
    const QueueClass target_queue_,
    const vr::runtime::RenderGraphQueueBatchDiagnosticId::underlying_type
        source_batch_id_,
    const vr::runtime::RenderGraphQueueBatchDiagnosticId::underlying_type
        target_batch_id_,
    const vr::runtime::RenderGraphPassDiagnosticId::underlying_type
        source_pass_id_,
    const vr::runtime::RenderGraphPassDiagnosticId::underlying_type
        target_pass_id_,
    const char* source_pass_debug_name_,
    const char* target_pass_debug_name_,
    const std::uint32_t resource_count_,
    const bool queue_transfer_,
    const bool host_boundary_ = false) {
    return vr::runtime::RenderGraphQueueDependencyDiagnostics{
        .dependency_id = dependency_id_,
        .source_queue = source_queue_,
        .target_queue = target_queue_,
        .source_batch_id = source_batch_id_,
        .target_batch_id = target_batch_id_,
        .source_pass_id = source_pass_id_,
        .target_pass_id = target_pass_id_,
        .source_pass_debug_name = source_pass_debug_name_,
        .target_pass_debug_name = target_pass_debug_name_,
        .resource_count = resource_count_,
        .queue_transfer = queue_transfer_,
        .host_boundary = host_boundary_,
    };
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeGraphicsFallbackQueueTimelineDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.graphics_fallback_active = true;
    diagnostics.effective_queue_batch_count = 1U;
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        0U,
        QueueClass::graphics,
        {0U, 1U},
        {"upload_payload", "prepare_present_target"}));
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
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        0U,
        QueueClass::transfer,
        {0U},
        {"text_2d_upload_instances"},
        {},
        {0U},
        {},
        0U,
        1U,
        false,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        1U,
        QueueClass::graphics,
        {1U},
        {"overlay_pass"},
        {0U},
        {},
        {},
        1U));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(0U,
                                      QueueClass::transfer,
                                      QueueClass::graphics,
                                      0U,
                                      1U,
                                      0U,
                                      1U,
                                      "text_2d_upload_instances",
                                      "overlay_pass",
                                      1U,
                                      true));
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
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        0U,
        QueueClass::compute,
        {0U},
        {"particle_2d_gpu_build"},
        {},
        {0U},
        {},
        0U,
        1U,
        false,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        1U,
        QueueClass::graphics,
        {1U},
        {"overlay_pass"},
        {0U},
        {},
        {},
        1U));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(0U,
                                      QueueClass::compute,
                                      QueueClass::graphics,
                                      0U,
                                      1U,
                                      0U,
                                      1U,
                                      "particle_2d_gpu_build",
                                      "overlay_pass",
                                      1U,
                                      true));
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
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        0U,
        QueueClass::transfer,
        {0U},
        {"upload_payload"},
        {},
        {0U},
        {},
        0U,
        1U,
        false,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        1U,
        QueueClass::compute,
        {1U},
        {"simulate_payload"},
        {0U},
        {1U},
        {},
        1U,
        1U,
        false,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        2U,
        QueueClass::graphics,
        {2U},
        {"prepare_present_target"},
        {1U},
        {},
        {},
        2U));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(0U,
                                      QueueClass::transfer,
                                      QueueClass::compute,
                                      0U,
                                      1U,
                                      0U,
                                      1U,
                                      "upload_payload",
                                      "simulate_payload",
                                      1U,
                                      true));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(1U,
                                      QueueClass::compute,
                                      QueueClass::graphics,
                                      1U,
                                      2U,
                                      1U,
                                      2U,
                                      "simulate_payload",
                                      "prepare_present_target",
                                      1U,
                                      true));
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
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        0U,
        QueueClass::graphics,
        {0U},
        {"scene_prepare"},
        {},
        {0U},
        {},
        0U,
        1U,
        false,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        1U,
        QueueClass::compute,
        {1U},
        {"simulate_payload"},
        {0U},
        {1U},
        {},
        1U,
        1U,
        false,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        2U,
        QueueClass::graphics,
        {2U, 3U},
        {"prepare_present_target", "present_transition"},
        {1U},
        {},
        {},
        1U));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(0U,
                                      QueueClass::graphics,
                                      QueueClass::compute,
                                      0U,
                                      1U,
                                      0U,
                                      1U,
                                      "scene_prepare",
                                      "simulate_payload",
                                      1U,
                                      true));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(1U,
                                      QueueClass::compute,
                                      QueueClass::graphics,
                                      1U,
                                      2U,
                                      1U,
                                      2U,
                                      "simulate_payload",
                                      "prepare_present_target",
                                      1U,
                                      true));
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
    diagnostics.dynamic_rendering_local_read_status =
        vr::render_graph::NativePassLocalReadStatus::disabled;
    diagnostics.dynamic_rendering_local_read_reason =
        vr::render_graph::NativePassLocalReadReason::device_feature_not_enabled;
    return diagnostics;
}

[[nodiscard]] vr::render_graph::CompiledRenderGraph BuildAliasingGoldenGraph();

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeTimingCaptureDiagnostics() {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics =
        MakeTransferComputeEnabledQueueTimelineDiagnostics();
    diagnostics.timing.available = true;
    diagnostics.timing.enabled = true;
    diagnostics.timing.domain = vr::runtime::RenderGraphTimingDomain::cpu_record;
    diagnostics.timing.queue_batch_range_count = 3U;
    diagnostics.timing.resolved_queue_batch_range_count = 3U;
    diagnostics.timing.total_duration_ns = 360U;
    diagnostics.timing.queue_batch_ranges.push_back(
        vr::runtime::RenderGraphQueueBatchTimingDiagnostics{
            .batch_id = 0U,
            .queue = QueueClass::transfer,
            .pass_ids = MakeDiagnosticIdVector<vr::runtime::RenderGraphPassDiagnosticId>({0U}),
            .pass_debug_names = {"upload_payload"},
            .signal_dependency_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphQueueDependencyDiagnosticId>({0U}),
            .marker_label = "queue_batch[0]/transfer/upload_payload",
            .relative_begin_ns = 0U,
            .relative_end_ns = 100U,
            .duration_ns = 100U,
            .resolved = true,
        });
    diagnostics.timing.queue_batch_ranges.push_back(
        vr::runtime::RenderGraphQueueBatchTimingDiagnostics{
            .batch_id = 1U,
            .queue = QueueClass::compute,
            .pass_ids = MakeDiagnosticIdVector<vr::runtime::RenderGraphPassDiagnosticId>({1U}),
            .pass_debug_names = {"simulate_payload"},
            .wait_dependency_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphQueueDependencyDiagnosticId>({0U}),
            .signal_dependency_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphQueueDependencyDiagnosticId>({1U}),
            .marker_label = "queue_batch[1]/compute/simulate_payload",
            .relative_begin_ns = 100U,
            .relative_end_ns = 220U,
            .duration_ns = 120U,
            .resolved = true,
        });
    diagnostics.timing.queue_batch_ranges.push_back(
        vr::runtime::RenderGraphQueueBatchTimingDiagnostics{
            .batch_id = 2U,
            .queue = QueueClass::graphics,
            .pass_ids = MakeDiagnosticIdVector<vr::runtime::RenderGraphPassDiagnosticId>({2U}),
            .pass_debug_names = {"present_transition"},
            .wait_dependency_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphQueueDependencyDiagnosticId>({1U}),
            .marker_label = "queue_batch[2]/graphics/present_transition",
            .relative_begin_ns = 220U,
            .relative_end_ns = 360U,
            .duration_ns = 140U,
            .resolved = true,
        });
    diagnostics.capture.available = true;
    diagnostics.capture.enabled = true;
    diagnostics.capture.marker_count = 3U;
    diagnostics.capture.artifact_count = 1U;
    diagnostics.capture.markers.push_back(
        vr::runtime::RenderGraphCaptureMarkerDiagnostics{
            .batch_id = 0U,
            .queue = QueueClass::transfer,
            .pass_ids = MakeDiagnosticIdVector<vr::runtime::RenderGraphPassDiagnosticId>({0U}),
            .pass_debug_names = {"upload_payload"},
            .label = "queue_batch[0]/transfer/upload_payload",
        });
    diagnostics.capture.markers.push_back(
        vr::runtime::RenderGraphCaptureMarkerDiagnostics{
            .batch_id = 1U,
            .queue = QueueClass::compute,
            .pass_ids = MakeDiagnosticIdVector<vr::runtime::RenderGraphPassDiagnosticId>({1U}),
            .pass_debug_names = {"simulate_payload"},
            .label = "queue_batch[1]/compute/simulate_payload",
        });
    diagnostics.capture.markers.push_back(
        vr::runtime::RenderGraphCaptureMarkerDiagnostics{
            .batch_id = 2U,
            .queue = QueueClass::graphics,
            .pass_ids = MakeDiagnosticIdVector<vr::runtime::RenderGraphPassDiagnosticId>({2U}),
            .pass_debug_names = {"present_transition"},
            .label = "queue_batch[2]/graphics/present_transition",
        });
    diagnostics.capture.artifacts.push_back(
        vr::runtime::RenderGraphCaptureArtifactDiagnostics{
            .captured = true,
            .kind = vr::runtime::RenderGraphCaptureArtifactKind::observability_snapshot,
            .artifact_label = "render_graph_capture.frame_0",
            .topology_json =
                vr::render_graph::BuildCompiledRenderGraphTopologyJson(
                    vr::render_graph::BuildCompiledRenderGraphTopologyView(
                        BuildAliasingGoldenGraph())),
            .queue_timeline_json =
                vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics),
        });
    return diagnostics;
}

[[nodiscard]] vr::render_graph::CompiledRenderGraph BuildAliasingGoldenGraph() {
    vr::render_graph::RenderGraphBuilder builder{};
    const auto temp_a = builder.CreateBuffer(
        "temp_a",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto temp_b = builder.CreateBuffer(
        "temp_b",
        vr::render_graph::BufferDesc{
            .size_bytes = 4096U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });

    const auto pass_a = builder.AddPass("pass_a");
    const auto pass_b = builder.AddPass("pass_b", true);
    const auto pass_c = builder.AddPass("pass_c");
    const auto pass_d = builder.AddPass("pass_d", true);

    const auto temp_a_written = builder.Write(
        pass_a,
        temp_a,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_write,
        });
    (void)builder.Read(
        pass_b,
        temp_a_written,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_read,
        });
    const auto temp_b_written = builder.Write(
        pass_c,
        temp_b,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_write,
        });
    (void)builder.Read(
        pass_d,
        temp_b_written,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_read,
        });

    return builder.Compile();
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics MakeTransientSummaryDiagnostics() {
    const auto compiled = BuildAliasingGoldenGraph();
    const auto topology_view =
        vr::render_graph::BuildCompiledRenderGraphTopologyView(compiled);

    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.compiled_pass_count = topology_view.summary.pass_count;
    diagnostics.executable_pass_count = topology_view.summary.executable_pass_count;
    diagnostics.transient_logical_total_bytes =
        topology_view.transient_memory.summary.logical_total_bytes;
    diagnostics.transient_physical_total_bytes =
        topology_view.transient_memory.summary.physical_total_bytes;
    diagnostics.transient_peak_live_bytes =
        topology_view.transient_memory.summary.peak_live_bytes;
    diagnostics.transient_saved_bytes =
        topology_view.transient_memory.summary.saved_bytes;
    diagnostics.transient_page_count =
        topology_view.transient_memory.summary.page_count;
    diagnostics.alias_barrier_count =
        topology_view.transient_memory.summary.alias_barrier_count;
    diagnostics.compile_liveness_ranges = topology_view.liveness_ranges;
    diagnostics.compile_transient_memory = topology_view.transient_memory;
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
            diagnostics_.dynamic_rendering_local_read_status ==
                vr::render_graph::NativePassLocalReadStatus::not_applicable ||
            diagnostics_.dynamic_rendering_local_read_reason ==
                vr::render_graph::NativePassLocalReadReason::none) {
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
            diagnostics_.dynamic_rendering_local_read_status);
        checksum += static_cast<std::uint64_t>(
            diagnostics_.dynamic_rendering_local_read_reason);
        vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::runtime::RenderGraphRuntimeDiagnostics));
    vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    vr::bench::BenchmarkContext::ClobberMemory();
}

void RunTimingCaptureDiagnosticsConsumerBenchmark(
    vr::bench::BenchmarkContext& bench_context_,
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) {
    std::uint64_t checksum = 0U;
    std::uint64_t bytes_processed = 0U;

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const auto observability_view =
            vr::runtime::BuildRenderGraphObservabilityView(diagnostics_);
        if (!observability_view.timing.available ||
            !observability_view.timing.enabled ||
            observability_view.timing.domain !=
                vr::runtime::RenderGraphTimingDomain::cpu_record ||
            observability_view.timing.queue_batch_ranges == nullptr ||
            observability_view.timing.queue_batch_ranges->size() != 3U ||
            observability_view.capture.markers == nullptr ||
            observability_view.capture.markers->size() != 3U ||
            observability_view.capture.artifacts == nullptr ||
            observability_view.capture.artifacts->size() != 1U) {
            throw std::runtime_error(
                "RenderGraph timing/capture diagnostics benchmark validation failed");
        }

        checksum += observability_view.timing.total_duration_ns;
        for (const auto& range_ : *observability_view.timing.queue_batch_ranges) {
            checksum += range_.duration_ns;
            checksum += range_.resolved ? 1U : 0U;
            bytes_processed += static_cast<std::uint64_t>(range_.marker_label.size());
        }
        for (const auto& marker_ : *observability_view.capture.markers) {
            checksum += marker_.pass_ids.size();
            bytes_processed += static_cast<std::uint64_t>(marker_.label.size());
        }
        for (const auto& artifact_ : *observability_view.capture.artifacts) {
            if (artifact_.kind !=
                    vr::runtime::RenderGraphCaptureArtifactKind::observability_snapshot ||
                artifact_.topology_json.empty() ||
                artifact_.queue_timeline_json.empty()) {
                throw std::runtime_error(
                    "RenderGraph timing/capture artifact consumer benchmark validation failed");
            }
            checksum += artifact_.captured ? 1U : 0U;
            bytes_processed += static_cast<std::uint64_t>(artifact_.topology_json.size());
            bytes_processed += static_cast<std::uint64_t>(artifact_.queue_timeline_json.size());
        }
        vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(bytes_processed);
    vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    vr::bench::BenchmarkContext::ClobberMemory();
}

void RunTransientSummaryConsumerBenchmark(
    vr::bench::BenchmarkContext& bench_context_,
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) {
    std::uint64_t checksum = 0U;
    const std::uint64_t iterations = bench_context_.Iterations();

    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const auto observability_view =
            vr::runtime::BuildRenderGraphObservabilityView(diagnostics_);
        if (observability_view.compile.transient_memory == nullptr ||
            observability_view.compile.liveness_ranges == nullptr ||
            observability_view.compile.transient_memory->summary.saved_bytes !=
                4096U ||
            observability_view.compile.transient_memory->summary.page_count !=
                1U ||
            observability_view.compile.transient_memory->summary.alias_barrier_count !=
                1U ||
            observability_view.compile.liveness_ranges->size() != 4U) {
            throw std::runtime_error(
                "RenderGraph transient summary consumer benchmark validation failed");
        }

        const auto& transient_memory =
            *observability_view.compile.transient_memory;
        checksum += transient_memory.summary.logical_total_bytes;
        checksum += transient_memory.summary.physical_total_bytes;
        checksum += transient_memory.summary.saved_bytes;
        checksum += transient_memory.summary.page_count;
        checksum += transient_memory.summary.alias_barrier_count;
        checksum += static_cast<std::uint64_t>(
            observability_view.compile.liveness_ranges->size());
        checksum += static_cast<std::uint64_t>(transient_memory.records.size());
        checksum += static_cast<std::uint64_t>(transient_memory.pages.size());
        checksum += static_cast<std::uint64_t>(
            transient_memory.timeline_samples.size());
        vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(
        iterations * sizeof(vr::runtime::RenderGraphRuntimeDiagnostics));
    vr::bench::BenchmarkContext::DoNotOptimize(checksum);
    vr::bench::BenchmarkContext::ClobberMemory();
}

void RunDiagnosticsLevelPredicateBenchmark(
    vr::bench::BenchmarkContext& bench_context_) {
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
        score += vr::runtime::DiagnosticsCollectsGpuTiming(level) ? 1U : 0U;
        score += vr::runtime::DiagnosticsCollectsCapture(level) ? 1U : 0U;
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(vr::runtime::DiagnosticsLevel));
    vr::bench::BenchmarkContext::DoNotOptimize(score);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(RuntimeDiagnostics_level_predicates, "core;runtime;cpu") {
    RunDiagnosticsLevelPredicateBenchmark(bench_context_);
}

VR_BENCHMARK_CASE(RenderGraphLevelPredicateConsumer_levels,
                  "core;runtime;cpu;render_graph;diagnostics;consumer;phase13") {
    RunDiagnosticsLevelPredicateBenchmark(bench_context_);
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

VR_BENCHMARK_CASE(RenderGraphTimingCaptureDiagnosticsConsumer_queue_batches,
                  "core;runtime;cpu;render_graph;timing;capture;diagnostics;phase12") {
    RunTimingCaptureDiagnosticsConsumerBenchmark(
        bench_context_,
        MakeTimingCaptureDiagnostics());
}

VR_BENCHMARK_CASE(RenderGraphTransientSummaryConsumer_aliasing_summary,
                  "core;runtime;cpu;render_graph;transient;liveness;aliasing;diagnostics;consumer;phase13") {
    RunTransientSummaryConsumerBenchmark(
        bench_context_,
        MakeTransientSummaryDiagnostics());
}

} // namespace

