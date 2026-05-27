#include "support/test_framework.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/runtime/profiles/minimal_profile.hpp"
#include "vr/runtime/profiles/runtime_2d_profile.hpp"
#include "vr/runtime/profiles/runtime_3d_profile.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/runtime_services.hpp"
#include "vr/runtime/services/command_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/frame_composer_service.hpp"
#include "vr/runtime/services/freetype_service.hpp"
#include "vr/runtime/services/glyph_atlas_service.hpp"
#include "vr/runtime/services/glyph_upload_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/ibl_bake_service.hpp"
#include "vr/runtime/services/ibl_service.hpp"
#include "vr/runtime/services/particle_render_service.hpp"
#include "vr/runtime/services/particle_simulation_service.hpp"
#include "vr/runtime/services/particle_upload_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/sky_environment_service.hpp"
#include "vr/runtime/services/texture_service.hpp"
#include "vr/runtime/services/upload_service.hpp"
#include "vr/render/scene_submission.hpp"
#include "vr/render_graph/compiled_render_graph_observability.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/runtime/runtime_ingress_ids.hpp"
#include "vr/text/text_runtime_contract.hpp"
#include "vr/vulkan_context.hpp"

#include <functional>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;

#ifndef VR_TEST_SOURCE_DIR
#error "VR_TEST_SOURCE_DIR must be defined for runtime configuration tests"
#endif

[[nodiscard]] std::filesystem::path SourceRoot() {
    return std::filesystem::path{VR_TEST_SOURCE_DIR};
}

[[nodiscard]] std::string ReadUtf8TextFile(const std::filesystem::path& path_) {
    std::ifstream stream(path_, std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Failed to open file: " + path_.string());
    }

    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string NormalizeNewlines(std::string text_) {
    std::size_t position = 0U;
    while ((position = text_.find("\r\n", position)) != std::string::npos) {
        text_.replace(position, 2U, "\n");
        ++position;
    }
    return text_;
}

[[nodiscard]] bool Contains(std::string_view haystack_,
                            std::string_view needle_) noexcept {
    return haystack_.find(needle_) != std::string_view::npos;
}

void WriteUtf8TextFile(const std::filesystem::path& path_,
                       const std::string& text_) {
    std::filesystem::create_directories(path_.parent_path());
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " +
                                 path_.string());
    }
    stream.write(text_.data(),
                 static_cast<std::streamsize>(text_.size()));
    if (!stream.good()) {
        throw std::runtime_error("Failed to write file: " + path_.string());
    }
}

[[nodiscard]] bool ShouldUpdateGoldens() noexcept {
    char* value = nullptr;
    std::size_t value_size = 0U;
    if (_dupenv_s(&value, &value_size, "VR_UPDATE_GOLDENS") != 0 ||
        value == nullptr) {
        return false;
    }

    const std::string_view view{value};
    const bool enabled = !view.empty() && view != "0";
    std::free(value);
    return enabled;
}

[[nodiscard]] std::filesystem::path RenderGraphGoldenRoot() {
    return SourceRoot() / "tests" / "goldens" / "render_graph_observability";
}

[[nodiscard]] std::string LoadGoldenText(
    const std::filesystem::path& relative_path_) {
    return NormalizeNewlines(
        ReadUtf8TextFile(RenderGraphGoldenRoot() / relative_path_));
}

void SyncGoldenTextIfRequested(const std::filesystem::path& relative_path_,
                               const std::string& actual_text_) {
    if (!ShouldUpdateGoldens()) {
        return;
    }
    WriteUtf8TextFile(RenderGraphGoldenRoot() / relative_path_,
                      NormalizeNewlines(actual_text_));
}

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

[[nodiscard]] vr::runtime::RenderGraphQueueBatchDiagnostics
MakeQueueBatchDiagnostics(
    const vr::runtime::RenderGraphQueueBatchDiagnosticId::underlying_type
        batch_id_,
    const vr::render_graph::QueueClass queue_,
    std::initializer_list<vr::runtime::RenderGraphPassDiagnosticId::underlying_type>
        pass_ids_,
    std::initializer_list<const char*> pass_debug_names_,
    std::initializer_list<
        vr::runtime::RenderGraphQueueDependencyDiagnosticId::underlying_type>
        wait_dependency_ids_ = {},
    std::initializer_list<
        vr::runtime::RenderGraphQueueDependencyDiagnosticId::underlying_type>
        signal_dependency_ids_ = {},
    const std::uint32_t submit_wait_count_ = 0U,
    const std::uint32_t submit_signal_count_ = 0U,
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
    diagnostics.submit_wait_count = submit_wait_count_;
    diagnostics.submit_signal_count = submit_signal_count_;
    diagnostics.submitted_on_owned_queue = submitted_on_owned_queue_;
    return diagnostics;
}

[[nodiscard]] vr::runtime::RenderGraphQueueDependencyDiagnostics
MakeQueueDependencyDiagnostics(
    const vr::runtime::RenderGraphQueueDependencyDiagnosticId::underlying_type
        dependency_id_,
    const vr::render_graph::QueueClass source_queue_,
    const vr::render_graph::QueueClass target_queue_,
    const vr::runtime::RenderGraphQueueBatchDiagnosticId::underlying_type
        source_batch_id_,
    const vr::runtime::RenderGraphQueueBatchDiagnosticId::underlying_type
        target_batch_id_,
    const vr::runtime::RenderGraphPassDiagnosticId::underlying_type
        source_pass_id_,
    const vr::runtime::RenderGraphPassDiagnosticId::underlying_type
        target_pass_id_,
    const char* source_pass_debug_name_,
    const char* target_pass_debug_name_) {
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
        .resource_count = 1U,
        .queue_transfer = true,
    };
}

[[nodiscard]] vr::runtime::RenderGraphRuntimeDiagnostics
MakeCanonicalObservabilityDiagnosticsForTesting() {
    const auto compiled = BuildAliasingGoldenGraph();
    const auto topology_view =
        vr::render_graph::BuildCompiledRenderGraphTopologyView(compiled);

    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.compiled_pass_count = topology_view.summary.pass_count;
    diagnostics.executable_pass_count =
        topology_view.summary.executable_pass_count;
    diagnostics.logical_raster_pass_count =
        topology_view.summary.logical_raster_pass_count;
    diagnostics.native_pass_group_count =
        topology_view.summary.native_pass_group_count;
    diagnostics.fused_raster_pass_count =
        topology_view.summary.fused_raster_pass_count;
    diagnostics.store_elision_count = topology_view.summary.store_elision_count;
    diagnostics.load_inference_count =
        topology_view.summary.load_inference_count;
    diagnostics.effective_clear_attachment_count =
        topology_view.summary.clear_attachment_count;
    diagnostics.local_read_candidate_count =
        topology_view.summary.local_read_candidate_count;
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

    diagnostics.transfer_queue_requested = true;
    diagnostics.compute_queue_requested = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.transfer_queue_enabled = true;
    diagnostics.compute_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 3U;
    diagnostics.effective_queue_dependency_count = 2U;
    diagnostics.effective_graphics_queue_batch_count = 1U;
    diagnostics.effective_transfer_queue_batch_count = 1U;
    diagnostics.effective_compute_queue_batch_count = 1U;
    diagnostics.effective_owned_submit_batch_count = 2U;
    diagnostics.effective_cross_queue_dependency_count = 2U;
    diagnostics.effective_total_submit_wait_count = 3U;
    diagnostics.effective_total_submit_signal_count = 2U;
    diagnostics.graphics_submit_wait_count = 2U;
    diagnostics.non_graphics_submit_batch_count = 2U;
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        0U,
        vr::render_graph::QueueClass::transfer,
        {0U},
        {"upload_payload"},
        {},
        {0U},
        0U,
        1U,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        1U,
        vr::render_graph::QueueClass::compute,
        {1U},
        {"simulate_payload"},
        {0U},
        {1U},
        1U,
        1U,
        true));
    diagnostics.effective_queue_batches.push_back(MakeQueueBatchDiagnostics(
        2U,
        vr::render_graph::QueueClass::graphics,
        {2U},
        {"prepare_present_target"},
        {1U},
        {},
        2U,
        0U,
        false));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(0U,
                                      vr::render_graph::QueueClass::transfer,
                                      vr::render_graph::QueueClass::compute,
                                      0U,
                                      1U,
                                      0U,
                                      1U,
                                      "upload_payload",
                                      "simulate_payload"));
    diagnostics.effective_queue_dependencies.push_back(
        MakeQueueDependencyDiagnostics(1U,
                                      vr::render_graph::QueueClass::compute,
                                      vr::render_graph::QueueClass::graphics,
                                      1U,
                                      2U,
                                      1U,
                                      2U,
                                      "simulate_payload",
                                      "prepare_present_target"));

    diagnostics.timing.available = true;
    diagnostics.timing.enabled = true;
    diagnostics.timing.domain = vr::runtime::RenderGraphTimingDomain::cpu_record;
    diagnostics.timing.queue_batch_range_count = 3U;
    diagnostics.timing.resolved_queue_batch_range_count = 3U;
    diagnostics.timing.total_duration_ns = 360U;
    diagnostics.timing.queue_batch_ranges.push_back(
        vr::runtime::RenderGraphQueueBatchTimingDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::transfer,
            .pass_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphPassDiagnosticId>({0U}),
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
            .queue = vr::render_graph::QueueClass::compute,
            .pass_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphPassDiagnosticId>({1U}),
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
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphPassDiagnosticId>({2U}),
            .pass_debug_names = {"prepare_present_target"},
            .wait_dependency_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphQueueDependencyDiagnosticId>({1U}),
            .marker_label = "queue_batch[2]/graphics/prepare_present_target",
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
            .queue = vr::render_graph::QueueClass::transfer,
            .pass_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphPassDiagnosticId>({0U}),
            .pass_debug_names = {"upload_payload"},
            .label = "queue_batch[0]/transfer/upload_payload",
        });
    diagnostics.capture.markers.push_back(
        vr::runtime::RenderGraphCaptureMarkerDiagnostics{
            .batch_id = 1U,
            .queue = vr::render_graph::QueueClass::compute,
            .pass_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphPassDiagnosticId>({1U}),
            .pass_debug_names = {"simulate_payload"},
            .label = "queue_batch[1]/compute/simulate_payload",
        });
    diagnostics.capture.markers.push_back(
        vr::runtime::RenderGraphCaptureMarkerDiagnostics{
            .batch_id = 2U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = MakeDiagnosticIdVector<
                vr::runtime::RenderGraphPassDiagnosticId>({2U}),
            .pass_debug_names = {"prepare_present_target"},
            .label = "queue_batch[2]/graphics/prepare_present_target",
        });

    diagnostics.capture.artifacts.push_back(
        vr::runtime::RenderGraphCaptureArtifactDiagnostics{
            .captured = true,
            .kind = vr::runtime::RenderGraphCaptureArtifactKind::observability_snapshot,
            .artifact_label = "render_graph_capture.frame_0",
            .topology_json =
                vr::render_graph::BuildCompiledRenderGraphTopologyJson(
                    topology_view),
            .queue_timeline_json =
                vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics),
        });
    return diagnostics;
}

struct RenderGraphConsumerDigest final {
    std::uint32_t compile_pass_count = 0U;
    std::uint32_t liveness_range_count = 0U;
    std::uint64_t transient_saved_bytes = 0U;
    std::uint32_t submission_batch_count = 0U;
    std::uint32_t cross_queue_dependency_count = 0U;
    std::uint32_t timing_range_count = 0U;
    std::uint64_t timing_total_duration_ns = 0U;
    std::uint32_t capture_artifact_count = 0U;
    std::string first_timing_label{};
    std::string first_artifact_label{};
};

[[nodiscard]] RenderGraphConsumerDigest ConsumeRenderGraphObservability(
    const vr::runtime::RenderGraphObservabilityView& view_) {
    RenderGraphConsumerDigest digest{};
    digest.compile_pass_count = view_.compile.compiled_pass_count;
    digest.submission_batch_count =
        view_.submission.effective_queue_batch_count;
    digest.cross_queue_dependency_count =
        view_.submission.effective_cross_queue_dependency_count;
    digest.timing_total_duration_ns = view_.timing.total_duration_ns;

    if (view_.compile.liveness_ranges != nullptr) {
        digest.liveness_range_count =
            static_cast<std::uint32_t>(view_.compile.liveness_ranges->size());
    }
    if (view_.compile.transient_memory != nullptr) {
        digest.transient_saved_bytes =
            view_.compile.transient_memory->summary.saved_bytes;
    }
    if (view_.timing.queue_batch_ranges != nullptr) {
        digest.timing_range_count = static_cast<std::uint32_t>(
            view_.timing.queue_batch_ranges->size());
        if (!view_.timing.queue_batch_ranges->empty()) {
            digest.first_timing_label =
                view_.timing.queue_batch_ranges->front().marker_label;
        }
    }
    if (view_.capture.artifacts != nullptr) {
        digest.capture_artifact_count = static_cast<std::uint32_t>(
            view_.capture.artifacts->size());
        if (!view_.capture.artifacts->empty()) {
            digest.first_artifact_label =
                view_.capture.artifacts->front().artifact_label;
        }
    }
    return digest;
}

struct LifecycleEventLog final {
    std::array<int, 16> events{};
    std::uint32_t count = 0U;

    void Push(const int value_) noexcept {
        if (count < events.size()) {
            events[count++] = value_;
        }
    }
};

inline LifecycleEventLog* g_lifecycle_log = nullptr;

struct MockLifecycleServiceA final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "MockLifecycleServiceA";

    template<typename ContextT>
    void Initialize(ContextT&) {
        g_lifecycle_log->Push(1);
    }

    template<typename ContextT>
    void PostInitialize(ContextT&) {
        g_lifecycle_log->Push(4);
    }

    template<typename ContextT>
    void Shutdown(ContextT&) {
        g_lifecycle_log->Push(9);
    }
};

struct MockLifecycleServiceB final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<MockLifecycleServiceA>;
    static constexpr std::string_view Name = "MockLifecycleServiceB";

    template<typename ContextT>
    void Initialize(ContextT&) {
        g_lifecycle_log->Push(2);
    }

    template<typename ContextT>
    void PostInitialize(ContextT&) {
        g_lifecycle_log->Push(5);
    }

    template<typename ContextT>
    void Shutdown(ContextT&) {
        g_lifecycle_log->Push(8);
    }
};

struct MockLifecycleServiceC final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<MockLifecycleServiceA, MockLifecycleServiceB>;
    static constexpr std::string_view Name = "MockLifecycleServiceC";

    template<typename ContextT>
    void Initialize(ContextT&) {
        g_lifecycle_log->Push(3);
    }

    template<typename ContextT>
    void PostInitialize(ContextT&) {
        g_lifecycle_log->Push(6);
    }

    template<typename ContextT>
    void Shutdown(ContextT&) {
        g_lifecycle_log->Push(7);
    }
};

struct MockPhaseServiceA final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "MockPhaseServiceA";

    template<typename ContextT>
    void BeginFrame(ContextT&) {
        g_lifecycle_log->Push(1);
    }

    template<typename ContextT>
    void PrepareFrame(ContextT&) {
        g_lifecycle_log->Push(3);
    }

    template<typename ContextT>
    void PreRecord(ContextT&) {
        g_lifecycle_log->Push(5);
    }

    template<typename ContextT>
    void PostRecord(ContextT&) {
        g_lifecycle_log->Push(6);
    }

    template<typename ContextT>
    void EndFrame(ContextT&) {
        g_lifecycle_log->Push(9);
    }

    template<typename ContextT>
    void Retire(ContextT&) {
        g_lifecycle_log->Push(11);
    }
};

struct MockPhaseServiceB final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<MockPhaseServiceA>;
    static constexpr std::string_view Name = "MockPhaseServiceB";

    template<typename ContextT>
    void BeginFrame(ContextT&) {
        g_lifecycle_log->Push(2);
    }

    template<typename ContextT>
    void PrepareFrame(ContextT&) {
        g_lifecycle_log->Push(4);
    }

    template<typename ContextT>
    void PreRecord(ContextT&) {
        g_lifecycle_log->Push(7);
    }

    template<typename ContextT>
    void PostRecord(ContextT&) {
        g_lifecycle_log->Push(8);
    }

    template<typename ContextT>
    void EndFrame(ContextT&) {
        g_lifecycle_log->Push(10);
    }

    template<typename ContextT>
    void Retire(ContextT&) {
        g_lifecycle_log->Push(12);
    }
};

VR_TEST_CASE(RuntimeConfig_modules_default_to_enabled, "unit;core;runtime") {
    vr::runtime::RuntimeModulesCreateInfo modules{};
    VR_CHECK(modules.enable_texture_host);
    VR_CHECK(modules.enable_frame_composer_host);
    VR_CHECK(modules.enable_ibl_host);
    VR_CHECK(!modules.enable_ibl_bake_host);
    VR_CHECK(modules.enable_sky_environment_gpu_host);
    VR_CHECK(modules.enable_upload_host);
    VR_CHECK(modules.enable_descriptor_host);
    VR_CHECK(modules.enable_pipeline_host);
    VR_CHECK(modules.enable_render_target_host);
    VR_CHECK(modules.enable_sampler_host);
    VR_CHECK(modules.enable_freetype_host);
    VR_CHECK(modules.enable_glyph_atlas_host);
    VR_CHECK(modules.enable_glyph_upload_host);
    VR_CHECK(modules.enable_particle_upload_host);
    VR_CHECK(modules.enable_particle_simulation_host);
}

VR_TEST_CASE(RuntimeConfig_runtime_ingress_ids_define_canonical_zero_invalid_contract,
             "unit;core;runtime;contract;ingress") {
    static_assert(std::is_trivially_copyable_v<vr::asset::TextureId>);
    static_assert(std::is_trivially_copyable_v<vr::geometry::GeometryResourceId>);
    static_assert(std::is_trivially_copyable_v<vr::geometry::GeometryImageId>);
    static_assert(std::is_trivially_copyable_v<vr::surface::SurfaceImageId>);
    static_assert(std::is_trivially_copyable_v<vr::geometry::GeometryAppearanceId>);
    static_assert(std::is_trivially_copyable_v<vr::render::IblEnvironmentId>);
    static_assert(std::is_trivially_copyable_v<vr::render::SceneSubmissionId>);
    static_assert(std::is_standard_layout_v<vr::render::SceneSubmissionMetadata>);
    static_assert(std::is_standard_layout_v<vr::render::SceneSubmissionViewSelection>);
    static_assert(std::is_standard_layout_v<vr::render::SceneSubmissionSchema<vr::ecs::Dim2>>);
    static_assert(std::is_standard_layout_v<vr::render::SceneSubmissionSchema<vr::ecs::Dim3>>);
    static_assert(std::is_standard_layout_v<vr::render_graph::FrameViewSchema2D>);
    static_assert(std::is_standard_layout_v<vr::render_graph::FrameViewSchema3D>);
    static_assert(sizeof(vr::asset::TextureId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::geometry::GeometryResourceId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::geometry::GeometryImageId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::surface::SurfaceImageId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::geometry::GeometryAppearanceId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::render::IblEnvironmentId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::render::SceneSubmissionId) == sizeof(std::uint64_t));

    const vr::asset::TextureId texture_id{};
    const vr::geometry::GeometryResourceId geometry_id{11U};
    const vr::geometry::GeometryImageId geometry_image_id{12U};
    const vr::surface::SurfaceImageId surface_image_id{13U};
    const vr::geometry::GeometryAppearanceId appearance_id{14U};
    const vr::render::IblEnvironmentId ibl_environment_id{15U};
    const vr::render::SceneSubmissionId submission_id{16U};

    VR_CHECK(!texture_id.IsValid());
    VR_CHECK(geometry_id.IsValid());
    VR_CHECK(geometry_image_id.IsValid());
    VR_CHECK(surface_image_id.IsValid());
    VR_CHECK(appearance_id.IsValid());
    VR_CHECK(ibl_environment_id.IsValid());
    VR_CHECK(submission_id.IsValid());
    VR_CHECK(geometry_id.value == 11U);
    VR_CHECK(geometry_image_id.value == 12U);
    VR_CHECK(surface_image_id.value == 13U);
    VR_CHECK(appearance_id.value == 14U);
    VR_CHECK(ibl_environment_id.value == 15U);
    VR_CHECK(submission_id.value == 16U);
}

VR_TEST_CASE(RuntimeConfig_render_graph_diagnostics_contract_uses_typed_identity_and_views,
             "unit;core;runtime;contract;diagnostics") {
    static_assert(std::is_trivially_copyable_v<vr::runtime::RenderGraphPassDiagnosticId>);
    static_assert(std::is_trivially_copyable_v<vr::runtime::RenderGraphQueueBatchDiagnosticId>);
    static_assert(std::is_trivially_copyable_v<vr::runtime::RenderGraphQueueDependencyDiagnosticId>);
    static_assert(std::is_trivially_copyable_v<vr::runtime::RenderGraphBarrierBatchDiagnosticId>);
    static_assert(std::is_trivially_copyable_v<vr::runtime::RenderGraphLogicalResourceDiagnosticId>);
    static_assert(sizeof(vr::runtime::RenderGraphPassDiagnosticId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::runtime::RenderGraphQueueBatchDiagnosticId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::runtime::RenderGraphQueueDependencyDiagnosticId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::runtime::RenderGraphBarrierBatchDiagnosticId) == sizeof(std::uint32_t));
    static_assert(sizeof(vr::runtime::RenderGraphLogicalResourceDiagnosticId) == sizeof(std::uint32_t));

    VR_CHECK(vr::runtime::DiagnosticsLevelName(vr::runtime::DiagnosticsLevel::CountersOnly) ==
             "counters_only");
    VR_CHECK(vr::runtime::RenderGraphQueueClassName(vr::render_graph::QueueClass::compute) ==
             "compute");

    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.graph_only_supported = true;
    diagnostics.graph_only_active = true;
    diagnostics.transfer_queue_requested = true;
    diagnostics.transfer_queue_enabled = true;
    diagnostics.multi_queue_requested = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.compiled_pass_count = 3U;
    diagnostics.recorded_pass_count = 2U;
    diagnostics.dynamic_rendering_local_read_status =
        vr::render_graph::NativePassLocalReadStatus::disabled;
    diagnostics.dynamic_rendering_local_read_reason =
        vr::render_graph::NativePassLocalReadReason::opt_in_not_requested;
    diagnostics.effective_queue_batch_count = 2U;
    diagnostics.effective_queue_dependency_count = 1U;
    diagnostics.effective_graphics_queue_batch_count = 1U;
    diagnostics.effective_transfer_queue_batch_count = 1U;
    diagnostics.effective_cross_queue_dependency_count = 1U;
    diagnostics.effective_total_submit_wait_count = 1U;
    diagnostics.effective_total_submit_signal_count = 1U;
    diagnostics.timing.available = true;
    diagnostics.timing.enabled = true;
    diagnostics.timing.domain = vr::runtime::RenderGraphTimingDomain::cpu_record;
    diagnostics.timing.queue_batch_range_count = 1U;
    diagnostics.timing.resolved_queue_batch_range_count = 1U;
    diagnostics.timing.total_duration_ns = 42U;
    diagnostics.timing.queue_batch_ranges.push_back(
        vr::runtime::RenderGraphQueueBatchTimingDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {0U},
            .pass_debug_names = {"capability_pass"},
            .marker_label = "queue_batch[0]/graphics/capability_pass",
            .relative_begin_ns = 0U,
            .relative_end_ns = 42U,
            .duration_ns = 42U,
            .resolved = true,
        });
    diagnostics.capture.available = true;
    diagnostics.capture.enabled = true;
    diagnostics.capture.marker_count = 1U;
    diagnostics.capture.artifact_count = 1U;
    diagnostics.capture.markers.push_back(
        vr::runtime::RenderGraphCaptureMarkerDiagnostics{
            .batch_id = 0U,
            .queue = vr::render_graph::QueueClass::graphics,
            .pass_ids = {0U},
            .pass_debug_names = {"capability_pass"},
            .label = "queue_batch[0]/graphics/capability_pass",
        });
    diagnostics.capture.artifacts.push_back(
        vr::runtime::RenderGraphCaptureArtifactDiagnostics{
            .captured = true,
            .kind = vr::runtime::RenderGraphCaptureArtifactKind::observability_snapshot,
            .artifact_label = "render_graph_capture.frame_0",
            .topology_json = "{\"summary\":{\"passCount\":1}}",
            .queue_timeline_json = "{\"mode\":\"graphics_only\"}",
        });

    const auto queue_timeline_view =
        vr::runtime::BuildRenderGraphQueueTimelineView(diagnostics);
    const auto observability_view =
        vr::runtime::BuildRenderGraphObservabilityView(diagnostics);

    if (vr::runtime::RuntimeDiagnosticsAvailableInBuild()) {
        VR_CHECK(queue_timeline_view.available);
        VR_CHECK(queue_timeline_view.batch_count == 2U);
        VR_CHECK(queue_timeline_view.dependency_count == 1U);
        VR_CHECK(queue_timeline_view.graphics_batch_count == 1U);
        VR_CHECK(queue_timeline_view.transfer_batch_count == 1U);
        VR_CHECK(queue_timeline_view.compute_batch_count == 0U);
        VR_CHECK(queue_timeline_view.cross_queue_dependency_count == 1U);
        VR_CHECK(queue_timeline_view.total_submit_wait_count == 1U);
        VR_CHECK(queue_timeline_view.total_submit_signal_count == 1U);
        VR_CHECK(queue_timeline_view.batches.empty());
        VR_CHECK(queue_timeline_view.dependencies.empty());
        VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineDebugString(diagnostics).empty());
        VR_CHECK(!vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics).empty());

        VR_CHECK(observability_view.runtime.available);
        VR_CHECK(observability_view.runtime.graph_only_active);
        VR_CHECK(observability_view.compile.compiled_pass_count == 3U);
        VR_CHECK(observability_view.compile.dynamic_rendering_local_read_status ==
                 vr::render_graph::NativePassLocalReadStatus::disabled);
        VR_CHECK(observability_view.record.recorded_pass_count == 2U);
        VR_CHECK(observability_view.submission.transfer_queue_enabled);
        VR_CHECK(observability_view.submission.effective_queue_batch_count == 2U);
        VR_CHECK(observability_view.submission.effective_queue_batches != nullptr);
        VR_CHECK(observability_view.submission.effective_queue_dependencies != nullptr);
        VR_CHECK(observability_view.submission.effective_queue_batches->empty());
        VR_CHECK(observability_view.timing.enabled);
        VR_CHECK(observability_view.timing.domain ==
                 vr::runtime::RenderGraphTimingDomain::cpu_record);
        VR_CHECK(observability_view.timing.queue_batch_ranges != nullptr);
        VR_CHECK(observability_view.timing.queue_batch_ranges->size() == 1U);
        VR_CHECK(observability_view.capture.enabled);
        VR_CHECK(observability_view.capture.markers != nullptr);
        VR_CHECK(observability_view.capture.artifacts != nullptr);
        VR_CHECK(observability_view.capture.markers->size() == 1U);
        VR_CHECK(observability_view.capture.artifacts->size() == 1U);
    } else {
        VR_CHECK(!queue_timeline_view.available);
        VR_CHECK(vr::runtime::BuildRenderGraphQueueTimelineDebugString(diagnostics).empty());
        VR_CHECK(vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics).empty());
        VR_CHECK(!observability_view.runtime.available);
        VR_CHECK(observability_view.compile.compiled_pass_count == 0U);
        VR_CHECK(observability_view.submission.effective_queue_batches == nullptr);
        VR_CHECK(observability_view.submission.effective_queue_dependencies == nullptr);
        VR_CHECK(!observability_view.timing.enabled);
        VR_CHECK(observability_view.timing.queue_batch_ranges == nullptr);
        VR_CHECK(!observability_view.capture.enabled);
        VR_CHECK(observability_view.capture.markers == nullptr);
        VR_CHECK(observability_view.capture.artifacts == nullptr);
    }
}

VR_TEST_CASE(
    RuntimeConfig_render_graph_observability_build_capability_matches_debug_release_contract,
    "unit;core;runtime;contract;diagnostics;render_graph") {
#if VR_ENABLE_DEBUG_OBSERVABILITY
    static_assert(vr::runtime::RuntimeDiagnosticsAvailableInBuild());
    static_assert(vr::render_graph::CompiledRenderGraphObservabilityAvailableInBuild());
#else
    static_assert(!vr::runtime::RuntimeDiagnosticsAvailableInBuild());
    static_assert(!vr::render_graph::CompiledRenderGraphObservabilityAvailableInBuild());
#endif

    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = true;
    diagnostics.frame_compiled = true;
    diagnostics.transfer_queue_enabled = true;
    diagnostics.multi_queue_enabled = true;
    diagnostics.effective_queue_batch_count = 1U;
    diagnostics.effective_queue_dependency_count = 0U;
    diagnostics.effective_graphics_queue_batch_count = 1U;

    const auto queue_view =
        vr::runtime::BuildRenderGraphQueueTimelineView(diagnostics);
    const std::string queue_debug =
        vr::runtime::BuildRenderGraphQueueTimelineDebugString(diagnostics);
    const std::string queue_json =
        vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics);
    const auto observability_view =
        vr::runtime::BuildRenderGraphObservabilityView(diagnostics);

    vr::render_graph::RenderGraphBuilder builder{};
    const auto payload = builder.CreateBuffer(
        "capability_payload",
        vr::render_graph::BufferDesc{
            .size_bytes = 256U,
            .usage = vr::render_graph::buffer_usage_storage_flag,
        });
    const auto write_pass = builder.AddPass("capability_write");
    const auto read_pass = builder.AddPass("capability_read", true);
    const auto written = builder.Write(
        write_pass,
        payload,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_write,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    (void)builder.Read(
        read_pass,
        written,
        vr::render_graph::AccessDesc{
            .access = vr::render_graph::AccessKind::shader_storage_read,
            .buffer_range = {.offset_bytes = 0U, .size_bytes = 256U},
        });
    const auto compiled = builder.Compile();
    const auto topology_view =
        vr::render_graph::BuildCompiledRenderGraphTopologyView(compiled);
    const std::string topology_json =
        vr::render_graph::BuildCompiledRenderGraphTopologyJson(topology_view);
    const std::string compiled_debug = compiled.BuildDebugString();
    const std::string compiled_json = compiled.BuildJson();

    if (vr::runtime::RuntimeDiagnosticsAvailableInBuild()) {
        VR_CHECK(vr::runtime::DiagnosticsCollectsFrameData(
            vr::runtime::DiagnosticsLevel::Detailed));
        VR_CHECK(vr::runtime::DiagnosticsCollectsServiceCounters(
            vr::runtime::DiagnosticsLevel::CountersOnly));
        VR_CHECK(vr::runtime::DiagnosticsCollectsDetailedData(
            vr::runtime::DiagnosticsLevel::Detailed));
        VR_CHECK(vr::runtime::DiagnosticsCollectsGpuTiming(
            vr::runtime::DiagnosticsLevel::GpuTiming));
        VR_CHECK(vr::runtime::DiagnosticsCollectsCapture(
            vr::runtime::DiagnosticsLevel::Capture));
        VR_CHECK(queue_view.available);
        VR_CHECK(!queue_debug.empty());
        VR_CHECK(!queue_json.empty());
        VR_CHECK(observability_view.compile.lazy_memory_resources != nullptr);
        VR_CHECK(observability_view.compile.liveness_ranges != nullptr);
        VR_CHECK(observability_view.compile.transient_memory != nullptr);
        VR_CHECK(observability_view.timing.queue_batch_ranges != nullptr);
        VR_CHECK(observability_view.capture.markers != nullptr);
        VR_CHECK(observability_view.capture.artifacts != nullptr);
        VR_CHECK(topology_view.summary.pass_count ==
                 static_cast<std::uint32_t>(compiled.Passes().size()));
        VR_CHECK(!topology_json.empty());
        VR_CHECK(!compiled_debug.empty());
        VR_CHECK(!compiled_json.empty());
    } else {
        VR_CHECK(!vr::runtime::DiagnosticsCollectsFrameData(
            vr::runtime::DiagnosticsLevel::Detailed));
        VR_CHECK(!vr::runtime::DiagnosticsCollectsServiceCounters(
            vr::runtime::DiagnosticsLevel::CountersOnly));
        VR_CHECK(!vr::runtime::DiagnosticsCollectsDetailedData(
            vr::runtime::DiagnosticsLevel::Detailed));
        VR_CHECK(!vr::runtime::DiagnosticsCollectsGpuTiming(
            vr::runtime::DiagnosticsLevel::GpuTiming));
        VR_CHECK(!vr::runtime::DiagnosticsCollectsCapture(
            vr::runtime::DiagnosticsLevel::Capture));
        VR_CHECK(!queue_view.available);
        VR_CHECK(queue_debug.empty());
        VR_CHECK(queue_json.empty());
        VR_CHECK(observability_view.compile.lazy_memory_resources == nullptr);
        VR_CHECK(observability_view.compile.liveness_ranges == nullptr);
        VR_CHECK(observability_view.compile.transient_memory == nullptr);
        VR_CHECK(observability_view.timing.queue_batch_ranges == nullptr);
        VR_CHECK(observability_view.capture.markers == nullptr);
        VR_CHECK(observability_view.capture.artifacts == nullptr);
        VR_CHECK(topology_view.summary.pass_count == 0U);
        VR_CHECK(topology_view.transient_memory.records.empty());
        VR_CHECK(topology_json.empty());
        VR_CHECK(compiled_debug.empty());
        VR_CHECK(compiled_json.empty());
    }
}

VR_TEST_CASE(
    RuntimeConfig_render_graph_release_diagnostics_hot_path_is_compile_time_guarded,
    "unit;core;runtime;contract;diagnostics;render_graph") {
    const std::string runtime_service_header = NormalizeNewlines(
        ReadUtf8TextFile(
            SourceRoot() / std::filesystem::path{
                               "include/vr/runtime/services/render_graph_runtime_service.hpp"}));
    const std::string diagnostics_source = NormalizeNewlines(
        ReadUtf8TextFile(
            SourceRoot() / std::filesystem::path{
                               "src/runtime/services/render_graph_runtime_service_diagnostics.cpp"}));

    VR_CHECK(Contains(
        runtime_service_header,
        R"(#if VR_ENABLE_DEBUG_OBSERVABILITY
        RefreshDiagnostics(vr::runtime::detail::ResolveDevice(context_));
#endif)"));
    VR_CHECK(Contains(runtime_service_header,
                      R"(#if VR_ENABLE_DEBUG_OBSERVABILITY
        RefreshDiagnostics(device);
#endif)"));
    VR_CHECK(Contains(
        runtime_service_header,
        R"(#if VR_ENABLE_DEBUG_OBSERVABILITY
        if (CollectsTimingOrCapture()) {)"));
    VR_CHECK(Contains(runtime_service_header,
                      "RecordSingleQueueGraphByQueueBatch("));
    VR_CHECK(!Contains(
        runtime_service_header,
        "        RefreshDiagnostics(vr::runtime::detail::ResolveDevice(context_));\n    }"));
    VR_CHECK(!Contains(runtime_service_header,
                       "        RefreshDiagnostics(device);\n    }"));

    VR_CHECK(Contains(diagnostics_source,
                      R"(#if !VR_ENABLE_DEBUG_OBSERVABILITY
    (void)device_;
    return;
#else)"));
    VR_CHECK(!Contains(
        diagnostics_source,
        "if (!vr::runtime::RuntimeDiagnosticsAvailableInBuild() ||"));

    const std::string submission_source = NormalizeNewlines(
        ReadUtf8TextFile(
            SourceRoot() / std::filesystem::path{
                               "src/runtime/services/render_graph_runtime_service_submission.cpp"}));
    VR_CHECK(Contains(submission_source,
                      R"(#if VR_ENABLE_DEBUG_OBSERVABILITY

void RenderGraphRuntimeService::ResetRecordedTimingSamples() noexcept {)"));
    VR_CHECK(Contains(submission_source,
                      "const bool collect_timing_or_capture = CollectsTimingOrCapture();"));
}

VR_TEST_CASE(
    RuntimeConfig_render_graph_observability_goldens_match_canonical_consumer_artifacts,
    "unit;core;runtime;contract;diagnostics;render_graph;golden") {
    const auto compiled = BuildAliasingGoldenGraph();
    const auto topology_view =
        vr::render_graph::BuildCompiledRenderGraphTopologyView(compiled);
    const std::string topology_json =
        vr::render_graph::BuildCompiledRenderGraphTopologyJson(topology_view);

    auto diagnostics = MakeCanonicalObservabilityDiagnosticsForTesting();
    const std::string queue_timeline_json =
        vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics);

    if (!vr::runtime::RuntimeDiagnosticsAvailableInBuild()) {
        VR_CHECK(topology_view.summary.pass_count == 0U);
        VR_CHECK(topology_json.empty());
        VR_CHECK(queue_timeline_json.empty());
        return;
    }

    SyncGoldenTextIfRequested("compiled_topology_aliasing.json", topology_json);
    SyncGoldenTextIfRequested("queue_timeline_transfer_compute.json",
                              queue_timeline_json);

    VR_CHECK(NormalizeNewlines(topology_json) ==
             LoadGoldenText("compiled_topology_aliasing.json"));
    VR_CHECK(NormalizeNewlines(queue_timeline_json) ==
             LoadGoldenText("queue_timeline_transfer_compute.json"));

    VR_REQUIRE(diagnostics.capture.artifacts.size() == 1U);
    const auto& artifact = diagnostics.capture.artifacts.front();
    VR_CHECK(artifact.captured);
    VR_CHECK(artifact.kind ==
             vr::runtime::RenderGraphCaptureArtifactKind::observability_snapshot);
    VR_CHECK(artifact.artifact_label == "render_graph_capture.frame_0");
    VR_CHECK(NormalizeNewlines(artifact.topology_json) ==
             LoadGoldenText("compiled_topology_aliasing.json"));
    VR_CHECK(NormalizeNewlines(artifact.queue_timeline_json) ==
             LoadGoldenText("queue_timeline_transfer_compute.json"));
}

VR_TEST_CASE(
    RuntimeConfig_render_graph_observability_consumer_composes_compile_submission_timing_capture_contract,
    "unit;core;runtime;contract;diagnostics;render_graph;consumer") {
    const auto diagnostics = MakeCanonicalObservabilityDiagnosticsForTesting();
    const auto observability_view =
        vr::runtime::BuildRenderGraphObservabilityView(diagnostics);
    const auto digest = ConsumeRenderGraphObservability(observability_view);

    if (!vr::runtime::RuntimeDiagnosticsAvailableInBuild()) {
        VR_CHECK(digest.compile_pass_count == 0U);
        VR_CHECK(digest.liveness_range_count == 0U);
        VR_CHECK(digest.transient_saved_bytes == 0U);
        VR_CHECK(digest.submission_batch_count == 0U);
        VR_CHECK(digest.cross_queue_dependency_count == 0U);
        VR_CHECK(digest.timing_range_count == 0U);
        VR_CHECK(digest.timing_total_duration_ns == 0U);
        VR_CHECK(digest.capture_artifact_count == 0U);
        VR_CHECK(digest.first_timing_label.empty());
        VR_CHECK(digest.first_artifact_label.empty());
        VR_CHECK(observability_view.compile.transient_memory == nullptr);
        VR_CHECK(observability_view.timing.queue_batch_ranges == nullptr);
        VR_CHECK(observability_view.capture.artifacts == nullptr);
        return;
    }

    VR_REQUIRE(observability_view.compile.liveness_ranges != nullptr);
    VR_REQUIRE(observability_view.compile.transient_memory != nullptr);
    VR_REQUIRE(observability_view.submission.effective_queue_batches != nullptr);
    VR_REQUIRE(observability_view.submission.effective_queue_dependencies != nullptr);
    VR_REQUIRE(observability_view.timing.queue_batch_ranges != nullptr);
    VR_REQUIRE(observability_view.capture.markers != nullptr);
    VR_REQUIRE(observability_view.capture.artifacts != nullptr);

    VR_CHECK(digest.compile_pass_count == 4U);
    VR_CHECK(digest.liveness_range_count == 4U);
    VR_CHECK(digest.transient_saved_bytes == 4096U);
    VR_CHECK(digest.submission_batch_count == 3U);
    VR_CHECK(digest.cross_queue_dependency_count == 2U);
    VR_CHECK(digest.timing_range_count == 3U);
    VR_CHECK(digest.timing_total_duration_ns == 360U);
    VR_CHECK(digest.capture_artifact_count == 1U);
    VR_CHECK(digest.first_timing_label ==
             "queue_batch[0]/transfer/upload_payload");
    VR_CHECK(digest.first_artifact_label == "render_graph_capture.frame_0");
    VR_CHECK(observability_view.compile.transient_memory->summary.saved_bytes ==
             4096U);
    VR_CHECK(observability_view.compile.transient_memory->summary.page_count ==
             1U);
    VR_CHECK(observability_view.compile.transient_memory->summary.alias_barrier_count ==
             1U);
    VR_CHECK(observability_view.submission.effective_queue_batches->size() == 3U);
    VR_CHECK(observability_view.submission.effective_queue_dependencies->size() ==
             2U);
    VR_CHECK(observability_view.timing.queue_batch_ranges->size() == 3U);
    VR_CHECK(observability_view.capture.markers->size() == 3U);
    VR_CHECK(observability_view.capture.artifacts->size() == 1U);
    VR_CHECK(observability_view.capture.artifacts->front().topology_json ==
             vr::render_graph::BuildCompiledRenderGraphTopologyJson(
                 vr::render_graph::BuildCompiledRenderGraphTopologyView(
                     BuildAliasingGoldenGraph())));
    VR_CHECK(observability_view.capture.artifacts->front().queue_timeline_json ==
             vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics));
}

VR_TEST_CASE(RuntimeConfig_default_state_before_initialize_is_safe, "unit;core;runtime") {
    Runtime runtime{};

    VR_CHECK(!runtime.IsInitialized());
    VR_CHECK(!runtime.IsRunning());
    VR_CHECK(!runtime.HasTextureHost());
    VR_CHECK(!runtime.HasFrameComposerHost());
    VR_CHECK(!runtime.HasIblHost());
    VR_CHECK(!runtime.HasIblBakeHost());
    VR_CHECK(!runtime.HasSkyEnvironmentHost());
    VR_CHECK(!runtime.HasUploadHost());
    VR_CHECK(!runtime.HasDescriptorHost());
    VR_CHECK(!runtime.HasPipelineHost());
    VR_CHECK(!runtime.HasRenderTargetHost());
    VR_CHECK(!runtime.HasSamplerHost());
    VR_CHECK(!runtime.HasFreeTypeHost());
    VR_CHECK(!runtime.HasGlyphAtlasHost());
    VR_CHECK(!runtime.HasGlyphUploadHost());
    VR_CHECK(!runtime.HasParticleUploadHost());
    VR_CHECK(!runtime.HasParticleSimulationHost());
    VR_CHECK(!runtime.ParticleUploadService().IsAvailable());
    VR_CHECK(!runtime.ParticleSimulationService().IsAvailable());
    VR_CHECK(!runtime.SkyEnvironmentService().IsAvailable());
    VR_CHECK(!runtime.Particles().IsAvailable());

    const Runtime::CreateInfo& config = runtime.Config();
    VR_CHECK(config.modules.enable_upload_host);
    VR_CHECK(config.modules.enable_texture_host);
    VR_CHECK(config.modules.enable_frame_composer_host);
    VR_CHECK(config.modules.enable_ibl_host);
    VR_CHECK(!config.modules.enable_ibl_bake_host);
    VR_CHECK(config.modules.enable_sky_environment_gpu_host);
    VR_CHECK(config.modules.enable_descriptor_host);
    VR_CHECK(config.modules.enable_pipeline_host);
    VR_CHECK(config.modules.enable_render_target_host);
    VR_CHECK(config.modules.enable_sampler_host);
    VR_CHECK(config.modules.enable_freetype_host);
    VR_CHECK(config.modules.enable_glyph_atlas_host);
    VR_CHECK(config.modules.enable_glyph_upload_host);
    VR_CHECK(config.modules.enable_particle_upload_host);
    VR_CHECK(config.modules.enable_particle_simulation_host);
    VR_CHECK(config.diagnostics.level == vr::runtime::DiagnosticsLevel::Off);
}

VR_TEST_CASE(RuntimeConfig_text_runtime_feature_contract_enables_dynamic_rendering_and_sync2,
             "unit;core;runtime;text") {
    Runtime::CreateInfo create_info{};
    VR_CHECK(create_info.bindless.update_after_bind_policy ==
             vr::render::BindlessUpdateAfterBindPolicy::disabled);
    vr::text::ApplyTextRuntimeFeatureContract(create_info);

    VR_CHECK(create_info.platform.device.required_vulkan13_features.dynamicRendering == VK_TRUE);
    VR_CHECK(create_info.platform.device.required_vulkan13_features.synchronization2 == VK_TRUE);
    VR_CHECK(create_info.bindless.update_after_bind_policy ==
             vr::render::BindlessUpdateAfterBindPolicy::disabled);

    const auto default_text_create_info =
        vr::text::MakeDefaultTextRuntimeCreateInfo<Runtime::CreateInfo>();
    VR_CHECK(default_text_create_info.platform.device.required_vulkan13_features.dynamicRendering == VK_TRUE);
    VR_CHECK(default_text_create_info.platform.device.required_vulkan13_features.synchronization2 == VK_TRUE);
    VR_CHECK(default_text_create_info.bindless.update_after_bind_policy ==
             vr::render::BindlessUpdateAfterBindPolicy::disabled);
    VR_CHECK(default_text_create_info.platform.device.feature_chain_policy ==
             vr::VulkanFeatureChainPolicy::minimal_required);
}

VR_TEST_CASE(RuntimeConfig_vulkan_device_feature_chain_policy_defaults_to_minimal_required,
             "unit;core;runtime;vulkan") {
    vr::VulkanDeviceCreateInfo device_create_info{};
    VR_CHECK(device_create_info.feature_chain_policy ==
             vr::VulkanFeatureChainPolicy::minimal_required);
    VR_CHECK(!device_create_info.request_dynamic_rendering_local_read);

    device_create_info.feature_chain_policy =
        vr::VulkanFeatureChainPolicy::explicit_vulkan12_vulkan13;
    VR_CHECK(device_create_info.feature_chain_policy ==
             vr::VulkanFeatureChainPolicy::explicit_vulkan12_vulkan13);
    device_create_info.request_dynamic_rendering_local_read = true;
    VR_CHECK(device_create_info.request_dynamic_rendering_local_read);
}

VR_TEST_CASE(RuntimeConfig_unavailable_modules_throw_before_initialize, "unit;core;runtime") {
    Runtime runtime{};

    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GpuMemory(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Texture(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.FrameComposer(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Ibl(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.IblBake(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.SkyEnvironment(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Upload(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Descriptor(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Pipeline(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.RenderTarget(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Sampler(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.FreeType(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GlyphAtlas(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GlyphUpload(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.ParticleUpload(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.ParticleSimulation(); }));
}

VR_TEST_CASE(RuntimeConfig_particle_service_dependency_contract_matches_runtime_design,
             "unit;core;runtime;particle") {
    using namespace vr::runtime;
    using namespace vr::runtime::profiles;
    using namespace vr::runtime::services;

    static_assert(RuntimeService<GpuMemoryService>);
    static_assert(RuntimeService<ParticleRenderService>);
    static_assert(RuntimeService<SkyEnvironmentService>);

    static_assert(service_depends_on_v<UploadService, GpuMemoryService>);
    static_assert(service_depends_on_v<TextureService, GpuMemoryService>);
    static_assert(service_depends_on_v<TextureService, UploadService>);
    static_assert(service_depends_on_v<RenderTargetService, GpuMemoryService>);
    static_assert(service_depends_on_v<FrameComposerService, RenderTargetService>);
    static_assert(service_depends_on_v<IblService, TextureService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, TextureService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, DescriptorService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, PipelineService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, SamplerService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, UploadService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, GpuMemoryService>);
    static_assert(service_depends_on_v<IblBakeService, IblService>);
    static_assert(service_depends_on_v<GlyphAtlasService, FreeTypeService>);
    static_assert(service_depends_on_v<GlyphUploadService, GlyphAtlasService>);
    static_assert(service_depends_on_v<ParticleUploadService, GpuMemoryService>);
    static_assert(service_depends_on_v<ParticleUploadService, UploadService>);
    static_assert(service_depends_on_v<ParticleSimulationService, GpuMemoryService>);
    static_assert(service_depends_on_v<ParticleSimulationService, UploadService>);
    static_assert(service_depends_on_v<ParticleSimulationService, DescriptorService>);
    static_assert(service_depends_on_v<ParticleSimulationService, PipelineService>);
    static_assert(service_depends_on_v<ParticleRenderService, ParticleUploadService>);
    static_assert(service_depends_on_v<ParticleRenderService, ParticleSimulationService>);
    static_assert(service_depends_on_v<ParticleRenderService, TextureService>);
    static_assert(service_depends_on_v<ParticleRenderService, DescriptorService>);
    static_assert(service_depends_on_v<ParticleRenderService, PipelineService>);
    static_assert(service_depends_on_v<ParticleRenderService, SamplerService>);
    static_assert(service_depends_on_v<ParticleRenderService, RenderTargetService>);
    static_assert(service_depends_on_v<ParticleRenderService, UploadService>);

    static_assert(profile_contains_v<MinimalProfile, services::CommandService>);
    static_assert(profile_contains_v<Runtime2DProfile, ParticleUploadService>);
    static_assert(profile_contains_v<Runtime2DProfile, ParticleSimulationService>);
    static_assert(profile_contains_v<Runtime2DProfile, ParticleRenderService>);
    static_assert(!profile_contains_v<Runtime2DProfile, SkyEnvironmentService>);
    static_assert(profile_contains_v<Runtime3DProfile, FrameComposerService>);
    static_assert(profile_contains_v<Runtime3DProfile, IblService>);
    static_assert(profile_contains_v<Runtime3DProfile, SkyEnvironmentService>);
    static_assert(profile_contains_v<Runtime3DProfile, IblBakeService>);
    static_assert(profile_contains_v<Runtime3DProfile, ParticleUploadService>);
    static_assert(profile_contains_v<Runtime3DProfile, ParticleSimulationService>);
    static_assert(profile_contains_v<Runtime3DProfile, ParticleRenderService>);

    static_assert(profile_satisfies_service_dependencies_v<Runtime2DProfile, ParticleUploadService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime2DProfile, ParticleSimulationService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime2DProfile, ParticleRenderService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime3DProfile, IblBakeService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime3DProfile, SkyEnvironmentService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime3DProfile, ParticleRenderService>);

    VR_CHECK(true);
}

VR_TEST_CASE(RuntimeConfig_runtime_services_registry_exposes_profiled_typed_access,
             "unit;core;runtime;services") {
    using namespace vr::runtime;
    using namespace vr::runtime::services;

    Runtime runtime{};
    auto& services = runtime.Services();

    static_assert(Runtime::RuntimeServicesType::Contains<ParticleRenderService>());
    static_assert(Runtime::RuntimeServicesType::Contains<FrameComposerService>());
    static_assert(Runtime::RuntimeServicesType::Contains<SkyEnvironmentService>());
    static_assert(!Runtime::RuntimeServicesType::Contains<int>());

    VR_CHECK(services.TryGet<ParticleRenderService>() != nullptr);
    VR_CHECK(services.TryGet<ParticleSimulationService>() != nullptr);
    VR_CHECK(services.TryGet<FrameComposerService>() != nullptr);
    VR_CHECK(services.TryGet<IblService>() != nullptr);
    VR_CHECK(services.TryGet<SkyEnvironmentService>() != nullptr);

    VR_CHECK(!services.Get<ParticleRenderService>().IsAvailable());
    VR_CHECK(!services.Get<ParticleSimulationService>().IsAvailable());
    VR_CHECK(!services.Get<FrameComposerService>().IsAvailable());
    VR_CHECK(!services.Get<IblService>().IsAvailable());
    VR_CHECK(!services.Get<SkyEnvironmentService>().IsAvailable());
}

VR_TEST_CASE(RuntimeConfig_runtime_services_lifecycle_dispatch_uses_forward_init_and_reverse_shutdown,
             "unit;core;runtime;services") {
    using MockProfile = vr::runtime::RuntimeProfile<
        MockLifecycleServiceA,
        MockLifecycleServiceB,
        MockLifecycleServiceC>;
    using Services = vr::runtime::RuntimeServices<MockProfile>;
    using Kernel = vr::runtime::RuntimeKernel<vr::platform::ActiveBackendTag, 2U>;

    Services services{};
    Kernel kernel{};
    MockLifecycleServiceA service_a{};
    MockLifecycleServiceB service_b{};
    MockLifecycleServiceC service_c{};
    services.Bind(service_a, service_b, service_c);

    LifecycleEventLog log{};
    g_lifecycle_log = &log;

    auto init_context = vr::runtime::RuntimeInitContext<MockProfile, vr::platform::ActiveBackendTag, 2U>{
        .services = services,
        .kernel = kernel,
        .device = nullptr,
    };
    auto post_init_context =
        vr::runtime::RuntimePostInitContext<MockProfile, vr::platform::ActiveBackendTag, 2U>{
            .services = services,
            .kernel = kernel,
            .device = nullptr,
        };
    auto shutdown_context =
        vr::runtime::RuntimeShutdownContext<MockProfile, vr::platform::ActiveBackendTag, 2U>{
            .services = services,
            .kernel = kernel,
            .device = nullptr,
        };

    services.Initialize(init_context);
    services.PostInitialize(post_init_context);
    services.Shutdown(shutdown_context);
    g_lifecycle_log = nullptr;

    VR_REQUIRE(log.count == 9U);
    VR_CHECK(log.events[0] == 1);
    VR_CHECK(log.events[1] == 2);
    VR_CHECK(log.events[2] == 3);
    VR_CHECK(log.events[3] == 4);
    VR_CHECK(log.events[4] == 5);
    VR_CHECK(log.events[5] == 6);
    VR_CHECK(log.events[6] == 7);
    VR_CHECK(log.events[7] == 8);
    VR_CHECK(log.events[8] == 9);
}

VR_TEST_CASE(RuntimeConfig_runtime_execution_dispatches_service_phases_in_document_order,
             "unit;core;runtime;services") {
    using MockProfile = vr::runtime::RuntimeProfile<MockPhaseServiceA, MockPhaseServiceB>;
    using Services = vr::runtime::RuntimeServices<MockProfile>;
    using Kernel = vr::runtime::RuntimeKernel<vr::platform::ActiveBackendTag, 2U>;
    using FrameContext = vr::runtime::RuntimeFrameContext<MockProfile, vr::platform::ActiveBackendTag, 2U>;
    using Execution = vr::runtime::RuntimeExecution<FrameContext>;

    Services services{};
    Kernel kernel{};
    vr::runtime::CommandService commands{};
    MockPhaseServiceA service_a{};
    MockPhaseServiceB service_b{};
    services.Bind(service_a, service_b);

    FrameContext frame_context{
        .frame = {},
        .progress = {},
        .timelines = {},
        .services = services,
        .kernel = kernel,
        .commands = commands,
        .swapchain_targets = nullptr,
    };

    LifecycleEventLog log{};
    g_lifecycle_log = &log;

    Execution execution{frame_context};
    execution.MarkBeginFrame();
    execution.ServiceBeginFrame(services);
    execution.Prepare(services);
    execution.MarkFlushUploads();
    execution.PreRecord(services);
    execution.MarkRecord();
    execution.MarkSubmit();
    execution.PostRecord(services);
    execution.MarkPresent();
    execution.EndFrame(services);
    execution.Retire(services);
    execution.MarkDiagnostics();
    g_lifecycle_log = nullptr;

    VR_REQUIRE(log.count == 12U);
    constexpr std::array<int, 12> expected_order = {1, 2, 3, 4, 5, 7, 6, 8, 10, 9, 12, 11};
    for (std::uint32_t index = 0U; index < log.count; ++index) {
        VR_CHECK(log.events[index] == expected_order[index]);
    }
    VR_CHECK(execution.Trace().last_completed_stage == vr::runtime::RuntimeExecutionStage::Diagnostics);
    VR_CHECK(execution.Trace().completed_stage_count == 11U);
}

VR_TEST_CASE(RuntimeConfig_runtime_root_wraps_legacy_facade_with_typed_status,
             "unit;core;runtime;services") {
    using RuntimeRoot = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
    using ParticleRenderService = vr::runtime::services::ParticleRenderService;
    using ParticleSimulationService = vr::runtime::services::ParticleSimulationService;

    static_assert(std::is_same_v<RuntimeRoot::CreateInfo,
                                 vr::runtime::RuntimeCreateInfo<vr::platform::ActiveBackendTag, 2U>>);

    RuntimeRoot runtime{};
    VR_CHECK(runtime.Kernel().IsBound());
    VR_CHECK(runtime.Frames().IsBound());
    VR_CHECK(runtime.Frames().FramesInFlight() == 2U);
    VR_CHECK(runtime.Commands().IsBound());
    VR_CHECK(runtime.Retire().IsBound());
    VR_CHECK(runtime.Kernel().Clock().frame_id == 0U);
    VR_CHECK(runtime.LastExecutionTrace().completed_stage_count == 0U);
    const auto timelines = runtime.Kernel().BuildQueueTimelines();
    VR_CHECK(timelines.graphics.next_value == 1U);
    VR_CHECK(timelines.graphics.submitted_value == 0U);
    VR_CHECK(timelines.graphics.completed_value == 0U);
    VR_CHECK(!timelines.graphics.IsAvailable());
    VR_CHECK(!timelines.transfer.IsAvailable());
    VR_CHECK(!timelines.compute.IsAvailable());
    const auto dependency = runtime.Kernel().BuildGraphicsDependency();
    VR_CHECK(dependency.source_queue == vr::runtime::QueueKind::graphics);
    VR_CHECK(dependency.value == 0U);
    const auto transfer_dependency = runtime.Kernel().BuildTransferDependency();
    VR_CHECK(transfer_dependency.source_queue == vr::runtime::QueueKind::transfer);
    VR_CHECK(transfer_dependency.value == 0U);
    const auto compute_dependency = runtime.Kernel().BuildComputeDependency();
    VR_CHECK(compute_dependency.source_queue == vr::runtime::QueueKind::compute);
    VR_CHECK(compute_dependency.value == 0U);
    VR_CHECK(runtime.Services().TryGet<ParticleRenderService>() != nullptr);
    VR_CHECK(runtime.Services().TryGet<ParticleSimulationService>() != nullptr);
    VR_CHECK(!runtime.Services().Get<ParticleSimulationService>().HasComputeTimelineProgress());
    VR_CHECK(runtime.Services().Get<ParticleSimulationService>().LastSubmittedValue() == 0U);
    VR_CHECK(runtime.Services().Get<ParticleSimulationService>().CompletedSubmitValue() == 0U);
    VR_CHECK(runtime.Config().diagnostics.level == vr::runtime::DiagnosticsLevel::Off);

    VR_CHECK(vr::runtime::ToRuntimeStatusCode(vr::render::TickCode::Submitted) ==
             vr::runtime::RuntimeStatusCode::Ok);
    VR_CHECK(vr::runtime::ToRuntimeStatusCode(vr::render::TickCode::SkippedWindowHidden) ==
             vr::runtime::RuntimeStatusCode::WindowHidden);
    VR_CHECK(vr::runtime::ToRuntimeStatusCode(vr::render::TickCode::RecreateRequested) ==
             vr::runtime::RuntimeStatusCode::SwapchainRecreated);
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

