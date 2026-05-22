#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <algorithm>
#include <array>

namespace {

void AppendDependencyInfoData(vr::render_graph::VulkanDependencyInfoData& destination_,
                              const vr::render_graph::VulkanDependencyInfoData& source_) {
    destination_.memory_barriers.insert(destination_.memory_barriers.end(),
                                        source_.memory_barriers.begin(),
                                        source_.memory_barriers.end());
    destination_.buffer_barriers.insert(destination_.buffer_barriers.end(),
                                        source_.buffer_barriers.begin(),
                                        source_.buffer_barriers.end());
    destination_.image_barriers.insert(destination_.image_barriers.end(),
                                       source_.image_barriers.begin(),
                                       source_.image_barriers.end());
}

void AppendSemaphoreUnique(std::vector<VkSemaphore>& semaphores_,
                           const VkSemaphore semaphore_) {
    if (semaphore_ == VK_NULL_HANDLE) {
        return;
    }
    if (std::find(semaphores_.begin(), semaphores_.end(), semaphore_) == semaphores_.end()) {
        semaphores_.push_back(semaphore_);
    }
}

void AppendFrameSubmitWaitUnique(std::vector<vr::render::FrameSubmitWait>& waits_,
                                 const vr::render::FrameSubmitWait& wait_) {
    if (wait_.semaphore == VK_NULL_HANDLE) {
        return;
    }
    const auto existing = std::find_if(
        waits_.begin(),
        waits_.end(),
        [&](const vr::render::FrameSubmitWait& candidate_) {
            return candidate_.semaphore == wait_.semaphore &&
                   candidate_.stage_mask == wait_.stage_mask;
        });
    if (existing == waits_.end()) {
        waits_.push_back(wait_);
    }
}

} // namespace

namespace vr::runtime::services {

VkResult RenderGraphRuntimeService::SubmitPreparedMultiQueueWork(
    VulkanContext& device_) {
    if (!prepared_multi_queue_submission.active ||
        prepared_multi_queue_submission.submitted) {
        return VK_SUCCESS;
    }
    if (device_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error(
            "RenderGraphRuntimeService multi-queue submit requires Vulkan synchronization2");
    }

    VkResult last_result = VK_SUCCESS;
    for (auto& batch_ : prepared_multi_queue_submission.owned_submit_batches) {
        std::vector<VkSemaphoreSubmitInfo> wait_infos(batch_.wait_semaphores.size());
        std::vector<VkSemaphoreSubmitInfo> signal_infos(batch_.signal_semaphores.size());

        for (std::size_t wait_index = 0; wait_index < batch_.wait_semaphores.size(); ++wait_index) {
            wait_infos[wait_index].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait_infos[wait_index].semaphore = batch_.wait_semaphores[wait_index];
            wait_infos[wait_index].value = 0U;
            wait_infos[wait_index].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            wait_infos[wait_index].deviceIndex = 0U;
        }
        for (std::size_t signal_index = 0; signal_index < batch_.signal_semaphores.size(); ++signal_index) {
            signal_infos[signal_index].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal_infos[signal_index].semaphore = batch_.signal_semaphores[signal_index];
            signal_infos[signal_index].value = 0U;
            signal_infos[signal_index].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signal_infos[signal_index].deviceIndex = 0U;
        }

        VkCommandBufferSubmitInfo command_buffer_info{};
        command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        command_buffer_info.commandBuffer = batch_.command_buffer;
        command_buffer_info.deviceMask = 0U;

        VkSubmitInfo2 submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit_info.waitSemaphoreInfoCount =
            static_cast<std::uint32_t>(batch_.wait_semaphores.size());
        submit_info.pWaitSemaphoreInfos =
            submit_info.waitSemaphoreInfoCount != 0U ? wait_infos.data() : nullptr;
        submit_info.commandBufferInfoCount = 1U;
        submit_info.pCommandBufferInfos = &command_buffer_info;
        submit_info.signalSemaphoreInfoCount =
            static_cast<std::uint32_t>(batch_.signal_semaphores.size());
        submit_info.pSignalSemaphoreInfos =
            submit_info.signalSemaphoreInfoCount != 0U ? signal_infos.data() : nullptr;

        last_result = vkQueueSubmit2(batch_.submit_queue, 1U, &submit_info, VK_NULL_HANDLE);
        if (last_result != VK_SUCCESS) {
            throw std::runtime_error("RenderGraphRuntimeService multi-queue vkQueueSubmit2 failed");
        }

        if (batch_.queue == render_graph::QueueClass::transfer) {
            const std::uint64_t submit_value = next_transfer_submit_value++;
            last_transfer_submitted_value =
                (std::max)(last_transfer_submitted_value, submit_value);
            prepared_multi_queue_submission.transfer_submitted_value =
                (std::max)(prepared_multi_queue_submission.transfer_submitted_value, submit_value);
        } else if (batch_.queue == render_graph::QueueClass::compute) {
            const std::uint64_t submit_value = next_compute_submit_value++;
            last_compute_submitted_value =
                (std::max)(last_compute_submitted_value, submit_value);
            prepared_multi_queue_submission.compute_submitted_value =
                (std::max)(prepared_multi_queue_submission.compute_submitted_value, submit_value);
        }
    }

    prepared_multi_queue_submission.submitted = true;
    return last_result;
}

std::uint32_t RenderGraphRuntimeService::ResolveQueueFamilyIndex(
    const VulkanContext& device_,
    const render_graph::QueueClass queue_) const {
    switch (queue_) {
    case render_graph::QueueClass::transfer:
        if (!device_.QueueFamilies().transfer.has_value()) {
            break;
        }
        return device_.QueueFamilies().transfer.value();
    case render_graph::QueueClass::compute:
        if (!device_.QueueFamilies().compute.has_value()) {
            break;
        }
        return device_.QueueFamilies().compute.value();
    case render_graph::QueueClass::graphics:
        if (!device_.QueueFamilies().graphics.has_value()) {
            break;
        }
        return device_.QueueFamilies().graphics.value();
    default:
        break;
    }
    throw std::runtime_error(
        "RenderGraphRuntimeService could not resolve a queue family index for the requested queue class");
}

VkQueue RenderGraphRuntimeService::ResolveSubmitQueue(
    const VulkanContext& device_,
    const render_graph::QueueClass queue_) const noexcept {
    switch (queue_) {
    case render_graph::QueueClass::transfer:
        return device_.TransferQueue();
    case render_graph::QueueClass::compute:
        return device_.ComputeQueue();
    case render_graph::QueueClass::graphics:
        return device_.GraphicsQueue();
    default:
        break;
    }
    return VK_NULL_HANDLE;
}

RenderGraphRuntimeService::QueueCommandResources&
RenderGraphRuntimeService::EnsureQueueCommandResources(
    VulkanContext& device_,
    MultiQueueFrameSlot& slot_,
    const render_graph::QueueClass queue_) {
    QueueCommandResources* resources = nullptr;
    switch (queue_) {
    case render_graph::QueueClass::graphics:
        resources = &slot_.graphics;
        break;
    case render_graph::QueueClass::transfer:
        resources = &slot_.transfer;
        break;
    case render_graph::QueueClass::compute:
        resources = &slot_.compute;
        break;
    default:
        throw std::runtime_error(
            "RenderGraphRuntimeService could not resolve owned command resources for the requested queue");
    }

    const std::uint32_t queue_family_index = ResolveQueueFamilyIndex(device_, queue_);
    if (resources->pool != VK_NULL_HANDLE &&
        resources->queue_family_index != queue_family_index) {
        DestroyQueueCommandResources(device_.Device(), *resources);
    }

    if (resources->pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pool_create_info{};
        pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                                 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_create_info.queueFamilyIndex = queue_family_index;
        const VkResult create_result =
            vkCreateCommandPool(device_.Device(), &pool_create_info, nullptr, &resources->pool);
        if (create_result != VK_SUCCESS) {
            throw std::runtime_error(
                "RenderGraphRuntimeService failed to create a multi-queue command pool");
        }
        resources->queue_family_index = queue_family_index;
        resources->used_primary_count = 0U;
    }

    return *resources;
}

VkCommandBuffer RenderGraphRuntimeService::BeginOwnedCommandBuffer(
    VulkanContext& device_,
    QueueCommandResources& resources_) {
    if (resources_.used_primary_count >= resources_.primary_buffers.size()) {
        VkCommandBuffer new_command_buffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = resources_.pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1U;
        const VkResult allocate_result =
            vkAllocateCommandBuffers(device_.Device(), &allocate_info, &new_command_buffer);
        if (allocate_result != VK_SUCCESS) {
            throw std::runtime_error(
                "RenderGraphRuntimeService failed to allocate a multi-queue command buffer");
        }
        resources_.primary_buffers.push_back(new_command_buffer);
    }

    const VkCommandBuffer command_buffer =
        resources_.primary_buffers[resources_.used_primary_count++];
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult begin_result =
        vkBeginCommandBuffer(command_buffer, &begin_info);
    if (begin_result != VK_SUCCESS) {
        throw std::runtime_error(
            "RenderGraphRuntimeService failed to begin a multi-queue command buffer");
    }
    return command_buffer;
}

void RenderGraphRuntimeService::EnsureDependencySemaphores(
    VulkanContext& device_,
    MultiQueueFrameSlot& slot_,
    const render_graph::BarrierPlan& barrier_plan_) {
    if (slot_.dependency_semaphores.size() < barrier_plan_.queue_dependencies.size()) {
        slot_.dependency_semaphores.resize(barrier_plan_.queue_dependencies.size());
    }

    for (std::size_t dependency_index = 0U;
         dependency_index < barrier_plan_.queue_dependencies.size();
         ++dependency_index) {
        const auto& dependency_ = barrier_plan_.queue_dependencies[dependency_index];
        if (dependency_.host_boundary ||
            dependency_.source_queue == dependency_.target_queue) {
            continue;
        }
        if (slot_.dependency_semaphores[dependency_index].semaphore != VK_NULL_HANDLE) {
            continue;
        }

        VkSemaphoreCreateInfo semaphore_create_info{};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        const VkResult create_result =
            vkCreateSemaphore(device_.Device(),
                              &semaphore_create_info,
                              nullptr,
                              &slot_.dependency_semaphores[dependency_index].semaphore);
        if (create_result != VK_SUCCESS) {
            throw std::runtime_error(
                "RenderGraphRuntimeService failed to allocate a cross-queue dependency semaphore");
        }
    }
}

std::vector<std::uint32_t> RenderGraphRuntimeService::BuildBatchIndexByPass() const {
    std::vector<std::uint32_t> batch_index_by_pass(
        compiled_graph.Passes().size(),
        render_graph::invalid_render_graph_index);
    const auto& queue_batches = compiled_graph.PlannedBarriers().queue_batches;
    for (std::uint32_t batch_index = 0U;
         batch_index < static_cast<std::uint32_t>(queue_batches.size());
         ++batch_index) {
        for (const auto pass_handle_ : queue_batches[batch_index].passes) {
            if (pass_handle_.index < batch_index_by_pass.size()) {
                batch_index_by_pass[pass_handle_.index] = batch_index;
            }
        }
    }
    return batch_index_by_pass;
}

render_graph::RenderGraphRecordStats RenderGraphRuntimeService::RecordPreparedMultiQueueGraph(
    VulkanContext& device_,
    render::RenderTargetHost& render_target_host_,
    render::DescriptorHost* descriptor_host_,
    const VkCommandBuffer graphics_command_buffer_) {
    if (graphics_command_buffer_ == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderGraphRuntimeService multi-queue path requires a graphics command buffer");
    }

    const auto& barrier_plan = compiled_graph.PlannedBarriers();
    const auto& queue_batches = barrier_plan.queue_batches;
    if (queue_batches.empty()) {
        return {};
    }

    EnsureMultiQueueFrameSlotCapacity(frame_index + 1U);
    MultiQueueFrameSlot& frame_slot_ref = multi_queue_frame_slots[frame_index];
    EnsureDependencySemaphores(device_, frame_slot_ref, barrier_plan);

    const auto batch_index_by_pass = BuildBatchIndexByPass();
    std::vector<BatchDependencyAggregation> batch_dependencies(queue_batches.size());
    for (const auto& transfer_batch_ : command_ready_vulkan_barriers.queue_transfer_batches) {
        if (transfer_batch_.source_pass.index >= batch_index_by_pass.size() ||
            transfer_batch_.target_pass.index >= batch_index_by_pass.size()) {
            continue;
        }

        const std::uint32_t source_batch_index =
            batch_index_by_pass[transfer_batch_.source_pass.index];
        const std::uint32_t target_batch_index =
            batch_index_by_pass[transfer_batch_.target_pass.index];
        if (source_batch_index >= batch_dependencies.size() ||
            target_batch_index >= batch_dependencies.size()) {
            continue;
        }

        AppendDependencyInfoData(batch_dependencies[source_batch_index].end_dependency,
                                 transfer_batch_.release_dependency);
        AppendDependencyInfoData(batch_dependencies[target_batch_index].begin_dependency,
                                 transfer_batch_.acquire_dependency);
    }

    PreparedMultiQueueSubmission prepared{};
    prepared.graphics_batch_index =
        static_cast<std::uint32_t>(queue_batches.size() - 1U);
    std::array<bool, 3U> external_wait_consumed_by_queue{};

    const auto queue_class_index =
        [](const render_graph::QueueClass queue_) constexpr noexcept -> std::size_t {
            switch (queue_) {
            case render_graph::QueueClass::graphics:
                return 0U;
            case render_graph::QueueClass::compute:
                return 1U;
            case render_graph::QueueClass::transfer:
                return 2U;
            default:
                return 0U;
            }
        };

    const auto append_external_submit_waits =
        [&](std::vector<VkSemaphore>& waits_,
            const render_graph::QueueClass queue_) {
            bool& consumed = external_wait_consumed_by_queue[queue_class_index(queue_)];
            if (consumed) {
                return;
            }
            for (const ExternalQueueWait& wait_ : external_queue_waits) {
                if (wait_.queue != queue_) {
                    continue;
                }
                AppendSemaphoreUnique(waits_, wait_.semaphore);
            }
            consumed = true;
        };

    const auto append_external_graphics_waits =
        [&](std::vector<vr::render::FrameSubmitWait>& waits_,
            const render_graph::QueueClass queue_) {
            bool& consumed = external_wait_consumed_by_queue[queue_class_index(queue_)];
            if (consumed) {
                return;
            }
            for (const ExternalQueueWait& wait_ : external_queue_waits) {
                if (wait_.queue != queue_) {
                    continue;
                }
                AppendFrameSubmitWaitUnique(waits_,
                                            vr::render::FrameSubmitWait{
                                                .semaphore = wait_.semaphore,
                                                .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                            });
            }
            consumed = true;
        };

    const auto accumulate_stats =
        [](render_graph::RenderGraphRecordStats& destination_,
           const render_graph::RenderGraphRecordStats& source_) {
            destination_.pass_count += source_.pass_count;
            destination_.rendering_scope_count += source_.rendering_scope_count;
            destination_.command_batch_count += source_.command_batch_count;
            destination_.memory_barrier_count += source_.memory_barrier_count;
            destination_.buffer_barrier_count += source_.buffer_barrier_count;
            destination_.image_barrier_count += source_.image_barrier_count;
        };

    render_graph::RenderGraphRecordStats total_stats{};
    for (std::uint32_t batch_index = 0U;
         batch_index < static_cast<std::uint32_t>(queue_batches.size());
         ++batch_index) {
        const auto& queue_batch_ = queue_batches[batch_index];
        const auto* begin_dependency =
            batch_dependencies[batch_index].begin_dependency.Empty()
                ? nullptr
                : &batch_dependencies[batch_index].begin_dependency;
        const auto* end_dependency =
            batch_dependencies[batch_index].end_dependency.Empty()
                ? nullptr
                : &batch_dependencies[batch_index].end_dependency;
        const bool terminal_graphics_batch =
            queue_batch_.queue == render_graph::QueueClass::graphics &&
            batch_index == prepared.graphics_batch_index;

        if (terminal_graphics_batch) {
            render_graph::GraphCommandContext graphics_context{
                device_,
                frame_index,
                graphics_command_buffer_,
                compiled_graph,
                physical_resources,
                render_target_host_,
                descriptor_host_,
                lowered_vulkan_barriers,
                command_ready_vulkan_barriers,
            };
            accumulate_stats(total_stats,
                             render_graph::RenderGraphExecutor::RecordQueueBatch(
                                 graphics_context,
                                 queue_batch_,
                                 begin_dependency,
                                 end_dependency));

            for (const auto dependency_index : queue_batch_.wait_dependency_indices) {
                if (dependency_index >= barrier_plan.queue_dependencies.size() ||
                    dependency_index >= frame_slot_ref.dependency_semaphores.size()) {
                    continue;
                }
                const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
                if (dependency_.host_boundary ||
                    dependency_.source_queue == dependency_.target_queue) {
                    continue;
                }

                const VkSemaphore semaphore =
                    frame_slot_ref.dependency_semaphores[dependency_index].semaphore;
                if (semaphore == VK_NULL_HANDLE) {
                    continue;
                }
                AppendFrameSubmitWaitUnique(prepared.graphics_waits,
                                            vr::render::FrameSubmitWait{
                                                .semaphore = semaphore,
                                                .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                            });
            }
            append_external_graphics_waits(prepared.graphics_waits,
                                           render_graph::QueueClass::graphics);
            continue;
        }

        QueueCommandResources& command_resources =
            EnsureQueueCommandResources(device_, frame_slot_ref, queue_batch_.queue);
        const VkCommandBuffer owned_command_buffer =
            BeginOwnedCommandBuffer(device_, command_resources);
        render_graph::GraphCommandContext batch_context{
            device_,
            frame_index,
            owned_command_buffer,
            compiled_graph,
            physical_resources,
            render_target_host_,
            descriptor_host_,
            lowered_vulkan_barriers,
            command_ready_vulkan_barriers,
        };
        accumulate_stats(total_stats,
                         render_graph::RenderGraphExecutor::RecordQueueBatch(
                             batch_context,
                             queue_batch_,
                             begin_dependency,
                             end_dependency));
        const VkResult end_result = vkEndCommandBuffer(owned_command_buffer);
        if (end_result != VK_SUCCESS) {
            throw std::runtime_error(
                "RenderGraphRuntimeService failed to end a multi-queue command buffer");
        }

        PreparedQueueSubmitBatch submit_batch{};
        submit_batch.batch_index = batch_index;
        submit_batch.queue = queue_batch_.queue;
        submit_batch.submit_queue = ResolveSubmitQueue(device_, queue_batch_.queue);
        submit_batch.command_buffer = owned_command_buffer;

        for (const auto dependency_index : queue_batch_.wait_dependency_indices) {
            if (dependency_index >= barrier_plan.queue_dependencies.size() ||
                dependency_index >= frame_slot_ref.dependency_semaphores.size()) {
                continue;
            }
            const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
            if (dependency_.host_boundary ||
                dependency_.source_queue == dependency_.target_queue) {
                continue;
            }
            const VkSemaphore semaphore =
                frame_slot_ref.dependency_semaphores[dependency_index].semaphore;
            if (semaphore != VK_NULL_HANDLE) {
                AppendSemaphoreUnique(submit_batch.wait_semaphores, semaphore);
            }
        }
        append_external_submit_waits(submit_batch.wait_semaphores, queue_batch_.queue);
        for (const auto dependency_index : queue_batch_.signal_dependency_indices) {
            if (dependency_index >= barrier_plan.queue_dependencies.size() ||
                dependency_index >= frame_slot_ref.dependency_semaphores.size()) {
                continue;
            }
            const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
            if (dependency_.host_boundary ||
                dependency_.source_queue == dependency_.target_queue) {
                continue;
            }
            const VkSemaphore semaphore =
                frame_slot_ref.dependency_semaphores[dependency_index].semaphore;
            if (semaphore != VK_NULL_HANDLE) {
                AppendSemaphoreUnique(submit_batch.signal_semaphores, semaphore);
            }
        }
        prepared.owned_submit_batches.push_back(std::move(submit_batch));
    }

    total_stats.queue_transfer_batch_count =
        static_cast<std::uint32_t>(command_ready_vulkan_barriers.queue_transfer_batches.size());
    prepared.active = !prepared.owned_submit_batches.empty() ||
                      !prepared.graphics_waits.empty();
    prepared_multi_queue_submission = std::move(prepared);
    return total_stats;
}

} // namespace vr::runtime::services
