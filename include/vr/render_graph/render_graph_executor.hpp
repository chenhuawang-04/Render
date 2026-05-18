#pragma once

#include "vr/render_graph/graph_command_context.hpp"

#include <cstdint>

namespace vr::render_graph {

struct RenderGraphRecordStats final {
    std::uint32_t pass_count = 0U;
    std::uint32_t command_batch_count = 0U;
    std::uint32_t queue_transfer_batch_count = 0U;
    std::uint32_t memory_barrier_count = 0U;
    std::uint32_t buffer_barrier_count = 0U;
    std::uint32_t image_barrier_count = 0U;
};

class RenderGraphExecutor final {
public:
    [[nodiscard]] static RenderGraphRecordStats Record(GraphCommandContext& context_);
};

} // namespace vr::render_graph
