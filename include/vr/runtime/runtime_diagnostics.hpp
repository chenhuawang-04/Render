#pragma once

#include "vr/render_graph/compiled_render_graph_observability.hpp"
#include "vr/render_graph/frame_history_contract.hpp"
#include "vr/render_graph/native_pass_plan.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/asset/texture_host.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/frame_composer_host.hpp"
#include "vr/render/ibl_bake_host.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"
#include "vr/text/glyph_upload_host.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace vr::runtime {

enum class DiagnosticsLevel : std::uint8_t {
    Off = 0,
    CountersOnly = 1,
    Detailed = 2,
    GpuTiming = 3,
    Capture = 4,
};

[[nodiscard]] constexpr std::string_view DiagnosticsLevelName(
    const DiagnosticsLevel level_) noexcept {
    switch (level_) {
    case DiagnosticsLevel::Off:
        return "off";
    case DiagnosticsLevel::CountersOnly:
        return "counters_only";
    case DiagnosticsLevel::Detailed:
        return "detailed";
    case DiagnosticsLevel::GpuTiming:
        return "gpu_timing";
    case DiagnosticsLevel::Capture:
        return "capture";
    }
    return "unknown";
}

struct RuntimeDiagnosticsCreateInfo final {
    DiagnosticsLevel level = DiagnosticsLevel::Off;
};

[[nodiscard]] constexpr bool RuntimeDiagnosticsAvailableInBuild() noexcept {
#if VR_ENABLE_DEBUG_OBSERVABILITY
    return true;
#else
    return false;
#endif
}

[[nodiscard]] constexpr DiagnosticsLevel ResolveRuntimeDiagnosticsLevelForBuild(
    const DiagnosticsLevel level_) noexcept {
    return RuntimeDiagnosticsAvailableInBuild() ? level_
                                                : DiagnosticsLevel::Off;
}

namespace detail {

#define VR_DEFINE_RUNTIME_DIAGNOSTIC_ID_TYPE(type_name_, underlying_type_)                      \
    struct type_name_ final {                                                                   \
        using underlying_type = underlying_type_;                                               \
                                                                                                 \
        static constexpr underlying_type invalid_value =                                        \
            (std::numeric_limits<underlying_type>::max)();                                      \
                                                                                                 \
        underlying_type value = invalid_value;                                                  \
                                                                                                 \
        constexpr type_name_() noexcept = default;                                              \
        constexpr type_name_(const underlying_type value_) noexcept                             \
            : value(value_) {}                                                                  \
                                                                                                 \
        [[nodiscard]] constexpr bool IsValid() const noexcept {                                 \
            return value != invalid_value;                                                      \
        }                                                                                       \
                                                                                                 \
        [[nodiscard]] static constexpr type_name_ Invalid() noexcept {                          \
            return {};                                                                          \
        }                                                                                       \
                                                                                                 \
        constexpr operator underlying_type() const noexcept {                                   \
            return value;                                                                       \
        }                                                                                       \
                                                                                                 \
        friend constexpr bool operator==(const type_name_&,                                     \
                                         const type_name_&) noexcept = default;                 \
                                                                                                 \
        constexpr auto operator<=>(const type_name_&) const noexcept = default;                 \
    };                                                                                          \
    static_assert(std::is_standard_layout_v<type_name_>);                                       \
    static_assert(std::is_trivially_copyable_v<type_name_>);                                    \
    static_assert(sizeof(type_name_) == sizeof(underlying_type_))

} // namespace detail

VR_DEFINE_RUNTIME_DIAGNOSTIC_ID_TYPE(RenderGraphPassDiagnosticId, std::uint32_t);
VR_DEFINE_RUNTIME_DIAGNOSTIC_ID_TYPE(RenderGraphQueueBatchDiagnosticId, std::uint32_t);
VR_DEFINE_RUNTIME_DIAGNOSTIC_ID_TYPE(RenderGraphQueueDependencyDiagnosticId, std::uint32_t);
VR_DEFINE_RUNTIME_DIAGNOSTIC_ID_TYPE(RenderGraphBarrierBatchDiagnosticId, std::uint32_t);
VR_DEFINE_RUNTIME_DIAGNOSTIC_ID_TYPE(RenderGraphLogicalResourceDiagnosticId, std::uint32_t);

#undef VR_DEFINE_RUNTIME_DIAGNOSTIC_ID_TYPE

struct FrameStats final {
    std::uint64_t frame_id = 0U;
    std::uint32_t frame_index = 0U;
    std::uint32_t image_index = 0U;
    bool upload_submitted = false;
    bool upload_cross_queue_wait = false;
};

struct SwapchainStats final {
    bool valid = false;
    std::uint64_t generation = 0U;
    std::uint32_t image_count = 0U;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
};

struct QueueTimelineStats final {
    std::uint64_t graphics_submitted = 0U;
    std::uint64_t graphics_completed = 0U;
    std::uint64_t transfer_submitted = 0U;
    std::uint64_t transfer_completed = 0U;
    std::uint64_t compute_submitted = 0U;
    std::uint64_t compute_completed = 0U;
};

struct CommandStats final {
    std::uint32_t frame_slot_count = 0U;
    std::uint32_t used_primary_count = 0U;
};

struct DescriptorStats final {
    std::uint32_t total_pool_count = 0U;
    std::uint32_t frame_pool_count = 0U;
    std::uint32_t total_allocated_set_count = 0U;
    std::uint32_t frame_allocated_set_count = 0U;
    vr::DescriptorIndexingCaps descriptor_indexing{};
    vr::render::DescriptorHostStats host{};
    vr::render::DescriptorValidationStats validation{};
};

struct BindlessStats final {
    vr::render::BindlessResourceSystemStats resources{};
};

struct ParticleRenderStats final {
    bool service_available = false;
};

struct AllocationStats final {
    std::uint64_t upload_capacity_bytes = 0U;
    std::uint32_t upload_staging_page_growth_count = 0U;
    std::uint32_t descriptor_total_pool_count = 0U;
};

struct RenderGraphLazyMemoryResourceDiagnostics final {
    RenderGraphLogicalResourceDiagnosticId logical_resource_id{};
    std::string debug_name{};
    bool requested = false;
    bool realized = false;
    std::string unavailable_reason{};
};

struct RenderGraphFrameHistoryResourceDiagnostics final {
    render::RenderTargetHandle handle{};
    std::uint32_t resource_revision = 0U;
};

struct RenderGraphFrameHistoryDiagnostics final {
    render_graph::TextureDesc desc{};
    RenderGraphFrameHistoryResourceDiagnostics previous{};
    RenderGraphFrameHistoryResourceDiagnostics current{};
    render::SceneSubmissionId previous_submission_id{};
    std::uint64_t previous_frame_index = 0U;
    render_graph::FrameHistoryInvalidationReason invalidation_reason =
        render_graph::FrameHistoryInvalidationReason::none;
    bool previous_available = false;
    bool current_writable = false;
};

struct RenderGraphQueueBatchDiagnostics final {
    RenderGraphQueueBatchDiagnosticId batch_id{};
    render_graph::QueueClass queue = render_graph::QueueClass::graphics;
    std::vector<RenderGraphPassDiagnosticId> pass_ids{};
    std::vector<std::string> pass_debug_names{};
    std::vector<RenderGraphQueueDependencyDiagnosticId> wait_dependency_ids{};
    std::vector<RenderGraphQueueDependencyDiagnosticId> signal_dependency_ids{};
    std::vector<RenderGraphBarrierBatchDiagnosticId> barrier_batch_ids{};
    std::uint32_t submit_wait_count = 0U;
    std::uint32_t submit_signal_count = 0U;
    bool contains_host_boundary = false;
    bool submitted_on_owned_queue = false;
};

struct RenderGraphQueueDependencyDiagnostics final {
    RenderGraphQueueDependencyDiagnosticId dependency_id{};
    render_graph::QueueClass source_queue = render_graph::QueueClass::graphics;
    render_graph::QueueClass target_queue = render_graph::QueueClass::graphics;
    RenderGraphQueueBatchDiagnosticId source_batch_id{};
    RenderGraphQueueBatchDiagnosticId target_batch_id{};
    RenderGraphPassDiagnosticId source_pass_id{};
    RenderGraphPassDiagnosticId target_pass_id{};
    std::string source_pass_debug_name{};
    std::string target_pass_debug_name{};
    std::uint32_t resource_count = 0U;
    bool queue_transfer = false;
    bool host_boundary = false;
};

[[nodiscard]] constexpr std::string_view RenderGraphQueueClassName(
    const render_graph::QueueClass queue_) noexcept {
    switch (queue_) {
    case render_graph::QueueClass::graphics:
        return "graphics";
    case render_graph::QueueClass::compute:
        return "compute";
    case render_graph::QueueClass::transfer:
        return "transfer";
    }
    return "unknown";
}

enum class RenderGraphTimingDomain : std::uint8_t {
    unavailable = 0U,
    cpu_record = 1U,
    gpu_timestamp = 2U,
};

[[nodiscard]] constexpr std::string_view RenderGraphTimingDomainName(
    const RenderGraphTimingDomain domain_) noexcept {
    switch (domain_) {
    case RenderGraphTimingDomain::unavailable:
        return "unavailable";
    case RenderGraphTimingDomain::cpu_record:
        return "cpu_record";
    case RenderGraphTimingDomain::gpu_timestamp:
        return "gpu_timestamp";
    }
    return "unknown";
}

enum class RenderGraphCaptureArtifactKind : std::uint8_t {
    unavailable = 0U,
    observability_snapshot = 1U,
};

[[nodiscard]] constexpr std::string_view RenderGraphCaptureArtifactKindName(
    const RenderGraphCaptureArtifactKind kind_) noexcept {
    switch (kind_) {
    case RenderGraphCaptureArtifactKind::unavailable:
        return "unavailable";
    case RenderGraphCaptureArtifactKind::observability_snapshot:
        return "observability_snapshot";
    }
    return "unknown";
}

struct RenderGraphQueueBatchTimingDiagnostics final {
    RenderGraphQueueBatchDiagnosticId batch_id{};
    render_graph::QueueClass queue = render_graph::QueueClass::graphics;
    std::vector<RenderGraphPassDiagnosticId> pass_ids{};
    std::vector<std::string> pass_debug_names{};
    std::vector<RenderGraphQueueDependencyDiagnosticId> wait_dependency_ids{};
    std::vector<RenderGraphQueueDependencyDiagnosticId> signal_dependency_ids{};
    std::vector<RenderGraphBarrierBatchDiagnosticId> barrier_batch_ids{};
    std::string marker_label{};
    std::uint64_t relative_begin_ns = 0U;
    std::uint64_t relative_end_ns = 0U;
    std::uint64_t duration_ns = 0U;
    bool resolved = false;
};

struct RenderGraphTimingDiagnostics final {
    bool available = false;
    bool enabled = false;
    bool gpu_timestamp_supported = false;
    RenderGraphTimingDomain domain = RenderGraphTimingDomain::unavailable;
    std::uint32_t queue_batch_range_count = 0U;
    std::uint32_t resolved_queue_batch_range_count = 0U;
    std::uint64_t total_duration_ns = 0U;
    std::vector<RenderGraphQueueBatchTimingDiagnostics> queue_batch_ranges{};
};

struct RenderGraphCaptureMarkerDiagnostics final {
    RenderGraphQueueBatchDiagnosticId batch_id{};
    render_graph::QueueClass queue = render_graph::QueueClass::graphics;
    std::vector<RenderGraphPassDiagnosticId> pass_ids{};
    std::vector<std::string> pass_debug_names{};
    std::string label{};
};

struct RenderGraphCaptureArtifactDiagnostics final {
    bool captured = false;
    RenderGraphCaptureArtifactKind kind =
        RenderGraphCaptureArtifactKind::unavailable;
    std::string artifact_label{};
    std::string topology_json{};
    std::string queue_timeline_json{};
};

struct RenderGraphCaptureDiagnostics final {
    bool available = false;
    bool enabled = false;
    std::uint32_t marker_count = 0U;
    std::uint32_t artifact_count = 0U;
    std::vector<RenderGraphCaptureMarkerDiagnostics> markers{};
    std::vector<RenderGraphCaptureArtifactDiagnostics> artifacts{};
};

struct RenderGraphRuntimeDiagnostics final {
    bool available = false;
    bool frame_compiled = false;
    bool graph_only_supported = false;
    bool graph_only_active = false;
    bool transfer_queue_requested = false;
    bool compute_queue_requested = false;
    bool multi_queue_requested = false;
    bool transfer_queue_enabled = false;
    bool compute_queue_enabled = false;
    bool multi_queue_enabled = false;
    bool graphics_fallback_active = false;
    bool dynamic_rendering_local_read_supported = false;
    bool dynamic_rendering_local_read_requested = false;
    bool dynamic_rendering_local_read_enabled = false;
    std::uint32_t compiled_pass_count = 0U;
    std::uint32_t executable_pass_count = 0U;
    std::uint32_t logical_raster_pass_count = 0U;
    std::uint32_t native_pass_group_count = 0U;
    std::uint32_t fused_raster_pass_count = 0U;
    std::uint32_t recorded_pass_count = 0U;
    std::uint32_t recorded_rendering_scope_count = 0U;
    std::uint32_t recorded_command_batch_count = 0U;
    std::uint32_t recorded_image_barrier_count = 0U;
    std::uint32_t recorded_buffer_barrier_count = 0U;
    std::uint32_t recorded_queue_transfer_batch_count = 0U;
    std::uint32_t store_elision_count = 0U;
    std::uint32_t load_inference_count = 0U;
    std::uint32_t effective_clear_attachment_count = 0U;
    std::uint32_t local_read_candidate_count = 0U;
    std::uint64_t transient_logical_total_bytes = 0U;
    std::uint64_t transient_physical_total_bytes = 0U;
    std::uint64_t transient_peak_live_bytes = 0U;
    std::uint64_t transient_saved_bytes = 0U;
    std::uint32_t transient_page_count = 0U;
    std::uint32_t alias_barrier_count = 0U;
    std::uint32_t lazy_memory_requested_count = 0U;
    std::uint32_t lazy_memory_realized_count = 0U;
    std::uint32_t lazy_memory_unavailable_count = 0U;
    std::uint32_t effective_queue_batch_count = 0U;
    std::uint32_t effective_queue_dependency_count = 0U;
    std::uint32_t effective_graphics_queue_batch_count = 0U;
    std::uint32_t effective_transfer_queue_batch_count = 0U;
    std::uint32_t effective_compute_queue_batch_count = 0U;
    std::uint32_t effective_owned_submit_batch_count = 0U;
    std::uint32_t effective_cross_queue_dependency_count = 0U;
    std::uint32_t effective_total_submit_wait_count = 0U;
    std::uint32_t effective_total_submit_signal_count = 0U;
    std::uint32_t graphics_submit_wait_count = 0U;
    std::uint32_t non_graphics_submit_batch_count = 0U;
    std::string queue_fallback_reason{};
    render_graph::NativePassLocalReadStatus dynamic_rendering_local_read_status =
        render_graph::NativePassLocalReadStatus::not_applicable;
    render_graph::NativePassLocalReadReason dynamic_rendering_local_read_reason =
        render_graph::NativePassLocalReadReason::none;
    std::vector<RenderGraphLazyMemoryResourceDiagnostics> lazy_memory_resources{};
    std::vector<render_graph::CompiledRenderGraphLivenessTopologyView>
        compile_liveness_ranges{};
    render_graph::CompiledRenderGraphTransientTopologyView
        compile_transient_memory{};
    std::vector<RenderGraphQueueBatchDiagnostics> effective_queue_batches{};
    std::vector<RenderGraphQueueDependencyDiagnostics> effective_queue_dependencies{};
    RenderGraphFrameHistoryDiagnostics frame_color_history{};
    RenderGraphTimingDiagnostics timing{};
    RenderGraphCaptureDiagnostics capture{};
};

enum class RenderGraphQueueTimelineMode : std::uint8_t {
    unavailable = 0U,
    graphics_only = 1U,
    graphics_fallback = 2U,
    transfer_enabled = 3U,
    compute_enabled = 4U,
    transfer_compute_enabled = 5U,
};

struct RenderGraphQueueTimelineBatchView final {
    RenderGraphQueueBatchDiagnosticId batch_id{};
    render_graph::QueueClass queue = render_graph::QueueClass::graphics;
    std::uint32_t pass_count = 0U;
    std::uint32_t wait_dependency_count = 0U;
    std::uint32_t signal_dependency_count = 0U;
    std::uint32_t barrier_batch_count = 0U;
    std::uint32_t submit_wait_count = 0U;
    std::uint32_t submit_signal_count = 0U;
    bool contains_host_boundary = false;
    bool submitted_on_owned_queue = false;
    std::vector<RenderGraphPassDiagnosticId> pass_ids{};
    std::vector<std::string> pass_debug_names{};
    std::vector<RenderGraphQueueDependencyDiagnosticId> wait_dependency_ids{};
    std::vector<RenderGraphQueueDependencyDiagnosticId> signal_dependency_ids{};
    std::vector<RenderGraphBarrierBatchDiagnosticId> barrier_batch_ids{};
};

struct RenderGraphQueueTimelineDependencyView final {
    RenderGraphQueueDependencyDiagnosticId dependency_id{};
    render_graph::QueueClass source_queue = render_graph::QueueClass::graphics;
    render_graph::QueueClass target_queue = render_graph::QueueClass::graphics;
    RenderGraphQueueBatchDiagnosticId source_batch_id{};
    RenderGraphQueueBatchDiagnosticId target_batch_id{};
    RenderGraphPassDiagnosticId source_pass_id{};
    RenderGraphPassDiagnosticId target_pass_id{};
    std::string source_pass_debug_name{};
    std::string target_pass_debug_name{};
    std::uint32_t resource_count = 0U;
    bool queue_transfer = false;
    bool host_boundary = false;
};

struct RenderGraphQueueTimelineView final {
    bool available = false;
    RenderGraphQueueTimelineMode mode = RenderGraphQueueTimelineMode::unavailable;
    std::string mode_name{};
    std::string fallback_reason{};
    bool transfer_requested = false;
    bool compute_requested = false;
    bool multi_queue_requested = false;
    bool transfer_enabled = false;
    bool compute_enabled = false;
    bool multi_queue_enabled = false;
    bool graphics_fallback_active = false;
    std::uint32_t batch_count = 0U;
    std::uint32_t dependency_count = 0U;
    std::uint32_t graphics_batch_count = 0U;
    std::uint32_t transfer_batch_count = 0U;
    std::uint32_t compute_batch_count = 0U;
    std::uint32_t owned_submit_batch_count = 0U;
    std::uint32_t cross_queue_dependency_count = 0U;
    std::uint32_t total_submit_wait_count = 0U;
    std::uint32_t total_submit_signal_count = 0U;
    std::vector<RenderGraphQueueTimelineBatchView> batches{};
    std::vector<RenderGraphQueueTimelineDependencyView> dependencies{};
};

[[nodiscard]] constexpr std::string_view RenderGraphQueueTimelineModeName(
    const RenderGraphQueueTimelineMode mode_) noexcept {
    switch (mode_) {
    case RenderGraphQueueTimelineMode::unavailable:
        return "unavailable";
    case RenderGraphQueueTimelineMode::graphics_only:
        return "graphics_only";
    case RenderGraphQueueTimelineMode::graphics_fallback:
        return "graphics_fallback";
    case RenderGraphQueueTimelineMode::transfer_enabled:
        return "transfer_enabled";
    case RenderGraphQueueTimelineMode::compute_enabled:
        return "compute_enabled";
    case RenderGraphQueueTimelineMode::transfer_compute_enabled:
        return "transfer_compute_enabled";
    }
    return "unknown";
}

namespace detail {

template<typename IdT>
inline void AppendDiagnosticIdListToStream(std::ostringstream& oss_,
                                           const std::vector<IdT>& values_) {
    oss_ << '[';
    for (std::size_t index = 0U; index < values_.size(); ++index) {
        if (index != 0U) {
            oss_ << ',';
        }
        oss_ << static_cast<typename IdT::underlying_type>(values_[index]);
    }
    oss_ << ']';
}

inline void AppendStringListToStream(std::ostringstream& oss_,
                                     const std::vector<std::string>& values_) {
    oss_ << '[';
    for (std::size_t index = 0U; index < values_.size(); ++index) {
        if (index != 0U) {
            oss_ << ',';
        }
        oss_ << values_[index];
    }
    oss_ << ']';
}

} // namespace detail

[[nodiscard]] constexpr RenderGraphQueueTimelineMode ResolveRenderGraphQueueTimelineMode(
    const RenderGraphRuntimeDiagnostics& diagnostics_) noexcept {
    const bool available =
        diagnostics_.frame_compiled ||
        diagnostics_.effective_queue_batch_count != 0U ||
        diagnostics_.effective_queue_dependency_count != 0U;
    if (!available) {
        return RenderGraphQueueTimelineMode::unavailable;
    }
    if (diagnostics_.graphics_fallback_active) {
        return RenderGraphQueueTimelineMode::graphics_fallback;
    }
    if (diagnostics_.transfer_queue_enabled && diagnostics_.compute_queue_enabled) {
        return RenderGraphQueueTimelineMode::transfer_compute_enabled;
    }
    if (diagnostics_.transfer_queue_enabled) {
        return RenderGraphQueueTimelineMode::transfer_enabled;
    }
    if (diagnostics_.compute_queue_enabled) {
        return RenderGraphQueueTimelineMode::compute_enabled;
    }
    return RenderGraphQueueTimelineMode::graphics_only;
}

[[nodiscard]] inline RenderGraphQueueTimelineView BuildRenderGraphQueueTimelineView(
    const RenderGraphRuntimeDiagnostics& diagnostics_) {
    if constexpr (!RuntimeDiagnosticsAvailableInBuild()) {
        return {};
    }
    RenderGraphQueueTimelineView view{};
    view.available =
        diagnostics_.frame_compiled ||
        diagnostics_.effective_queue_batch_count != 0U ||
        diagnostics_.effective_queue_dependency_count != 0U;
    view.mode = ResolveRenderGraphQueueTimelineMode(diagnostics_);
    view.mode_name = std::string(RenderGraphQueueTimelineModeName(view.mode));
    view.fallback_reason = diagnostics_.queue_fallback_reason;
    view.transfer_requested = diagnostics_.transfer_queue_requested;
    view.compute_requested = diagnostics_.compute_queue_requested;
    view.multi_queue_requested = diagnostics_.multi_queue_requested;
    view.transfer_enabled = diagnostics_.transfer_queue_enabled;
    view.compute_enabled = diagnostics_.compute_queue_enabled;
    view.multi_queue_enabled = diagnostics_.multi_queue_enabled;
    view.graphics_fallback_active = diagnostics_.graphics_fallback_active;
    view.batch_count = diagnostics_.effective_queue_batch_count != 0U
        ? diagnostics_.effective_queue_batch_count
        : static_cast<std::uint32_t>(diagnostics_.effective_queue_batches.size());
    view.dependency_count = diagnostics_.effective_queue_dependency_count != 0U
        ? diagnostics_.effective_queue_dependency_count
        : static_cast<std::uint32_t>(diagnostics_.effective_queue_dependencies.size());
    view.graphics_batch_count = diagnostics_.effective_graphics_queue_batch_count;
    view.transfer_batch_count = diagnostics_.effective_transfer_queue_batch_count;
    view.compute_batch_count = diagnostics_.effective_compute_queue_batch_count;
    view.owned_submit_batch_count = diagnostics_.effective_owned_submit_batch_count;
    view.cross_queue_dependency_count = diagnostics_.effective_cross_queue_dependency_count;
    view.total_submit_wait_count = diagnostics_.effective_total_submit_wait_count;
    view.total_submit_signal_count = diagnostics_.effective_total_submit_signal_count;
    const bool derive_queue_batch_counts =
        diagnostics_.effective_graphics_queue_batch_count == 0U &&
        diagnostics_.effective_transfer_queue_batch_count == 0U &&
        diagnostics_.effective_compute_queue_batch_count == 0U;

    view.batches.reserve(diagnostics_.effective_queue_batches.size());
    for (const auto& batch_ : diagnostics_.effective_queue_batches) {
        RenderGraphQueueTimelineBatchView batch_view{};
        batch_view.batch_id = batch_.batch_id;
        batch_view.queue = batch_.queue;
        batch_view.pass_count = static_cast<std::uint32_t>(batch_.pass_ids.size());
        batch_view.wait_dependency_count =
            static_cast<std::uint32_t>(batch_.wait_dependency_ids.size());
        batch_view.signal_dependency_count =
            static_cast<std::uint32_t>(batch_.signal_dependency_ids.size());
        batch_view.barrier_batch_count =
            static_cast<std::uint32_t>(batch_.barrier_batch_ids.size());
        batch_view.submit_wait_count = batch_.submit_wait_count;
        batch_view.submit_signal_count = batch_.submit_signal_count;
        batch_view.contains_host_boundary = batch_.contains_host_boundary;
        batch_view.submitted_on_owned_queue = batch_.submitted_on_owned_queue;
        batch_view.pass_ids = batch_.pass_ids;
        batch_view.pass_debug_names = batch_.pass_debug_names;
        batch_view.wait_dependency_ids = batch_.wait_dependency_ids;
        batch_view.signal_dependency_ids = batch_.signal_dependency_ids;
        batch_view.barrier_batch_ids = batch_.barrier_batch_ids;

        if (derive_queue_batch_counts) {
            if (batch_view.queue == render_graph::QueueClass::graphics) {
                ++view.graphics_batch_count;
            } else if (batch_view.queue == render_graph::QueueClass::transfer) {
                ++view.transfer_batch_count;
            } else if (batch_view.queue == render_graph::QueueClass::compute) {
                ++view.compute_batch_count;
            }
        }

        if (diagnostics_.effective_owned_submit_batch_count == 0U &&
            batch_view.submitted_on_owned_queue) {
            ++view.owned_submit_batch_count;
        }
        if (diagnostics_.effective_total_submit_wait_count == 0U) {
            view.total_submit_wait_count += batch_.submit_wait_count;
        }
        if (diagnostics_.effective_total_submit_signal_count == 0U) {
            view.total_submit_signal_count += batch_.submit_signal_count;
        }
        view.batches.push_back(std::move(batch_view));
    }

    view.dependencies.reserve(diagnostics_.effective_queue_dependencies.size());
    for (const auto& dependency_ : diagnostics_.effective_queue_dependencies) {
        RenderGraphQueueTimelineDependencyView dependency_view{};
        dependency_view.dependency_id = dependency_.dependency_id;
        dependency_view.source_queue = dependency_.source_queue;
        dependency_view.target_queue = dependency_.target_queue;
        dependency_view.source_batch_id = dependency_.source_batch_id;
        dependency_view.target_batch_id = dependency_.target_batch_id;
        dependency_view.source_pass_id = dependency_.source_pass_id;
        dependency_view.target_pass_id = dependency_.target_pass_id;
        dependency_view.source_pass_debug_name = dependency_.source_pass_debug_name;
        dependency_view.target_pass_debug_name = dependency_.target_pass_debug_name;
        dependency_view.resource_count = dependency_.resource_count;
        dependency_view.queue_transfer = dependency_.queue_transfer;
        dependency_view.host_boundary = dependency_.host_boundary;
        if (diagnostics_.effective_cross_queue_dependency_count == 0U &&
            (dependency_view.queue_transfer ||
             dependency_view.source_queue != dependency_view.target_queue)) {
            ++view.cross_queue_dependency_count;
        }
        view.dependencies.push_back(std::move(dependency_view));
    }

    return view;
}

[[nodiscard]] inline std::string EscapeRenderGraphQueueTimelineJsonString(
    std::string_view value_) {
    std::string escaped{};
    escaped.reserve(value_.size());
    for (const char ch : value_) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

[[nodiscard]] inline std::string BuildRenderGraphQueueTimelineDebugString(
    const RenderGraphQueueTimelineView& view_) {
    if constexpr (!RuntimeDiagnosticsAvailableInBuild()) {
        (void)view_;
        return {};
    }
    std::ostringstream oss{};
    oss << "requested transfer=" << (view_.transfer_requested ? 1 : 0)
        << " compute=" << (view_.compute_requested ? 1 : 0)
        << " multi=" << (view_.multi_queue_requested ? 1 : 0) << '\n';
    oss << "enabled transfer=" << (view_.transfer_enabled ? 1 : 0)
        << " compute=" << (view_.compute_enabled ? 1 : 0)
        << " multi=" << (view_.multi_queue_enabled ? 1 : 0)
        << " graphics_fallback=" << (view_.graphics_fallback_active ? 1 : 0)
        << '\n';
    oss << "effective_batches=" << view_.batch_count
        << " effective_dependencies=" << view_.dependency_count
        << " graphics_batches=" << view_.graphics_batch_count
        << " transfer_batches=" << view_.transfer_batch_count
        << " compute_batches=" << view_.compute_batch_count
        << " owned_submit_batches=" << view_.owned_submit_batch_count
        << " cross_queue_dependencies=" << view_.cross_queue_dependency_count
        << " total_submit_waits=" << view_.total_submit_wait_count
        << " total_submit_signals=" << view_.total_submit_signal_count
        << '\n';
    for (const auto& batch_ : view_.batches) {
        oss << "batch[" << static_cast<RenderGraphQueueBatchDiagnosticId::underlying_type>(batch_.batch_id)
            << "] queue=" << RenderGraphQueueClassName(batch_.queue)
            << " pass_ids=";
        detail::AppendDiagnosticIdListToStream(oss, batch_.pass_ids);
        oss << " passes=";
        detail::AppendStringListToStream(oss, batch_.pass_debug_names);
        oss << " wait_deps=";
        detail::AppendDiagnosticIdListToStream(oss, batch_.wait_dependency_ids);
        oss << " signal_deps=";
        detail::AppendDiagnosticIdListToStream(oss, batch_.signal_dependency_ids);
        oss << " barrier_batches=";
        detail::AppendDiagnosticIdListToStream(oss, batch_.barrier_batch_ids);
        oss << " submit_waits=" << batch_.submit_wait_count
            << " submit_signals=" << batch_.submit_signal_count
            << " host_boundary=" << (batch_.contains_host_boundary ? 1 : 0)
            << " owned_submit=" << (batch_.submitted_on_owned_queue ? 1 : 0)
            << '\n';
    }
    for (const auto& dependency_ : view_.dependencies) {
        oss << "dependency[" << static_cast<RenderGraphQueueDependencyDiagnosticId::underlying_type>(dependency_.dependency_id)
            << "] " << RenderGraphQueueClassName(dependency_.source_queue)
            << '[' << static_cast<RenderGraphQueueBatchDiagnosticId::underlying_type>(dependency_.source_batch_id)
            << "] -> " << RenderGraphQueueClassName(dependency_.target_queue)
            << '[' << static_cast<RenderGraphQueueBatchDiagnosticId::underlying_type>(dependency_.target_batch_id)
            << "] source_pass_id="
            << static_cast<RenderGraphPassDiagnosticId::underlying_type>(dependency_.source_pass_id)
            << " target_pass_id="
            << static_cast<RenderGraphPassDiagnosticId::underlying_type>(dependency_.target_pass_id)
            << " source_pass=" << dependency_.source_pass_debug_name
            << " target_pass=" << dependency_.target_pass_debug_name
            << " resources=" << dependency_.resource_count
            << " queue_transfer=" << (dependency_.queue_transfer ? 1 : 0)
            << " host_boundary=" << (dependency_.host_boundary ? 1 : 0)
            << '\n';
    }
    return oss.str();
}

[[nodiscard]] inline std::string BuildRenderGraphQueueTimelineDebugString(
    const RenderGraphRuntimeDiagnostics& diagnostics_) {
    return BuildRenderGraphQueueTimelineDebugString(
        BuildRenderGraphQueueTimelineView(diagnostics_));
}

[[nodiscard]] inline std::string BuildRenderGraphQueueTimelineJson(
    const RenderGraphQueueTimelineView& view_) {
    if constexpr (!RuntimeDiagnosticsAvailableInBuild()) {
        (void)view_;
        return {};
    }
    std::ostringstream oss{};
    oss << "{\n"
        << "  \"available\": " << (view_.available ? "true" : "false") << ",\n"
        << "  \"mode\": \"" << EscapeRenderGraphQueueTimelineJsonString(view_.mode_name) << "\",\n"
        << "  \"fallbackReason\": \""
        << EscapeRenderGraphQueueTimelineJsonString(view_.fallback_reason) << "\",\n"
        << "  \"requested\": {\n"
        << "    \"transfer\": " << (view_.transfer_requested ? "true" : "false") << ",\n"
        << "    \"compute\": " << (view_.compute_requested ? "true" : "false") << ",\n"
        << "    \"multiQueue\": " << (view_.multi_queue_requested ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"enabled\": {\n"
        << "    \"transfer\": " << (view_.transfer_enabled ? "true" : "false") << ",\n"
        << "    \"compute\": " << (view_.compute_enabled ? "true" : "false") << ",\n"
        << "    \"multiQueue\": " << (view_.multi_queue_enabled ? "true" : "false") << ",\n"
        << "    \"graphicsFallback\": " << (view_.graphics_fallback_active ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"counts\": {\n"
        << "    \"batches\": " << view_.batch_count << ",\n"
        << "    \"dependencies\": " << view_.dependency_count << ",\n"
        << "    \"graphicsBatches\": " << view_.graphics_batch_count << ",\n"
        << "    \"transferBatches\": " << view_.transfer_batch_count << ",\n"
        << "    \"computeBatches\": " << view_.compute_batch_count << ",\n"
        << "    \"ownedSubmitBatches\": " << view_.owned_submit_batch_count << ",\n"
        << "    \"crossQueueDependencies\": " << view_.cross_queue_dependency_count << ",\n"
        << "    \"totalSubmitWaits\": " << view_.total_submit_wait_count << ",\n"
        << "    \"totalSubmitSignals\": " << view_.total_submit_signal_count << "\n"
        << "  },\n"
        << "  \"batches\": [\n";
    for (std::size_t batch_index = 0U; batch_index < view_.batches.size(); ++batch_index) {
        const auto& batch_ = view_.batches[batch_index];
        oss << "    {\n"
            << "      \"id\": " << static_cast<RenderGraphQueueBatchDiagnosticId::underlying_type>(batch_.batch_id) << ",\n"
            << "      \"queue\": \"" << EscapeRenderGraphQueueTimelineJsonString(RenderGraphQueueClassName(batch_.queue)) << "\",\n"
            << "      \"passCount\": " << batch_.pass_count << ",\n"
            << "      \"passIds\": [";
        for (std::size_t pass_index = 0U; pass_index < batch_.pass_ids.size(); ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << static_cast<RenderGraphPassDiagnosticId::underlying_type>(batch_.pass_ids[pass_index]);
        }
        oss << "],\n"
            << "      \"waitDependencyCount\": " << batch_.wait_dependency_count << ",\n"
            << "      \"waitDependencyIds\": [";
        for (std::size_t dependency_index = 0U;
             dependency_index < batch_.wait_dependency_ids.size();
             ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << static_cast<RenderGraphQueueDependencyDiagnosticId::underlying_type>(
                batch_.wait_dependency_ids[dependency_index]);
        }
        oss << "],\n"
            << "      \"signalDependencyCount\": " << batch_.signal_dependency_count << ",\n"
            << "      \"signalDependencyIds\": [";
        for (std::size_t dependency_index = 0U;
             dependency_index < batch_.signal_dependency_ids.size();
             ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << static_cast<RenderGraphQueueDependencyDiagnosticId::underlying_type>(
                batch_.signal_dependency_ids[dependency_index]);
        }
        oss << "],\n"
            << "      \"barrierBatchCount\": " << batch_.barrier_batch_count << ",\n"
            << "      \"barrierBatchIds\": [";
        for (std::size_t barrier_index = 0U;
             barrier_index < batch_.barrier_batch_ids.size();
             ++barrier_index) {
            if (barrier_index != 0U) {
                oss << ", ";
            }
            oss << static_cast<RenderGraphBarrierBatchDiagnosticId::underlying_type>(
                batch_.barrier_batch_ids[barrier_index]);
        }
        oss << "],\n"
            << "      \"submitWaitCount\": " << batch_.submit_wait_count << ",\n"
            << "      \"submitSignalCount\": " << batch_.submit_signal_count << ",\n"
            << "      \"containsHostBoundary\": " << (batch_.contains_host_boundary ? "true" : "false") << ",\n"
            << "      \"submittedOnOwnedQueue\": " << (batch_.submitted_on_owned_queue ? "true" : "false") << ",\n"
            << "      \"passes\": [";
        for (std::size_t pass_index = 0U; pass_index < batch_.pass_debug_names.size(); ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << '"' << EscapeRenderGraphQueueTimelineJsonString(batch_.pass_debug_names[pass_index]) << '"';
        }
        oss << "]\n"
            << "    }";
        if (batch_index + 1U != view_.batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n"
        << "  \"dependencies\": [\n";
    for (std::size_t dependency_index = 0U;
         dependency_index < view_.dependencies.size();
         ++dependency_index) {
        const auto& dependency_ = view_.dependencies[dependency_index];
        oss << "    {\n"
            << "      \"id\": " << static_cast<RenderGraphQueueDependencyDiagnosticId::underlying_type>(dependency_.dependency_id) << ",\n"
            << "      \"sourceQueue\": \"" << EscapeRenderGraphQueueTimelineJsonString(RenderGraphQueueClassName(dependency_.source_queue)) << "\",\n"
            << "      \"targetQueue\": \"" << EscapeRenderGraphQueueTimelineJsonString(RenderGraphQueueClassName(dependency_.target_queue)) << "\",\n"
            << "      \"sourceBatchId\": " << static_cast<RenderGraphQueueBatchDiagnosticId::underlying_type>(dependency_.source_batch_id) << ",\n"
            << "      \"targetBatchId\": " << static_cast<RenderGraphQueueBatchDiagnosticId::underlying_type>(dependency_.target_batch_id) << ",\n"
            << "      \"sourcePassId\": " << static_cast<RenderGraphPassDiagnosticId::underlying_type>(dependency_.source_pass_id) << ",\n"
            << "      \"targetPassId\": " << static_cast<RenderGraphPassDiagnosticId::underlying_type>(dependency_.target_pass_id) << ",\n"
            << "      \"sourcePass\": \"" << EscapeRenderGraphQueueTimelineJsonString(dependency_.source_pass_debug_name) << "\",\n"
            << "      \"targetPass\": \"" << EscapeRenderGraphQueueTimelineJsonString(dependency_.target_pass_debug_name) << "\",\n"
            << "      \"resourceCount\": " << dependency_.resource_count << ",\n"
            << "      \"queueTransfer\": " << (dependency_.queue_transfer ? "true" : "false") << ",\n"
            << "      \"hostBoundary\": " << (dependency_.host_boundary ? "true" : "false") << "\n"
            << "    }";
        if (dependency_index + 1U != view_.dependencies.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ]\n"
        << '}';
    return oss.str();
}

[[nodiscard]] inline std::string BuildRenderGraphQueueTimelineJson(
    const RenderGraphRuntimeDiagnostics& diagnostics_) {
    return BuildRenderGraphQueueTimelineJson(
        BuildRenderGraphQueueTimelineView(diagnostics_));
}

struct RenderGraphRuntimeFeatureDiagnosticsView final {
    bool available = false;
    bool frame_compiled = false;
    bool graph_only_supported = false;
    bool graph_only_active = false;
    bool dynamic_rendering_local_read_supported = false;
    bool dynamic_rendering_local_read_requested = false;
    bool dynamic_rendering_local_read_enabled = false;
};

struct RenderGraphCompileDiagnosticsView final {
    std::uint32_t compiled_pass_count = 0U;
    std::uint32_t executable_pass_count = 0U;
    std::uint32_t logical_raster_pass_count = 0U;
    std::uint32_t native_pass_group_count = 0U;
    std::uint32_t fused_raster_pass_count = 0U;
    std::uint32_t store_elision_count = 0U;
    std::uint32_t load_inference_count = 0U;
    std::uint32_t effective_clear_attachment_count = 0U;
    std::uint32_t local_read_candidate_count = 0U;
    render_graph::NativePassLocalReadStatus dynamic_rendering_local_read_status =
        render_graph::NativePassLocalReadStatus::not_applicable;
    render_graph::NativePassLocalReadReason dynamic_rendering_local_read_reason =
        render_graph::NativePassLocalReadReason::none;
    std::uint64_t transient_logical_total_bytes = 0U;
    std::uint64_t transient_physical_total_bytes = 0U;
    std::uint64_t transient_peak_live_bytes = 0U;
    std::uint64_t transient_saved_bytes = 0U;
    std::uint32_t transient_page_count = 0U;
    std::uint32_t alias_barrier_count = 0U;
    std::uint32_t lazy_memory_requested_count = 0U;
    std::uint32_t lazy_memory_realized_count = 0U;
    std::uint32_t lazy_memory_unavailable_count = 0U;
    const std::vector<RenderGraphLazyMemoryResourceDiagnostics>* lazy_memory_resources = nullptr;
    const std::vector<render_graph::CompiledRenderGraphLivenessTopologyView>* liveness_ranges =
        nullptr;
    const render_graph::CompiledRenderGraphTransientTopologyView* transient_memory =
        nullptr;
};

struct RenderGraphRecordDiagnosticsView final {
    std::uint32_t recorded_pass_count = 0U;
    std::uint32_t recorded_rendering_scope_count = 0U;
    std::uint32_t recorded_command_batch_count = 0U;
    std::uint32_t recorded_image_barrier_count = 0U;
    std::uint32_t recorded_buffer_barrier_count = 0U;
    std::uint32_t recorded_queue_transfer_batch_count = 0U;
};

struct RenderGraphSubmissionDiagnosticsView final {
    bool transfer_queue_requested = false;
    bool compute_queue_requested = false;
    bool multi_queue_requested = false;
    bool transfer_queue_enabled = false;
    bool compute_queue_enabled = false;
    bool multi_queue_enabled = false;
    bool graphics_fallback_active = false;
    std::string_view queue_fallback_reason{};
    std::uint32_t effective_queue_batch_count = 0U;
    std::uint32_t effective_queue_dependency_count = 0U;
    std::uint32_t effective_graphics_queue_batch_count = 0U;
    std::uint32_t effective_transfer_queue_batch_count = 0U;
    std::uint32_t effective_compute_queue_batch_count = 0U;
    std::uint32_t effective_owned_submit_batch_count = 0U;
    std::uint32_t effective_cross_queue_dependency_count = 0U;
    std::uint32_t effective_total_submit_wait_count = 0U;
    std::uint32_t effective_total_submit_signal_count = 0U;
    std::uint32_t graphics_submit_wait_count = 0U;
    std::uint32_t non_graphics_submit_batch_count = 0U;
    const std::vector<RenderGraphQueueBatchDiagnostics>* effective_queue_batches = nullptr;
    const std::vector<RenderGraphQueueDependencyDiagnostics>* effective_queue_dependencies = nullptr;
};

struct RenderGraphTimingDiagnosticsView final {
    bool available = false;
    bool enabled = false;
    bool gpu_timestamp_supported = false;
    RenderGraphTimingDomain domain = RenderGraphTimingDomain::unavailable;
    std::uint32_t queue_batch_range_count = 0U;
    std::uint32_t resolved_queue_batch_range_count = 0U;
    std::uint64_t total_duration_ns = 0U;
    const std::vector<RenderGraphQueueBatchTimingDiagnostics>* queue_batch_ranges =
        nullptr;
};

struct RenderGraphCaptureDiagnosticsView final {
    bool available = false;
    bool enabled = false;
    std::uint32_t marker_count = 0U;
    std::uint32_t artifact_count = 0U;
    const std::vector<RenderGraphCaptureMarkerDiagnostics>* markers = nullptr;
    const std::vector<RenderGraphCaptureArtifactDiagnostics>* artifacts = nullptr;
};

struct RenderGraphObservabilityView final {
    RenderGraphRuntimeFeatureDiagnosticsView runtime{};
    RenderGraphCompileDiagnosticsView compile{};
    RenderGraphRecordDiagnosticsView record{};
    RenderGraphSubmissionDiagnosticsView submission{};
    RenderGraphTimingDiagnosticsView timing{};
    RenderGraphCaptureDiagnosticsView capture{};
};

[[nodiscard]] inline RenderGraphObservabilityView BuildRenderGraphObservabilityView(
    const RenderGraphRuntimeDiagnostics& diagnostics_) noexcept {
    if constexpr (!RuntimeDiagnosticsAvailableInBuild()) {
        (void)diagnostics_;
        return {};
    }
    return RenderGraphObservabilityView{
        .runtime = {
            .available = diagnostics_.available,
            .frame_compiled = diagnostics_.frame_compiled,
            .graph_only_supported = diagnostics_.graph_only_supported,
            .graph_only_active = diagnostics_.graph_only_active,
            .dynamic_rendering_local_read_supported =
                diagnostics_.dynamic_rendering_local_read_supported,
            .dynamic_rendering_local_read_requested =
                diagnostics_.dynamic_rendering_local_read_requested,
            .dynamic_rendering_local_read_enabled =
                diagnostics_.dynamic_rendering_local_read_enabled,
        },
        .compile = {
            .compiled_pass_count = diagnostics_.compiled_pass_count,
            .executable_pass_count = diagnostics_.executable_pass_count,
            .logical_raster_pass_count = diagnostics_.logical_raster_pass_count,
            .native_pass_group_count = diagnostics_.native_pass_group_count,
            .fused_raster_pass_count = diagnostics_.fused_raster_pass_count,
            .store_elision_count = diagnostics_.store_elision_count,
            .load_inference_count = diagnostics_.load_inference_count,
            .effective_clear_attachment_count =
                diagnostics_.effective_clear_attachment_count,
            .local_read_candidate_count = diagnostics_.local_read_candidate_count,
            .dynamic_rendering_local_read_status =
                diagnostics_.dynamic_rendering_local_read_status,
            .dynamic_rendering_local_read_reason =
                diagnostics_.dynamic_rendering_local_read_reason,
            .transient_logical_total_bytes =
                diagnostics_.transient_logical_total_bytes,
            .transient_physical_total_bytes =
                diagnostics_.transient_physical_total_bytes,
            .transient_peak_live_bytes = diagnostics_.transient_peak_live_bytes,
            .transient_saved_bytes = diagnostics_.transient_saved_bytes,
            .transient_page_count = diagnostics_.transient_page_count,
            .alias_barrier_count = diagnostics_.alias_barrier_count,
            .lazy_memory_requested_count = diagnostics_.lazy_memory_requested_count,
            .lazy_memory_realized_count = diagnostics_.lazy_memory_realized_count,
            .lazy_memory_unavailable_count =
                diagnostics_.lazy_memory_unavailable_count,
            .lazy_memory_resources = &diagnostics_.lazy_memory_resources,
            .liveness_ranges = &diagnostics_.compile_liveness_ranges,
            .transient_memory = &diagnostics_.compile_transient_memory,
        },
        .record = {
            .recorded_pass_count = diagnostics_.recorded_pass_count,
            .recorded_rendering_scope_count =
                diagnostics_.recorded_rendering_scope_count,
            .recorded_command_batch_count =
                diagnostics_.recorded_command_batch_count,
            .recorded_image_barrier_count =
                diagnostics_.recorded_image_barrier_count,
            .recorded_buffer_barrier_count =
                diagnostics_.recorded_buffer_barrier_count,
            .recorded_queue_transfer_batch_count =
                diagnostics_.recorded_queue_transfer_batch_count,
        },
        .submission = {
            .transfer_queue_requested = diagnostics_.transfer_queue_requested,
            .compute_queue_requested = diagnostics_.compute_queue_requested,
            .multi_queue_requested = diagnostics_.multi_queue_requested,
            .transfer_queue_enabled = diagnostics_.transfer_queue_enabled,
            .compute_queue_enabled = diagnostics_.compute_queue_enabled,
            .multi_queue_enabled = diagnostics_.multi_queue_enabled,
            .graphics_fallback_active = diagnostics_.graphics_fallback_active,
            .queue_fallback_reason = diagnostics_.queue_fallback_reason,
            .effective_queue_batch_count = diagnostics_.effective_queue_batch_count,
            .effective_queue_dependency_count =
                diagnostics_.effective_queue_dependency_count,
            .effective_graphics_queue_batch_count =
                diagnostics_.effective_graphics_queue_batch_count,
            .effective_transfer_queue_batch_count =
                diagnostics_.effective_transfer_queue_batch_count,
            .effective_compute_queue_batch_count =
                diagnostics_.effective_compute_queue_batch_count,
            .effective_owned_submit_batch_count =
                diagnostics_.effective_owned_submit_batch_count,
            .effective_cross_queue_dependency_count =
                diagnostics_.effective_cross_queue_dependency_count,
            .effective_total_submit_wait_count =
                diagnostics_.effective_total_submit_wait_count,
            .effective_total_submit_signal_count =
                diagnostics_.effective_total_submit_signal_count,
            .graphics_submit_wait_count =
                diagnostics_.graphics_submit_wait_count,
            .non_graphics_submit_batch_count =
                diagnostics_.non_graphics_submit_batch_count,
            .effective_queue_batches = &diagnostics_.effective_queue_batches,
            .effective_queue_dependencies =
                &diagnostics_.effective_queue_dependencies,
        },
        .timing = {
            .available = diagnostics_.timing.available,
            .enabled = diagnostics_.timing.enabled,
            .gpu_timestamp_supported =
                diagnostics_.timing.gpu_timestamp_supported,
            .domain = diagnostics_.timing.domain,
            .queue_batch_range_count =
                diagnostics_.timing.queue_batch_range_count,
            .resolved_queue_batch_range_count =
                diagnostics_.timing.resolved_queue_batch_range_count,
            .total_duration_ns = diagnostics_.timing.total_duration_ns,
            .queue_batch_ranges = &diagnostics_.timing.queue_batch_ranges,
        },
        .capture = {
            .available = diagnostics_.capture.available,
            .enabled = diagnostics_.capture.enabled,
            .marker_count = diagnostics_.capture.marker_count,
            .artifact_count = diagnostics_.capture.artifact_count,
            .markers = &diagnostics_.capture.markers,
            .artifacts = &diagnostics_.capture.artifacts,
        },
    };
}

struct RuntimeFrameDiagnosticsV2 final {
    bool collected = false;
    DiagnosticsLevel level = DiagnosticsLevel::Off;

    FrameStats frame{};
    SwapchainStats swapchain{};
    QueueTimelineStats queues{};
    CommandStats commands{};

    vr::render::UploadFrameStats upload{};
    DescriptorStats descriptor{};
    BindlessStats bindless{};
    vr::render::PipelineHostStats pipeline{};
    vr::render::RenderTargetHostStats render_target{};

    vr::asset::TextureHostStats texture{};
    vr::render::FrameComposerHostStats frame_composer{};
    vr::render::IblHostStats ibl{};
    vr::render::IblBakeHostStats ibl_bake{};
    vr::text::GlyphAtlasHostStats glyph_atlas{};
    vr::text::GlyphUploadHostStats glyph_upload{};

    vr::particle::ParticleUploadHostStats particle_upload{};
    vr::particle::ParticleSimulationHostStats particle_simulation{};
    ParticleRenderStats particle_render{};

    AllocationStats allocations{};
    RenderGraphRuntimeDiagnostics render_graph{};
};

[[nodiscard]] constexpr bool DiagnosticsCollectsFrameData(DiagnosticsLevel level_) noexcept {
    return ResolveRuntimeDiagnosticsLevelForBuild(level_) != DiagnosticsLevel::Off;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsServiceCounters(DiagnosticsLevel level_) noexcept {
    return ResolveRuntimeDiagnosticsLevelForBuild(level_) >=
        DiagnosticsLevel::CountersOnly;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsDetailedData(DiagnosticsLevel level_) noexcept {
    return ResolveRuntimeDiagnosticsLevelForBuild(level_) >=
        DiagnosticsLevel::Detailed;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsGpuTiming(DiagnosticsLevel level_) noexcept {
    return ResolveRuntimeDiagnosticsLevelForBuild(level_) >=
        DiagnosticsLevel::GpuTiming;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsCapture(DiagnosticsLevel level_) noexcept {
    return ResolveRuntimeDiagnosticsLevelForBuild(level_) >=
        DiagnosticsLevel::Capture;
}

} // namespace vr::runtime

