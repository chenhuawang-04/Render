#include "native_pass_plan_internal.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace vr::render_graph::native_pass_detail {
template<typename FnT>
void ForEachPassAccess(const CompiledPass& pass_, FnT&& function_) {
    for (const auto& read_ : pass_.reads) {
        function_(read_);
    }
    for (const auto& write_ : pass_.writes) {
        function_(write_);
    }
}

[[nodiscard]] std::optional<NativePassFusionBlockReason>
CheckAttachmentCompatibility(const CompiledRenderGraph& compiled_graph_,
                             const NativePassAttachmentPlan& group_attachments_,
                             const RasterPassDesc& next_raster_pass_,
                             std::string& detail_) {
    if (group_attachments_.color_attachments.size() !=
        next_raster_pass_.color_attachments.size()) {
        std::ostringstream oss{};
        oss << "group color attachment count="
            << group_attachments_.color_attachments.size()
            << ", next pass count="
            << next_raster_pass_.color_attachments.size();
        detail_ = oss.str();
        return NativePassFusionBlockReason::color_attachment_count_mismatch;
    }

    for (std::size_t color_index = 0U;
         color_index < group_attachments_.color_attachments.size();
         ++color_index) {
        const auto& group_attachment =
            group_attachments_.color_attachments[color_index];
        const auto& next_attachment =
            next_raster_pass_.color_attachments[color_index];
        if (group_attachment.target.index != next_attachment.target.index) {
            std::ostringstream oss{};
            oss << "color attachment[" << color_index << "] target mismatch: "
                << ResolveResourceName(compiled_graph_, group_attachment.target)
                << " vs "
                << ResolveResourceName(compiled_graph_, next_attachment.target);
            detail_ = oss.str();
            return NativePassFusionBlockReason::color_attachment_target_mismatch;
        }
    }

    if (group_attachments_.has_depth_attachment !=
        next_raster_pass_.has_depth_attachment) {
        std::ostringstream oss{};
        oss << "group has_depth_attachment="
            << (group_attachments_.has_depth_attachment ? "true" : "false")
            << ", next pass has_depth_attachment="
            << (next_raster_pass_.has_depth_attachment ? "true" : "false");
        detail_ = oss.str();
        return NativePassFusionBlockReason::
            depth_attachment_presence_mismatch;
    }

    if (group_attachments_.has_depth_attachment &&
        next_raster_pass_.has_depth_attachment) {
        if (group_attachments_.depth_attachment.target.index !=
            next_raster_pass_.depth_attachment.target.index) {
            std::ostringstream oss{};
            oss << "depth attachment target mismatch: "
                << ResolveResourceName(
                       compiled_graph_,
                       group_attachments_.depth_attachment.target)
                << " vs "
                << ResolveResourceName(
                       compiled_graph_,
                       next_raster_pass_.depth_attachment.target);
            detail_ = oss.str();
            return NativePassFusionBlockReason::depth_attachment_target_mismatch;
        }
        if (group_attachments_.depth_attachment.read_only !=
            next_raster_pass_.depth_attachment.read_only) {
            std::ostringstream oss{};
            oss << "depth attachment read_only mismatch: group="
                << (group_attachments_.depth_attachment.read_only ? "true" : "false")
                << ", next="
                << (next_raster_pass_.depth_attachment.read_only ? "true" : "false");
            detail_ = oss.str();
            return NativePassFusionBlockReason::
                depth_attachment_read_only_mismatch;
        }
    }

    if (group_attachments_.layer_count != next_raster_pass_.layer_count) {
        std::ostringstream oss{};
        oss << "layer_count mismatch: group="
            << group_attachments_.layer_count
            << ", next=" << next_raster_pass_.layer_count;
        detail_ = oss.str();
        return NativePassFusionBlockReason::layer_count_mismatch;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NativePassFusionBlockReason>
CheckInteriorAttachmentSemanticCompatibility(
    const CompiledRenderGraph& compiled_graph_,
    const CompiledPass& current_last_pass_,
    const CompiledPass& next_pass_,
    std::string& detail_) {
    if (!current_last_pass_.raster_pass.has_value() ||
        !next_pass_.raster_pass.has_value()) {
        return std::nullopt;
    }

    const auto& current_last_raster = *current_last_pass_.raster_pass;
    const auto& next_raster = *next_pass_.raster_pass;

    for (std::size_t color_index = 0U;
         color_index < current_last_raster.color_attachments.size() &&
         color_index < next_raster.color_attachments.size();
         ++color_index) {
        const auto& current_last_attachment =
            current_last_raster.color_attachments[color_index];
        const auto& next_attachment =
            next_raster.color_attachments[color_index];
        const std::string resource_name = ResolveResourceName(
            compiled_graph_,
            next_attachment.target);

        if (next_attachment.load_op != AttachmentLoadOp::load) {
            std::ostringstream oss{};
            oss << "pass '" << next_pass_.debug_name
                << "' color attachment[" << color_index << "] '"
                << resource_name << "' requests loadOp="
                << AttachmentLoadOpToString(next_attachment.load_op)
                << "; interior fused logical passes must preserve contents with load";
            detail_ = oss.str();
            return NativePassFusionBlockReason::
                interior_color_load_op_requires_split;
        }

        if (current_last_attachment.store_op != AttachmentStoreOp::store) {
            std::ostringstream oss{};
            oss << "pass '" << current_last_pass_.debug_name
                << "' color attachment[" << color_index << "] '"
                << resource_name << "' requests storeOp="
                << AttachmentStoreOpToString(current_last_attachment.store_op)
                << " before pass '" << next_pass_.debug_name
                << "'; interior fused logical passes must preserve contents with store";
            detail_ = oss.str();
            return NativePassFusionBlockReason::
                interior_color_store_op_requires_split;
        }
    }

    if (!current_last_raster.has_depth_attachment ||
        !next_raster.has_depth_attachment) {
        return std::nullopt;
    }

    const auto& current_last_depth = current_last_raster.depth_attachment;
    const auto& next_depth = next_raster.depth_attachment;
    const std::string depth_resource_name = ResolveResourceName(
        compiled_graph_,
        next_depth.target);

    if (next_depth.load_op != AttachmentLoadOp::load) {
        std::ostringstream oss{};
        oss << "pass '" << next_pass_.debug_name
            << "' depth attachment '" << depth_resource_name
            << "' requests loadOp="
            << AttachmentLoadOpToString(next_depth.load_op)
            << "; interior fused logical passes must preserve contents with load";
        detail_ = oss.str();
        return NativePassFusionBlockReason::
            interior_depth_load_op_requires_split;
    }

    if (current_last_depth.store_op != AttachmentStoreOp::store) {
        std::ostringstream oss{};
        oss << "pass '" << current_last_pass_.debug_name
            << "' depth attachment '" << depth_resource_name
            << "' requests storeOp="
            << AttachmentStoreOpToString(current_last_depth.store_op)
            << " before pass '" << next_pass_.debug_name
            << "'; interior fused logical passes must preserve contents with store";
        detail_ = oss.str();
        return NativePassFusionBlockReason::
            interior_depth_store_op_requires_split;
    }

    if (!ResourceHasStencilAspect(compiled_graph_, next_depth.target)) {
        return std::nullopt;
    }

    if (next_depth.stencil_load_op != AttachmentLoadOp::load) {
        std::ostringstream oss{};
        oss << "pass '" << next_pass_.debug_name
            << "' stencil aspect of '" << depth_resource_name
            << "' requests loadOp="
            << AttachmentLoadOpToString(next_depth.stencil_load_op)
            << "; interior fused logical passes must preserve contents with load";
        detail_ = oss.str();
        return NativePassFusionBlockReason::
            interior_stencil_load_op_requires_split;
    }

    if (current_last_depth.stencil_store_op != AttachmentStoreOp::store) {
        std::ostringstream oss{};
        oss << "pass '" << current_last_pass_.debug_name
            << "' stencil aspect of '" << depth_resource_name
            << "' requests storeOp="
            << AttachmentStoreOpToString(current_last_depth.stencil_store_op)
            << " before pass '" << next_pass_.debug_name
            << "'; interior fused logical passes must preserve contents with store";
        detail_ = oss.str();
        return NativePassFusionBlockReason::
            interior_stencil_store_op_requires_split;
    }

    return std::nullopt;
}

[[nodiscard]] ResourceDependencyCheckResult
CheckResourceDependencies(const CompiledRenderGraph& compiled_graph_,
                          const NativePassAttachmentPlan& group_attachments_,
                          const CompiledPass& next_pass_,
                          const std::vector<GroupResourceState>& states_) {
    ResourceDependencyCheckResult result{};
    const auto evaluate_access = [&](const AccessDesc& access_)
        -> std::optional<NativePassFusionBlockReason> {
        if (!IsValidResourceVersionHandle(access_.resource) ||
            access_.access == AccessKind::none ||
            access_.resource.resource_index >= states_.size()) {
            return std::nullopt;
        }

        const auto& state = states_[access_.resource.resource_index];
        if (!state.seen) {
            return std::nullopt;
        }

        const auto attachment_role = FindAttachmentRole(
            group_attachments_, access_.resource.resource_index);
        const bool attachment_access =
            attachment_role.has_value() &&
            IsAttachmentCompatibleAccess(*attachment_role, access_.access);
        const bool attachment_safe =
            attachment_access && !state.has_non_attachment_access;
        if (attachment_safe) {
            return std::nullopt;
        }

        if (!state.has_write && !IsWriteAccess(access_.access)) {
            return std::nullopt;
        }

        const ResourceHandle resource_handle =
            HandleForResourceIndex(access_.resource.resource_index);
        const std::string resource_name =
            ResolveResourceName(compiled_graph_, resource_handle);
        const bool attachment_local_read_candidate =
            attachment_role.has_value() &&
            access_.access == AccessKind::shader_sample_read &&
            state.has_write &&
            !state.has_non_attachment_access;

        if ((access_.access == AccessKind::shader_sample_read &&
             state.has_write) ||
            (state.has_shader_sample_read && IsWriteAccess(access_.access))) {
            std::ostringstream oss{};
            oss << "resource '" << resource_name
                << "' is sampled across the candidate native pass boundary";
            result.local_read_candidate =
                result.local_read_candidate || attachment_local_read_candidate;
            result.detail = oss.str();
            return NativePassFusionBlockReason::sampled_group_resource_read;
        }

        std::ostringstream oss{};
        oss << "resource '" << resource_name
            << "' has a non-attachment dependency with access="
            << AccessKindToString(access_.access)
            << " in pass '" << next_pass_.debug_name << '\'';
        result.detail = oss.str();
        return NativePassFusionBlockReason::non_attachment_resource_dependency;
    };

    for (const auto& read_ : next_pass_.reads) {
        if (const auto reason_ = evaluate_access(read_);
            reason_.has_value()) {
            result.block_reason = reason_;
            return result;
        }
    }
    for (const auto& write_ : next_pass_.writes) {
        if (const auto reason_ = evaluate_access(write_);
            reason_.has_value()) {
            result.block_reason = reason_;
            return result;
        }
    }
    return result;
}

[[nodiscard]] NativePassBoundaryDecision EvaluateBoundary(
    const CompiledRenderGraph& compiled_graph_,
    const NativePassPlannerConfig& planner_config_,
    const OpenNativePassGroup& open_group_,
    const CompiledPass& next_pass_,
    const std::uint32_t next_pass_order_,
    const std::vector<GroupResourceState>& group_states_) {
    NativePassBoundaryDecision boundary{};
    boundary.previous_pass_order = open_group_.group.last_pass_order;
    boundary.previous_pass =
        open_group_.group.logical_passes.empty()
            ? invalid_pass_handle
            : open_group_.group.logical_passes.back();
    boundary.next_pass_order = next_pass_order_;
    boundary.next_pass = next_pass_.handle;

    if (!next_pass_.raster_pass.has_value()) {
        boundary.block_reason =
            NativePassFusionBlockReason::next_pass_not_raster;
        boundary.detail =
            "next logical pass does not declare raster attachments";
        return boundary;
    }

    if (open_group_.group.queue != next_pass_.queue) {
        std::ostringstream oss{};
        oss << "group queue=" << QueueClassToString(open_group_.group.queue)
            << ", next pass queue=" << QueueClassToString(next_pass_.queue);
        boundary.block_reason =
            NativePassFusionBlockReason::queue_mismatch;
        boundary.detail = oss.str();
        return boundary;
    }

    if (open_group_.has_force_split ||
        next_pass_.compile_hints.force_native_pass_split) {
        const std::string split_pass_name =
            open_group_.has_force_split
                ? ResolvePassName(
                      compiled_graph_,
                      open_group_.group.last_pass_order)
                : next_pass_.debug_name;
        boundary.block_reason =
            NativePassFusionBlockReason::force_split_hint;
        boundary.detail =
            "force_native_pass_split requested by pass '" +
            split_pass_name + '\'';
        return boundary;
    }

    if (open_group_.has_side_effect || next_pass_.side_effect) {
        const std::string side_effect_pass_name =
            open_group_.has_side_effect
                ? ResolvePassName(
                      compiled_graph_,
                      open_group_.group.last_pass_order)
                : next_pass_.debug_name;
        boundary.block_reason = NativePassFusionBlockReason::side_effect;
        boundary.detail =
            "side_effect pass '" + side_effect_pass_name +
            "' must remain a native pass boundary";
        return boundary;
    }

    if (open_group_.has_host_boundary || PassHasHostBoundary(next_pass_)) {
        const std::string host_pass_name =
            open_group_.has_host_boundary
                ? ResolvePassName(
                      compiled_graph_,
                      open_group_.group.last_pass_order)
                : next_pass_.debug_name;
        boundary.block_reason = NativePassFusionBlockReason::host_boundary;
        boundary.detail =
            "host access detected in pass '" + host_pass_name + '\'';
        return boundary;
    }

    std::string detail{};
    if (const auto reason_ = CheckAttachmentCompatibility(
            compiled_graph_,
            open_group_.group.attachments,
            *next_pass_.raster_pass,
            detail);
        reason_.has_value()) {
        boundary.block_reason = *reason_;
        boundary.detail = std::move(detail);
        return boundary;
    }

    const auto& current_last_pass =
        compiled_graph_.Passes()[open_group_.group.last_pass_order];
    if (const auto reason_ = CheckInteriorAttachmentSemanticCompatibility(
            compiled_graph_,
            current_last_pass,
            next_pass_,
            detail);
        reason_.has_value()) {
        boundary.block_reason = *reason_;
        boundary.detail = std::move(detail);
        return boundary;
    }

    const auto dependency_check = CheckResourceDependencies(
        compiled_graph_,
        open_group_.group.attachments,
        next_pass_,
        group_states_);
    if (dependency_check.block_reason.has_value()) {
        boundary.block_reason = *dependency_check.block_reason;
        boundary.detail = dependency_check.detail;
        boundary.local_read = BuildLocalReadDecision(
            planner_config_,
            dependency_check.local_read_candidate,
            dependency_check.local_read_candidate
                ? dependency_check.detail
                : std::string{});
        return boundary;
    }

    boundary.fused = true;
    boundary.block_reason = NativePassFusionBlockReason::none;
    boundary.detail =
        "compatible attachments and no blocking inter-pass dependency";
    return boundary;
}

} // namespace vr::render_graph::native_pass_detail

namespace vr::render_graph {
using namespace native_pass_detail;

NativePassPlan BuildNativePassPlan(
    const CompiledRenderGraph& compiled_graph_,
    const NativePassPlannerConfig& planner_config_) {
    NativePassPlan plan{};
    plan.planner_config = planner_config_;
    plan.group_index_by_pass_order.assign(
        compiled_graph_.Passes().size(),
        invalid_render_graph_index);
    if (compiled_graph_.Passes().empty()) {
        return plan;
    }

    std::uint32_t max_resource_index = 0U;
    for (const auto& resource_ : compiled_graph_.Resources()) {
        max_resource_index =
            (std::max)(max_resource_index, resource_.handle.index);
    }
    for (const auto& pass_ : compiled_graph_.Passes()) {
        if (pass_.raster_pass.has_value()) {
            for (const auto& color_attachment_ :
                 pass_.raster_pass->color_attachments) {
                max_resource_index =
                    (std::max)(max_resource_index, color_attachment_.target.index);
            }
            if (pass_.raster_pass->has_depth_attachment) {
                max_resource_index =
                    (std::max)(max_resource_index,
                               pass_.raster_pass->depth_attachment.target.index);
            }
        }
        ForEachPassAccess(pass_, [&](const AccessDesc& access_) {
            if (!IsValidResourceVersionHandle(access_.resource)) {
                return;
            }
            max_resource_index =
                (std::max)(max_resource_index, access_.resource.resource_index);
        });
    }

    std::vector<GroupResourceState> group_states(
        static_cast<std::size_t>(max_resource_index) + 1U);
    std::vector<std::uint32_t> touched_state_indices{};
    touched_state_indices.reserve(16U);

    const auto clear_group_states = [&]() {
        for (const auto resource_index : touched_state_indices) {
            group_states[resource_index] = {};
        }
        touched_state_indices.clear();
    };

    std::optional<OpenNativePassGroup> open_group{};
    const auto start_group = [&](const std::uint32_t pass_order_,
                                 const CompiledPass& pass_) {
        clear_group_states();
        OpenNativePassGroup group{};
        group.group.queue = pass_.queue;
        group.group.first_pass_order = pass_order_;
        group.group.last_pass_order = pass_order_;
        group.group.logical_passes.push_back(pass_.handle);
        group.group.attachments = MakeAttachmentPlan(*pass_.raster_pass);
        group.has_side_effect = pass_.side_effect;
        group.has_host_boundary = PassHasHostBoundary(pass_);
        group.has_force_split = pass_.compile_hints.force_native_pass_split;
        AccumulatePassState(pass_,
                            group.group.attachments,
                            group_states,
                            touched_state_indices);
        open_group = std::move(group);
    };

    const auto finalize_group = [&]() {
        if (!open_group.has_value()) {
            return;
        }
        NativePassGroup completed_group = std::move(open_group->group);
        completed_group.group_index =
            static_cast<std::uint32_t>(plan.groups.size());
        for (std::uint32_t pass_order = completed_group.first_pass_order;
             pass_order <= completed_group.last_pass_order;
             ++pass_order) {
            plan.group_index_by_pass_order[pass_order] =
                completed_group.group_index;
        }
        plan.groups.push_back(std::move(completed_group));
        open_group.reset();
        clear_group_states();
    };

    for (std::uint32_t pass_order = 0U;
         pass_order < static_cast<std::uint32_t>(compiled_graph_.Passes().size());
         ++pass_order) {
        const auto& pass_ = compiled_graph_.Passes()[pass_order];

        if (pass_order == 0U) {
            if (IsRasterPass(pass_)) {
                start_group(pass_order, pass_);
            }
            continue;
        }

        const auto& previous_pass =
            compiled_graph_.Passes()[pass_order - 1U];
        const bool record_boundary =
            IsRasterPass(previous_pass) || IsRasterPass(pass_);
        NativePassBoundaryDecision boundary{};
        bool evaluated_boundary = false;
        if (record_boundary) {
            evaluated_boundary = true;
            if (open_group.has_value() &&
                open_group->group.last_pass_order == pass_order - 1U) {
                boundary = EvaluateBoundary(compiled_graph_,
                                            planner_config_,
                                            *open_group,
                                            pass_,
                                            pass_order,
                                            group_states);
            } else {
                boundary.previous_pass_order = pass_order - 1U;
                boundary.previous_pass = previous_pass.handle;
                boundary.next_pass_order = pass_order;
                boundary.next_pass = pass_.handle;
                boundary.fused = false;
                boundary.block_reason =
                    NativePassFusionBlockReason::previous_pass_not_raster;
                boundary.detail =
                    "previous logical pass does not declare raster attachments";
            }
            if (boundary.local_read.candidate) {
                ++plan.summary.local_read_candidate_count;
            }
            plan.boundaries.push_back(boundary);
        }

        if (evaluated_boundary && boundary.fused) {
            open_group->group.last_pass_order = pass_order;
            open_group->group.logical_passes.push_back(pass_.handle);
            AccumulatePassState(pass_,
                                open_group->group.attachments,
                                group_states,
                                touched_state_indices);
            open_group->has_side_effect =
                open_group->has_side_effect || pass_.side_effect;
            open_group->has_host_boundary =
                open_group->has_host_boundary || PassHasHostBoundary(pass_);
            open_group->has_force_split =
                open_group->has_force_split ||
                pass_.compile_hints.force_native_pass_split;
            continue;
        }

        if (open_group.has_value() &&
            open_group->group.last_pass_order == pass_order - 1U) {
            finalize_group();
        }

        if (IsRasterPass(pass_)) {
            start_group(pass_order, pass_);
        }
    }

    finalize_group();
    PopulateAttachmentDecisions(compiled_graph_, plan);
    FinalizePlanLocalReadSummary(plan);
    return plan;
}

} // namespace vr::render_graph
