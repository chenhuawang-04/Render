#include "vr/render_graph/render_graph_executor.hpp"

#ifdef FindResource
#undef FindResource
#endif

#include "vr/render_graph/compiled_render_graph.hpp"

#include <algorithm>
#include <stdexcept>

namespace vr::render_graph {

namespace {

void PrepareDescriptorSetsForPass(GraphCommandContext& context_,
                                  const CompiledPass& pass_) {
    const auto& descriptor_plan = context_.Graph().DescriptorPlan();
    const auto write_batch_it = std::find_if(
        descriptor_plan.writes.begin(),
        descriptor_plan.writes.end(),
        [&](const DescriptorWriteBatch& batch_) {
            return batch_.pass.index == pass_.handle.index;
        });
    if (write_batch_it == descriptor_plan.writes.end()) {
        context_.SetCurrentPassDescriptorSets({});
        return;
    }
    if (!context_.HasDescriptorHost()) {
        throw std::runtime_error(
            "RenderGraphExecutor::Record requires DescriptorHost for passes with descriptor bindings");
    }

    std::uint32_t max_set_index = 0U;
    for (const auto& write_ : write_batch_it->writes) {
        max_set_index = std::max(max_set_index, write_.set);
    }

    std::vector<VkDescriptorSet> descriptor_sets(max_set_index + 1U, VK_NULL_HANDLE);
    for (const auto& write_ : write_batch_it->writes) {
        switch (write_.source) {
        case DescriptorBindingSource::bindless_table:
            descriptor_sets[write_.set] =
                context_.Descriptors().GetBindlessDescriptorSet(
                    render::BindlessTableId{.value = write_.source_id});
            break;
        case DescriptorBindingSource::none:
        default:
            throw std::runtime_error(
                "RenderGraphExecutor::Record encountered unsupported descriptor binding source");
        }
        if (descriptor_sets[write_.set] == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "RenderGraphExecutor::Record resolved null descriptor set from descriptor binding plan");
        }
    }

    context_.SetCurrentPassDescriptorSets(std::move(descriptor_sets));
}

} // namespace

RenderGraphRecordStats RenderGraphExecutor::Record(GraphCommandContext& context_) {
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

        context_.SetCurrentPass(pass_->handle);
        PrepareDescriptorSetsForPass(context_, *pass_);

        if (has_raster_pass) {
            const auto rendering_info = context_.BuildRenderingInfo(*pass_->raster_pass);
            context_.BeginRendering(rendering_info);
        }

        if (pass_->execute) {
            pass_->execute(context_);
        }

        if (has_raster_pass) {
            context_.EndRendering();
        }
        context_.ClearCurrentPass();
        stats.pass_count += 1U;
    }

    stats.queue_transfer_batch_count = static_cast<std::uint32_t>(command_ready.queue_transfer_batches.size());
    return stats;
}

} // namespace vr::render_graph
