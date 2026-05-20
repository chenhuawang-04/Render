#include "vr/render_graph/native_pass_plan.hpp"

#include "vr/render_graph/compiled_render_graph.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace vr::render_graph {
namespace {

struct AttachmentRole final {
    enum class Kind : std::uint8_t {
        color = 0U,
        depth = 1U,
    };

    Kind kind = Kind::color;
    std::size_t color_index = 0U;
};

struct GroupResourceState final {
    bool seen = false;
    bool has_write = false;
    bool has_shader_sample_read = false;
    bool has_non_attachment_access = false;
};

struct ResourceDependencyCheckResult final {
    std::optional<NativePassFusionBlockReason> block_reason{};
    bool local_read_candidate = false;
    std::string detail{};
};

struct OpenNativePassGroup final {
    NativePassGroup group{};
    bool has_side_effect = false;
    bool has_host_boundary = false;
    bool has_force_split = false;
};

[[nodiscard]] const char* QueueClassToString(const QueueClass queue_) noexcept {
    switch (queue_) {
    case QueueClass::graphics:
        return "graphics";
    case QueueClass::compute:
        return "compute";
    case QueueClass::transfer:
        return "transfer";
    }
    return "unknown";
}

[[nodiscard]] const char* AccessKindToString(const AccessKind access_) noexcept {
    switch (access_) {
    case AccessKind::none:
        return "none";
    case AccessKind::color_attachment_read:
        return "color_attachment_read";
    case AccessKind::color_attachment_write:
        return "color_attachment_write";
    case AccessKind::depth_stencil_read:
        return "depth_stencil_read";
    case AccessKind::depth_stencil_write:
        return "depth_stencil_write";
    case AccessKind::depth_stencil_read_write:
        return "depth_stencil_read_write";
    case AccessKind::shader_sample_read:
        return "shader_sample_read";
    case AccessKind::shader_storage_read:
        return "shader_storage_read";
    case AccessKind::shader_storage_write:
        return "shader_storage_write";
    case AccessKind::shader_storage_read_write:
        return "shader_storage_read_write";
    case AccessKind::uniform_read:
        return "uniform_read";
    case AccessKind::vertex_buffer_read:
        return "vertex_buffer_read";
    case AccessKind::index_buffer_read:
        return "index_buffer_read";
    case AccessKind::indirect_command_read:
        return "indirect_command_read";
    case AccessKind::transfer_read:
        return "transfer_read";
    case AccessKind::transfer_write:
        return "transfer_write";
    case AccessKind::present:
        return "present";
    case AccessKind::host_read:
        return "host_read";
    case AccessKind::host_write:
        return "host_write";
    }
    return "unknown";
}

[[nodiscard]] const char* AttachmentLoadOpToString(
    const AttachmentLoadOp load_op_) noexcept {
    switch (load_op_) {
    case AttachmentLoadOp::load:
        return "load";
    case AttachmentLoadOp::clear:
        return "clear";
    case AttachmentLoadOp::dont_care:
        return "dont_care";
    }
    return "unknown";
}

[[nodiscard]] const char* AttachmentStoreOpToString(
    const AttachmentStoreOp store_op_) noexcept {
    switch (store_op_) {
    case AttachmentStoreOp::store:
        return "store";
    case AttachmentStoreOp::dont_care:
        return "dont_care";
    }
    return "unknown";
}

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

} // namespace

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
