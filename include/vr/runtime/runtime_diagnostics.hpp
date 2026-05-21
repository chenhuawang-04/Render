#pragma once

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

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
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

struct RuntimeDiagnosticsCreateInfo final {
    DiagnosticsLevel level = DiagnosticsLevel::Off;
};

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
    std::uint32_t logical_resource_index = 0U;
    std::string debug_name{};
    bool requested = false;
    bool realized = false;
    std::string unavailable_reason{};
};

struct RenderGraphQueueBatchDiagnostics final {
    std::uint32_t batch_index = 0U;
    std::string queue_name{};
    std::vector<std::uint32_t> pass_indices{};
    std::vector<std::string> pass_debug_names{};
    std::vector<std::uint32_t> wait_dependency_indices{};
    std::vector<std::uint32_t> signal_dependency_indices{};
    std::vector<std::uint32_t> barrier_batch_indices{};
    std::uint32_t submit_wait_count = 0U;
    std::uint32_t submit_signal_count = 0U;
    bool contains_host_boundary = false;
    bool submitted_on_owned_queue = false;
};

struct RenderGraphQueueDependencyDiagnostics final {
    std::uint32_t dependency_index = 0U;
    std::string source_queue_name{};
    std::string target_queue_name{};
    std::uint32_t source_batch_index = 0U;
    std::uint32_t target_batch_index = 0U;
    std::uint32_t source_pass_index = 0U;
    std::uint32_t target_pass_index = 0U;
    std::string source_pass_debug_name{};
    std::string target_pass_debug_name{};
    std::uint32_t resource_count = 0U;
    bool queue_transfer = false;
    bool host_boundary = false;
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
    std::uint32_t graphics_submit_wait_count = 0U;
    std::uint32_t non_graphics_submit_batch_count = 0U;
    std::string queue_fallback_reason{};
    std::string dynamic_rendering_local_read_status{};
    std::string dynamic_rendering_local_read_reason{};
    std::string effective_queue_timeline_debug_string{};
    std::string effective_queue_timeline_json{};
    std::vector<RenderGraphLazyMemoryResourceDiagnostics> lazy_memory_resources{};
    std::vector<RenderGraphQueueBatchDiagnostics> effective_queue_batches{};
    std::vector<RenderGraphQueueDependencyDiagnostics> effective_queue_dependencies{};
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
    std::uint32_t batch_index = 0U;
    std::string queue_name{};
    std::uint32_t pass_count = 0U;
    std::uint32_t wait_dependency_count = 0U;
    std::uint32_t signal_dependency_count = 0U;
    std::uint32_t barrier_batch_count = 0U;
    std::uint32_t submit_wait_count = 0U;
    std::uint32_t submit_signal_count = 0U;
    bool contains_host_boundary = false;
    bool submitted_on_owned_queue = false;
    std::vector<std::string> pass_debug_names{};
};

struct RenderGraphQueueTimelineDependencyView final {
    std::uint32_t dependency_index = 0U;
    std::string source_queue_name{};
    std::string target_queue_name{};
    std::uint32_t source_batch_index = 0U;
    std::uint32_t target_batch_index = 0U;
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
    view.batch_count = diagnostics_.effective_queue_batch_count;
    view.dependency_count = diagnostics_.effective_queue_dependency_count;

    view.batches.reserve(diagnostics_.effective_queue_batches.size());
    for (const auto& batch_ : diagnostics_.effective_queue_batches) {
        RenderGraphQueueTimelineBatchView batch_view{};
        batch_view.batch_index = batch_.batch_index;
        batch_view.queue_name = batch_.queue_name;
        batch_view.pass_count = static_cast<std::uint32_t>(batch_.pass_debug_names.size());
        batch_view.wait_dependency_count = static_cast<std::uint32_t>(batch_.wait_dependency_indices.size());
        batch_view.signal_dependency_count = static_cast<std::uint32_t>(batch_.signal_dependency_indices.size());
        batch_view.barrier_batch_count = static_cast<std::uint32_t>(batch_.barrier_batch_indices.size());
        batch_view.submit_wait_count = batch_.submit_wait_count;
        batch_view.submit_signal_count = batch_.submit_signal_count;
        batch_view.contains_host_boundary = batch_.contains_host_boundary;
        batch_view.submitted_on_owned_queue = batch_.submitted_on_owned_queue;
        batch_view.pass_debug_names = batch_.pass_debug_names;

        if (batch_view.queue_name == "graphics") {
            ++view.graphics_batch_count;
        } else if (batch_view.queue_name == "transfer") {
            ++view.transfer_batch_count;
        } else if (batch_view.queue_name == "compute") {
            ++view.compute_batch_count;
        }

        if (batch_view.submitted_on_owned_queue) {
            ++view.owned_submit_batch_count;
        }
        view.total_submit_wait_count += batch_.submit_wait_count;
        view.total_submit_signal_count += batch_.submit_signal_count;
        view.batches.push_back(std::move(batch_view));
    }

    view.dependencies.reserve(diagnostics_.effective_queue_dependencies.size());
    for (const auto& dependency_ : diagnostics_.effective_queue_dependencies) {
        RenderGraphQueueTimelineDependencyView dependency_view{};
        dependency_view.dependency_index = dependency_.dependency_index;
        dependency_view.source_queue_name = dependency_.source_queue_name;
        dependency_view.target_queue_name = dependency_.target_queue_name;
        dependency_view.source_batch_index = dependency_.source_batch_index;
        dependency_view.target_batch_index = dependency_.target_batch_index;
        dependency_view.source_pass_debug_name = dependency_.source_pass_debug_name;
        dependency_view.target_pass_debug_name = dependency_.target_pass_debug_name;
        dependency_view.resource_count = dependency_.resource_count;
        dependency_view.queue_transfer = dependency_.queue_transfer;
        dependency_view.host_boundary = dependency_.host_boundary;
        if (dependency_view.queue_transfer ||
            dependency_view.source_queue_name != dependency_view.target_queue_name) {
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

[[nodiscard]] inline std::string BuildRenderGraphQueueTimelineJson(
    const RenderGraphQueueTimelineView& view_) {
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
            << "      \"index\": " << batch_.batch_index << ",\n"
            << "      \"queue\": \"" << EscapeRenderGraphQueueTimelineJsonString(batch_.queue_name) << "\",\n"
            << "      \"passCount\": " << batch_.pass_count << ",\n"
            << "      \"waitDependencyCount\": " << batch_.wait_dependency_count << ",\n"
            << "      \"signalDependencyCount\": " << batch_.signal_dependency_count << ",\n"
            << "      \"barrierBatchCount\": " << batch_.barrier_batch_count << ",\n"
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
            << "      \"index\": " << dependency_.dependency_index << ",\n"
            << "      \"sourceQueue\": \"" << EscapeRenderGraphQueueTimelineJsonString(dependency_.source_queue_name) << "\",\n"
            << "      \"targetQueue\": \"" << EscapeRenderGraphQueueTimelineJsonString(dependency_.target_queue_name) << "\",\n"
            << "      \"sourceBatchIndex\": " << dependency_.source_batch_index << ",\n"
            << "      \"targetBatchIndex\": " << dependency_.target_batch_index << ",\n"
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
    return level_ != DiagnosticsLevel::Off;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsServiceCounters(DiagnosticsLevel level_) noexcept {
    return level_ >= DiagnosticsLevel::CountersOnly;
}

[[nodiscard]] constexpr bool DiagnosticsCollectsDetailedData(DiagnosticsLevel level_) noexcept {
    return level_ >= DiagnosticsLevel::Detailed;
}

} // namespace vr::runtime

