#pragma once

#include "vr/render_graph/render_graph_types.hpp"

#include <string>
#include <vector>

namespace vr::render_graph {

class CompiledRenderGraph;

struct LogicalBarrier final {
    ResourceVersionHandle resource{};
    ResourceKind kind = ResourceKind::buffer;
    AccessKind before = AccessKind::none;
    AccessKind after = AccessKind::none;
    QueueClass src_queue = QueueClass::graphics;
    QueueClass dst_queue = QueueClass::graphics;
    SubresourceRange subresource_range{};
    BufferRange buffer_range{};
    PassHandle src_pass{};
    PassHandle dst_pass{};
    std::uint32_t src_pass_order = invalid_render_graph_index;
    std::uint32_t dst_pass_order = invalid_render_graph_index;
    bool queue_transfer = false;
    bool host_boundary = false;
    bool aliasing = false;
    bool uav_ordering = false;
};

struct CompiledBarrierBatch final {
    PassHandle pass{};
    QueueClass queue = QueueClass::graphics;
    std::vector<LogicalBarrier> barriers{};
};

struct QueueDependencyPlan final {
    QueueClass source_queue = QueueClass::graphics;
    QueueClass target_queue = QueueClass::graphics;
    std::uint32_t source_batch_index = invalid_render_graph_index;
    std::uint32_t target_batch_index = invalid_render_graph_index;
    PassHandle source_pass{};
    PassHandle target_pass{};
    std::vector<ResourceVersionHandle> resources{};
    bool queue_transfer = false;
    bool host_boundary = false;
};

struct QueueSubmitBatch final {
    QueueClass queue = QueueClass::graphics;
    std::vector<PassHandle> passes{};
    std::vector<std::uint32_t> wait_dependency_indices{};
    std::vector<std::uint32_t> signal_dependency_indices{};
    std::vector<std::uint32_t> barrier_batch_indices{};
    bool contains_host_boundary = false;
};

struct AliasCandidate final {
    ResourceHandle first{};
    ResourceHandle second{};
    std::string first_debug_name{};
    std::string second_debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    bool same_compatibility_class = false;
    bool overlapping_liveness = false;
    bool aliasable = false;
};

struct AliasBarrierDecision final {
    ResourceHandle previous{};
    ResourceHandle next{};
    std::string previous_debug_name{};
    std::string next_debug_name{};
    bool required = false;
    bool realized = false;
};

struct BarrierPlan final {
    std::vector<CompiledBarrierBatch> barrier_batches{};
    std::vector<QueueDependencyPlan> queue_dependencies{};
    std::vector<QueueSubmitBatch> queue_batches{};
    std::vector<AliasCandidate> alias_candidates{};
    std::vector<AliasBarrierDecision> alias_barriers{};

    [[nodiscard]] std::string BuildDebugString() const;
    [[nodiscard]] std::string BuildJson() const;
};

[[nodiscard]] BarrierPlan BuildBarrierPlan(const CompiledRenderGraph& compiled_graph_);

} // namespace vr::render_graph
