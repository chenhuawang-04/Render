#pragma once

#include "vr/render_graph/compiled_render_graph.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace vr::render_graph::native_pass_detail {

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

[[nodiscard]] const char* QueueClassToString(QueueClass queue_) noexcept;
[[nodiscard]] const char* AccessKindToString(AccessKind access_) noexcept;
[[nodiscard]] const char* AttachmentLoadOpToString(AttachmentLoadOp load_op_) noexcept;
[[nodiscard]] const char* AttachmentStoreOpToString(AttachmentStoreOp store_op_) noexcept;
[[nodiscard]] bool TextureFormatHasStencil(TextureFormat format_) noexcept;
[[nodiscard]] bool ResourceHasStencilAspect(const CompiledRenderGraph& compiled_graph_,
                                            ResourceHandle handle_);
[[nodiscard]] NativePassLocalReadDecision BuildLocalReadDecision(
    const NativePassPlannerConfig& planner_config_,
    bool candidate_,
    std::string detail_);
void FinalizePlanLocalReadSummary(NativePassPlan& plan_);
[[nodiscard]] bool IsHostAccess(AccessKind access_) noexcept;
[[nodiscard]] bool IsWriteAccess(AccessKind access_) noexcept;
[[nodiscard]] bool IsColorAttachmentAccess(AccessKind access_) noexcept;
[[nodiscard]] bool IsDepthAttachmentAccess(AccessKind access_) noexcept;
[[nodiscard]] bool IsRasterPass(const CompiledPass& pass_) noexcept;
[[nodiscard]] bool PassHasHostBoundary(const CompiledPass& pass_) noexcept;
[[nodiscard]] ResourceHandle HandleForResourceIndex(std::uint32_t resource_index_) noexcept;
[[nodiscard]] std::string ResolvePassName(const CompiledRenderGraph& compiled_graph_,
                                          std::uint32_t pass_order_);
[[nodiscard]] std::string ResolveResourceName(const CompiledRenderGraph& compiled_graph_,
                                              ResourceHandle handle_);
[[nodiscard]] std::string EscapeJsonString(const std::string& value_);
[[nodiscard]] NativePassAttachmentPlan MakeAttachmentPlan(const RasterPassDesc& raster_pass_);
[[nodiscard]] bool ResourceHasAccessBefore(const CompiledRenderGraph& compiled_graph_,
                                           std::uint32_t resource_index_,
                                           std::uint32_t pass_order_);
[[nodiscard]] bool ResourceHasAccessAfter(const CompiledRenderGraph& compiled_graph_,
                                          std::uint32_t resource_index_,
                                          std::uint32_t pass_order_);
[[nodiscard]] AttachmentLoadOp ResolveEffectiveLoadOp(
    const CompiledRenderGraph& compiled_graph_,
    ResourceHandle target_,
    AttachmentLoadOp requested_load_op_,
    std::uint32_t first_pass_order_,
    NativePassAttachmentLoadOpReason& reason_,
    bool& inferred_);
[[nodiscard]] AttachmentStoreOp ResolveEffectiveStoreOp(
    const CompiledRenderGraph& compiled_graph_,
    ResourceHandle target_,
    AttachmentStoreOp requested_store_op_,
    std::uint32_t last_pass_order_,
    NativePassAttachmentStoreOpReason& reason_,
    bool& elided_);
void PopulateAttachmentDecisions(const CompiledRenderGraph& compiled_graph_,
                                 NativePassPlan& plan_);
[[nodiscard]] std::optional<AttachmentRole> FindAttachmentRole(
    const NativePassAttachmentPlan& attachment_plan_,
    std::uint32_t resource_index_);
[[nodiscard]] bool IsAttachmentCompatibleAccess(const AttachmentRole& role_,
                                                AccessKind access_) noexcept;
void AccumulatePassState(const CompiledPass& pass_,
                         const NativePassAttachmentPlan& attachment_plan_,
                         std::vector<GroupResourceState>& states_,
                         std::vector<std::uint32_t>& touched_indices_);
[[nodiscard]] std::optional<NativePassFusionBlockReason> CheckAttachmentCompatibility(
    const CompiledRenderGraph& compiled_graph_,
    const NativePassAttachmentPlan& group_attachments_,
    const RasterPassDesc& next_raster_pass_,
    std::string& detail_);
[[nodiscard]] std::optional<NativePassFusionBlockReason>
CheckInteriorAttachmentSemanticCompatibility(
    const CompiledRenderGraph& compiled_graph_,
    const CompiledPass& current_last_pass_,
    const CompiledPass& next_pass_,
    std::string& detail_);
[[nodiscard]] ResourceDependencyCheckResult CheckResourceDependencies(
    const CompiledRenderGraph& compiled_graph_,
    const NativePassAttachmentPlan& group_attachments_,
    const CompiledPass& next_pass_,
    const std::vector<GroupResourceState>& states_);
[[nodiscard]] NativePassBoundaryDecision EvaluateBoundary(
    const CompiledRenderGraph& compiled_graph_,
    const NativePassPlannerConfig& planner_config_,
    const OpenNativePassGroup& open_group_,
    const CompiledPass& next_pass_,
    std::uint32_t next_pass_order_,
    const std::vector<GroupResourceState>& group_states_);
void AppendGroupAttachmentsJson(std::ostringstream& oss_,
                                const CompiledRenderGraph& compiled_graph_,
                                const NativePassAttachmentPlan& attachments_);

} // namespace vr::render_graph::native_pass_detail
