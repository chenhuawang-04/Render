#include "vr/render_graph/vulkan_barrier_plan.hpp"

#ifdef FindResource
#undef FindResource
#endif

#include "vr/render/render_target_host.hpp"
#include "vr/render_graph/compiled_render_graph.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"

#include <algorithm>
#include <sstream>

namespace vr::render_graph {
namespace {

[[nodiscard]] const char* QueueClassToString(const QueueClass queue_) noexcept {
    switch (queue_) {
    case QueueClass::graphics:
        return "graphics";
    case QueueClass::compute:
        return "compute";
    case QueueClass::transfer:
        return "transfer";
    default:
        break;
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
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] bool IsDepthFormat(const TextureFormat format_) noexcept {
    return format_ == TextureFormat::d32_sfloat;
}

[[nodiscard]] const char* LayoutToString(const VkImageLayout layout_) noexcept {
    switch (layout_) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return "undefined";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return "color_attachment_optimal";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return "depth_stencil_attachment_optimal";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return "depth_stencil_read_only_optimal";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return "shader_read_only_optimal";
    case VK_IMAGE_LAYOUT_GENERAL:
        return "general";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return "transfer_src_optimal";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return "transfer_dst_optimal";
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return "present_src";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] std::uint32_t ResolveQueueFamilyIndex(const QueueFamilyIndices& queue_families_,
                                                    const QueueClass queue_) noexcept {
    const std::uint32_t graphics_family = queue_families_.graphics.value_or(0U);
    switch (queue_) {
    case QueueClass::graphics:
        return graphics_family;
    case QueueClass::compute:
        return queue_families_.compute.value_or(graphics_family);
    case QueueClass::transfer:
        return queue_families_.transfer.value_or(graphics_family);
    default:
        break;
    }
    return graphics_family;
}

[[nodiscard]] VkImageSubresourceRange BuildImageSubresourceRange(
    const render::RenderTargetHost& render_target_host_,
    const render::RenderTargetHandle handle_,
    const SubresourceRange& range_) {
    VkImageSubresourceRange vk_range = render_target_host_.DefaultSubresourceRange(handle_);
    if (range_.level_count != 0U) {
        vk_range.baseMipLevel = range_.base_mip_level;
        vk_range.levelCount = range_.level_count;
    }
    if (range_.layer_count != 0U) {
        vk_range.baseArrayLayer = range_.base_array_layer;
        vk_range.layerCount = range_.layer_count;
    }
    return vk_range;
}

[[nodiscard]] VkDeviceSize ResolveBufferSize(const PhysicalBufferRecord& record_,
                                             const BufferRange& range_) noexcept {
    if (range_.size_bytes != 0U) {
        return range_.size_bytes;
    }
    if (record_.imported) {
        return record_.imported_buffer.size_bytes;
    }
    return record_.owned_resource.size;
}

[[nodiscard]] VkBuffer ResolveBufferHandle(const PhysicalBufferRecord& record_) noexcept {
    return record_.imported ? record_.imported_buffer.buffer : record_.owned_resource.buffer;
}

[[nodiscard]] bool TryBuildImageBarrier(const VulkanResourceTable& resource_table_,
                                        const render::RenderTargetHost& render_target_host_,
                                        const ResourceHandle logical_,
                                        const LoweredVulkanBarrier& barrier_,
                                        const VkPipelineStageFlags2 src_stage_mask_,
                                        const VkAccessFlags2 src_access_mask_,
                                        const VkPipelineStageFlags2 dst_stage_mask_,
                                        const VkAccessFlags2 dst_access_mask_,
                                        const std::uint32_t src_queue_family_index_,
                                        const std::uint32_t dst_queue_family_index_,
                                        VkImageMemoryBarrier2& image_barrier_) {
    const auto* texture_ = resource_table_.FindTexture(logical_);
    if (texture_ == nullptr) {
        return false;
    }

    const auto resolved_view = render_target_host_.ResolveView(texture_->render_target);
    if (resolved_view.image == VK_NULL_HANDLE) {
        return false;
    }

    image_barrier_ = {};
    image_barrier_.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_barrier_.srcStageMask = src_stage_mask_;
    image_barrier_.srcAccessMask = src_access_mask_;
    image_barrier_.dstStageMask = dst_stage_mask_;
    image_barrier_.dstAccessMask = dst_access_mask_;
    image_barrier_.oldLayout = barrier_.old_layout;
    image_barrier_.newLayout = barrier_.new_layout;
    image_barrier_.srcQueueFamilyIndex = src_queue_family_index_;
    image_barrier_.dstQueueFamilyIndex = dst_queue_family_index_;
    image_barrier_.image = resolved_view.image;
    image_barrier_.subresourceRange = BuildImageSubresourceRange(render_target_host_,
                                                                 texture_->render_target,
                                                                 barrier_.subresource_range);
    return true;
}

[[nodiscard]] VulkanCommandBarrierBatch* FindCommandBatch(
    std::vector<VulkanCommandBarrierBatch>& command_batches_,
    const PassHandle pass_,
    const QueueClass queue_) noexcept {
    for (auto& batch_ : command_batches_) {
        if (batch_.pass.index == pass_.index && batch_.queue == queue_) {
            return &batch_;
        }
    }
    return nullptr;
}

[[nodiscard]] VulkanQueueTransferBatch* FindQueueTransferBatch(
    std::vector<VulkanQueueTransferBatch>& queue_transfer_batches_,
    const PassHandle source_pass_,
    const PassHandle target_pass_,
    const QueueClass source_queue_,
    const QueueClass target_queue_) noexcept {
    for (auto& batch_ : queue_transfer_batches_) {
        if (batch_.source_pass.index == source_pass_.index &&
            batch_.target_pass.index == target_pass_.index &&
            batch_.source_queue == source_queue_ &&
            batch_.target_queue == target_queue_) {
            return &batch_;
        }
    }
    return nullptr;
}

void AppendDependencyInfoJson(std::ostringstream& oss_,
                              const VulkanDependencyInfoData& dependency_) {
    oss_ << "{\"memoryBarrierCount\": " << dependency_.memory_barriers.size()
         << ", \"bufferBarrierCount\": " << dependency_.buffer_barriers.size()
         << ", \"imageBarrierCount\": " << dependency_.image_barriers.size() << '}';
}

} // namespace

VkDependencyInfo VulkanDependencyInfoData::BuildVkDependencyInfo() const noexcept {
    VkDependencyInfo dependency_info{};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.memoryBarrierCount = static_cast<std::uint32_t>(memory_barriers.size());
    dependency_info.pMemoryBarriers = memory_barriers.empty() ? nullptr : memory_barriers.data();
    dependency_info.bufferMemoryBarrierCount = static_cast<std::uint32_t>(buffer_barriers.size());
    dependency_info.pBufferMemoryBarriers = buffer_barriers.empty() ? nullptr : buffer_barriers.data();
    dependency_info.imageMemoryBarrierCount = static_cast<std::uint32_t>(image_barriers.size());
    dependency_info.pImageMemoryBarriers = image_barriers.empty() ? nullptr : image_barriers.data();
    return dependency_info;
}

VulkanAccessInfo DescribeVulkanAccess(const CompiledResource& resource_,
                                      const AccessKind access_) noexcept {
    const bool is_depth = resource_.kind == ResourceKind::texture && IsDepthFormat(resource_.texture.format);

    switch (access_) {
    case AccessKind::none:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .access_mask = VK_ACCESS_2_NONE,
            .image_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
    case AccessKind::color_attachment_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
    case AccessKind::color_attachment_write:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .image_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
    case AccessKind::depth_stencil_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        };
    case AccessKind::depth_stencil_write:
    case AccessKind::depth_stencil_read_write:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
    case AccessKind::shader_sample_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .image_layout = is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                     : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    case AccessKind::shader_storage_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::shader_storage_write:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::shader_storage_read_write:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                           VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::uniform_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .access_mask = VK_ACCESS_2_UNIFORM_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::vertex_buffer_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            .access_mask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::index_buffer_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
            .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::indirect_command_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .access_mask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::transfer_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        };
    case AccessKind::transfer_write:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .image_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        };
    case AccessKind::present:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .access_mask = VK_ACCESS_2_NONE,
            .image_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
    case AccessKind::host_read:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .access_mask = VK_ACCESS_2_HOST_READ_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    case AccessKind::host_write:
        return VulkanAccessInfo{
            .stage_mask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .access_mask = VK_ACCESS_2_HOST_WRITE_BIT,
            .image_layout = VK_IMAGE_LAYOUT_GENERAL,
        };
    default:
        break;
    }

    return {};
}

std::string VulkanBarrierPlan::BuildDebugString() const {
    std::ostringstream oss{};
    oss << "vulkan_barrier_batches=" << barrier_batches.size() << '\n';
    for (const auto& batch_ : barrier_batches) {
        oss << "pass=" << batch_.pass.index
            << " queue=" << QueueClassToString(batch_.queue)
            << " barriers=" << batch_.barriers.size() << '\n';
        for (const auto& barrier_ : batch_.barriers) {
            oss << "  resource=" << barrier_.debug_name
                << " before=" << AccessKindToString(barrier_.before)
                << " after=" << AccessKindToString(barrier_.after)
                << " stages=" << barrier_.src_stage_mask << "->" << barrier_.dst_stage_mask
                << " access=" << barrier_.src_access_mask << "->" << barrier_.dst_access_mask;
            if (barrier_.kind == ResourceKind::texture) {
                oss << " layout=" << LayoutToString(barrier_.old_layout)
                    << "->" << LayoutToString(barrier_.new_layout);
            }
            if (barrier_.queue_transfer) {
                oss << " queue_family=" << barrier_.src_queue_family_index
                    << "->" << barrier_.dst_queue_family_index;
            }
            if (barrier_.uav_ordering) {
                oss << " uav";
            }
            if (barrier_.host_boundary) {
                oss << " host_boundary";
            }
            if (barrier_.aliasing) {
                oss << " alias";
            }
            oss << '\n';
        }
    }
    return oss.str();
}

std::string VulkanBarrierPlan::BuildJson() const {
    std::ostringstream oss{};
    oss << "{\n";
    oss << "  \"barrierBatches\": [\n";
    for (std::size_t batch_index = 0; batch_index < barrier_batches.size(); ++batch_index) {
        const auto& batch_ = barrier_batches[batch_index];
        oss << "    {\n";
        oss << "      \"pass\": " << batch_.pass.index << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(batch_.queue) << "\",\n";
        oss << "      \"barriers\": [";
        for (std::size_t barrier_index = 0; barrier_index < batch_.barriers.size(); ++barrier_index) {
            const auto& barrier_ = batch_.barriers[barrier_index];
            if (barrier_index != 0U) {
                oss << ", ";
            }
            oss << "{\"resourceIndex\": " << barrier_.resource.resource_index
                << ", \"version\": " << barrier_.resource.version
                << ", \"name\": \"" << barrier_.debug_name << "\""
                << ", \"kind\": \"" << (barrier_.kind == ResourceKind::texture ? "texture" : "buffer") << "\""
                << ", \"before\": \"" << AccessKindToString(barrier_.before) << "\""
                << ", \"after\": \"" << AccessKindToString(barrier_.after) << "\""
                << ", \"srcQueue\": \"" << QueueClassToString(barrier_.src_queue) << "\""
                << ", \"dstQueue\": \"" << QueueClassToString(barrier_.dst_queue) << "\""
                << ", \"srcStageMask\": " << barrier_.src_stage_mask
                << ", \"srcAccessMask\": " << barrier_.src_access_mask
                << ", \"dstStageMask\": " << barrier_.dst_stage_mask
                << ", \"dstAccessMask\": " << barrier_.dst_access_mask
                << ", \"oldLayout\": \"" << LayoutToString(barrier_.old_layout) << "\""
                << ", \"newLayout\": \"" << LayoutToString(barrier_.new_layout) << "\""
                << ", \"queueTransfer\": " << (barrier_.queue_transfer ? "true" : "false")
                << ", \"hostBoundary\": " << (barrier_.host_boundary ? "true" : "false")
                << ", \"uavOrdering\": " << (barrier_.uav_ordering ? "true" : "false")
                << ", \"srcQueueFamilyIndex\": " << barrier_.src_queue_family_index
                << ", \"dstQueueFamilyIndex\": " << barrier_.dst_queue_family_index;
            if (barrier_.kind == ResourceKind::texture) {
                oss << ", \"subresourceRange\": {"
                    << "\"baseMipLevel\": " << barrier_.subresource_range.base_mip_level
                    << ", \"levelCount\": " << barrier_.subresource_range.level_count
                    << ", \"baseArrayLayer\": " << barrier_.subresource_range.base_array_layer
                    << ", \"layerCount\": " << barrier_.subresource_range.layer_count << "}";
            } else {
                oss << ", \"bufferRange\": {"
                    << "\"offsetBytes\": " << barrier_.buffer_range.offset_bytes
                    << ", \"sizeBytes\": " << barrier_.buffer_range.size_bytes << "}";
            }
            oss << '}';
        }
        oss << "]\n";
        oss << "    }";
        if (batch_index + 1U != barrier_batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

std::string VulkanCommandReadyPlan::BuildDebugString() const {
    std::ostringstream oss{};
    oss << "command_batches=" << command_batches.size() << '\n';
    for (const auto& batch_ : command_batches) {
        oss << "pass=" << batch_.pass.index
            << " queue=" << QueueClassToString(batch_.queue)
            << " barriers=" << batch_.barriers.size()
            << " image=" << batch_.dependency.image_barriers.size()
            << " buffer=" << batch_.dependency.buffer_barriers.size() << '\n';
    }

    oss << "queue_transfer_batches=" << queue_transfer_batches.size() << '\n';
    for (const auto& batch_ : queue_transfer_batches) {
        oss << "source_pass=" << batch_.source_pass.index
            << " target_pass=" << batch_.target_pass.index
            << " queues=" << QueueClassToString(batch_.source_queue)
            << "->" << QueueClassToString(batch_.target_queue)
            << " barriers=" << batch_.barriers.size()
            << " release_image=" << batch_.release_dependency.image_barriers.size()
            << " acquire_image=" << batch_.acquire_dependency.image_barriers.size()
            << " release_buffer=" << batch_.release_dependency.buffer_barriers.size()
            << " acquire_buffer=" << batch_.acquire_dependency.buffer_barriers.size() << '\n';
    }
    return oss.str();
}

std::string VulkanCommandReadyPlan::BuildJson() const {
    std::ostringstream oss{};
    oss << "{\n";
    oss << "  \"commandBatches\": [\n";
    for (std::size_t batch_index = 0; batch_index < command_batches.size(); ++batch_index) {
        const auto& batch_ = command_batches[batch_index];
        oss << "    {\n";
        oss << "      \"pass\": " << batch_.pass.index << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(batch_.queue) << "\",\n";
        oss << "      \"barriers\": " << batch_.barriers.size() << ",\n";
        oss << "      \"dependency\": ";
        AppendDependencyInfoJson(oss, batch_.dependency);
        oss << '\n';
        oss << "    }";
        if (batch_index + 1U != command_batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"queueTransferBatches\": [\n";
    for (std::size_t batch_index = 0; batch_index < queue_transfer_batches.size(); ++batch_index) {
        const auto& batch_ = queue_transfer_batches[batch_index];
        oss << "    {\n";
        oss << "      \"sourcePass\": " << batch_.source_pass.index << ",\n";
        oss << "      \"targetPass\": " << batch_.target_pass.index << ",\n";
        oss << "      \"sourceQueue\": \"" << QueueClassToString(batch_.source_queue) << "\",\n";
        oss << "      \"targetQueue\": \"" << QueueClassToString(batch_.target_queue) << "\",\n";
        oss << "      \"barriers\": " << batch_.barriers.size() << ",\n";
        oss << "      \"releaseDependency\": ";
        AppendDependencyInfoJson(oss, batch_.release_dependency);
        oss << ",\n";
        oss << "      \"acquireDependency\": ";
        AppendDependencyInfoJson(oss, batch_.acquire_dependency);
        oss << '\n';
        oss << "    }";
        if (batch_index + 1U != queue_transfer_batches.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

VulkanBarrierPlan LowerToVulkanBarrierPlan(const CompiledRenderGraph& compiled_graph_,
                                           const QueueFamilyIndices& queue_families_) {
    VulkanBarrierPlan plan{};

    for (const auto& batch_ : compiled_graph_.PlannedBarriers().barrier_batches) {
        VulkanBarrierBatch lowered_batch{
            .pass = batch_.pass,
            .queue = batch_.queue,
        };

        for (const auto& barrier_ : batch_.barriers) {
            const auto* resource_ = compiled_graph_.FindResource(ResourceHandle{
                .index = barrier_.resource.resource_index,
                .generation = 1U,
            });
            if (resource_ == nullptr) {
                continue;
            }

            const auto before_ = DescribeVulkanAccess(*resource_, barrier_.before);
            const auto after_ = DescribeVulkanAccess(*resource_, barrier_.after);
            lowered_batch.barriers.push_back(LoweredVulkanBarrier{
                .resource = barrier_.resource,
                .debug_name = resource_->debug_name,
                .kind = resource_->kind,
                .before = barrier_.before,
                .after = barrier_.after,
                .src_queue = barrier_.src_queue,
                .dst_queue = barrier_.dst_queue,
                .src_pass = barrier_.src_pass,
                .dst_pass = barrier_.dst_pass,
                .src_stage_mask = before_.stage_mask,
                .src_access_mask = before_.access_mask,
                .dst_stage_mask = after_.stage_mask,
                .dst_access_mask = after_.access_mask,
                .old_layout = (resource_->kind == ResourceKind::texture)
                    ? (barrier_.aliasing ? VK_IMAGE_LAYOUT_UNDEFINED : before_.image_layout)
                    : VK_IMAGE_LAYOUT_UNDEFINED,
                .new_layout = (resource_->kind == ResourceKind::texture) ? after_.image_layout : VK_IMAGE_LAYOUT_UNDEFINED,
                .src_queue_family_index = barrier_.queue_transfer
                    ? ResolveQueueFamilyIndex(queue_families_, barrier_.src_queue)
                    : VK_QUEUE_FAMILY_IGNORED,
                .dst_queue_family_index = barrier_.queue_transfer
                    ? ResolveQueueFamilyIndex(queue_families_, barrier_.dst_queue)
                    : VK_QUEUE_FAMILY_IGNORED,
                .subresource_range = barrier_.subresource_range,
                .buffer_range = barrier_.buffer_range,
                .queue_transfer = barrier_.queue_transfer,
                .host_boundary = barrier_.host_boundary,
                .aliasing = barrier_.aliasing,
                .uav_ordering = barrier_.uav_ordering,
            });
        }

        if (!lowered_batch.barriers.empty()) {
            plan.barrier_batches.push_back(std::move(lowered_batch));
        }
    }

    return plan;
}

VulkanCommandReadyPlan BuildCommandReadyVulkanBarrierPlan(
    const VulkanBarrierPlan& lowered_plan_,
    const VulkanResourceTable& resource_table_,
    const render::RenderTargetHost& render_target_host_) {
    VulkanCommandReadyPlan plan{};

    for (const auto& batch_ : lowered_plan_.barrier_batches) {
        VulkanCommandBarrierBatch* command_batch = FindCommandBatch(plan.command_batches,
                                                                    batch_.pass,
                                                                    batch_.queue);
        if (command_batch == nullptr) {
            plan.command_batches.push_back(VulkanCommandBarrierBatch{
                .pass = batch_.pass,
                .queue = batch_.queue,
            });
            command_batch = &plan.command_batches.back();
        }

        for (const auto& barrier_ : batch_.barriers) {
            const ResourceHandle logical{
                .index = barrier_.resource.resource_index,
                .generation = 1U,
            };

            const bool needs_queue_dependency = barrier_.queue_transfer &&
                                               (barrier_.aliasing ||
                                                barrier_.src_queue_family_index != barrier_.dst_queue_family_index);
            if (!needs_queue_dependency) {
                if (barrier_.aliasing) {
                    if (barrier_.kind == ResourceKind::texture) {
                        VkImageMemoryBarrier2 image_barrier{};
                        if (!TryBuildImageBarrier(resource_table_,
                                                  render_target_host_,
                                                  logical,
                                                  barrier_,
                                                  barrier_.src_stage_mask,
                                                  barrier_.src_access_mask,
                                                  barrier_.dst_stage_mask,
                                                  barrier_.dst_access_mask,
                                                  VK_QUEUE_FAMILY_IGNORED,
                                                  VK_QUEUE_FAMILY_IGNORED,
                                                  image_barrier)) {
                            continue;
                        }
                        command_batch->dependency.image_barriers.push_back(image_barrier);
                    } else {
                        VkMemoryBarrier2 memory_barrier{};
                        memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                        memory_barrier.srcStageMask = barrier_.src_stage_mask;
                        memory_barrier.srcAccessMask = barrier_.src_access_mask;
                        memory_barrier.dstStageMask = barrier_.dst_stage_mask;
                        memory_barrier.dstAccessMask = barrier_.dst_access_mask;
                        command_batch->dependency.memory_barriers.push_back(memory_barrier);
                    }
                    command_batch->barriers.push_back(barrier_);
                    continue;
                }

                if (barrier_.kind == ResourceKind::texture) {
                    VkImageMemoryBarrier2 image_barrier{};
                    if (!TryBuildImageBarrier(resource_table_,
                                              render_target_host_,
                                              logical,
                                              barrier_,
                                              barrier_.src_stage_mask,
                                              barrier_.src_access_mask,
                                              barrier_.dst_stage_mask,
                                              barrier_.dst_access_mask,
                                              VK_QUEUE_FAMILY_IGNORED,
                                              VK_QUEUE_FAMILY_IGNORED,
                                              image_barrier)) {
                        continue;
                    }
                    command_batch->dependency.image_barriers.push_back(image_barrier);
                } else {
                    const auto* buffer_ = resource_table_.FindBuffer(logical);
                    if (buffer_ == nullptr) {
                        continue;
                    }
                    const VkBuffer buffer_handle = ResolveBufferHandle(*buffer_);
                    if (buffer_handle == VK_NULL_HANDLE) {
                        continue;
                    }
                    VkBufferMemoryBarrier2 buffer_barrier{};
                    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    buffer_barrier.srcStageMask = barrier_.src_stage_mask;
                    buffer_barrier.srcAccessMask = barrier_.src_access_mask;
                    buffer_barrier.dstStageMask = barrier_.dst_stage_mask;
                    buffer_barrier.dstAccessMask = barrier_.dst_access_mask;
                    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    buffer_barrier.buffer = buffer_handle;
                    buffer_barrier.offset = barrier_.buffer_range.offset_bytes;
                    buffer_barrier.size = ResolveBufferSize(*buffer_, barrier_.buffer_range);
                    command_batch->dependency.buffer_barriers.push_back(buffer_barrier);
                }
                command_batch->barriers.push_back(barrier_);
                continue;
            }

            VulkanQueueTransferBatch* transfer_batch = FindQueueTransferBatch(plan.queue_transfer_batches,
                                                                              barrier_.src_pass,
                                                                              barrier_.dst_pass,
                                                                              barrier_.src_queue,
                                                                              barrier_.dst_queue);
            if (transfer_batch == nullptr) {
                plan.queue_transfer_batches.push_back(VulkanQueueTransferBatch{
                    .source_pass = barrier_.src_pass,
                    .target_pass = barrier_.dst_pass,
                    .source_queue = barrier_.src_queue,
                    .target_queue = barrier_.dst_queue,
                });
                transfer_batch = &plan.queue_transfer_batches.back();
            }

            if (barrier_.aliasing) {
                if (barrier_.kind == ResourceKind::texture) {
                    VkImageMemoryBarrier2 release_barrier{};
                    if (!TryBuildImageBarrier(resource_table_,
                                              render_target_host_,
                                              logical,
                                              barrier_,
                                              barrier_.src_stage_mask,
                                              barrier_.src_access_mask,
                                              VK_PIPELINE_STAGE_2_NONE,
                                              VK_ACCESS_2_NONE,
                                              barrier_.src_queue_family_index,
                                              barrier_.dst_queue_family_index,
                                              release_barrier)) {
                        continue;
                    }
                    transfer_batch->release_dependency.image_barriers.push_back(release_barrier);

                    VkImageMemoryBarrier2 acquire_barrier = release_barrier;
                    acquire_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                    acquire_barrier.srcAccessMask = VK_ACCESS_2_NONE;
                    acquire_barrier.dstStageMask = barrier_.dst_stage_mask;
                    acquire_barrier.dstAccessMask = barrier_.dst_access_mask;
                    transfer_batch->acquire_dependency.image_barriers.push_back(acquire_barrier);
                } else {
                    VkMemoryBarrier2 release_barrier{};
                    release_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                    release_barrier.srcStageMask = barrier_.src_stage_mask;
                    release_barrier.srcAccessMask = barrier_.src_access_mask;
                    release_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
                    release_barrier.dstAccessMask = VK_ACCESS_2_NONE;
                    transfer_batch->release_dependency.memory_barriers.push_back(release_barrier);

                    VkMemoryBarrier2 acquire_barrier{};
                    acquire_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                    acquire_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                    acquire_barrier.srcAccessMask = VK_ACCESS_2_NONE;
                    acquire_barrier.dstStageMask = barrier_.dst_stage_mask;
                    acquire_barrier.dstAccessMask = barrier_.dst_access_mask;
                    transfer_batch->acquire_dependency.memory_barriers.push_back(acquire_barrier);
                }
                transfer_batch->barriers.push_back(barrier_);
                continue;
            }

            if (barrier_.kind == ResourceKind::texture) {
                VkImageMemoryBarrier2 release_barrier{};
                if (!TryBuildImageBarrier(resource_table_,
                                          render_target_host_,
                                          logical,
                                          barrier_,
                                          barrier_.src_stage_mask,
                                          barrier_.src_access_mask,
                                          VK_PIPELINE_STAGE_2_NONE,
                                          VK_ACCESS_2_NONE,
                                          barrier_.src_queue_family_index,
                                          barrier_.dst_queue_family_index,
                                          release_barrier)) {
                    continue;
                }
                transfer_batch->release_dependency.image_barriers.push_back(release_barrier);

                VkImageMemoryBarrier2 acquire_barrier = release_barrier;
                acquire_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                acquire_barrier.srcAccessMask = VK_ACCESS_2_NONE;
                acquire_barrier.dstStageMask = barrier_.dst_stage_mask;
                acquire_barrier.dstAccessMask = barrier_.dst_access_mask;
                transfer_batch->acquire_dependency.image_barriers.push_back(acquire_barrier);
            } else {
                const auto* buffer_ = resource_table_.FindBuffer(logical);
                if (buffer_ == nullptr) {
                    continue;
                }
                const VkBuffer buffer_handle = ResolveBufferHandle(*buffer_);
                if (buffer_handle == VK_NULL_HANDLE) {
                    continue;
                }

                VkBufferMemoryBarrier2 release_barrier{};
                release_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                release_barrier.srcStageMask = barrier_.src_stage_mask;
                release_barrier.srcAccessMask = barrier_.src_access_mask;
                release_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
                release_barrier.dstAccessMask = VK_ACCESS_2_NONE;
                release_barrier.srcQueueFamilyIndex = barrier_.src_queue_family_index;
                release_barrier.dstQueueFamilyIndex = barrier_.dst_queue_family_index;
                release_barrier.buffer = buffer_handle;
                release_barrier.offset = barrier_.buffer_range.offset_bytes;
                release_barrier.size = ResolveBufferSize(*buffer_, barrier_.buffer_range);
                transfer_batch->release_dependency.buffer_barriers.push_back(release_barrier);

                VkBufferMemoryBarrier2 acquire_barrier = release_barrier;
                acquire_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                acquire_barrier.srcAccessMask = VK_ACCESS_2_NONE;
                acquire_barrier.dstStageMask = barrier_.dst_stage_mask;
                acquire_barrier.dstAccessMask = barrier_.dst_access_mask;
                transfer_batch->acquire_dependency.buffer_barriers.push_back(acquire_barrier);
            }
            transfer_batch->barriers.push_back(barrier_);
        }
    }

    plan.command_batches.erase(
        std::remove_if(plan.command_batches.begin(),
                       plan.command_batches.end(),
                       [](const VulkanCommandBarrierBatch& batch_) {
                           return batch_.dependency.Empty();
                       }),
        plan.command_batches.end());

    plan.queue_transfer_batches.erase(
        std::remove_if(plan.queue_transfer_batches.begin(),
                       plan.queue_transfer_batches.end(),
                       [](const VulkanQueueTransferBatch& batch_) {
                           return batch_.release_dependency.Empty() && batch_.acquire_dependency.Empty();
                       }),
        plan.queue_transfer_batches.end());

    return plan;
}

} // namespace vr::render_graph
