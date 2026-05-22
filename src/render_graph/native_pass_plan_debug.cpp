#include "native_pass_plan_internal.hpp"

#include <sstream>

namespace vr::render_graph::native_pass_detail {
void AppendGroupAttachmentsJson(std::ostringstream& oss_,
                                const CompiledRenderGraph& compiled_graph_,
                                const NativePassAttachmentPlan& attachments_) {
    oss_ << "{";
    oss_ << "\"layerCount\": " << attachments_.layer_count << ", ";
    oss_ << "\"colorAttachments\": [";
    for (std::size_t color_index = 0U;
         color_index < attachments_.color_attachments.size();
         ++color_index) {
        if (color_index != 0U) {
            oss_ << ", ";
        }
        const auto& attachment_ = attachments_.color_attachments[color_index];
        oss_ << "{"
             << "\"resourceIndex\": " << attachment_.target.index << ", "
             << "\"name\": \""
             << EscapeJsonString(
                    ResolveResourceName(compiled_graph_, attachment_.target))
             << "\", "
             << "\"requestedLoadOp\": \""
             << AttachmentLoadOpToString(attachment_.requested_load_op)
             << "\", "
             << "\"effectiveLoadOp\": \""
             << AttachmentLoadOpToString(attachment_.effective_load_op)
             << "\", "
             << "\"loadReason\": \""
             << NativePassAttachmentLoadOpReasonName(attachment_.load_reason)
             << "\", "
             << "\"requestedStoreOp\": \""
             << AttachmentStoreOpToString(attachment_.requested_store_op)
             << "\", "
             << "\"effectiveStoreOp\": \""
             << AttachmentStoreOpToString(attachment_.effective_store_op)
             << "\", "
             << "\"storeReason\": \""
             << NativePassAttachmentStoreOpReasonName(attachment_.store_reason)
             << "\", "
             << "\"loadInferred\": "
             << (attachment_.load_inferred ? "true" : "false") << ", "
             << "\"storeElided\": "
             << (attachment_.store_elided ? "true" : "false") << ", "
             << "\"clearValue\": {"
             << "\"red\": " << attachment_.clear_value.red << ", "
             << "\"green\": " << attachment_.clear_value.green << ", "
             << "\"blue\": " << attachment_.clear_value.blue << ", "
             << "\"alpha\": " << attachment_.clear_value.alpha << "}}";
    }
    oss_ << "]";
    if (attachments_.has_depth_attachment) {
        oss_ << ", \"depthAttachment\": {"
             << "\"resourceIndex\": "
             << attachments_.depth_attachment.target.index << ", "
             << "\"name\": \""
             << EscapeJsonString(ResolveResourceName(
                    compiled_graph_, attachments_.depth_attachment.target))
             << "\", "
             << "\"readOnly\": "
             << (attachments_.depth_attachment.read_only ? "true" : "false")
             << ", "
             << "\"requestedLoadOp\": \""
             << AttachmentLoadOpToString(
                    attachments_.depth_attachment.requested_load_op)
             << "\", "
             << "\"effectiveLoadOp\": \""
             << AttachmentLoadOpToString(
                    attachments_.depth_attachment.effective_load_op)
             << "\", "
             << "\"loadReason\": \""
             << NativePassAttachmentLoadOpReasonName(
                    attachments_.depth_attachment.load_reason)
             << "\", "
             << "\"requestedStoreOp\": \""
             << AttachmentStoreOpToString(
                    attachments_.depth_attachment.requested_store_op)
             << "\", "
             << "\"effectiveStoreOp\": \""
             << AttachmentStoreOpToString(
                    attachments_.depth_attachment.effective_store_op)
             << "\", "
             << "\"storeReason\": \""
             << NativePassAttachmentStoreOpReasonName(
                    attachments_.depth_attachment.store_reason)
             << "\", "
             << "\"requestedStencilLoadOp\": \""
             << AttachmentLoadOpToString(
                    attachments_.depth_attachment.requested_stencil_load_op)
             << "\", "
             << "\"effectiveStencilLoadOp\": \""
             << AttachmentLoadOpToString(
                    attachments_.depth_attachment.effective_stencil_load_op)
             << "\", "
             << "\"stencilLoadReason\": \""
             << NativePassAttachmentLoadOpReasonName(
                    attachments_.depth_attachment.stencil_load_reason)
             << "\", "
             << "\"requestedStencilStoreOp\": \""
             << AttachmentStoreOpToString(
                    attachments_.depth_attachment.requested_stencil_store_op)
             << "\", "
             << "\"effectiveStencilStoreOp\": \""
             << AttachmentStoreOpToString(
                    attachments_.depth_attachment.effective_stencil_store_op)
             << "\", "
             << "\"stencilStoreReason\": \""
             << NativePassAttachmentStoreOpReasonName(
                    attachments_.depth_attachment.stencil_store_reason)
             << "\", "
             << "\"loadInferred\": "
             << (attachments_.depth_attachment.load_inferred ? "true" : "false")
             << ", "
             << "\"storeElided\": "
             << (attachments_.depth_attachment.store_elided ? "true" : "false")
             << ", "
             << "\"clearValue\": {"
             << "\"depth\": "
             << attachments_.depth_attachment.clear_value.depth << ", "
             << "\"stencil\": "
             << attachments_.depth_attachment.clear_value.stencil << "}}";
    } else {
        oss_ << ", \"depthAttachment\": null";
    }
    oss_ << "}";
}

} // namespace vr::render_graph::native_pass_detail

namespace vr::render_graph {
using namespace native_pass_detail;

std::string BuildNativePassPlanDebugString(
    const CompiledRenderGraph& compiled_graph_) {
    const auto& plan = compiled_graph_.NativePasses();
    std::ostringstream oss{};
    oss << "logical_raster_passes=" << plan.summary.logical_raster_pass_count
        << '\n';
    oss << "native_pass_groups=" << plan.groups.size() << '\n';
    oss << "fused_raster_passes=" << plan.summary.fused_raster_pass_count
        << '\n';
    oss << "native_pass_store_elisions=" << plan.summary.store_elision_count
        << '\n';
    oss << "native_pass_load_inferences=" << plan.summary.load_inference_count
        << '\n';
    oss << "native_pass_effective_clears="
        << plan.summary.clear_attachment_count << '\n';
    oss << "native_pass_local_read requested="
        << (plan.local_read.requested ? 1 : 0)
        << " supported=" << (plan.local_read.supported ? 1 : 0)
        << " device_enabled=" << (plan.local_read.device_enabled ? 1 : 0)
        << " candidates=" << plan.summary.local_read_candidate_count
        << " status=" << NativePassLocalReadStatusName(plan.local_read.status)
        << " reason=" << NativePassLocalReadReasonName(plan.local_read.reason)
        << '\n';
    for (std::size_t group_index = 0U;
         group_index < plan.groups.size();
         ++group_index) {
        const auto& group_ = plan.groups[group_index];
        oss << "native_pass_group[" << group_index << "]"
            << " queue=" << QueueClassToString(group_.queue)
            << " first=" << group_.first_pass_order
            << " last=" << group_.last_pass_order
            << " passes=";
        for (std::size_t pass_index = 0U;
             pass_index < group_.logical_passes.size();
             ++pass_index) {
            if (pass_index != 0U) {
                oss << ',';
            }
            const auto pass_order =
                group_.first_pass_order + static_cast<std::uint32_t>(pass_index);
            oss << ResolvePassName(compiled_graph_, pass_order);
        }
        oss << " colors=";
        for (std::size_t color_index = 0U;
             color_index < group_.attachments.color_attachments.size();
             ++color_index) {
            if (color_index != 0U) {
                oss << ',';
            }
            const auto& color_attachment =
                group_.attachments.color_attachments[color_index];
            oss << ResolveResourceName(compiled_graph_, color_attachment.target)
                << "{requested="
                << AttachmentLoadOpToString(
                       color_attachment.requested_load_op)
                << '/'
                << AttachmentStoreOpToString(
                       color_attachment.requested_store_op)
                << ", effective="
                << AttachmentLoadOpToString(
                       color_attachment.effective_load_op)
                << '/'
                << AttachmentStoreOpToString(
                       color_attachment.effective_store_op)
                << ", load_reason="
                << NativePassAttachmentLoadOpReasonName(
                       color_attachment.load_reason)
                << ", store_reason="
                << NativePassAttachmentStoreOpReasonName(
                       color_attachment.store_reason)
                << ", load_inferred="
                << (color_attachment.load_inferred ? 1 : 0)
                << ", store_elided="
                << (color_attachment.store_elided ? 1 : 0)
                << '}';
        }
        if (group_.attachments.has_depth_attachment) {
            oss << " depth="
                << ResolveResourceName(compiled_graph_,
                                       group_.attachments.depth_attachment.target)
                << " depth_read_only="
                << (group_.attachments.depth_attachment.read_only ? 1 : 0)
                << " depth_requested="
                << AttachmentLoadOpToString(
                       group_.attachments.depth_attachment.requested_load_op)
                << '/'
                << AttachmentStoreOpToString(
                       group_.attachments.depth_attachment.requested_store_op)
                << " depth_effective="
                << AttachmentLoadOpToString(
                       group_.attachments.depth_attachment.effective_load_op)
                << '/'
                << AttachmentStoreOpToString(
                       group_.attachments.depth_attachment.effective_store_op)
                << " depth_load_reason="
                << NativePassAttachmentLoadOpReasonName(
                       group_.attachments.depth_attachment.load_reason)
                << " depth_store_reason="
                << NativePassAttachmentStoreOpReasonName(
                       group_.attachments.depth_attachment.store_reason)
                << " depth_store_elided="
                << (group_.attachments.depth_attachment.store_elided ? 1 : 0);
        }
        oss << " layer_count=" << group_.attachments.layer_count << '\n';
    }

    oss << "native_pass_boundaries=" << plan.boundaries.size() << '\n';
    for (std::size_t boundary_index = 0U;
         boundary_index < plan.boundaries.size();
         ++boundary_index) {
        const auto& boundary_ = plan.boundaries[boundary_index];
        oss << "native_pass_boundary[" << boundary_index << "] "
            << ResolvePassName(compiled_graph_, boundary_.previous_pass_order)
            << " -> "
            << ResolvePassName(compiled_graph_, boundary_.next_pass_order)
            << " fused=" << (boundary_.fused ? 1 : 0)
            << " reason="
            << NativePassFusionBlockReasonName(boundary_.block_reason);
        if (boundary_.local_read.candidate) {
            oss << " local_read_candidate=1"
                << " local_read_status="
                << NativePassLocalReadStatusName(boundary_.local_read.status)
                << " local_read_reason="
                << NativePassLocalReadReasonName(boundary_.local_read.reason);
        }
        if (!boundary_.detail.empty()) {
            oss << " detail=" << boundary_.detail;
        }
        oss << '\n';
    }
    return oss.str();
}

std::string BuildNativePassPlanJson(
    const CompiledRenderGraph& compiled_graph_) {
    const auto& plan = compiled_graph_.NativePasses();
    std::ostringstream oss{};
    oss << "{\n";
    oss << "    \"summary\": {\n";
    oss << "      \"logicalRasterPassCount\": "
        << plan.summary.logical_raster_pass_count << ",\n";
    oss << "      \"nativePassGroupCount\": "
        << plan.summary.native_pass_group_count << ",\n";
    oss << "      \"fusedRasterPassCount\": "
        << plan.summary.fused_raster_pass_count << ",\n";
    oss << "      \"storeElisionCount\": "
        << plan.summary.store_elision_count << ",\n";
    oss << "      \"loadInferenceCount\": "
        << plan.summary.load_inference_count << ",\n";
    oss << "      \"effectiveClearAttachmentCount\": "
        << plan.summary.clear_attachment_count << ",\n";
    oss << "      \"localReadCandidateCount\": "
        << plan.summary.local_read_candidate_count << "\n";
    oss << "    },\n";
    oss << "    \"localRead\": {\n";
    oss << "      \"requested\": "
        << (plan.local_read.requested ? "true" : "false") << ",\n";
    oss << "      \"supported\": "
        << (plan.local_read.supported ? "true" : "false") << ",\n";
    oss << "      \"deviceEnabled\": "
        << (plan.local_read.device_enabled ? "true" : "false") << ",\n";
    oss << "      \"candidate\": "
        << (plan.local_read.candidate ? "true" : "false") << ",\n";
    oss << "      \"status\": \""
        << NativePassLocalReadStatusName(plan.local_read.status) << "\",\n";
    oss << "      \"reason\": \""
        << NativePassLocalReadReasonName(plan.local_read.reason) << "\",\n";
    oss << "      \"detail\": \""
        << EscapeJsonString(plan.local_read.detail) << "\"\n";
    oss << "    },\n";
    oss << "    \"groupIndexByPassOrder\": [";
    for (std::size_t pass_order = 0U;
         pass_order < plan.group_index_by_pass_order.size();
         ++pass_order) {
        if (pass_order != 0U) {
            oss << ", ";
        }
        oss << plan.group_index_by_pass_order[pass_order];
    }
    oss << "],\n";

    oss << "    \"groups\": [\n";
    for (std::size_t group_index = 0U;
         group_index < plan.groups.size();
         ++group_index) {
        const auto& group_ = plan.groups[group_index];
        oss << "      {\n";
        oss << "        \"groupIndex\": " << group_.group_index << ",\n";
        oss << "        \"queue\": \"" << QueueClassToString(group_.queue)
            << "\",\n";
        oss << "        \"firstPassOrder\": " << group_.first_pass_order << ",\n";
        oss << "        \"lastPassOrder\": " << group_.last_pass_order << ",\n";
        oss << "        \"passes\": [";
        for (std::size_t pass_index = 0U;
             pass_index < group_.logical_passes.size();
             ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            oss << group_.logical_passes[pass_index].index;
        }
        oss << "],\n";
        oss << "        \"passNames\": [";
        for (std::size_t pass_index = 0U;
             pass_index < group_.logical_passes.size();
             ++pass_index) {
            if (pass_index != 0U) {
                oss << ", ";
            }
            const auto pass_order =
                group_.first_pass_order + static_cast<std::uint32_t>(pass_index);
            oss << "\""
                << EscapeJsonString(
                       ResolvePassName(compiled_graph_, pass_order))
                << "\"";
        }
        oss << "],\n";
        oss << "        \"attachments\": ";
        AppendGroupAttachmentsJson(oss, compiled_graph_, group_.attachments);
        oss << '\n';
        oss << "      }";
        if (group_index + 1U != plan.groups.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ],\n";

    oss << "    \"boundaries\": [\n";
    for (std::size_t boundary_index = 0U;
         boundary_index < plan.boundaries.size();
         ++boundary_index) {
        const auto& boundary_ = plan.boundaries[boundary_index];
        oss << "      {\n";
        oss << "        \"previousPassOrder\": "
            << boundary_.previous_pass_order << ",\n";
        oss << "        \"previousPassIndex\": "
            << boundary_.previous_pass.index << ",\n";
        oss << "        \"previousPassName\": \""
            << EscapeJsonString(
                   ResolvePassName(compiled_graph_, boundary_.previous_pass_order))
            << "\",\n";
        oss << "        \"nextPassOrder\": "
            << boundary_.next_pass_order << ",\n";
        oss << "        \"nextPassIndex\": " << boundary_.next_pass.index
            << ",\n";
        oss << "        \"nextPassName\": \""
            << EscapeJsonString(
                   ResolvePassName(compiled_graph_, boundary_.next_pass_order))
            << "\",\n";
        oss << "        \"fused\": "
            << (boundary_.fused ? "true" : "false") << ",\n";
        oss << "        \"reason\": \""
            << NativePassFusionBlockReasonName(boundary_.block_reason)
            << "\",\n";
        oss << "        \"localRead\": {\n";
        oss << "          \"candidate\": "
            << (boundary_.local_read.candidate ? "true" : "false") << ",\n";
        oss << "          \"requested\": "
            << (boundary_.local_read.requested ? "true" : "false") << ",\n";
        oss << "          \"supported\": "
            << (boundary_.local_read.supported ? "true" : "false") << ",\n";
        oss << "          \"deviceEnabled\": "
            << (boundary_.local_read.device_enabled ? "true" : "false")
            << ",\n";
        oss << "          \"status\": \""
            << NativePassLocalReadStatusName(boundary_.local_read.status)
            << "\",\n";
        oss << "          \"reason\": \""
            << NativePassLocalReadReasonName(boundary_.local_read.reason)
            << "\",\n";
        oss << "          \"detail\": \""
            << EscapeJsonString(boundary_.local_read.detail) << "\"\n";
        oss << "        },\n";
        oss << "        \"detail\": \""
            << EscapeJsonString(boundary_.detail) << "\"\n";
        oss << "      }";
        if (boundary_index + 1U != plan.boundaries.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "    ]\n";
    oss << "  }";
    return oss.str();
}

} // namespace vr::render_graph
