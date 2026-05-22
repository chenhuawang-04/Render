#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <algorithm>
#include <sstream>

namespace {

[[nodiscard]] std::string_view QueueClassName(
    const vr::render_graph::QueueClass queue_) noexcept {
    switch (queue_) {
    case vr::render_graph::QueueClass::graphics:
        return "graphics";
    case vr::render_graph::QueueClass::compute:
        return "compute";
    case vr::render_graph::QueueClass::transfer:
        return "transfer";
    default:
        break;
    }
    return "unknown";
}

void AppendIndexListToStream(std::ostringstream& oss_,
                             const std::vector<std::uint32_t>& values_) {
    oss_ << '[';
    for (std::size_t index = 0U; index < values_.size(); ++index) {
        if (index != 0U) {
            oss_ << ',';
        }
        oss_ << values_[index];
    }
    oss_ << ']';
}

void AppendStringListToStream(std::ostringstream& oss_,
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

[[nodiscard]] bool BarrierPlanContainsHostBoundary(
    const vr::render_graph::BarrierPlan& barrier_plan_) noexcept {
    return std::any_of(
               barrier_plan_.queue_batches.begin(),
               barrier_plan_.queue_batches.end(),
               [](const vr::render_graph::QueueSubmitBatch& batch_) {
                   return batch_.contains_host_boundary;
               }) ||
           std::any_of(
               barrier_plan_.queue_dependencies.begin(),
               barrier_plan_.queue_dependencies.end(),
               [](const vr::render_graph::QueueDependencyPlan& dependency_) {
                   return dependency_.host_boundary;
               });
}

} // namespace

namespace vr::runtime::services {

const render_graph::CompiledPass* RenderGraphRuntimeService::TryFindPass(
    const render_graph::PassHandle pass_) const noexcept {
    if (pass_.index >= compiled_graph.Passes().size()) {
        return nullptr;
    }
    return &compiled_graph.Passes()[pass_.index];
}

std::string RenderGraphRuntimeService::ResolvePassDebugName(
    const render_graph::PassHandle pass_) const {
    if (const auto* compiled_pass = TryFindPass(pass_);
        compiled_pass != nullptr) {
        return compiled_pass->debug_name;
    }
    return {};
}

const RenderGraphRuntimeService::PreparedQueueSubmitBatch*
RenderGraphRuntimeService::TryFindPreparedSubmitBatch(
    const std::uint32_t batch_index_) const noexcept {
    const auto existing = std::find_if(
        prepared_multi_queue_submission.owned_submit_batches.begin(),
        prepared_multi_queue_submission.owned_submit_batches.end(),
        [&](const PreparedQueueSubmitBatch& batch_) {
            return batch_.batch_index == batch_index_;
        });
    return existing != prepared_multi_queue_submission.owned_submit_batches.end()
        ? &(*existing)
        : nullptr;
}

void RenderGraphRuntimeService::BuildEffectiveQueueDiagnostics(
    vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) const {
    diagnostics_.graphics_submit_wait_count = static_cast<std::uint32_t>(
        prepared_multi_queue_submission.graphics_waits.size());
    diagnostics_.non_graphics_submit_batch_count = static_cast<std::uint32_t>(
        prepared_multi_queue_submission.owned_submit_batches.size());

    if (!has_compiled_graph) {
        return;
    }

    const auto& barrier_plan = compiled_graph.PlannedBarriers();
    const bool preserve_host_boundary_fallback_topology =
        queue_execution_policy.graphics_fallback_active &&
        !queue_execution_policy.multi_queue_enabled &&
        !barrier_plan.queue_batches.empty() &&
        BarrierPlanContainsHostBoundary(barrier_plan);
    const bool preserve_effective_queue_topology =
        (queue_execution_policy.multi_queue_enabled ||
         preserve_host_boundary_fallback_topology) &&
        !barrier_plan.queue_batches.empty();
    const auto resolve_effective_queue_name =
        [&](const render_graph::QueueClass queue_) -> std::string {
            return preserve_host_boundary_fallback_topology
                ? std::string("graphics")
                : std::string(QueueClassName(queue_));
        };

    if (preserve_effective_queue_topology) {
        diagnostics_.effective_queue_batches.reserve(barrier_plan.queue_batches.size());
        diagnostics_.effective_queue_dependencies.reserve(barrier_plan.queue_dependencies.size());

        for (std::uint32_t dependency_index = 0U;
             dependency_index < static_cast<std::uint32_t>(barrier_plan.queue_dependencies.size());
             ++dependency_index) {
            const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
            diagnostics_.effective_queue_dependencies.push_back(
                vr::runtime::RenderGraphQueueDependencyDiagnostics{
                    .dependency_index = dependency_index,
                    .source_queue_name = resolve_effective_queue_name(dependency_.source_queue),
                    .target_queue_name = resolve_effective_queue_name(dependency_.target_queue),
                    .source_batch_index = dependency_.source_batch_index,
                    .target_batch_index = dependency_.target_batch_index,
                    .source_pass_index = dependency_.source_pass.index,
                    .target_pass_index = dependency_.target_pass.index,
                    .source_pass_debug_name = ResolvePassDebugName(dependency_.source_pass),
                    .target_pass_debug_name = ResolvePassDebugName(dependency_.target_pass),
                    .resource_count = static_cast<std::uint32_t>(dependency_.resources.size()),
                    .queue_transfer = dependency_.queue_transfer,
                    .host_boundary = dependency_.host_boundary,
                });
        }

        for (std::uint32_t batch_index = 0U;
             batch_index < static_cast<std::uint32_t>(barrier_plan.queue_batches.size());
             ++batch_index) {
            const auto& batch_ = barrier_plan.queue_batches[batch_index];
            vr::runtime::RenderGraphQueueBatchDiagnostics batch_diagnostics{};
            batch_diagnostics.batch_index = batch_index;
            batch_diagnostics.queue_name = resolve_effective_queue_name(batch_.queue);
            batch_diagnostics.wait_dependency_indices = batch_.wait_dependency_indices;
            batch_diagnostics.signal_dependency_indices = batch_.signal_dependency_indices;
            batch_diagnostics.barrier_batch_indices = batch_.barrier_batch_indices;
            batch_diagnostics.contains_host_boundary = batch_.contains_host_boundary;
            batch_diagnostics.pass_indices.reserve(batch_.passes.size());
            batch_diagnostics.pass_debug_names.reserve(batch_.passes.size());
            for (const auto pass_handle_ : batch_.passes) {
                batch_diagnostics.pass_indices.push_back(pass_handle_.index);
                batch_diagnostics.pass_debug_names.push_back(
                    ResolvePassDebugName(pass_handle_));
            }

            if (!preserve_host_boundary_fallback_topology) {
                if (const auto* prepared_batch = TryFindPreparedSubmitBatch(batch_index);
                    prepared_batch != nullptr) {
                    batch_diagnostics.submit_wait_count =
                        static_cast<std::uint32_t>(prepared_batch->wait_semaphores.size());
                    batch_diagnostics.submit_signal_count =
                        static_cast<std::uint32_t>(prepared_batch->signal_semaphores.size());
                    batch_diagnostics.submitted_on_owned_queue = true;
                } else if (batch_index == prepared_multi_queue_submission.graphics_batch_index) {
                    batch_diagnostics.submit_wait_count = diagnostics_.graphics_submit_wait_count;
                }
            }

            diagnostics_.effective_queue_batches.push_back(std::move(batch_diagnostics));
        }
    } else {
        vr::runtime::RenderGraphQueueBatchDiagnostics batch_diagnostics{};
        batch_diagnostics.batch_index = 0U;
        batch_diagnostics.queue_name = "graphics";
        for (const auto& pass_ : compiled_graph.Passes()) {
            if (!pass_.executable) {
                continue;
            }
            batch_diagnostics.pass_indices.push_back(pass_.handle.index);
            batch_diagnostics.pass_debug_names.push_back(pass_.debug_name);
        }
        if (!batch_diagnostics.pass_indices.empty()) {
            diagnostics_.effective_queue_batches.push_back(std::move(batch_diagnostics));
        }
    }

    diagnostics_.effective_queue_batch_count = static_cast<std::uint32_t>(
        diagnostics_.effective_queue_batches.size());
    diagnostics_.effective_queue_dependency_count = static_cast<std::uint32_t>(
        diagnostics_.effective_queue_dependencies.size());
}

std::string RenderGraphRuntimeService::BuildEffectiveQueueTimelineDebugString(
    const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) const {
    std::ostringstream oss{};
    oss << "requested transfer=" << (diagnostics_.transfer_queue_requested ? 1 : 0)
        << " compute=" << (diagnostics_.compute_queue_requested ? 1 : 0)
        << " multi=" << (diagnostics_.multi_queue_requested ? 1 : 0) << '\n';
    oss << "enabled transfer=" << (diagnostics_.transfer_queue_enabled ? 1 : 0)
        << " compute=" << (diagnostics_.compute_queue_enabled ? 1 : 0)
        << " multi=" << (diagnostics_.multi_queue_enabled ? 1 : 0)
        << " graphics_fallback=" << (diagnostics_.graphics_fallback_active ? 1 : 0)
        << '\n';
    oss << "effective_batches=" << diagnostics_.effective_queue_batch_count
        << " effective_dependencies=" << diagnostics_.effective_queue_dependency_count
        << " graphics_submit_waits=" << diagnostics_.graphics_submit_wait_count
        << " owned_submit_batches=" << diagnostics_.non_graphics_submit_batch_count
        << '\n';
    for (const auto& batch_ : diagnostics_.effective_queue_batches) {
        oss << "batch[" << batch_.batch_index << "] queue=" << batch_.queue_name
            << " passes=";
        AppendStringListToStream(oss, batch_.pass_debug_names);
        oss << " wait_deps=";
        AppendIndexListToStream(oss, batch_.wait_dependency_indices);
        oss << " signal_deps=";
        AppendIndexListToStream(oss, batch_.signal_dependency_indices);
        oss << " submit_waits=" << batch_.submit_wait_count
            << " submit_signals=" << batch_.submit_signal_count
            << " host_boundary=" << (batch_.contains_host_boundary ? 1 : 0)
            << " owned_submit=" << (batch_.submitted_on_owned_queue ? 1 : 0)
            << '\n';
    }
    for (const auto& dependency_ : diagnostics_.effective_queue_dependencies) {
        oss << "dependency[" << dependency_.dependency_index << "] "
            << dependency_.source_queue_name << '[' << dependency_.source_batch_index << ']'
            << " -> " << dependency_.target_queue_name << '[' << dependency_.target_batch_index << ']'
            << " source_pass=" << dependency_.source_pass_debug_name
            << " target_pass=" << dependency_.target_pass_debug_name
            << " resources=" << dependency_.resource_count
            << " queue_transfer=" << (dependency_.queue_transfer ? 1 : 0)
            << " host_boundary=" << (dependency_.host_boundary ? 1 : 0)
            << '\n';
    }
    return oss.str();
}

std::string RenderGraphRuntimeService::ResolveUnsupportedMultiQueueTopologyReason(
    const render_graph::BarrierPlan& barrier_plan_) const {
    if (barrier_plan_.queue_batches.empty()) {
        return "RenderGraph multi-queue submit requires at least one queue batch";
    }

    for (std::uint32_t batch_index = 0U;
         batch_index < static_cast<std::uint32_t>(barrier_plan_.queue_batches.size());
         ++batch_index) {
        const auto& batch_ = barrier_plan_.queue_batches[batch_index];
        if (batch_.contains_host_boundary) {
            return "RenderGraph multi-queue submit does not yet support host boundary queue batches";
        }
    }

    if (barrier_plan_.queue_batches.back().queue != render_graph::QueueClass::graphics) {
        return "RenderGraph multi-queue submit requires a terminal graphics batch for present submission";
    }

    const auto host_boundary = std::find_if(
        barrier_plan_.queue_dependencies.begin(),
        barrier_plan_.queue_dependencies.end(),
        [](const render_graph::QueueDependencyPlan& dependency_) {
            return dependency_.host_boundary;
        });
    if (host_boundary != barrier_plan_.queue_dependencies.end()) {
        return "RenderGraph multi-queue submit does not yet support host boundary dependencies";
    }

    return {};
}

void RenderGraphRuntimeService::ApplyGraphicsFallback(
    const QueueFamilyIndices& queue_families_,
    std::string reason_) {
    queue_execution_policy.effective_queue_families =
        render_graph::BuildGraphicsOnlyQueueFamilies(queue_families_);
    queue_execution_policy.transfer_enabled = false;
    queue_execution_policy.compute_enabled = false;
    queue_execution_policy.multi_queue_enabled = false;
    queue_execution_policy.graphics_fallback_active =
        queue_execution_policy.multi_queue_requested;
    queue_execution_policy.fallback_reason = std::move(reason_);
}

void RenderGraphRuntimeService::RefreshDiagnostics(const VulkanContext& device_) {
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = has_compiled_graph;
    diagnostics.frame_compiled = has_compiled_graph && !compiled_graph.Empty();
    diagnostics.graph_only_supported = SupportsGraphExecution(device_);
    diagnostics.graph_only_active =
        diagnostics.graph_only_supported &&
        CanExecuteGraphRecord(device_) &&
        record_stats.pass_count > 0U;
    diagnostics.transfer_queue_requested = queue_execution_policy.transfer_requested;
    diagnostics.compute_queue_requested = queue_execution_policy.compute_requested;
    diagnostics.multi_queue_requested = queue_execution_policy.multi_queue_requested;
    diagnostics.transfer_queue_enabled = queue_execution_policy.transfer_enabled;
    diagnostics.compute_queue_enabled = queue_execution_policy.compute_enabled;
    diagnostics.multi_queue_enabled = queue_execution_policy.multi_queue_enabled;
    diagnostics.graphics_fallback_active = queue_execution_policy.graphics_fallback_active;
    diagnostics.queue_fallback_reason = queue_execution_policy.fallback_reason;
    const auto& local_read_caps = device_.DynamicRenderingLocalReadCapsInfo();
    diagnostics.dynamic_rendering_local_read_supported =
        local_read_caps.supported;
    diagnostics.dynamic_rendering_local_read_requested =
        local_read_caps.requested;
    diagnostics.dynamic_rendering_local_read_enabled =
        local_read_caps.enabled;
    if (has_compiled_graph) {
        diagnostics.compiled_pass_count =
            static_cast<std::uint32_t>(compiled_graph.Passes().size());
        diagnostics.executable_pass_count = static_cast<std::uint32_t>(
            std::count_if(compiled_graph.Passes().begin(),
                          compiled_graph.Passes().end(),
                          [](const render_graph::CompiledPass& pass_) {
                              return pass_.executable;
                          }));
        diagnostics.logical_raster_pass_count =
            compiled_graph.NativePasses().summary.logical_raster_pass_count;
        diagnostics.native_pass_group_count =
            compiled_graph.NativePasses().summary.native_pass_group_count;
        diagnostics.fused_raster_pass_count =
            compiled_graph.NativePasses().summary.fused_raster_pass_count;
        diagnostics.store_elision_count =
            compiled_graph.NativePasses().summary.store_elision_count;
        diagnostics.load_inference_count =
            compiled_graph.NativePasses().summary.load_inference_count;
        diagnostics.effective_clear_attachment_count =
            compiled_graph.NativePasses().summary.clear_attachment_count;
        diagnostics.local_read_candidate_count =
            compiled_graph.NativePasses().summary.local_read_candidate_count;
        diagnostics.dynamic_rendering_local_read_requested =
            compiled_graph.NativePasses().local_read.requested;
        diagnostics.dynamic_rendering_local_read_supported =
            compiled_graph.NativePasses().local_read.supported;
        diagnostics.dynamic_rendering_local_read_enabled =
            compiled_graph.NativePasses().local_read.device_enabled;
        diagnostics.dynamic_rendering_local_read_status = std::string(
            render_graph::NativePassLocalReadStatusName(
                compiled_graph.NativePasses().local_read.status));
        diagnostics.dynamic_rendering_local_read_reason = std::string(
            render_graph::NativePassLocalReadReasonName(
                compiled_graph.NativePasses().local_read.reason));
        const auto& timeline = compiled_graph.TransientAllocations().timeline;
        diagnostics.transient_logical_total_bytes = timeline.logical_total_bytes;
        diagnostics.transient_physical_total_bytes = timeline.physical_total_bytes;
        diagnostics.transient_peak_live_bytes = timeline.peak_live_bytes;
        diagnostics.transient_saved_bytes = timeline.saved_bytes;
        diagnostics.transient_page_count = timeline.page_count;
        diagnostics.alias_barrier_count = timeline.alias_barrier_count;
    } else {
        diagnostics.dynamic_rendering_local_read_status = std::string(
            render_graph::NativePassLocalReadStatusName(
                render_graph::NativePassLocalReadStatus::not_applicable));
        diagnostics.dynamic_rendering_local_read_reason = std::string(
            render_graph::NativePassLocalReadReasonName(
                render_graph::NativePassLocalReadReason::none));
    }
    diagnostics.recorded_pass_count = record_stats.pass_count;
    diagnostics.recorded_rendering_scope_count =
        record_stats.rendering_scope_count;
    diagnostics.recorded_command_batch_count = record_stats.command_batch_count;
    diagnostics.recorded_image_barrier_count = record_stats.image_barrier_count;
    diagnostics.recorded_buffer_barrier_count = record_stats.buffer_barrier_count;
    diagnostics.recorded_queue_transfer_batch_count = record_stats.queue_transfer_batch_count;
    diagnostics.lazy_memory_requested_count =
        physical_resources.Stats().lazy_memory_requested_count;
    diagnostics.lazy_memory_realized_count =
        physical_resources.Stats().lazy_memory_realized_count;
    diagnostics.lazy_memory_unavailable_count =
        physical_resources.Stats().lazy_memory_unavailable_count;
    diagnostics.lazy_memory_resources.reserve(
        physical_resources.LazyMemoryResolutions().size());
    for (const auto& resolution_ : physical_resources.LazyMemoryResolutions()) {
        diagnostics.lazy_memory_resources.push_back(
            vr::runtime::RenderGraphLazyMemoryResourceDiagnostics{
                .logical_resource_index = resolution_.logical.index,
                .debug_name = resolution_.debug_name,
                .requested = resolution_.requested,
                .realized = resolution_.realized,
                .unavailable_reason = resolution_.unavailable_reason,
            });
    }
    BuildEffectiveQueueDiagnostics(diagnostics);
    diagnostics.effective_queue_timeline_debug_string =
        BuildEffectiveQueueTimelineDebugString(diagnostics);
    diagnostics.effective_queue_timeline_json =
        vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics);
    last_diagnostics = std::move(diagnostics);
}

} // namespace vr::runtime::services
