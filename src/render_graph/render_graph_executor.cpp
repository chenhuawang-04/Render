#include "vr/render_graph/render_graph_executor.hpp"

#ifdef FindResource
#undef FindResource
#endif

#include "vr/render_graph/compiled_render_graph.hpp"

#include <stdexcept>

namespace vr::render_graph {

RenderGraphRecordStats RenderGraphExecutor::Record(const GraphCommandContext& context_) {
    RenderGraphRecordStats stats{};

    const VkCommandBuffer command_buffer = context_.CommandBuffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return stats;
    }

    const auto& command_ready = context_.CommandReadyBarriers();
    if (!command_ready.queue_transfer_batches.empty()) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record currently supports only single-queue command batches");
    }

    std::vector<const VulkanCommandBarrierBatch*> command_batches_by_pass(
        context_.Graph().Passes().size(),
        nullptr);
    for (const auto& batch_ : command_ready.command_batches) {
        if (batch_.pass.index < command_batches_by_pass.size()) {
            command_batches_by_pass[batch_.pass.index] = &batch_;
        }
    }

    for (const auto pass_handle_ : context_.Graph().ExecutionOrder()) {
        const auto* pass_ = context_.Graph().FindPass(pass_handle_);
        if (pass_ == nullptr) {
            continue;
        }

        const auto* command_batch = (pass_->handle.index < command_batches_by_pass.size())
            ? command_batches_by_pass[pass_->handle.index]
            : nullptr;

        if (command_batch != nullptr) {
            const VkDependencyInfo dependency_info = command_batch->dependency.BuildVkDependencyInfo();
            if (dependency_info.memoryBarrierCount != 0U ||
                dependency_info.bufferMemoryBarrierCount != 0U ||
                dependency_info.imageMemoryBarrierCount != 0U) {
                vkCmdPipelineBarrier2(command_buffer, &dependency_info);
                stats.command_batch_count += 1U;
                stats.memory_barrier_count += static_cast<std::uint32_t>(command_batch->dependency.memory_barriers.size());
                stats.buffer_barrier_count += static_cast<std::uint32_t>(command_batch->dependency.buffer_barriers.size());
                stats.image_barrier_count += static_cast<std::uint32_t>(command_batch->dependency.image_barriers.size());
            }
        }

        const bool has_raster_pass = pass_->raster_pass.has_value();
        if (!has_raster_pass && !pass_->execute) {
            continue;
        }

        if (has_raster_pass) {
            const auto rendering_info = context_.BuildRenderingInfo(*pass_->raster_pass);
            context_.BeginRendering(rendering_info);
        }

        if (pass_->execute) {
            pass_->execute(const_cast<GraphCommandContext&>(context_));
        }

        if (has_raster_pass) {
            context_.EndRendering();
        }
        stats.pass_count += 1U;
    }

    stats.queue_transfer_batch_count = static_cast<std::uint32_t>(command_ready.queue_transfer_batches.size());
    return stats;
}

} // namespace vr::render_graph
