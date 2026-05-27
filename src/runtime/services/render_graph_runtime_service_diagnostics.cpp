#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <algorithm>
#include <sstream>

namespace {

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

[[nodiscard]] std::string BuildQueueBatchMarkerLabel(
    const vr::render_graph::QueueClass queue_,
    const std::uint32_t batch_index_,
    const std::vector<std::string>& pass_debug_names_) {
    std::ostringstream label{};
    label << "queue_batch[" << batch_index_ << "]/"
          << vr::runtime::RenderGraphQueueClassName(queue_);
    if (!pass_debug_names_.empty()) {
        label << '/';
        if (pass_debug_names_.size() == 1U) {
            label << pass_debug_names_.front();
        } else {
            label << pass_debug_names_.front()
                  << ".."
                  << pass_debug_names_.back();
        }
    }
    return label.str();
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
    vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
    const bool include_detailed_) const {
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
    const auto resolve_effective_queue =
        [&](const render_graph::QueueClass queue_) noexcept {
            return preserve_host_boundary_fallback_topology
                ? render_graph::QueueClass::graphics
                : queue_;
        };

    if (preserve_effective_queue_topology) {
        if (include_detailed_) {
            diagnostics_.effective_queue_batches.reserve(
                barrier_plan.queue_batches.size());
            diagnostics_.effective_queue_dependencies.reserve(
                barrier_plan.queue_dependencies.size());
        }

        for (std::uint32_t dependency_index = 0U;
             dependency_index < static_cast<std::uint32_t>(barrier_plan.queue_dependencies.size());
             ++dependency_index) {
            const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
            const auto source_queue = resolve_effective_queue(
                dependency_.source_queue);
            const auto target_queue = resolve_effective_queue(
                dependency_.target_queue);

            ++diagnostics_.effective_queue_dependency_count;
            if (dependency_.queue_transfer || source_queue != target_queue) {
                ++diagnostics_.effective_cross_queue_dependency_count;
            }

            if (include_detailed_) {
                diagnostics_.effective_queue_dependencies.push_back(
                    vr::runtime::RenderGraphQueueDependencyDiagnostics{
                        .dependency_id = dependency_index,
                        .source_queue = source_queue,
                        .target_queue = target_queue,
                        .source_batch_id = dependency_.source_batch_index,
                        .target_batch_id = dependency_.target_batch_index,
                        .source_pass_id = dependency_.source_pass.index,
                        .target_pass_id = dependency_.target_pass.index,
                        .source_pass_debug_name =
                            ResolvePassDebugName(dependency_.source_pass),
                        .target_pass_debug_name =
                            ResolvePassDebugName(dependency_.target_pass),
                        .resource_count = static_cast<std::uint32_t>(
                            dependency_.resources.size()),
                        .queue_transfer = dependency_.queue_transfer,
                        .host_boundary = dependency_.host_boundary,
                    });
            }
        }

        for (std::uint32_t batch_index = 0U;
             batch_index < static_cast<std::uint32_t>(barrier_plan.queue_batches.size());
             ++batch_index) {
            const auto& batch_ = barrier_plan.queue_batches[batch_index];
            const auto effective_queue = resolve_effective_queue(batch_.queue);
            ++diagnostics_.effective_queue_batch_count;
            switch (effective_queue) {
            case render_graph::QueueClass::graphics:
                ++diagnostics_.effective_graphics_queue_batch_count;
                break;
            case render_graph::QueueClass::compute:
                ++diagnostics_.effective_compute_queue_batch_count;
                break;
            case render_graph::QueueClass::transfer:
                ++diagnostics_.effective_transfer_queue_batch_count;
                break;
            }

            vr::runtime::RenderGraphQueueBatchDiagnostics batch_diagnostics{};
            if (include_detailed_) {
                batch_diagnostics.batch_id = batch_index;
                batch_diagnostics.queue = effective_queue;
                batch_diagnostics.wait_dependency_ids.reserve(
                    batch_.wait_dependency_indices.size());
                batch_diagnostics.signal_dependency_ids.reserve(
                    batch_.signal_dependency_indices.size());
                batch_diagnostics.barrier_batch_ids.reserve(
                    batch_.barrier_batch_indices.size());
                batch_diagnostics.contains_host_boundary =
                    batch_.contains_host_boundary;
                batch_diagnostics.pass_ids.reserve(batch_.passes.size());
                batch_diagnostics.pass_debug_names.reserve(batch_.passes.size());
                for (const auto dependency_id :
                     batch_.wait_dependency_indices) {
                    batch_diagnostics.wait_dependency_ids.push_back(
                        dependency_id);
                }
                for (const auto dependency_id :
                     batch_.signal_dependency_indices) {
                    batch_diagnostics.signal_dependency_ids.push_back(
                        dependency_id);
                }
                for (const auto barrier_batch_id :
                     batch_.barrier_batch_indices) {
                    batch_diagnostics.barrier_batch_ids.push_back(
                        barrier_batch_id);
                }
                for (const auto pass_handle_ : batch_.passes) {
                    batch_diagnostics.pass_ids.push_back(pass_handle_.index);
                    batch_diagnostics.pass_debug_names.push_back(
                        ResolvePassDebugName(pass_handle_));
                }
            }

            std::uint32_t submit_wait_count = 0U;
            std::uint32_t submit_signal_count = 0U;
            bool submitted_on_owned_queue = false;

            if (!preserve_host_boundary_fallback_topology) {
                if (const auto* prepared_batch = TryFindPreparedSubmitBatch(batch_index);
                    prepared_batch != nullptr) {
                    submit_wait_count =
                        static_cast<std::uint32_t>(prepared_batch->wait_semaphores.size());
                    submit_signal_count =
                        static_cast<std::uint32_t>(prepared_batch->signal_semaphores.size());
                    submitted_on_owned_queue = true;
                } else if (batch_index == prepared_multi_queue_submission.graphics_batch_index) {
                    submit_wait_count = diagnostics_.graphics_submit_wait_count;
                }
            }

            diagnostics_.effective_total_submit_wait_count += submit_wait_count;
            diagnostics_.effective_total_submit_signal_count += submit_signal_count;
            if (submitted_on_owned_queue) {
                ++diagnostics_.effective_owned_submit_batch_count;
            }

            if (include_detailed_) {
                batch_diagnostics.submit_wait_count = submit_wait_count;
                batch_diagnostics.submit_signal_count = submit_signal_count;
                batch_diagnostics.submitted_on_owned_queue =
                    submitted_on_owned_queue;
                diagnostics_.effective_queue_batches.push_back(
                    std::move(batch_diagnostics));
            }
        }
    } else {
        vr::runtime::RenderGraphQueueBatchDiagnostics batch_diagnostics{};
        std::uint32_t executable_pass_count = 0U;
        if (include_detailed_) {
            batch_diagnostics.batch_id = 0U;
            batch_diagnostics.queue = render_graph::QueueClass::graphics;
        }
        for (const auto& pass_ : compiled_graph.Passes()) {
            if (!pass_.executable) {
                continue;
            }
            ++executable_pass_count;
            if (include_detailed_) {
                batch_diagnostics.pass_ids.push_back(pass_.handle.index);
                batch_diagnostics.pass_debug_names.push_back(pass_.debug_name);
            }
        }
        if (executable_pass_count != 0U) {
            diagnostics_.effective_queue_batch_count = 1U;
            diagnostics_.effective_graphics_queue_batch_count = 1U;
            if (include_detailed_) {
                diagnostics_.effective_queue_batches.push_back(
                    std::move(batch_diagnostics));
            }
        }
    }
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
#if !VR_ENABLE_DEBUG_OBSERVABILITY
    (void)device_;
    return;
#else
    if (diagnostics_level == vr::runtime::DiagnosticsLevel::Off) {
        last_diagnostics = {};
        return;
    }

    const bool include_detailed =
        vr::runtime::DiagnosticsCollectsDetailedData(diagnostics_level);
    const bool include_gpu_timing =
        vr::runtime::DiagnosticsCollectsGpuTiming(diagnostics_level);
    const bool include_capture =
        vr::runtime::DiagnosticsCollectsCapture(diagnostics_level);
    vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
    diagnostics.available = has_compiled_graph;
    diagnostics.frame_compiled = has_compiled_graph && !compiled_graph.Empty();
    diagnostics.graph_only_supported = SupportsGraphExecution(device_);
    diagnostics.graph_only_active = IsGraphOnlyRecordActive(device_);
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
        diagnostics.dynamic_rendering_local_read_status =
            compiled_graph.NativePasses().local_read.status;
        diagnostics.dynamic_rendering_local_read_reason =
            compiled_graph.NativePasses().local_read.reason;
        const auto& timeline = compiled_graph.TransientAllocations().timeline;
        diagnostics.transient_logical_total_bytes = timeline.logical_total_bytes;
        diagnostics.transient_physical_total_bytes = timeline.physical_total_bytes;
        diagnostics.transient_peak_live_bytes = timeline.peak_live_bytes;
        diagnostics.transient_saved_bytes = timeline.saved_bytes;
        diagnostics.transient_page_count = timeline.page_count;
        diagnostics.alias_barrier_count = timeline.alias_barrier_count;
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
    if (include_detailed) {
        diagnostics.lazy_memory_resources.reserve(
            physical_resources.LazyMemoryResolutions().size());
        for (const auto& resolution_ : physical_resources.LazyMemoryResolutions()) {
            diagnostics.lazy_memory_resources.push_back(
                vr::runtime::RenderGraphLazyMemoryResourceDiagnostics{
                    .logical_resource_id = resolution_.logical.index,
                    .debug_name = resolution_.debug_name,
                    .requested = resolution_.requested,
                    .realized = resolution_.realized,
                    .unavailable_reason = resolution_.unavailable_reason,
                });
        }
        if (has_compiled_graph &&
            vr::render_graph::CompiledRenderGraphObservabilityAvailableInBuild()) {
            auto topology_view =
                vr::render_graph::BuildCompiledRenderGraphTopologyView(
                    compiled_graph);
            diagnostics.compile_liveness_ranges =
                std::move(topology_view.liveness_ranges);
            diagnostics.compile_transient_memory =
                std::move(topology_view.transient_memory);
        }
    }
    BuildEffectiveQueueDiagnostics(diagnostics, include_detailed);
    diagnostics.frame_color_history = {
        .desc = frame_color_history.desc,
        .previous =
            {
                .handle = frame_color_history.published_slot != invalid_frame_index &&
                                  frame_color_history.published_slot <
                                      frame_color_history.slots.size()
                    ? frame_color_history.slots[frame_color_history.published_slot]
                          .handle
                    : render::invalid_render_target_handle,
                .resource_revision =
                    frame_color_history.published_slot != invalid_frame_index &&
                            frame_color_history.published_slot <
                                frame_color_history.slots.size()
                        ? frame_color_history.slots[frame_color_history.published_slot]
                              .resource_revision
                        : 0U,
            },
        .current =
            {
                .handle = frame_color_history.write_slot <
                                  frame_color_history.slots.size()
                    ? frame_color_history.slots[frame_color_history.write_slot]
                          .handle
                    : render::invalid_render_target_handle,
                .resource_revision =
                    frame_color_history.write_slot <
                            frame_color_history.slots.size()
                        ? frame_color_history.slots[frame_color_history.write_slot]
                              .resource_revision
                        : 0U,
            },
        .previous_submission_id = frame_color_history.previous_submission_id,
        .previous_frame_index = frame_color_history.previous_frame_index,
        .invalidation_reason = frame_color_history.last_invalidation_reason,
        .previous_available =
            frame_color_history.published_slot != invalid_frame_index &&
            frame_color_history.published_slot < frame_color_history.slots.size() &&
            frame_color_history.last_invalidation_reason ==
                render_graph::FrameHistoryInvalidationReason::none,
        .current_writable =
            frame_color_history.initialized &&
            frame_color_history.write_slot < frame_color_history.slots.size() &&
            render::IsValidRenderTargetHandle(
                frame_color_history.slots[frame_color_history.write_slot].handle),
    };
    if (include_gpu_timing) {
        diagnostics.timing.available = has_compiled_graph;
        diagnostics.timing.enabled = true;
        diagnostics.timing.gpu_timestamp_supported = false;
        diagnostics.timing.domain =
            last_recorded_queue_batch_timings.empty()
                ? vr::runtime::RenderGraphTimingDomain::unavailable
                : vr::runtime::RenderGraphTimingDomain::cpu_record;
        diagnostics.timing.queue_batch_range_count = static_cast<std::uint32_t>(
            last_recorded_queue_batch_timings.size());
        diagnostics.timing.resolved_queue_batch_range_count =
            diagnostics.timing.queue_batch_range_count;
        diagnostics.timing.total_duration_ns =
            last_recorded_timing_total_duration_ns;
        diagnostics.timing.queue_batch_ranges =
            last_recorded_queue_batch_timings;
    }
    if (include_capture) {
        diagnostics.capture.available = has_compiled_graph;
        diagnostics.capture.enabled = true;

        if (!last_recorded_queue_batch_timings.empty()) {
            diagnostics.capture.markers.reserve(
                last_recorded_queue_batch_timings.size());
            for (const auto& timing_ : last_recorded_queue_batch_timings) {
                diagnostics.capture.markers.push_back(
                    vr::runtime::RenderGraphCaptureMarkerDiagnostics{
                        .batch_id = timing_.batch_id,
                        .queue = timing_.queue,
                        .pass_ids = timing_.pass_ids,
                        .pass_debug_names = timing_.pass_debug_names,
                        .label = timing_.marker_label,
                    });
            }
        } else {
            diagnostics.capture.markers.reserve(
                diagnostics.effective_queue_batches.size());
            for (const auto& batch_ : diagnostics.effective_queue_batches) {
                diagnostics.capture.markers.push_back(
                    vr::runtime::RenderGraphCaptureMarkerDiagnostics{
                        .batch_id = batch_.batch_id,
                        .queue = batch_.queue,
                        .pass_ids = batch_.pass_ids,
                        .pass_debug_names = batch_.pass_debug_names,
                        .label = BuildQueueBatchMarkerLabel(
                            batch_.queue,
                            batch_.batch_id.value,
                            batch_.pass_debug_names),
                    });
            }
        }
        diagnostics.capture.marker_count = static_cast<std::uint32_t>(
            diagnostics.capture.markers.size());

        if (has_compiled_graph &&
            vr::render_graph::CompiledRenderGraphObservabilityAvailableInBuild()) {
            diagnostics.capture.artifacts.push_back(
                vr::runtime::RenderGraphCaptureArtifactDiagnostics{
                    .captured = true,
                    .kind = vr::runtime::RenderGraphCaptureArtifactKind::observability_snapshot,
                    .artifact_label =
                        "render_graph_capture.frame_" + std::to_string(frame_index),
                    .topology_json =
                        vr::render_graph::BuildCompiledRenderGraphTopologyJson(
                            vr::render_graph::BuildCompiledRenderGraphTopologyView(
                                compiled_graph)),
                    .queue_timeline_json =
                        vr::runtime::BuildRenderGraphQueueTimelineJson(
                            diagnostics),
                });
        }
        diagnostics.capture.artifact_count = static_cast<std::uint32_t>(
            diagnostics.capture.artifacts.size());
    }
    last_diagnostics = std::move(diagnostics);
#endif
}

} // namespace vr::runtime::services
