#include "native_pass_plan_internal.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace vr::render_graph::native_pass_detail {
[[nodiscard]] bool TextureFormatHasStencil(
    const TextureFormat format_) noexcept {
    switch (format_) {
    case TextureFormat::unknown:
    case TextureFormat::r8g8b8a8_unorm:
    case TextureFormat::r16g16b16a16_sfloat:
    case TextureFormat::d32_sfloat:
        return false;
    }
    return false;
}

[[nodiscard]] bool ResourceHasStencilAspect(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceHandle handle_) {
    const auto* resource_ = compiled_graph_.FindResource(handle_);
    if (resource_ == nullptr || resource_->kind != ResourceKind::texture) {
        return false;
    }
    return TextureFormatHasStencil(resource_->texture.format);
}

[[nodiscard]] NativePassLocalReadDecision BuildLocalReadDecision(
    const NativePassPlannerConfig& planner_config_,
    const bool candidate_,
    std::string detail_) {
    NativePassLocalReadDecision decision{};
    decision.requested = planner_config_.request_dynamic_rendering_local_read;
    decision.supported = planner_config_.dynamic_rendering_local_read_supported;
    decision.device_enabled =
        planner_config_.dynamic_rendering_local_read_enabled;
    decision.candidate = candidate_;
    decision.detail = std::move(detail_);

    if (!candidate_) {
        decision.status = NativePassLocalReadStatus::not_applicable;
        decision.reason = NativePassLocalReadReason::none;
        return decision;
    }

    if (!decision.requested) {
        decision.status = NativePassLocalReadStatus::disabled;
        decision.reason = NativePassLocalReadReason::opt_in_not_requested;
        return decision;
    }
    if (!decision.supported) {
        decision.status = NativePassLocalReadStatus::unsupported;
        decision.reason = NativePassLocalReadReason::capability_unavailable;
        return decision;
    }
    if (!decision.device_enabled) {
        decision.status = NativePassLocalReadStatus::disabled;
        decision.reason = NativePassLocalReadReason::device_feature_not_enabled;
        return decision;
    }

    decision.status = NativePassLocalReadStatus::disabled;
    decision.reason = NativePassLocalReadReason::not_implemented_live;
    return decision;
}

void FinalizePlanLocalReadSummary(NativePassPlan& plan_) {
    plan_.local_read.requested =
        plan_.planner_config.request_dynamic_rendering_local_read;
    plan_.local_read.supported =
        plan_.planner_config.dynamic_rendering_local_read_supported;
    plan_.local_read.device_enabled =
        plan_.planner_config.dynamic_rendering_local_read_enabled;
    plan_.local_read.candidate =
        plan_.summary.local_read_candidate_count != 0U;

    if (!plan_.local_read.candidate) {
        plan_.local_read.status = NativePassLocalReadStatus::not_applicable;
        plan_.local_read.reason =
            NativePassLocalReadReason::no_candidate_boundary;
        plan_.local_read.detail =
            "no sampled attachment boundary qualified for local-read/TBR evaluation";
        return;
    }

    plan_.local_read = BuildLocalReadDecision(
        plan_.planner_config,
        true,
        "sampled attachment dependencies remain split until local-read/TBR execution is explicitly enabled");
}

[[nodiscard]] bool IsHostAccess(const AccessKind access_) noexcept {
    return access_ == AccessKind::host_read ||
           access_ == AccessKind::host_write;
}

[[nodiscard]] bool IsWriteAccess(const AccessKind access_) noexcept {
    switch (access_) {
    case AccessKind::color_attachment_write:
    case AccessKind::depth_stencil_write:
    case AccessKind::depth_stencil_read_write:
    case AccessKind::shader_storage_write:
    case AccessKind::shader_storage_read_write:
    case AccessKind::transfer_write:
    case AccessKind::present:
    case AccessKind::host_write:
        return true;
    default:
        break;
    }
    return false;
}

[[nodiscard]] bool IsColorAttachmentAccess(const AccessKind access_) noexcept {
    return access_ == AccessKind::color_attachment_read ||
           access_ == AccessKind::color_attachment_write;
}

[[nodiscard]] bool IsDepthAttachmentAccess(const AccessKind access_) noexcept {
    return access_ == AccessKind::depth_stencil_read ||
           access_ == AccessKind::depth_stencil_write ||
           access_ == AccessKind::depth_stencil_read_write;
}

template<typename FnT>
void ForEachPassAccess(const CompiledPass& pass_, FnT&& function_) {
    for (const auto& read_ : pass_.reads) {
        function_(read_);
    }
    for (const auto& write_ : pass_.writes) {
        function_(write_);
    }
}

[[nodiscard]] bool IsRasterPass(const CompiledPass& pass_) noexcept {
    return pass_.raster_pass.has_value();
}

[[nodiscard]] bool PassHasHostBoundary(const CompiledPass& pass_) noexcept {
    const auto has_host_access = [](const AccessDesc& access_) {
        return IsHostAccess(access_.access);
    };
    return std::any_of(pass_.reads.begin(), pass_.reads.end(), has_host_access) ||
           std::any_of(pass_.writes.begin(), pass_.writes.end(), has_host_access);
}

[[nodiscard]] ResourceHandle HandleForResourceIndex(
    const std::uint32_t resource_index_) noexcept {
    return ResourceHandle{
        .index = resource_index_,
        .generation = 1U,
    };
}

[[nodiscard]] std::string ResolvePassName(const CompiledRenderGraph& compiled_graph_,
                                          const std::uint32_t pass_order_) {
    if (pass_order_ >= compiled_graph_.Passes().size()) {
        return "pass?";
    }
    return compiled_graph_.Passes()[pass_order_].debug_name;
}

[[nodiscard]] std::string ResolveResourceName(const CompiledRenderGraph& compiled_graph_,
                                              const ResourceHandle handle_) {
    if (const auto* resource_ = compiled_graph_.FindResource(handle_);
        resource_ != nullptr) {
        return resource_->debug_name;
    }

    std::ostringstream oss{};
    oss << "resource#" << handle_.index;
    return oss.str();
}

[[nodiscard]] std::string EscapeJsonString(const std::string& value_) {
    std::ostringstream oss{};
    for (const char character_ : value_) {
        switch (character_) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            oss << character_;
            break;
        }
    }
    return oss.str();
}

[[nodiscard]] NativePassAttachmentPlan MakeAttachmentPlan(
    const RasterPassDesc& raster_pass_) {
    NativePassAttachmentPlan attachment_plan{};
    attachment_plan.layer_count = raster_pass_.layer_count;
    attachment_plan.color_attachments.reserve(
        raster_pass_.color_attachments.size());
    for (const auto& color_attachment_ : raster_pass_.color_attachments) {
        attachment_plan.color_attachments.push_back(
            NativePassColorAttachmentPlan{
                .target = color_attachment_.target,
            });
    }
    attachment_plan.has_depth_attachment =
        raster_pass_.has_depth_attachment;
    if (raster_pass_.has_depth_attachment) {
        attachment_plan.depth_attachment = NativePassDepthAttachmentPlan{
            .target = raster_pass_.depth_attachment.target,
            .read_only = raster_pass_.depth_attachment.read_only,
        };
    }
    return attachment_plan;
}

template<typename FnT>
void ForEachResourceAccessAfter(const CompiledRenderGraph& compiled_graph_,
                                const std::uint32_t pass_order_begin_,
                                FnT&& function_) {
    for (std::uint32_t pass_order = pass_order_begin_;
         pass_order < static_cast<std::uint32_t>(compiled_graph_.Passes().size());
         ++pass_order) {
        ForEachPassAccess(compiled_graph_.Passes()[pass_order], function_);
    }
}

[[nodiscard]] bool ResourceHasAccessBefore(
    const CompiledRenderGraph& compiled_graph_,
    const std::uint32_t resource_index_,
    const std::uint32_t pass_order_) {
    for (std::uint32_t current_pass_order = 0U;
         current_pass_order < pass_order_ &&
         current_pass_order <
             static_cast<std::uint32_t>(compiled_graph_.Passes().size());
         ++current_pass_order) {
        bool found = false;
        ForEachPassAccess(
            compiled_graph_.Passes()[current_pass_order],
            [&](const AccessDesc& access_) {
                found = found || (IsValidResourceVersionHandle(access_.resource) &&
                                  access_.resource.resource_index ==
                                      resource_index_);
            });
        if (found) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ResourceHasAccessAfter(
    const CompiledRenderGraph& compiled_graph_,
    const std::uint32_t resource_index_,
    const std::uint32_t pass_order_) {
    if (pass_order_ == invalid_render_graph_index) {
        return false;
    }

    bool found = false;
    ForEachResourceAccessAfter(
        compiled_graph_,
        pass_order_ + 1U,
        [&](const AccessDesc& access_) {
            found = found || (IsValidResourceVersionHandle(access_.resource) &&
                              access_.resource.resource_index ==
                                  resource_index_);
        });
    return found;
}

[[nodiscard]] AttachmentLoadOp ResolveEffectiveLoadOp(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceHandle target_,
    const AttachmentLoadOp requested_load_op_,
    const std::uint32_t first_pass_order_,
    NativePassAttachmentLoadOpReason& reason_,
    bool& inferred_) {
    inferred_ = false;
    switch (requested_load_op_) {
    case AttachmentLoadOp::clear:
        reason_ = NativePassAttachmentLoadOpReason::authored_clear;
        return AttachmentLoadOp::clear;
    case AttachmentLoadOp::dont_care:
        reason_ = NativePassAttachmentLoadOpReason::authored_dont_care;
        return AttachmentLoadOp::dont_care;
    case AttachmentLoadOp::load:
        break;
    }

    const auto* resource_ = compiled_graph_.FindResource(target_);
    const bool preserve_external_contents =
        resource_ != nullptr &&
        resource_->lifetime != ResourceLifetime::transient;
    if (preserve_external_contents ||
        ResourceHasAccessBefore(compiled_graph_,
                                target_.index,
                                first_pass_order_)) {
        reason_ = NativePassAttachmentLoadOpReason::preserve_previous_contents;
        return AttachmentLoadOp::load;
    }

    reason_ = NativePassAttachmentLoadOpReason::first_use_no_preserve;
    inferred_ = true;
    return AttachmentLoadOp::dont_care;
}

[[nodiscard]] AttachmentStoreOp ResolveEffectiveStoreOp(
    const CompiledRenderGraph& compiled_graph_,
    const ResourceHandle target_,
    const AttachmentStoreOp requested_store_op_,
    const std::uint32_t last_pass_order_,
    NativePassAttachmentStoreOpReason& reason_,
    bool& elided_) {
    elided_ = false;
    if (requested_store_op_ == AttachmentStoreOp::dont_care) {
        reason_ = NativePassAttachmentStoreOpReason::authored_dont_care;
        return AttachmentStoreOp::dont_care;
    }

    const auto* resource_ = compiled_graph_.FindResource(target_);
    if (resource_ == nullptr ||
        resource_->lifetime != ResourceLifetime::transient) {
        reason_ =
            NativePassAttachmentStoreOpReason::preserve_external_lifetime;
        return AttachmentStoreOp::store;
    }

    if (ResourceHasAccessAfter(compiled_graph_,
                               target_.index,
                               last_pass_order_)) {
        reason_ = NativePassAttachmentStoreOpReason::preserve_for_future_use;
        return AttachmentStoreOp::store;
    }

    reason_ = NativePassAttachmentStoreOpReason::elided_transient_last_use;
    elided_ = true;
    return AttachmentStoreOp::dont_care;
}

void PopulateAttachmentDecisions(const CompiledRenderGraph& compiled_graph_,
                                 NativePassPlan& plan_) {
    const std::uint32_t local_read_candidate_count =
        plan_.summary.local_read_candidate_count;
    plan_.summary = {};
    plan_.summary.local_read_candidate_count = local_read_candidate_count;
    for (const auto& pass_ : compiled_graph_.Passes()) {
        if (pass_.raster_pass.has_value()) {
            ++plan_.summary.logical_raster_pass_count;
        }
    }

    plan_.summary.native_pass_group_count =
        static_cast<std::uint32_t>(plan_.groups.size());
    if (plan_.summary.logical_raster_pass_count >=
        plan_.summary.native_pass_group_count) {
        plan_.summary.fused_raster_pass_count =
            plan_.summary.logical_raster_pass_count -
            plan_.summary.native_pass_group_count;
    }

    for (auto& group_ : plan_.groups) {
        if (group_.first_pass_order >= compiled_graph_.Passes().size() ||
            group_.last_pass_order >= compiled_graph_.Passes().size()) {
            continue;
        }
        const auto& first_pass = compiled_graph_.Passes()[group_.first_pass_order];
        const auto& last_pass = compiled_graph_.Passes()[group_.last_pass_order];
        if (!first_pass.raster_pass.has_value() ||
            !last_pass.raster_pass.has_value()) {
            continue;
        }

        for (std::size_t color_index = 0U;
             color_index < group_.attachments.color_attachments.size() &&
             color_index < first_pass.raster_pass->color_attachments.size() &&
             color_index < last_pass.raster_pass->color_attachments.size();
             ++color_index) {
            auto& group_attachment =
                group_.attachments.color_attachments[color_index];
            const auto& first_attachment =
                first_pass.raster_pass->color_attachments[color_index];
            const auto& last_attachment =
                last_pass.raster_pass->color_attachments[color_index];
            group_attachment.requested_load_op = first_attachment.load_op;
            group_attachment.requested_store_op = last_attachment.store_op;
            group_attachment.clear_value = first_attachment.clear_value;
            group_attachment.effective_load_op =
                ResolveEffectiveLoadOp(compiled_graph_,
                                       group_attachment.target,
                                       group_attachment.requested_load_op,
                                       group_.first_pass_order,
                                       group_attachment.load_reason,
                                       group_attachment.load_inferred);
            group_attachment.effective_store_op =
                ResolveEffectiveStoreOp(compiled_graph_,
                                        group_attachment.target,
                                        group_attachment.requested_store_op,
                                        group_.last_pass_order,
                                        group_attachment.store_reason,
                                        group_attachment.store_elided);
            plan_.summary.load_inference_count +=
                group_attachment.load_inferred ? 1U : 0U;
            plan_.summary.store_elision_count +=
                group_attachment.store_elided ? 1U : 0U;
            plan_.summary.clear_attachment_count +=
                (group_attachment.effective_load_op ==
                 AttachmentLoadOp::clear)
                    ? 1U
                    : 0U;
        }

        if (group_.attachments.has_depth_attachment &&
            first_pass.raster_pass->has_depth_attachment &&
            last_pass.raster_pass->has_depth_attachment) {
            auto& depth_attachment = group_.attachments.depth_attachment;
            const auto& first_depth = first_pass.raster_pass->depth_attachment;
            const auto& last_depth = last_pass.raster_pass->depth_attachment;
            depth_attachment.requested_load_op = first_depth.load_op;
            depth_attachment.requested_store_op = last_depth.store_op;
            depth_attachment.requested_stencil_load_op =
                first_depth.stencil_load_op;
            depth_attachment.requested_stencil_store_op =
                last_depth.stencil_store_op;
            depth_attachment.clear_value = first_depth.clear_value;
            depth_attachment.effective_load_op =
                ResolveEffectiveLoadOp(compiled_graph_,
                                       depth_attachment.target,
                                       depth_attachment.requested_load_op,
                                       group_.first_pass_order,
                                       depth_attachment.load_reason,
                                       depth_attachment.load_inferred);
            depth_attachment.effective_store_op =
                ResolveEffectiveStoreOp(compiled_graph_,
                                        depth_attachment.target,
                                        depth_attachment.requested_store_op,
                                        group_.last_pass_order,
                                        depth_attachment.store_reason,
                                        depth_attachment.store_elided);
            depth_attachment.effective_stencil_load_op =
                ResolveEffectiveLoadOp(compiled_graph_,
                                       depth_attachment.target,
                                       depth_attachment.requested_stencil_load_op,
                                       group_.first_pass_order,
                                       depth_attachment.stencil_load_reason,
                                       depth_attachment.stencil_load_inferred);
            depth_attachment.effective_stencil_store_op =
                ResolveEffectiveStoreOp(compiled_graph_,
                                        depth_attachment.target,
                                        depth_attachment.requested_stencil_store_op,
                                        group_.last_pass_order,
                                        depth_attachment.stencil_store_reason,
                                        depth_attachment.stencil_store_elided);
            plan_.summary.load_inference_count +=
                depth_attachment.load_inferred ? 1U : 0U;
            plan_.summary.store_elision_count +=
                depth_attachment.store_elided ? 1U : 0U;
            plan_.summary.clear_attachment_count +=
                (depth_attachment.effective_load_op ==
                 AttachmentLoadOp::clear)
                    ? 1U
                    : 0U;
        }
    }
}

[[nodiscard]] std::optional<AttachmentRole> FindAttachmentRole(
    const NativePassAttachmentPlan& attachment_plan_,
    const std::uint32_t resource_index_) {
    for (std::size_t color_index = 0U;
         color_index < attachment_plan_.color_attachments.size();
         ++color_index) {
        if (attachment_plan_.color_attachments[color_index].target.index ==
            resource_index_) {
            return AttachmentRole{
                .kind = AttachmentRole::Kind::color,
                .color_index = color_index,
            };
        }
    }

    if (attachment_plan_.has_depth_attachment &&
        attachment_plan_.depth_attachment.target.index == resource_index_) {
        return AttachmentRole{
            .kind = AttachmentRole::Kind::depth,
            .color_index = 0U,
        };
    }

    return std::nullopt;
}

[[nodiscard]] bool IsAttachmentCompatibleAccess(
    const AttachmentRole& role_,
    const AccessKind access_) noexcept {
    switch (role_.kind) {
    case AttachmentRole::Kind::color:
        return IsColorAttachmentAccess(access_);
    case AttachmentRole::Kind::depth:
        return IsDepthAttachmentAccess(access_);
    }
    return false;
}

void AccumulatePassState(const CompiledPass& pass_,
                         const NativePassAttachmentPlan& attachment_plan_,
                         std::vector<GroupResourceState>& states_,
                         std::vector<std::uint32_t>& touched_indices_) {
    const auto accumulate_access = [&](const AccessDesc& access_) {
        if (!IsValidResourceVersionHandle(access_.resource) ||
            access_.resource.resource_index >= states_.size()) {
            return;
        }

        auto& state = states_[access_.resource.resource_index];
        if (!state.seen) {
            state.seen = true;
            touched_indices_.push_back(access_.resource.resource_index);
        }

        const auto role_ = FindAttachmentRole(
            attachment_plan_, access_.resource.resource_index);
        const bool attachment_access =
            role_.has_value() &&
            IsAttachmentCompatibleAccess(*role_, access_.access);
        state.has_write = state.has_write || IsWriteAccess(access_.access);
        state.has_shader_sample_read =
            state.has_shader_sample_read ||
            access_.access == AccessKind::shader_sample_read;
        state.has_non_attachment_access =
            state.has_non_attachment_access || !attachment_access;
    };

    ForEachPassAccess(pass_, accumulate_access);
}

} // namespace vr::render_graph::native_pass_detail
