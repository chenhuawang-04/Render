#pragma once

#include "vr/render_graph/compiled_render_graph.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace vr::render_graph {

struct QueueExecutionCapabilities final {
    QueueFamilyIndices queue_families{};
    bool has_graphics_queue = false;
    bool has_compute_queue = false;
    bool has_transfer_queue = false;
};

struct QueueExecutionPolicy final {
    QueueFamilyIndices effective_queue_families{};
    bool transfer_requested = false;
    bool compute_requested = false;
    bool multi_queue_requested = false;
    bool transfer_enabled = false;
    bool compute_enabled = false;
    bool multi_queue_enabled = false;
    bool graphics_fallback_active = false;
    std::string fallback_reason{};
};

[[nodiscard]] inline QueueExecutionCapabilities InspectQueueExecutionCapabilities(
    const VulkanContext& device_) noexcept {
    return QueueExecutionCapabilities{
        .queue_families = device_.QueueFamilies(),
        .has_graphics_queue = device_.GraphicsQueue() != VK_NULL_HANDLE,
        .has_compute_queue = device_.ComputeQueue() != VK_NULL_HANDLE,
        .has_transfer_queue = device_.TransferQueue() != VK_NULL_HANDLE,
    };
}

[[nodiscard]] inline QueueFamilyIndices BuildGraphicsOnlyQueueFamilies(
    const QueueFamilyIndices& queue_families_) noexcept {
    QueueFamilyIndices effective = queue_families_;
    if (effective.graphics.has_value()) {
        effective.compute = effective.graphics;
        effective.transfer = effective.graphics;
    }
    return effective;
}

[[nodiscard]] inline bool GraphRequestsQueue(const CompiledRenderGraph& graph_,
                                             const QueueClass queue_) noexcept {
    return std::any_of(
        graph_.Passes().begin(),
        graph_.Passes().end(),
        [queue_](const CompiledPass& pass_) {
            return pass_.queue == queue_;
        });
}

[[nodiscard]] inline QueueExecutionPolicy ResolveQueueExecutionPolicy(
    const CompiledRenderGraph& graph_,
    const QueueExecutionCapabilities& capabilities_,
    const bool multi_queue_submit_enabled_) {
    QueueExecutionPolicy policy{};
    policy.effective_queue_families = capabilities_.queue_families;
    policy.transfer_requested = GraphRequestsQueue(graph_, QueueClass::transfer);
    policy.compute_requested = GraphRequestsQueue(graph_, QueueClass::compute);
    policy.multi_queue_requested = policy.transfer_requested || policy.compute_requested;

    const auto fallback_to_graphics = [&](std::string reason_) {
        policy.effective_queue_families =
            BuildGraphicsOnlyQueueFamilies(capabilities_.queue_families);
        policy.transfer_enabled = false;
        policy.compute_enabled = false;
        policy.multi_queue_enabled = false;
        policy.graphics_fallback_active = policy.multi_queue_requested;
        policy.fallback_reason = std::move(reason_);
    };

    if (!policy.multi_queue_requested) {
        return policy;
    }

    if (!capabilities_.queue_families.graphics.has_value() ||
        !capabilities_.has_graphics_queue) {
        fallback_to_graphics(
            "RenderGraph queue policy requires a graphics queue but none is available");
        return policy;
    }

    if (!multi_queue_submit_enabled_) {
        fallback_to_graphics(
            "RenderGraph multi-queue submit path is not enabled yet; falling back to graphics queue");
        return policy;
    }

    if (policy.transfer_requested && !capabilities_.has_transfer_queue) {
        fallback_to_graphics(
            "RenderGraph transfer queue is unavailable; falling back to graphics queue");
        return policy;
    }
    if (policy.compute_requested && !capabilities_.has_compute_queue) {
        fallback_to_graphics(
            "RenderGraph compute queue is unavailable; falling back to graphics queue");
        return policy;
    }

    policy.transfer_enabled = policy.transfer_requested;
    policy.compute_enabled = policy.compute_requested;
    policy.multi_queue_enabled = policy.transfer_enabled || policy.compute_enabled;
    policy.graphics_fallback_active = false;
    policy.fallback_reason.clear();
    return policy;
}

} // namespace vr::render_graph
