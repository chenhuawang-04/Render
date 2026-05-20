#pragma once

#include "vr/render_graph/render_graph_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vr::render_graph {

class CompiledRenderGraph;

struct PassCompileHints final {
    bool force_native_pass_split = false;
};

struct NativePassPlannerConfig final {
    bool request_dynamic_rendering_local_read = false;
    bool dynamic_rendering_local_read_supported = false;
    bool dynamic_rendering_local_read_enabled = false;
};

enum class NativePassFusionBlockReason : std::uint8_t {
    none = 0U,
    previous_pass_not_raster = 1U,
    next_pass_not_raster = 2U,
    queue_mismatch = 3U,
    force_split_hint = 4U,
    side_effect = 5U,
    host_boundary = 6U,
    color_attachment_count_mismatch = 7U,
    color_attachment_target_mismatch = 8U,
    depth_attachment_presence_mismatch = 9U,
    depth_attachment_target_mismatch = 10U,
    depth_attachment_read_only_mismatch = 11U,
    layer_count_mismatch = 12U,
    sampled_group_resource_read = 13U,
    non_attachment_resource_dependency = 14U,
    interior_color_load_op_requires_split = 15U,
    interior_color_store_op_requires_split = 16U,
    interior_depth_load_op_requires_split = 17U,
    interior_depth_store_op_requires_split = 18U,
    interior_stencil_load_op_requires_split = 19U,
    interior_stencil_store_op_requires_split = 20U,
};

[[nodiscard]] constexpr std::string_view NativePassFusionBlockReasonName(
    const NativePassFusionBlockReason reason_) noexcept {
    switch (reason_) {
    case NativePassFusionBlockReason::none:
        return "none";
    case NativePassFusionBlockReason::previous_pass_not_raster:
        return "previous_pass_not_raster";
    case NativePassFusionBlockReason::next_pass_not_raster:
        return "next_pass_not_raster";
    case NativePassFusionBlockReason::queue_mismatch:
        return "queue_mismatch";
    case NativePassFusionBlockReason::force_split_hint:
        return "force_split_hint";
    case NativePassFusionBlockReason::side_effect:
        return "side_effect";
    case NativePassFusionBlockReason::host_boundary:
        return "host_boundary";
    case NativePassFusionBlockReason::color_attachment_count_mismatch:
        return "color_attachment_count_mismatch";
    case NativePassFusionBlockReason::color_attachment_target_mismatch:
        return "color_attachment_target_mismatch";
    case NativePassFusionBlockReason::depth_attachment_presence_mismatch:
        return "depth_attachment_presence_mismatch";
    case NativePassFusionBlockReason::depth_attachment_target_mismatch:
        return "depth_attachment_target_mismatch";
    case NativePassFusionBlockReason::depth_attachment_read_only_mismatch:
        return "depth_attachment_read_only_mismatch";
    case NativePassFusionBlockReason::layer_count_mismatch:
        return "layer_count_mismatch";
    case NativePassFusionBlockReason::sampled_group_resource_read:
        return "sampled_group_resource_read";
    case NativePassFusionBlockReason::non_attachment_resource_dependency:
        return "non_attachment_resource_dependency";
    case NativePassFusionBlockReason::interior_color_load_op_requires_split:
        return "interior_color_load_op_requires_split";
    case NativePassFusionBlockReason::interior_color_store_op_requires_split:
        return "interior_color_store_op_requires_split";
    case NativePassFusionBlockReason::interior_depth_load_op_requires_split:
        return "interior_depth_load_op_requires_split";
    case NativePassFusionBlockReason::interior_depth_store_op_requires_split:
        return "interior_depth_store_op_requires_split";
    case NativePassFusionBlockReason::interior_stencil_load_op_requires_split:
        return "interior_stencil_load_op_requires_split";
    case NativePassFusionBlockReason::interior_stencil_store_op_requires_split:
        return "interior_stencil_store_op_requires_split";
    }
    return "unknown";
}

enum class NativePassAttachmentLoadOpReason : std::uint8_t {
    preserve_previous_contents = 0U,
    authored_clear = 1U,
    authored_dont_care = 2U,
    first_use_no_preserve = 3U,
};

[[nodiscard]] constexpr std::string_view NativePassAttachmentLoadOpReasonName(
    const NativePassAttachmentLoadOpReason reason_) noexcept {
    switch (reason_) {
    case NativePassAttachmentLoadOpReason::preserve_previous_contents:
        return "preserve_previous_contents";
    case NativePassAttachmentLoadOpReason::authored_clear:
        return "authored_clear";
    case NativePassAttachmentLoadOpReason::authored_dont_care:
        return "authored_dont_care";
    case NativePassAttachmentLoadOpReason::first_use_no_preserve:
        return "first_use_no_preserve";
    }
    return "unknown";
}

enum class NativePassAttachmentStoreOpReason : std::uint8_t {
    preserve_for_future_use = 0U,
    preserve_external_lifetime = 1U,
    authored_dont_care = 2U,
    elided_transient_last_use = 3U,
};

[[nodiscard]] constexpr std::string_view NativePassAttachmentStoreOpReasonName(
    const NativePassAttachmentStoreOpReason reason_) noexcept {
    switch (reason_) {
    case NativePassAttachmentStoreOpReason::preserve_for_future_use:
        return "preserve_for_future_use";
    case NativePassAttachmentStoreOpReason::preserve_external_lifetime:
        return "preserve_external_lifetime";
    case NativePassAttachmentStoreOpReason::authored_dont_care:
        return "authored_dont_care";
    case NativePassAttachmentStoreOpReason::elided_transient_last_use:
        return "elided_transient_last_use";
    }
    return "unknown";
}

enum class NativePassLocalReadStatus : std::uint8_t {
    not_applicable = 0U,
    disabled = 1U,
    unsupported = 2U,
};

[[nodiscard]] constexpr std::string_view NativePassLocalReadStatusName(
    const NativePassLocalReadStatus status_) noexcept {
    switch (status_) {
    case NativePassLocalReadStatus::not_applicable:
        return "not_applicable";
    case NativePassLocalReadStatus::disabled:
        return "disabled";
    case NativePassLocalReadStatus::unsupported:
        return "unsupported";
    }
    return "unknown";
}

enum class NativePassLocalReadReason : std::uint8_t {
    none = 0U,
    no_candidate_boundary = 1U,
    opt_in_not_requested = 2U,
    capability_unavailable = 3U,
    device_feature_not_enabled = 4U,
    not_implemented_live = 5U,
};

[[nodiscard]] constexpr std::string_view NativePassLocalReadReasonName(
    const NativePassLocalReadReason reason_) noexcept {
    switch (reason_) {
    case NativePassLocalReadReason::none:
        return "none";
    case NativePassLocalReadReason::no_candidate_boundary:
        return "no_candidate_boundary";
    case NativePassLocalReadReason::opt_in_not_requested:
        return "opt_in_not_requested";
    case NativePassLocalReadReason::capability_unavailable:
        return "capability_unavailable";
    case NativePassLocalReadReason::device_feature_not_enabled:
        return "device_feature_not_enabled";
    case NativePassLocalReadReason::not_implemented_live:
        return "not_implemented_live";
    }
    return "unknown";
}

struct NativePassLocalReadDecision final {
    bool requested = false;
    bool supported = false;
    bool device_enabled = false;
    bool candidate = false;
    NativePassLocalReadStatus status =
        NativePassLocalReadStatus::not_applicable;
    NativePassLocalReadReason reason = NativePassLocalReadReason::none;
    std::string detail{};
};

struct NativePassColorAttachmentPlan final {
    ResourceHandle target{};
    AttachmentLoadOp requested_load_op = AttachmentLoadOp::load;
    AttachmentStoreOp requested_store_op = AttachmentStoreOp::store;
    AttachmentLoadOp effective_load_op = AttachmentLoadOp::load;
    AttachmentStoreOp effective_store_op = AttachmentStoreOp::store;
    ClearColorValue clear_value{};
    NativePassAttachmentLoadOpReason load_reason =
        NativePassAttachmentLoadOpReason::preserve_previous_contents;
    NativePassAttachmentStoreOpReason store_reason =
        NativePassAttachmentStoreOpReason::preserve_for_future_use;
    bool load_inferred = false;
    bool store_elided = false;
};

struct NativePassDepthAttachmentPlan final {
    ResourceHandle target{};
    bool read_only = false;
    AttachmentLoadOp requested_load_op = AttachmentLoadOp::load;
    AttachmentStoreOp requested_store_op = AttachmentStoreOp::store;
    AttachmentLoadOp effective_load_op = AttachmentLoadOp::load;
    AttachmentStoreOp effective_store_op = AttachmentStoreOp::store;
    AttachmentLoadOp requested_stencil_load_op = AttachmentLoadOp::dont_care;
    AttachmentStoreOp requested_stencil_store_op = AttachmentStoreOp::dont_care;
    AttachmentLoadOp effective_stencil_load_op = AttachmentLoadOp::dont_care;
    AttachmentStoreOp effective_stencil_store_op = AttachmentStoreOp::dont_care;
    ClearDepthStencilValue clear_value{};
    NativePassAttachmentLoadOpReason load_reason =
        NativePassAttachmentLoadOpReason::preserve_previous_contents;
    NativePassAttachmentStoreOpReason store_reason =
        NativePassAttachmentStoreOpReason::preserve_for_future_use;
    NativePassAttachmentLoadOpReason stencil_load_reason =
        NativePassAttachmentLoadOpReason::authored_dont_care;
    NativePassAttachmentStoreOpReason stencil_store_reason =
        NativePassAttachmentStoreOpReason::authored_dont_care;
    bool load_inferred = false;
    bool store_elided = false;
    bool stencil_load_inferred = false;
    bool stencil_store_elided = false;
};

struct NativePassAttachmentPlan final {
    std::vector<NativePassColorAttachmentPlan> color_attachments{};
    bool has_depth_attachment = false;
    NativePassDepthAttachmentPlan depth_attachment{};
    std::uint32_t layer_count = 1U;
};

struct NativePassGroup final {
    std::uint32_t group_index = invalid_render_graph_index;
    QueueClass queue = QueueClass::graphics;
    std::uint32_t first_pass_order = invalid_render_graph_index;
    std::uint32_t last_pass_order = invalid_render_graph_index;
    NativePassAttachmentPlan attachments{};
    std::vector<PassHandle> logical_passes{};
};

struct NativePassBoundaryDecision final {
    std::uint32_t previous_pass_order = invalid_render_graph_index;
    PassHandle previous_pass{};
    std::uint32_t next_pass_order = invalid_render_graph_index;
    PassHandle next_pass{};
    bool fused = false;
    NativePassFusionBlockReason block_reason =
        NativePassFusionBlockReason::none;
    NativePassLocalReadDecision local_read{};
    std::string detail{};
};

struct NativePassPlanSummary final {
    std::uint32_t logical_raster_pass_count = 0U;
    std::uint32_t native_pass_group_count = 0U;
    std::uint32_t fused_raster_pass_count = 0U;
    std::uint32_t store_elision_count = 0U;
    std::uint32_t load_inference_count = 0U;
    std::uint32_t clear_attachment_count = 0U;
    std::uint32_t local_read_candidate_count = 0U;
};

struct NativePassPlan final {
    NativePassPlannerConfig planner_config{};
    NativePassLocalReadDecision local_read{};
    std::vector<NativePassGroup> groups{};
    std::vector<NativePassBoundaryDecision> boundaries{};
    std::vector<std::uint32_t> group_index_by_pass_order{};
    NativePassPlanSummary summary{};

    [[nodiscard]] bool Empty() const noexcept {
        return groups.empty();
    }

    [[nodiscard]] std::uint32_t GroupIndexForPassOrder(
        const std::uint32_t pass_order_) const noexcept {
        if (pass_order_ >= group_index_by_pass_order.size()) {
            return invalid_render_graph_index;
        }
        return group_index_by_pass_order[pass_order_];
    }

    [[nodiscard]] const NativePassGroup* FindGroupByPassOrder(
        const std::uint32_t pass_order_) const noexcept {
        const std::uint32_t group_index =
            GroupIndexForPassOrder(pass_order_);
        if (group_index == invalid_render_graph_index ||
            group_index >= groups.size()) {
            return nullptr;
        }
        return &groups[group_index];
    }
};

[[nodiscard]] NativePassPlan BuildNativePassPlan(
    const CompiledRenderGraph& compiled_graph_,
    const NativePassPlannerConfig& planner_config_);
[[nodiscard]] std::string BuildNativePassPlanDebugString(
    const CompiledRenderGraph& compiled_graph_);
[[nodiscard]] std::string BuildNativePassPlanJson(
    const CompiledRenderGraph& compiled_graph_);

} // namespace vr::render_graph
