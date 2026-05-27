#include "vr/render_graph/compiled_render_graph_observability.hpp"

#include "native_pass_plan_internal.hpp"
#include "render_graph_builder_internal.hpp"

#include <sstream>

#if !VR_ENABLE_DEBUG_OBSERVABILITY

namespace vr::render_graph {

CompiledRenderGraphTopologyView BuildCompiledRenderGraphTopologyView(
    const CompiledRenderGraph&) {
    return {};
}

std::string BuildCompiledRenderGraphTopologyDebugString(
    const CompiledRenderGraphTopologyView&) {
    return {};
}

std::string BuildCompiledRenderGraphTopologyJson(
    const CompiledRenderGraphTopologyView&) {
    return {};
}

} // namespace vr::render_graph

#else

namespace vr::render_graph {
namespace {

using namespace builder_detail;

[[nodiscard]] ResourceHandle HandleForResourceIndex(
    const std::uint32_t resource_index_) noexcept {
    return ResourceHandle{
        .index = resource_index_,
        .generation = 1U,
    };
}

[[nodiscard]] std::string ResolvePassDebugName(
    const CompiledRenderGraph& compiled_graph_,
    const PassHandle pass_) {
    if (const auto* compiled_pass = compiled_graph_.FindPass(pass_);
        compiled_pass != nullptr) {
        return compiled_pass->debug_name;
    }
    return {};
}

[[nodiscard]] std::string ResolveResourceDebugName(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceHandle resource_) {
    if (const auto* compiled_resource = compiled_graph_.FindResource(resource_);
        compiled_resource != nullptr) {
        return compiled_resource->debug_name;
    }
    return {};
}

[[nodiscard]] RenderGraphResourceVersionTopologyView BuildResourceVersionView(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceVersionHandle version_) {
    RenderGraphResourceVersionTopologyView view{
        .version = version_,
    };
    if (const auto* liveness = FindLivenessRange(compiled_graph_.LivenessRanges(),
                                                 version_);
        liveness != nullptr) {
        view.debug_name = liveness->debug_name;
        view.kind = liveness->kind;
        return view;
    }
    if (const auto* resource = compiled_graph_.FindResource(
            HandleForResourceIndex(version_.resource_index));
        resource != nullptr) {
        view.debug_name = resource->debug_name;
        view.kind = resource->kind;
    }
    return view;
}

[[nodiscard]] RenderGraphAccessTopologyView BuildAccessView(
    const CompiledRenderGraph& compiled_graph_,
    const AccessDesc& access_) {
    return RenderGraphAccessTopologyView{
        .resource = BuildResourceVersionView(compiled_graph_, access_.resource),
        .access = access_.access,
        .subresource_range = access_.subresource_range,
        .buffer_range = access_.buffer_range,
    };
}

template<typename IdT>
void AppendTopologyIdList(std::ostringstream& oss_,
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

void AppendPassHandleList(std::ostringstream& oss_,
                          const std::vector<PassHandle>& values_) {
    oss_ << '[';
    for (std::size_t index = 0U; index < values_.size(); ++index) {
        if (index != 0U) {
            oss_ << ',';
        }
        oss_ << values_[index].index;
    }
    oss_ << ']';
}

void AppendResourceHandleList(std::ostringstream& oss_,
                              const std::vector<ResourceHandle>& values_) {
    oss_ << '[';
    for (std::size_t index = 0U; index < values_.size(); ++index) {
        if (index != 0U) {
            oss_ << ',';
        }
        oss_ << values_[index].index;
    }
    oss_ << ']';
}

void AppendStringList(std::ostringstream& oss_,
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

void AppendJsonResourceVersionView(std::ostringstream& oss_,
                                   const RenderGraphResourceVersionTopologyView& view_) {
    oss_ << "{"
         << "\"resourceIndex\": " << view_.version.resource_index
         << ", \"version\": " << view_.version.version
         << ", \"name\": \"" << EscapeJsonString(view_.debug_name) << "\""
         << ", \"kind\": \"" << ResourceKindToString(view_.kind) << "\""
         << '}';
}

void AppendJsonAccessView(std::ostringstream& oss_,
                          const RenderGraphAccessTopologyView& view_) {
    oss_ << "{"
         << "\"resourceIndex\": " << view_.resource.version.resource_index
         << ", \"version\": " << view_.resource.version.version
         << ", \"resourceName\": \"" << EscapeJsonString(view_.resource.debug_name) << "\""
         << ", \"resourceKind\": \"" << ResourceKindToString(view_.resource.kind) << "\""
         << ", \"access\": \"" << AccessKindToString(view_.access) << "\"";
    if (view_.resource.kind == ResourceKind::texture) {
        oss_ << ", \"subresourceRange\": {"
             << "\"baseMipLevel\": "
             << view_.subresource_range.base_mip_level
             << ", \"levelCount\": "
             << view_.subresource_range.level_count
             << ", \"baseArrayLayer\": "
             << view_.subresource_range.base_array_layer
             << ", \"layerCount\": "
             << view_.subresource_range.layer_count << '}';
    } else {
        oss_ << ", \"bufferRange\": {"
             << "\"offsetBytes\": " << view_.buffer_range.offset_bytes
             << ", \"sizeBytes\": " << view_.buffer_range.size_bytes << '}';
    }
    oss_ << '}';
}

} // namespace

CompiledRenderGraphTopologyView BuildCompiledRenderGraphTopologyView(
    const CompiledRenderGraph& compiled_graph_) {
    CompiledRenderGraphTopologyView view{};

    view.summary.execution_order_count = static_cast<std::uint32_t>(
        compiled_graph_.ExecutionOrder().size());
    view.summary.pass_count = static_cast<std::uint32_t>(
        compiled_graph_.Passes().size());
    view.summary.resource_count = static_cast<std::uint32_t>(
        compiled_graph_.Resources().size());
    view.summary.liveness_range_count = static_cast<std::uint32_t>(
        compiled_graph_.LivenessRanges().size());

    view.execution_order = compiled_graph_.ExecutionOrder();
    view.resources.reserve(compiled_graph_.Resources().size());
    for (const auto& resource : compiled_graph_.Resources()) {
        view.resources.push_back(CompiledRenderGraphResourceTopologyView{
            .resource = resource.handle,
            .debug_name = resource.debug_name,
            .kind = resource.kind,
            .lifetime = resource.lifetime,
        });
    }

    view.passes.reserve(compiled_graph_.Passes().size());
    for (const auto& pass : compiled_graph_.Passes()) {
        CompiledRenderGraphPassTopologyView pass_view{
            .pass = pass.handle,
            .debug_name = pass.debug_name,
            .side_effect = pass.side_effect,
            .executable = pass.executable,
            .queue = pass.queue,
            .force_native_pass_split =
                pass.compile_hints.force_native_pass_split,
            .raster_pass = pass.raster_pass.has_value(),
            .raster_color_attachment_count = pass.raster_pass.has_value()
                ? static_cast<std::uint32_t>(
                      pass.raster_pass->color_attachments.size())
                : 0U,
            .raster_has_depth_attachment = pass.raster_pass.has_value() &&
                                           pass.raster_pass->has_depth_attachment,
            .dependencies = pass.dependencies,
            .descriptor_bindings = pass.descriptor_bindings,
            .descriptor_binding_count = static_cast<std::uint32_t>(
                pass.descriptor_bindings.size()),
        };
        pass_view.dependency_debug_names.reserve(pass.dependencies.size());
        for (const auto dependency : pass.dependencies) {
            pass_view.dependency_debug_names.push_back(
                ResolvePassDebugName(compiled_graph_, dependency));
        }
        pass_view.reads.reserve(pass.reads.size());
        for (const auto& read : pass.reads) {
            pass_view.reads.push_back(
                BuildAccessView(compiled_graph_, read));
        }
        pass_view.writes.reserve(pass.writes.size());
        for (const auto& write : pass.writes) {
            pass_view.writes.push_back(
                BuildAccessView(compiled_graph_, write));
        }
        if (pass.executable) {
            ++view.summary.executable_pass_count;
        }
        view.passes.push_back(std::move(pass_view));
    }
    view.summary.culled_pass_count =
        view.summary.pass_count - view.summary.executable_pass_count;

    view.liveness_ranges.reserve(compiled_graph_.LivenessRanges().size());
    for (const auto& liveness : compiled_graph_.LivenessRanges()) {
        view.liveness_ranges.push_back(CompiledRenderGraphLivenessTopologyView{
            .resource = {
                .version = liveness.version,
                .debug_name = liveness.debug_name,
                .kind = liveness.kind,
            },
            .lifetime = liveness.lifetime,
            .first_pass_order = liveness.first_pass_order,
            .last_pass_order = liveness.last_pass_order,
        });
    }

    const auto& transient_plan = compiled_graph_.TransientAllocations();
    view.transient_memory.summary.logical_total_bytes =
        transient_plan.timeline.logical_total_bytes;
    view.transient_memory.summary.physical_total_bytes =
        transient_plan.timeline.physical_total_bytes;
    view.transient_memory.summary.peak_logical_live_bytes =
        transient_plan.timeline.peak_logical_live_bytes;
    view.transient_memory.summary.peak_live_bytes =
        transient_plan.timeline.peak_live_bytes;
    view.transient_memory.summary.saved_bytes =
        transient_plan.timeline.saved_bytes;
    view.transient_memory.summary.transient_resource_count =
        transient_plan.timeline.transient_resource_count;
    view.transient_memory.summary.eligible_resource_count =
        transient_plan.timeline.eligible_resource_count;
    view.transient_memory.summary.aliased_resource_count =
        transient_plan.timeline.aliased_resource_count;
    view.transient_memory.summary.page_count =
        transient_plan.timeline.page_count;
    view.transient_memory.summary.alias_candidate_count =
        static_cast<std::uint32_t>(transient_plan.alias_candidates.size());
    view.transient_memory.summary.alias_barrier_count =
        static_cast<std::uint32_t>(transient_plan.alias_barriers.size());
    view.transient_memory.summary.timeline_sample_count =
        static_cast<std::uint32_t>(transient_plan.timeline.samples.size());

    view.transient_memory.records.reserve(transient_plan.records.size());
    for (const auto& record : transient_plan.records) {
        view.transient_memory.records.push_back(
            CompiledRenderGraphTransientAllocationTopologyView{
                .resource = record.resource,
                .debug_name = record.debug_name,
                .kind = record.kind,
                .lifetime = record.lifetime,
                .size_bytes = record.footprint.size_bytes,
                .alignment_bytes = record.footprint.alignment_bytes,
                .memory_type_bits = record.footprint.memory_type_bits,
                .usage_flags = record.footprint.usage_flags,
                .dedicated_required = record.footprint.dedicated_required,
                .dedicated_preferred = record.footprint.dedicated_preferred,
                .host_visible = record.footprint.host_visible,
                .persistently_mapped = record.footprint.persistently_mapped,
                .lazy_memory_requested =
                    record.footprint.lazy_memory_requested,
                .first_pass_order = record.first_pass_order,
                .last_pass_order = record.last_pass_order,
                .page_index = record.page_index,
                .page_offset_bytes = record.page_offset_bytes,
                .alias_group = record.alias_group,
                .eligible = record.eligible,
                .aliased = record.aliased,
                .rejection_reason = record.rejection_reason,
            });
    }

    view.transient_memory.pages.reserve(transient_plan.pages.size());
    for (const auto& page : transient_plan.pages) {
        CompiledRenderGraphTransientPageTopologyView page_view{
            .page_index = page.page_index,
            .kind = page.kind,
            .size_bytes = page.size_bytes,
            .alignment_bytes = page.alignment_bytes,
            .lazy_memory_requested = page.compatibility.lazy_memory_requested,
        };
        page_view.resource_ids = page.resources;
        page_view.resource_debug_names.reserve(page.resources.size());
        for (const auto resource_handle : page.resources) {
            page_view.resource_debug_names.push_back(
                ResolveResourceDebugName(compiled_graph_, resource_handle));
        }
        view.transient_memory.pages.push_back(std::move(page_view));
    }

    view.transient_memory.timeline_samples.reserve(
        transient_plan.timeline.samples.size());
    for (const auto& sample : transient_plan.timeline.samples) {
        view.transient_memory.timeline_samples.push_back(
            CompiledRenderGraphTransientTimelineSampleTopologyView{
                .pass_order = sample.pass_order,
                .logical_live_bytes = sample.logical_live_bytes,
                .physical_live_bytes = sample.physical_live_bytes,
                .live_page_count = sample.live_page_count,
            });
    }

    view.transient_memory.alias_candidates.reserve(
        transient_plan.alias_candidates.size());
    for (const auto& candidate : transient_plan.alias_candidates) {
        view.transient_memory.alias_candidates.push_back(
            CompiledRenderGraphAliasCandidateTopologyView{
                .first = candidate.first,
                .second = candidate.second,
                .first_debug_name = candidate.first_debug_name,
                .second_debug_name = candidate.second_debug_name,
                .kind = candidate.kind,
                .same_compatibility_class =
                    candidate.same_compatibility_class,
                .overlapping_liveness = candidate.overlapping_liveness,
                .aliasable = candidate.aliasable,
                .non_alias_reason = candidate.non_alias_reason,
            });
    }

    view.transient_memory.alias_barriers.reserve(
        transient_plan.alias_barriers.size());
    for (const auto& barrier : transient_plan.alias_barriers) {
        view.transient_memory.alias_barriers.push_back(
            CompiledRenderGraphAliasBarrierTopologyView{
                .previous = barrier.previous,
                .next = barrier.next,
                .previous_debug_name = barrier.previous_debug_name,
                .next_debug_name = barrier.next_debug_name,
                .previous_last_pass_order =
                    barrier.previous_last_pass_order,
                .next_first_pass_order = barrier.next_first_pass_order,
                .page_index = barrier.page_index,
                .required = barrier.required,
                .realized = barrier.realized,
            });
    }

    const auto& barrier_plan = compiled_graph_.PlannedBarriers();
    view.summary.queue_batch_count = static_cast<std::uint32_t>(
        barrier_plan.queue_batches.size());
    view.summary.queue_dependency_count = static_cast<std::uint32_t>(
        barrier_plan.queue_dependencies.size());
    view.summary.barrier_batch_count = static_cast<std::uint32_t>(
        barrier_plan.barrier_batches.size());
    view.summary.alias_candidate_count = static_cast<std::uint32_t>(
        barrier_plan.alias_candidates.size());
    view.summary.alias_barrier_count = static_cast<std::uint32_t>(
        barrier_plan.alias_barriers.size());

    view.queue_batches.reserve(barrier_plan.queue_batches.size());
    for (std::uint32_t batch_index = 0U;
         batch_index < static_cast<std::uint32_t>(
                           barrier_plan.queue_batches.size());
         ++batch_index) {
        const auto& batch = barrier_plan.queue_batches[batch_index];
        RenderGraphQueueBatchTopologyView batch_view{
            .batch_id = batch_index,
            .queue = batch.queue,
            .pass_ids = batch.passes,
            .contains_host_boundary = batch.contains_host_boundary,
        };
        batch_view.pass_debug_names.reserve(batch.passes.size());
        for (const auto pass_handle : batch.passes) {
            batch_view.pass_debug_names.push_back(
                ResolvePassDebugName(compiled_graph_, pass_handle));
        }
        batch_view.wait_dependency_ids.reserve(
            batch.wait_dependency_indices.size());
        for (const auto dependency_index : batch.wait_dependency_indices) {
            batch_view.wait_dependency_ids.push_back(dependency_index);
        }
        batch_view.signal_dependency_ids.reserve(
            batch.signal_dependency_indices.size());
        for (const auto dependency_index : batch.signal_dependency_indices) {
            batch_view.signal_dependency_ids.push_back(dependency_index);
        }
        batch_view.barrier_batch_ids.reserve(
            batch.barrier_batch_indices.size());
        for (const auto barrier_batch_index : batch.barrier_batch_indices) {
            batch_view.barrier_batch_ids.push_back(barrier_batch_index);
        }
        if (batch.contains_host_boundary) {
            ++view.summary.queue_batch_host_boundary_count;
        }
        view.queue_batches.push_back(std::move(batch_view));
    }

    view.queue_dependencies.reserve(barrier_plan.queue_dependencies.size());
    for (std::uint32_t dependency_index = 0U;
         dependency_index < static_cast<std::uint32_t>(
                                barrier_plan.queue_dependencies.size());
         ++dependency_index) {
        const auto& dependency =
            barrier_plan.queue_dependencies[dependency_index];
        RenderGraphQueueDependencyTopologyView dependency_view{
            .dependency_id = dependency_index,
            .source_queue = dependency.source_queue,
            .target_queue = dependency.target_queue,
            .source_batch_id = dependency.source_batch_index,
            .target_batch_id = dependency.target_batch_index,
            .source_pass = dependency.source_pass,
            .target_pass = dependency.target_pass,
            .source_pass_debug_name =
                ResolvePassDebugName(compiled_graph_, dependency.source_pass),
            .target_pass_debug_name =
                ResolvePassDebugName(compiled_graph_, dependency.target_pass),
            .queue_transfer = dependency.queue_transfer,
            .host_boundary = dependency.host_boundary,
        };
        dependency_view.resources.reserve(dependency.resources.size());
        for (const auto version : dependency.resources) {
            dependency_view.resources.push_back(
                BuildResourceVersionView(compiled_graph_, version));
        }
        if (dependency.host_boundary) {
            ++view.summary.queue_dependency_host_boundary_count;
        }
        view.queue_dependencies.push_back(std::move(dependency_view));
    }

    view.barrier_batches.reserve(barrier_plan.barrier_batches.size());
    for (std::uint32_t batch_index = 0U;
         batch_index < static_cast<std::uint32_t>(
                           barrier_plan.barrier_batches.size());
         ++batch_index) {
        const auto& batch = barrier_plan.barrier_batches[batch_index];
        RenderGraphBarrierBatchTopologyView batch_view{
            .barrier_batch_id = batch_index,
            .pass = batch.pass,
            .pass_debug_name =
                ResolvePassDebugName(compiled_graph_, batch.pass),
            .queue = batch.queue,
        };
        batch_view.barriers.reserve(batch.barriers.size());
        view.summary.barrier_count += static_cast<std::uint32_t>(
            batch.barriers.size());
        for (const auto& barrier : batch.barriers) {
            batch_view.barriers.push_back(RenderGraphBarrierTopologyView{
                .resource =
                    BuildResourceVersionView(compiled_graph_, barrier.resource),
                .before = barrier.before,
                .after = barrier.after,
                .src_queue = barrier.src_queue,
                .dst_queue = barrier.dst_queue,
                .subresource_range = barrier.subresource_range,
                .buffer_range = barrier.buffer_range,
                .src_pass = barrier.src_pass,
                .dst_pass = barrier.dst_pass,
                .src_pass_debug_name =
                    ResolvePassDebugName(compiled_graph_, barrier.src_pass),
                .dst_pass_debug_name =
                    ResolvePassDebugName(compiled_graph_, barrier.dst_pass),
                .src_pass_order = barrier.src_pass_order,
                .dst_pass_order = barrier.dst_pass_order,
                .queue_transfer = barrier.queue_transfer,
                .host_boundary = barrier.host_boundary,
                .aliasing = barrier.aliasing,
                .uav_ordering = barrier.uav_ordering,
            });
        }
        view.barrier_batches.push_back(std::move(batch_view));
    }

    const auto& native_passes = compiled_graph_.NativePasses();
    view.native_pass_local_read = native_passes.local_read;
    view.summary.native_pass_group_count = static_cast<std::uint32_t>(
        native_passes.groups.size());
    view.summary.native_pass_boundary_count = static_cast<std::uint32_t>(
        native_passes.boundaries.size());
    view.summary.logical_raster_pass_count =
        native_passes.summary.logical_raster_pass_count;
    view.summary.fused_raster_pass_count =
        native_passes.summary.fused_raster_pass_count;
    view.summary.store_elision_count =
        native_passes.summary.store_elision_count;
    view.summary.load_inference_count =
        native_passes.summary.load_inference_count;
    view.summary.clear_attachment_count =
        native_passes.summary.clear_attachment_count;
    view.summary.local_read_candidate_count =
        native_passes.summary.local_read_candidate_count;

    view.native_pass_groups.reserve(native_passes.groups.size());
    for (std::uint32_t group_index = 0U;
         group_index < static_cast<std::uint32_t>(native_passes.groups.size());
         ++group_index) {
        const auto& group = native_passes.groups[group_index];
        RenderGraphNativePassGroupTopologyView group_view{
            .group_id = group_index,
            .queue = group.queue,
            .first_pass_order = group.first_pass_order,
            .last_pass_order = group.last_pass_order,
            .pass_ids = group.logical_passes,
            .color_attachment_count = static_cast<std::uint32_t>(
                group.attachments.color_attachments.size()),
            .has_depth_attachment = group.attachments.has_depth_attachment,
            .layer_count = group.attachments.layer_count,
        };
        group_view.pass_debug_names.reserve(group.logical_passes.size());
        for (const auto pass_handle : group.logical_passes) {
            group_view.pass_debug_names.push_back(
                ResolvePassDebugName(compiled_graph_, pass_handle));
        }
        view.native_pass_groups.push_back(std::move(group_view));
    }

    view.native_pass_boundaries.reserve(native_passes.boundaries.size());
    for (std::uint32_t boundary_index = 0U;
         boundary_index <
         static_cast<std::uint32_t>(native_passes.boundaries.size());
         ++boundary_index) {
        const auto& boundary = native_passes.boundaries[boundary_index];
        view.native_pass_boundaries.push_back(
            RenderGraphNativePassBoundaryTopologyView{
                .boundary_id = boundary_index,
                .previous_pass_order = boundary.previous_pass_order,
                .previous_pass = boundary.previous_pass,
                .previous_pass_debug_name = ResolvePassDebugName(
                    compiled_graph_, boundary.previous_pass),
                .next_pass_order = boundary.next_pass_order,
                .next_pass = boundary.next_pass,
                .next_pass_debug_name = ResolvePassDebugName(
                    compiled_graph_, boundary.next_pass),
                .fused = boundary.fused,
                .block_reason = boundary.block_reason,
                .local_read_candidate = boundary.local_read.candidate,
                .local_read_status = boundary.local_read.status,
                .local_read_reason = boundary.local_read.reason,
                .detail = boundary.detail,
            });
    }

    return view;
}

std::string BuildCompiledRenderGraphTopologyDebugString(
    const CompiledRenderGraphTopologyView& view_) {
    std::ostringstream oss{};
    oss << "execution_order=" << view_.summary.execution_order_count << '\n';
    oss << "liveness=" << view_.summary.liveness_range_count << '\n';
    oss << "logical_raster_passes="
        << view_.summary.logical_raster_pass_count << '\n';
    oss << "native_pass_groups="
        << view_.summary.native_pass_group_count << '\n';
    oss << "fused_raster_passes="
        << view_.summary.fused_raster_pass_count << '\n';
    oss << "native_pass_store_elisions="
        << view_.summary.store_elision_count << '\n';
    oss << "native_pass_load_inferences="
        << view_.summary.load_inference_count << '\n';
    oss << "native_pass_effective_clears="
        << view_.summary.clear_attachment_count << '\n';
    oss << "topology_summary execution_order="
        << view_.summary.execution_order_count
        << " passes=" << view_.summary.pass_count
        << " executable=" << view_.summary.executable_pass_count
        << " culled=" << view_.summary.culled_pass_count
        << " resources=" << view_.summary.resource_count
        << " liveness=" << view_.summary.liveness_range_count
        << " queue_batches=" << view_.summary.queue_batch_count
        << " queue_dependencies=" << view_.summary.queue_dependency_count
        << " barrier_batches=" << view_.summary.barrier_batch_count
        << " barriers=" << view_.summary.barrier_count
        << " native_pass_groups=" << view_.summary.native_pass_group_count
        << " native_pass_boundaries="
        << view_.summary.native_pass_boundary_count << '\n';

    for (const auto& pass : view_.passes) {
        oss << "pass[" << pass.pass.index << "] name=" << pass.debug_name
            << " queue=" << QueueClassToString(pass.queue)
            << " executable=" << (pass.executable ? 1 : 0)
            << " side_effect=" << (pass.side_effect ? 1 : 0)
            << " force_native_pass_split="
            << (pass.force_native_pass_split ? 1 : 0)
            << " raster=" << (pass.raster_pass ? 1 : 0)
            << " color_attachments="
            << pass.raster_color_attachment_count
            << " depth_attachment="
            << (pass.raster_has_depth_attachment ? 1 : 0)
            << " deps=";
        AppendPassHandleList(oss, pass.dependencies);
        oss << " reads=" << pass.reads.size()
            << " writes=" << pass.writes.size()
            << " descriptor_bindings=" << pass.descriptor_binding_count
            << '\n';
    }

    for (const auto& range : view_.liveness_ranges) {
        oss << "resource=" << range.resource.debug_name
            << " first=" << range.first_pass_order
            << " last=" << range.last_pass_order << '\n';
    }

    oss << "transient_memory logical_total_bytes="
        << view_.transient_memory.summary.logical_total_bytes
        << " physical_total_bytes="
        << view_.transient_memory.summary.physical_total_bytes
        << " peak_logical_live_bytes="
        << view_.transient_memory.summary.peak_logical_live_bytes
        << " peak_live_bytes="
        << view_.transient_memory.summary.peak_live_bytes
        << " saved_bytes=" << view_.transient_memory.summary.saved_bytes
        << " transient_resources="
        << view_.transient_memory.summary.transient_resource_count
        << " eligible_resources="
        << view_.transient_memory.summary.eligible_resource_count
        << " aliased_resources="
        << view_.transient_memory.summary.aliased_resource_count
        << " pages=" << view_.transient_memory.summary.page_count
        << " alias_candidates="
        << view_.transient_memory.summary.alias_candidate_count
        << " alias_barriers="
        << view_.transient_memory.summary.alias_barrier_count
        << " timeline_samples="
        << view_.transient_memory.summary.timeline_sample_count << '\n';
    for (const auto& record : view_.transient_memory.records) {
        oss << "transient_record[" << record.resource.index << "] name="
            << record.debug_name
            << " first=" << record.first_pass_order
            << " last=" << record.last_pass_order
            << " eligible=" << (record.eligible ? 1 : 0)
            << " aliased=" << (record.aliased ? 1 : 0)
            << " page=" << record.page_index
            << " alias_group=" << record.alias_group
            << " size=" << record.size_bytes
            << " alignment=" << record.alignment_bytes
            << " lazy_memory_requested="
            << (record.lazy_memory_requested ? 1 : 0);
        if (!record.rejection_reason.empty()) {
            oss << " rejection_reason=" << record.rejection_reason;
        }
        oss << '\n';
    }
    for (const auto& page : view_.transient_memory.pages) {
        oss << "transient_page[" << page.page_index << "] kind="
            << ResourceKindToString(page.kind)
            << " size=" << page.size_bytes
            << " alignment=" << page.alignment_bytes
            << " lazy_memory_requested="
            << (page.lazy_memory_requested ? 1 : 0)
            << " resources=";
        AppendResourceHandleList(oss, page.resource_ids);
        oss << " names=";
        AppendStringList(oss, page.resource_debug_names);
        oss << '\n';
    }
    for (const auto& sample : view_.transient_memory.timeline_samples) {
        oss << "transient_sample pass_order=" << sample.pass_order
            << " logical_live_bytes=" << sample.logical_live_bytes
            << " physical_live_bytes=" << sample.physical_live_bytes
            << " live_pages=" << sample.live_page_count << '\n';
    }
    for (const auto& candidate : view_.transient_memory.alias_candidates) {
        oss << "alias_candidate " << candidate.first.index << ':'
            << candidate.first_debug_name << " <-> "
            << candidate.second.index << ':' << candidate.second_debug_name
            << " kind=" << ResourceKindToString(candidate.kind)
            << " same_compatibility_class="
            << (candidate.same_compatibility_class ? 1 : 0)
            << " overlapping_liveness="
            << (candidate.overlapping_liveness ? 1 : 0)
            << " aliasable=" << (candidate.aliasable ? 1 : 0);
        if (!candidate.non_alias_reason.empty()) {
            oss << " non_alias_reason=" << candidate.non_alias_reason;
        }
        oss << '\n';
    }
    for (const auto& barrier : view_.transient_memory.alias_barriers) {
        oss << "alias_barrier " << barrier.previous.index << ':'
            << barrier.previous_debug_name << " -> "
            << barrier.next.index << ':' << barrier.next_debug_name
            << " page=" << barrier.page_index
            << " previous_last=" << barrier.previous_last_pass_order
            << " next_first=" << barrier.next_first_pass_order
            << " required=" << (barrier.required ? 1 : 0)
            << " realized=" << (barrier.realized ? 1 : 0) << '\n';
    }

    oss << "queue_batches=" << view_.queue_batches.size() << '\n';
    for (const auto& batch : view_.queue_batches) {
        oss << "queue_batch["
            << static_cast<RenderGraphQueueBatchTopologyId::underlying_type>(
                   batch.batch_id)
            << "] queue=" << QueueClassToString(batch.queue)
            << " pass_ids=";
        AppendPassHandleList(oss, batch.pass_ids);
        oss << " pass_names=";
        AppendStringList(oss, batch.pass_debug_names);
        oss << " wait_dependencies=";
        AppendTopologyIdList(oss, batch.wait_dependency_ids);
        oss << " signal_dependencies=";
        AppendTopologyIdList(oss, batch.signal_dependency_ids);
        oss << " barrier_batches=";
        AppendTopologyIdList(oss, batch.barrier_batch_ids);
        oss << " host_boundary=" << (batch.contains_host_boundary ? 1 : 0)
            << '\n';
    }

    oss << "queue_dependencies=" << view_.queue_dependencies.size() << '\n';
    for (const auto& dependency : view_.queue_dependencies) {
        oss << "queue_dependency["
            << static_cast<
                   RenderGraphQueueDependencyTopologyId::underlying_type>(
                   dependency.dependency_id)
            << "] "
            << QueueClassToString(dependency.source_queue) << '['
            << static_cast<RenderGraphQueueBatchTopologyId::underlying_type>(
                   dependency.source_batch_id)
            << "] -> " << QueueClassToString(dependency.target_queue) << '['
            << static_cast<RenderGraphQueueBatchTopologyId::underlying_type>(
                   dependency.target_batch_id)
            << "] source_pass=" << dependency.source_pass.index << ':'
            << dependency.source_pass_debug_name
            << " target_pass=" << dependency.target_pass.index << ':'
            << dependency.target_pass_debug_name
            << " resources=" << dependency.resources.size()
            << " queue_transfer=" << (dependency.queue_transfer ? 1 : 0)
            << " host_boundary=" << (dependency.host_boundary ? 1 : 0)
            << '\n';
    }

    oss << "barrier_batches=" << view_.barrier_batches.size() << '\n';
    for (const auto& batch : view_.barrier_batches) {
        oss << "barrier_batch["
            << static_cast<RenderGraphBarrierBatchTopologyId::underlying_type>(
                   batch.barrier_batch_id)
            << "] pass=" << batch.pass.index
            << ':' << batch.pass_debug_name
            << " queue=" << QueueClassToString(batch.queue)
            << " barriers=" << batch.barriers.size() << '\n';
    }

    oss << "native_pass_groups=" << view_.native_pass_groups.size() << '\n';
    for (const auto& group : view_.native_pass_groups) {
        oss << "native_pass_group["
            << static_cast<
                   RenderGraphNativePassGroupTopologyId::underlying_type>(
                   group.group_id)
            << "] queue=" << QueueClassToString(group.queue)
            << " first=" << group.first_pass_order
            << " last=" << group.last_pass_order
            << " pass_ids=";
        AppendPassHandleList(oss, group.pass_ids);
        oss << " pass_names=";
        AppendStringList(oss, group.pass_debug_names);
        oss << " color_attachments=" << group.color_attachment_count
            << " depth_attachment=" << (group.has_depth_attachment ? 1 : 0)
            << " layer_count=" << group.layer_count << '\n';
    }

    oss << "native_pass_boundaries=" << view_.native_pass_boundaries.size()
        << '\n';
    for (const auto& boundary : view_.native_pass_boundaries) {
        oss << "native_pass_boundary["
            << static_cast<
                   RenderGraphNativePassBoundaryTopologyId::underlying_type>(
                   boundary.boundary_id)
            << "] " << boundary.previous_pass.index << ':'
            << boundary.previous_pass_debug_name << " -> "
            << boundary.next_pass.index << ':'
            << boundary.next_pass_debug_name
            << " fused=" << (boundary.fused ? 1 : 0)
            << " reason=" << NativePassFusionBlockReasonName(boundary.block_reason)
            << " local_read_candidate="
            << (boundary.local_read_candidate ? 1 : 0)
            << " local_read_status="
            << NativePassLocalReadStatusName(boundary.local_read_status)
            << " local_read_reason="
            << NativePassLocalReadReasonName(boundary.local_read_reason);
        if (!boundary.detail.empty()) {
            oss << " detail=" << boundary.detail;
        }
        oss << '\n';
    }

    oss << "native_pass_local_read requested="
        << (view_.native_pass_local_read.requested ? 1 : 0)
        << " supported=" << (view_.native_pass_local_read.supported ? 1 : 0)
        << " device_enabled="
        << (view_.native_pass_local_read.device_enabled ? 1 : 0)
        << " candidate=" << (view_.native_pass_local_read.candidate ? 1 : 0)
        << " status="
        << NativePassLocalReadStatusName(view_.native_pass_local_read.status)
        << " reason="
        << NativePassLocalReadReasonName(view_.native_pass_local_read.reason)
        << '\n';

    return oss.str();
}

std::string BuildCompiledRenderGraphTopologyJson(
    const CompiledRenderGraphTopologyView& view_) {
    std::ostringstream oss{};
    oss << "{\n";
    oss << "  \"topologySummary\": {\n";
    oss << "    \"executionOrderCount\": "
        << view_.summary.execution_order_count << ",\n";
    oss << "    \"passCount\": " << view_.summary.pass_count << ",\n";
    oss << "    \"executablePassCount\": "
        << view_.summary.executable_pass_count << ",\n";
    oss << "    \"culledPassCount\": "
        << view_.summary.culled_pass_count << ",\n";
    oss << "    \"resourceCount\": " << view_.summary.resource_count << ",\n";
    oss << "    \"livenessRangeCount\": "
        << view_.summary.liveness_range_count << ",\n";
    oss << "    \"queueBatchCount\": "
        << view_.summary.queue_batch_count << ",\n";
    oss << "    \"queueDependencyCount\": "
        << view_.summary.queue_dependency_count << ",\n";
    oss << "    \"queueBatchHostBoundaryCount\": "
        << view_.summary.queue_batch_host_boundary_count << ",\n";
    oss << "    \"queueDependencyHostBoundaryCount\": "
        << view_.summary.queue_dependency_host_boundary_count << ",\n";
    oss << "    \"barrierBatchCount\": "
        << view_.summary.barrier_batch_count << ",\n";
    oss << "    \"barrierCount\": " << view_.summary.barrier_count << ",\n";
    oss << "    \"aliasCandidateCount\": "
        << view_.summary.alias_candidate_count << ",\n";
    oss << "    \"aliasBarrierCount\": "
        << view_.summary.alias_barrier_count << ",\n";
    oss << "    \"nativePassGroupCount\": "
        << view_.summary.native_pass_group_count << ",\n";
    oss << "    \"nativePassBoundaryCount\": "
        << view_.summary.native_pass_boundary_count << ",\n";
    oss << "    \"logicalRasterPassCount\": "
        << view_.summary.logical_raster_pass_count << ",\n";
    oss << "    \"fusedRasterPassCount\": "
        << view_.summary.fused_raster_pass_count << ",\n";
    oss << "    \"storeElisionCount\": "
        << view_.summary.store_elision_count << ",\n";
    oss << "    \"loadInferenceCount\": "
        << view_.summary.load_inference_count << ",\n";
    oss << "    \"effectiveClearAttachmentCount\": "
        << view_.summary.clear_attachment_count << ",\n";
    oss << "    \"localReadCandidateCount\": "
        << view_.summary.local_read_candidate_count << '\n';
    oss << "  },\n";

    oss << "  \"executionOrder\": [";
    for (std::size_t index = 0U; index < view_.execution_order.size(); ++index) {
        if (index != 0U) {
            oss << ", ";
        }
        oss << view_.execution_order[index].index;
    }
    oss << "],\n";

    oss << "  \"resources\": [\n";
    for (std::size_t index = 0U; index < view_.resources.size(); ++index) {
        const auto& resource = view_.resources[index];
        oss << "    {\n";
        oss << "      \"resourceId\": " << resource.resource.index << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(resource.debug_name)
            << "\",\n";
        oss << "      \"kind\": \"" << ResourceKindToString(resource.kind)
            << "\",\n";
        oss << "      \"lifetime\": \""
            << ResourceLifetimeToString(resource.lifetime) << "\"\n";
        oss << "    }";
        if (index + 1U != view_.resources.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"passes\": [\n";
    for (std::size_t index = 0U; index < view_.passes.size(); ++index) {
        const auto& pass = view_.passes[index];
        oss << "    {\n";
        oss << "      \"passId\": " << pass.pass.index << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(pass.debug_name)
            << "\",\n";
        oss << "      \"sideEffect\": "
            << (pass.side_effect ? "true" : "false") << ",\n";
        oss << "      \"executable\": "
            << (pass.executable ? "true" : "false") << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(pass.queue)
            << "\",\n";
        oss << "      \"compileHints\": {\"forceNativePassSplit\": "
            << (pass.force_native_pass_split ? "true" : "false") << "},\n";
        oss << "      \"raster\": {\"enabled\": "
            << (pass.raster_pass ? "true" : "false")
            << ", \"colorAttachmentCount\": "
            << pass.raster_color_attachment_count
            << ", \"hasDepthAttachment\": "
            << (pass.raster_has_depth_attachment ? "true" : "false")
            << "},\n";
        oss << "      \"dependencyPassIds\": [";
        for (std::size_t dependency_index = 0U;
             dependency_index < pass.dependencies.size();
             ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << pass.dependencies[dependency_index].index;
        }
        oss << "],\n";
        oss << "      \"dependencyPassNames\": [";
        for (std::size_t dependency_index = 0U;
             dependency_index < pass.dependency_debug_names.size();
             ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << '"' << EscapeJsonString(
                pass.dependency_debug_names[dependency_index]) << '"';
        }
        oss << "],\n";
        oss << "      \"reads\": [";
        for (std::size_t read_index = 0U;
             read_index < pass.reads.size();
             ++read_index) {
            if (read_index != 0U) {
                oss << ", ";
            }
            AppendJsonAccessView(oss, pass.reads[read_index]);
        }
        oss << "],\n";
        oss << "      \"writes\": [";
        for (std::size_t write_index = 0U;
             write_index < pass.writes.size();
             ++write_index) {
            if (write_index != 0U) {
                oss << ", ";
            }
            AppendJsonAccessView(oss, pass.writes[write_index]);
        }
        oss << "],\n";
        oss << "      \"descriptorBindingCount\": "
            << pass.descriptor_binding_count << '\n';
        oss << "    }";
        if (index + 1U != view_.passes.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"livenessRanges\": [\n";
    for (std::size_t index = 0U; index < view_.liveness_ranges.size();
         ++index) {
        const auto& range = view_.liveness_ranges[index];
        oss << "    {\n";
        oss << "      \"resourceIndex\": "
            << range.resource.version.resource_index << ",\n";
        oss << "      \"version\": " << range.resource.version.version
            << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(range.resource.debug_name)
            << "\",\n";
        oss << "      \"kind\": \"" << ResourceKindToString(range.resource.kind)
            << "\",\n";
        oss << "      \"lifetime\": \""
            << ResourceLifetimeToString(range.lifetime) << "\",\n";
        oss << "      \"firstPassOrder\": " << range.first_pass_order << ",\n";
        oss << "      \"lastPassOrder\": " << range.last_pass_order << '\n';
        oss << "    }";
        if (index + 1U != view_.liveness_ranges.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"transientMemory\": {\n";
    oss << "    \"summary\": {\n";
    oss << "      \"logicalTotalBytes\": "
        << view_.transient_memory.summary.logical_total_bytes << ",\n";
    oss << "      \"physicalTotalBytes\": "
        << view_.transient_memory.summary.physical_total_bytes << ",\n";
    oss << "      \"peakLogicalLiveBytes\": "
        << view_.transient_memory.summary.peak_logical_live_bytes << ",\n";
    oss << "      \"peakLiveBytes\": "
        << view_.transient_memory.summary.peak_live_bytes << ",\n";
    oss << "      \"savedBytes\": "
        << view_.transient_memory.summary.saved_bytes << ",\n";
    oss << "      \"transientResourceCount\": "
        << view_.transient_memory.summary.transient_resource_count << ",\n";
    oss << "      \"eligibleResourceCount\": "
        << view_.transient_memory.summary.eligible_resource_count << ",\n";
    oss << "      \"aliasedResourceCount\": "
        << view_.transient_memory.summary.aliased_resource_count << ",\n";
    oss << "      \"pageCount\": "
        << view_.transient_memory.summary.page_count << ",\n";
    oss << "      \"aliasCandidateCount\": "
        << view_.transient_memory.summary.alias_candidate_count << ",\n";
    oss << "      \"aliasBarrierCount\": "
        << view_.transient_memory.summary.alias_barrier_count << ",\n";
    oss << "      \"timelineSampleCount\": "
        << view_.transient_memory.summary.timeline_sample_count << "\n";
    oss << "    },\n";

    oss << "    \"records\": [\n";
    for (std::size_t index = 0U; index < view_.transient_memory.records.size();
         ++index) {
        const auto& record = view_.transient_memory.records[index];
        oss << "      {\n";
        oss << "        \"resourceId\": " << record.resource.index << ",\n";
        oss << "        \"name\": \"" << EscapeJsonString(record.debug_name)
            << "\",\n";
        oss << "        \"kind\": \"" << ResourceKindToString(record.kind)
            << "\",\n";
        oss << "        \"lifetime\": \""
            << ResourceLifetimeToString(record.lifetime) << "\",\n";
        oss << "        \"footprint\": {\n";
        oss << "          \"sizeBytes\": " << record.size_bytes << ",\n";
        oss << "          \"alignmentBytes\": " << record.alignment_bytes
            << ",\n";
        oss << "          \"memoryTypeBits\": " << record.memory_type_bits
            << ",\n";
        oss << "          \"usageFlags\": " << record.usage_flags << ",\n";
        oss << "          \"dedicatedRequired\": "
            << (record.dedicated_required ? "true" : "false") << ",\n";
        oss << "          \"dedicatedPreferred\": "
            << (record.dedicated_preferred ? "true" : "false") << ",\n";
        oss << "          \"hostVisible\": "
            << (record.host_visible ? "true" : "false") << ",\n";
        oss << "          \"persistentlyMapped\": "
            << (record.persistently_mapped ? "true" : "false") << ",\n";
        oss << "          \"lazyMemoryRequested\": "
            << (record.lazy_memory_requested ? "true" : "false") << "\n";
        oss << "        },\n";
        oss << "        \"firstPassOrder\": " << record.first_pass_order
            << ",\n";
        oss << "        \"lastPassOrder\": " << record.last_pass_order
            << ",\n";
        oss << "        \"pageIndex\": " << record.page_index << ",\n";
        oss << "        \"pageOffsetBytes\": " << record.page_offset_bytes
            << ",\n";
        oss << "        \"aliasGroup\": " << record.alias_group << ",\n";
        oss << "        \"eligible\": "
            << (record.eligible ? "true" : "false") << ",\n";
        oss << "        \"aliased\": "
            << (record.aliased ? "true" : "false") << ",\n";
        oss << "        \"rejectionReason\": \""
            << EscapeJsonString(record.rejection_reason) << "\"\n";
        oss << "      }";
        if (index + 1U != view_.transient_memory.records.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ],\n";

    oss << "    \"pages\": [\n";
    for (std::size_t index = 0U; index < view_.transient_memory.pages.size();
         ++index) {
        const auto& page = view_.transient_memory.pages[index];
        oss << "      {\n";
        oss << "        \"pageIndex\": " << page.page_index << ",\n";
        oss << "        \"kind\": \"" << ResourceKindToString(page.kind)
            << "\",\n";
        oss << "        \"sizeBytes\": " << page.size_bytes << ",\n";
        oss << "        \"alignmentBytes\": " << page.alignment_bytes << ",\n";
        oss << "        \"lazyMemoryRequested\": "
            << (page.lazy_memory_requested ? "true" : "false") << ",\n";
        oss << "        \"resourceIds\": [";
        for (std::size_t resource_index = 0U;
             resource_index < page.resource_ids.size();
             ++resource_index) {
            if (resource_index != 0U) {
                oss << ", ";
            }
            oss << page.resource_ids[resource_index].index;
        }
        oss << "],\n";
        oss << "        \"resourceNames\": [";
        for (std::size_t resource_index = 0U;
             resource_index < page.resource_debug_names.size();
             ++resource_index) {
            if (resource_index != 0U) {
                oss << ", ";
            }
            oss << '"'
                << EscapeJsonString(page.resource_debug_names[resource_index])
                << '"';
        }
        oss << "]\n";
        oss << "      }";
        if (index + 1U != view_.transient_memory.pages.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ],\n";

    oss << "    \"timelineSamples\": [\n";
    for (std::size_t index = 0U;
         index < view_.transient_memory.timeline_samples.size();
         ++index) {
        const auto& sample = view_.transient_memory.timeline_samples[index];
        oss << "      {\n";
        oss << "        \"passOrder\": " << sample.pass_order << ",\n";
        oss << "        \"logicalLiveBytes\": " << sample.logical_live_bytes
            << ",\n";
        oss << "        \"physicalLiveBytes\": "
            << sample.physical_live_bytes << ",\n";
        oss << "        \"livePageCount\": " << sample.live_page_count
            << "\n";
        oss << "      }";
        if (index + 1U != view_.transient_memory.timeline_samples.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ],\n";

    oss << "    \"aliasCandidates\": [\n";
    for (std::size_t index = 0U;
         index < view_.transient_memory.alias_candidates.size();
         ++index) {
        const auto& candidate = view_.transient_memory.alias_candidates[index];
        oss << "      {\n";
        oss << "        \"firstResourceId\": " << candidate.first.index
            << ",\n";
        oss << "        \"firstName\": \""
            << EscapeJsonString(candidate.first_debug_name) << "\",\n";
        oss << "        \"secondResourceId\": " << candidate.second.index
            << ",\n";
        oss << "        \"secondName\": \""
            << EscapeJsonString(candidate.second_debug_name) << "\",\n";
        oss << "        \"kind\": \"" << ResourceKindToString(candidate.kind)
            << "\",\n";
        oss << "        \"sameCompatibilityClass\": "
            << (candidate.same_compatibility_class ? "true" : "false")
            << ",\n";
        oss << "        \"overlappingLiveness\": "
            << (candidate.overlapping_liveness ? "true" : "false") << ",\n";
        oss << "        \"aliasable\": "
            << (candidate.aliasable ? "true" : "false") << ",\n";
        oss << "        \"nonAliasReason\": \""
            << EscapeJsonString(candidate.non_alias_reason) << "\"\n";
        oss << "      }";
        if (index + 1U != view_.transient_memory.alias_candidates.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ],\n";

    oss << "    \"aliasBarriers\": [\n";
    for (std::size_t index = 0U;
         index < view_.transient_memory.alias_barriers.size();
         ++index) {
        const auto& barrier = view_.transient_memory.alias_barriers[index];
        oss << "      {\n";
        oss << "        \"previousResourceId\": " << barrier.previous.index
            << ",\n";
        oss << "        \"previousName\": \""
            << EscapeJsonString(barrier.previous_debug_name) << "\",\n";
        oss << "        \"nextResourceId\": " << barrier.next.index << ",\n";
        oss << "        \"nextName\": \""
            << EscapeJsonString(barrier.next_debug_name) << "\",\n";
        oss << "        \"pageIndex\": " << barrier.page_index << ",\n";
        oss << "        \"previousLastPassOrder\": "
            << barrier.previous_last_pass_order << ",\n";
        oss << "        \"nextFirstPassOrder\": "
            << barrier.next_first_pass_order << ",\n";
        oss << "        \"required\": "
            << (barrier.required ? "true" : "false") << ",\n";
        oss << "        \"realized\": "
            << (barrier.realized ? "true" : "false") << "\n";
        oss << "      }";
        if (index + 1U != view_.transient_memory.alias_barriers.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ]\n";
    oss << "  },\n";

    oss << "  \"queueBatches\": [\n";
    for (std::size_t index = 0U; index < view_.queue_batches.size(); ++index) {
        const auto& batch = view_.queue_batches[index];
        oss << "    {\n";
        oss << "      \"queueBatchId\": "
            << static_cast<RenderGraphQueueBatchTopologyId::underlying_type>(
                   batch.batch_id)
            << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(batch.queue)
            << "\",\n";
        oss << "      \"passIds\": [";
        for (std::size_t pass_index = 0U; pass_index < batch.pass_ids.size();
             ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << batch.pass_ids[pass_index].index;
        }
        oss << "],\n";
        oss << "      \"passNames\": [";
        for (std::size_t pass_index = 0U;
             pass_index < batch.pass_debug_names.size();
             ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << '"' << EscapeJsonString(batch.pass_debug_names[pass_index])
                << '"';
        }
        oss << "],\n";
        oss << "      \"waitDependencyIds\": [";
        for (std::size_t dependency_index = 0U;
             dependency_index < batch.wait_dependency_ids.size();
             ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << static_cast<
                RenderGraphQueueDependencyTopologyId::underlying_type>(
                batch.wait_dependency_ids[dependency_index]);
        }
        oss << "],\n";
        oss << "      \"signalDependencyIds\": [";
        for (std::size_t dependency_index = 0U;
             dependency_index < batch.signal_dependency_ids.size();
             ++dependency_index) {
            if (dependency_index != 0U) {
                oss << ", ";
            }
            oss << static_cast<
                RenderGraphQueueDependencyTopologyId::underlying_type>(
                batch.signal_dependency_ids[dependency_index]);
        }
        oss << "],\n";
        oss << "      \"barrierBatchIds\": [";
        for (std::size_t barrier_index = 0U;
             barrier_index < batch.barrier_batch_ids.size();
             ++barrier_index) {
            if (barrier_index != 0U) {
                oss << ", ";
            }
            oss << static_cast<
                RenderGraphBarrierBatchTopologyId::underlying_type>(
                batch.barrier_batch_ids[barrier_index]);
        }
        oss << "],\n";
        oss << "      \"containsHostBoundary\": "
            << (batch.contains_host_boundary ? "true" : "false") << '\n';
        oss << "    }";
        if (index + 1U != view_.queue_batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"queueDependencies\": [\n";
    for (std::size_t index = 0U;
         index < view_.queue_dependencies.size();
         ++index) {
        const auto& dependency = view_.queue_dependencies[index];
        oss << "    {\n";
        oss << "      \"queueDependencyId\": "
            << static_cast<
                RenderGraphQueueDependencyTopologyId::underlying_type>(
                dependency.dependency_id)
            << ",\n";
        oss << "      \"sourceQueue\": \""
            << QueueClassToString(dependency.source_queue) << "\",\n";
        oss << "      \"targetQueue\": \""
            << QueueClassToString(dependency.target_queue) << "\",\n";
        oss << "      \"sourceBatchId\": "
            << static_cast<RenderGraphQueueBatchTopologyId::underlying_type>(
                   dependency.source_batch_id)
            << ",\n";
        oss << "      \"targetBatchId\": "
            << static_cast<RenderGraphQueueBatchTopologyId::underlying_type>(
                   dependency.target_batch_id)
            << ",\n";
        oss << "      \"sourcePassId\": " << dependency.source_pass.index
            << ",\n";
        oss << "      \"targetPassId\": " << dependency.target_pass.index
            << ",\n";
        oss << "      \"sourcePass\": \""
            << EscapeJsonString(dependency.source_pass_debug_name)
            << "\",\n";
        oss << "      \"targetPass\": \""
            << EscapeJsonString(dependency.target_pass_debug_name)
            << "\",\n";
        oss << "      \"resources\": [";
        for (std::size_t resource_index = 0U;
             resource_index < dependency.resources.size();
             ++resource_index) {
            if (resource_index != 0U) {
                oss << ", ";
            }
            AppendJsonResourceVersionView(oss,
                                          dependency.resources[resource_index]);
        }
        oss << "],\n";
        oss << "      \"queueTransfer\": "
            << (dependency.queue_transfer ? "true" : "false") << ",\n";
        oss << "      \"hostBoundary\": "
            << (dependency.host_boundary ? "true" : "false") << '\n';
        oss << "    }";
        if (index + 1U != view_.queue_dependencies.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"barrierBatches\": [\n";
    for (std::size_t index = 0U; index < view_.barrier_batches.size();
         ++index) {
        const auto& batch = view_.barrier_batches[index];
        oss << "    {\n";
        oss << "      \"barrierBatchId\": "
            << static_cast<RenderGraphBarrierBatchTopologyId::underlying_type>(
                   batch.barrier_batch_id)
            << ",\n";
        oss << "      \"passId\": " << batch.pass.index << ",\n";
        oss << "      \"passName\": \"" << EscapeJsonString(batch.pass_debug_name)
            << "\",\n";
        oss << "      \"queue\": \"" << QueueClassToString(batch.queue)
            << "\",\n";
        oss << "      \"barriers\": [";
        for (std::size_t barrier_index = 0U;
             barrier_index < batch.barriers.size();
             ++barrier_index) {
            if (barrier_index != 0U) {
                oss << ", ";
            }
            const auto& barrier = batch.barriers[barrier_index];
            oss << "{\"resourceIndex\": "
                << barrier.resource.version.resource_index
                << ", \"version\": " << barrier.resource.version.version
                << ", \"name\": \""
                << EscapeJsonString(barrier.resource.debug_name) << "\""
                << ", \"kind\": \""
                << ResourceKindToString(barrier.resource.kind) << "\""
                << ", \"before\": \""
                << AccessKindToString(barrier.before) << "\""
                << ", \"after\": \""
                << AccessKindToString(barrier.after) << "\""
                << ", \"srcQueue\": \""
                << QueueClassToString(barrier.src_queue) << "\""
                << ", \"dstQueue\": \""
                << QueueClassToString(barrier.dst_queue) << "\""
                << ", \"srcPassId\": " << barrier.src_pass.index
                << ", \"dstPassId\": " << barrier.dst_pass.index
                << ", \"srcPass\": \""
                << EscapeJsonString(barrier.src_pass_debug_name) << "\""
                << ", \"dstPass\": \""
                << EscapeJsonString(barrier.dst_pass_debug_name) << "\""
                << ", \"srcPassOrder\": " << barrier.src_pass_order
                << ", \"dstPassOrder\": " << barrier.dst_pass_order
                << ", \"queueTransfer\": "
                << (barrier.queue_transfer ? "true" : "false")
                << ", \"hostBoundary\": "
                << (barrier.host_boundary ? "true" : "false")
                << ", \"aliasing\": "
                << (barrier.aliasing ? "true" : "false")
                << ", \"uavOrdering\": "
                << (barrier.uav_ordering ? "true" : "false");
            if (barrier.resource.kind == ResourceKind::texture) {
                oss << ", \"subresourceRange\": {"
                    << "\"baseMipLevel\": "
                    << barrier.subresource_range.base_mip_level
                    << ", \"levelCount\": "
                    << barrier.subresource_range.level_count
                    << ", \"baseArrayLayer\": "
                    << barrier.subresource_range.base_array_layer
                    << ", \"layerCount\": "
                    << barrier.subresource_range.layer_count << '}';
            } else {
                oss << ", \"bufferRange\": {"
                    << "\"offsetBytes\": " << barrier.buffer_range.offset_bytes
                    << ", \"sizeBytes\": " << barrier.buffer_range.size_bytes
                    << '}';
            }
            oss << '}';
        }
        oss << "]\n";
        oss << "    }";
        if (index + 1U != view_.barrier_batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"nativePassTopology\": {\n";
    oss << "    \"localRead\": {\n";
    oss << "      \"requested\": "
        << (view_.native_pass_local_read.requested ? "true" : "false")
        << ",\n";
    oss << "      \"supported\": "
        << (view_.native_pass_local_read.supported ? "true" : "false")
        << ",\n";
    oss << "      \"deviceEnabled\": "
        << (view_.native_pass_local_read.device_enabled ? "true" : "false")
        << ",\n";
    oss << "      \"candidate\": "
        << (view_.native_pass_local_read.candidate ? "true" : "false")
        << ",\n";
    oss << "      \"status\": \""
        << NativePassLocalReadStatusName(view_.native_pass_local_read.status)
        << "\",\n";
    oss << "      \"reason\": \""
        << NativePassLocalReadReasonName(view_.native_pass_local_read.reason)
        << "\",\n";
    oss << "      \"detail\": \""
        << EscapeJsonString(view_.native_pass_local_read.detail) << "\"\n";
    oss << "    },\n";
    oss << "    \"groups\": [\n";
    for (std::size_t index = 0U; index < view_.native_pass_groups.size();
         ++index) {
        const auto& group = view_.native_pass_groups[index];
        oss << "      {\n";
        oss << "        \"groupId\": "
            << static_cast<
                RenderGraphNativePassGroupTopologyId::underlying_type>(
                group.group_id)
            << ",\n";
        oss << "        \"queue\": \"" << QueueClassToString(group.queue)
            << "\",\n";
        oss << "        \"firstPassOrder\": " << group.first_pass_order
            << ",\n";
        oss << "        \"lastPassOrder\": " << group.last_pass_order
            << ",\n";
        oss << "        \"passIds\": [";
        for (std::size_t pass_index = 0U; pass_index < group.pass_ids.size();
             ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << group.pass_ids[pass_index].index;
        }
        oss << "],\n";
        oss << "        \"passNames\": [";
        for (std::size_t pass_index = 0U;
             pass_index < group.pass_debug_names.size();
             ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << '"'
                << EscapeJsonString(group.pass_debug_names[pass_index]) << '"';
        }
        oss << "],\n";
        oss << "        \"colorAttachmentCount\": "
            << group.color_attachment_count << ",\n";
        oss << "        \"hasDepthAttachment\": "
            << (group.has_depth_attachment ? "true" : "false") << ",\n";
        oss << "        \"layerCount\": " << group.layer_count << '\n';
        oss << "      }";
        if (index + 1U != view_.native_pass_groups.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ],\n";
    oss << "    \"boundaries\": [\n";
    for (std::size_t index = 0U; index < view_.native_pass_boundaries.size();
         ++index) {
        const auto& boundary = view_.native_pass_boundaries[index];
        oss << "      {\n";
        oss << "        \"boundaryId\": "
            << static_cast<
                RenderGraphNativePassBoundaryTopologyId::underlying_type>(
                boundary.boundary_id)
            << ",\n";
        oss << "        \"previousPassOrder\": "
            << boundary.previous_pass_order << ",\n";
        oss << "        \"previousPassId\": "
            << boundary.previous_pass.index << ",\n";
        oss << "        \"previousPass\": \""
            << EscapeJsonString(boundary.previous_pass_debug_name)
            << "\",\n";
        oss << "        \"nextPassOrder\": " << boundary.next_pass_order
            << ",\n";
        oss << "        \"nextPassId\": " << boundary.next_pass.index
            << ",\n";
        oss << "        \"nextPass\": \""
            << EscapeJsonString(boundary.next_pass_debug_name) << "\",\n";
        oss << "        \"fused\": "
            << (boundary.fused ? "true" : "false") << ",\n";
        oss << "        \"reason\": \""
            << NativePassFusionBlockReasonName(boundary.block_reason)
            << "\",\n";
        oss << "        \"localRead\": {\n";
        oss << "          \"candidate\": "
            << (boundary.local_read_candidate ? "true" : "false") << ",\n";
        oss << "          \"status\": \""
            << NativePassLocalReadStatusName(boundary.local_read_status)
            << "\",\n";
        oss << "          \"reason\": \""
            << NativePassLocalReadReasonName(boundary.local_read_reason)
            << "\"\n";
        oss << "        },\n";
        oss << "        \"detail\": \""
            << EscapeJsonString(boundary.detail) << "\"\n";
        oss << "      }";
        if (index + 1U != view_.native_pass_boundaries.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ]\n";
    oss << "  }\n";
    oss << '}';
    return oss.str();
}

} // namespace vr::render_graph

#endif
