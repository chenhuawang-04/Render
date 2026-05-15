#pragma once

#include "vr/render_graph/barrier_plan.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/vulkan_context.hpp"

#include <string>
#include <vector>

namespace vr::render {
class RenderTargetHost;
}

namespace vr::render_graph {

class CompiledRenderGraph;
struct CompiledResource;
class VulkanResourceTable;

struct VulkanAccessInfo final {
    VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct LoweredVulkanBarrier final {
    ResourceVersionHandle resource{};
    std::string debug_name{};
    ResourceKind kind = ResourceKind::buffer;
    AccessKind before = AccessKind::none;
    AccessKind after = AccessKind::none;
    QueueClass src_queue = QueueClass::graphics;
    QueueClass dst_queue = QueueClass::graphics;
    PassHandle src_pass{};
    PassHandle dst_pass{};
    VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 src_access_mask = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 dst_stage_mask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 dst_access_mask = VK_ACCESS_2_NONE;
    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint32_t src_queue_family_index = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED;
    SubresourceRange subresource_range{};
    BufferRange buffer_range{};
    bool queue_transfer = false;
    bool host_boundary = false;
    bool aliasing = false;
    bool uav_ordering = false;
};

struct VulkanBarrierBatch final {
    PassHandle pass{};
    QueueClass queue = QueueClass::graphics;
    std::vector<LoweredVulkanBarrier> barriers{};
};

struct VulkanDependencyInfoData final {
    std::vector<VkMemoryBarrier2> memory_barriers{};
    std::vector<VkBufferMemoryBarrier2> buffer_barriers{};
    std::vector<VkImageMemoryBarrier2> image_barriers{};

    [[nodiscard]] bool Empty() const noexcept {
        return memory_barriers.empty() &&
               buffer_barriers.empty() &&
               image_barriers.empty();
    }

    [[nodiscard]] VkDependencyInfo BuildVkDependencyInfo() const noexcept;
};

struct VulkanCommandBarrierBatch final {
    PassHandle pass{};
    QueueClass queue = QueueClass::graphics;
    VulkanDependencyInfoData dependency{};
    std::vector<LoweredVulkanBarrier> barriers{};
};

struct VulkanQueueTransferBatch final {
    PassHandle source_pass{};
    PassHandle target_pass{};
    QueueClass source_queue = QueueClass::graphics;
    QueueClass target_queue = QueueClass::graphics;
    VulkanDependencyInfoData release_dependency{};
    VulkanDependencyInfoData acquire_dependency{};
    std::vector<LoweredVulkanBarrier> barriers{};
};

struct VulkanCommandReadyPlan final {
    std::vector<VulkanCommandBarrierBatch> command_batches{};
    std::vector<VulkanQueueTransferBatch> queue_transfer_batches{};

    [[nodiscard]] std::string BuildDebugString() const;
    [[nodiscard]] std::string BuildJson() const;
};

struct VulkanBarrierPlan final {
    std::vector<VulkanBarrierBatch> barrier_batches{};

    [[nodiscard]] std::string BuildDebugString() const;
    [[nodiscard]] std::string BuildJson() const;
};

[[nodiscard]] VulkanAccessInfo DescribeVulkanAccess(const CompiledResource& resource_,
                                                    AccessKind access_) noexcept;
[[nodiscard]] VulkanBarrierPlan LowerToVulkanBarrierPlan(
    const CompiledRenderGraph& compiled_graph_,
    const QueueFamilyIndices& queue_families_);
[[nodiscard]] VulkanCommandReadyPlan BuildCommandReadyVulkanBarrierPlan(
    const VulkanBarrierPlan& lowered_plan_,
    const VulkanResourceTable& resource_table_,
    const render::RenderTargetHost& render_target_host_);

} // namespace vr::render_graph
