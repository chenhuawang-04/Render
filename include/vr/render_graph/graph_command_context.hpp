#pragma once

#include "vr/render/render_pass_preset.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render_graph/native_pass_plan.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/render_graph/vulkan_barrier_plan.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"
#include "vr/vulkan_context.hpp"

#include <stdexcept>
#include <vector>

namespace vr::render_graph {

class CompiledRenderGraph;

[[nodiscard]] constexpr VkAttachmentLoadOp ToVkAttachmentLoadOp(const AttachmentLoadOp load_op_) noexcept {
    switch (load_op_) {
    case AttachmentLoadOp::load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case AttachmentLoadOp::clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case AttachmentLoadOp::dont_care:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default:
        break;
    }
    return VK_ATTACHMENT_LOAD_OP_LOAD;
}

[[nodiscard]] constexpr VkAttachmentStoreOp ToVkAttachmentStoreOp(const AttachmentStoreOp store_op_) noexcept {
    switch (store_op_) {
    case AttachmentStoreOp::store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case AttachmentStoreOp::dont_care:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    default:
        break;
    }
    return VK_ATTACHMENT_STORE_OP_STORE;
}

class GraphCommandContext final {
public:
    GraphCommandContext(VulkanContext& device_,
                        const std::uint32_t frame_index_,
                        const VkCommandBuffer command_buffer_,
                        const CompiledRenderGraph& compiled_graph_,
                        const VulkanResourceTable& physical_resources_,
                        render::RenderTargetHost& render_target_host_,
                        render::DescriptorHost* descriptor_host_,
                        const VulkanBarrierPlan& lowered_vulkan_barriers_,
                        const VulkanCommandReadyPlan& command_ready_vulkan_barriers_) noexcept
        : device(&device_),
          frame_index(frame_index_),
          command_buffer(command_buffer_),
          compiled_graph(&compiled_graph_),
          physical_resources(&physical_resources_),
          render_target_host(&render_target_host_),
          descriptor_host(descriptor_host_),
          lowered_vulkan_barriers(&lowered_vulkan_barriers_),
          command_ready_vulkan_barriers(&command_ready_vulkan_barriers_) {}

    [[nodiscard]] VulkanContext& Device() const noexcept {
        return *device;
    }

    [[nodiscard]] VkCommandBuffer CommandBuffer() const noexcept {
        return command_buffer;
    }

    [[nodiscard]] std::uint32_t FrameIndex() const noexcept {
        return frame_index;
    }

    [[nodiscard]] const CompiledRenderGraph& Graph() const noexcept {
        return *compiled_graph;
    }

    [[nodiscard]] const VulkanResourceTable& PhysicalResources() const noexcept {
        return *physical_resources;
    }

    [[nodiscard]] render::RenderTargetHost& RenderTargets() const noexcept {
        return *render_target_host;
    }

    [[nodiscard]] bool HasDescriptorHost() const noexcept {
        return descriptor_host != nullptr;
    }

    [[nodiscard]] render::DescriptorHost& Descriptors() const {
        if (descriptor_host == nullptr) {
            throw std::runtime_error("GraphCommandContext does not have DescriptorHost bound");
        }
        return *descriptor_host;
    }

    [[nodiscard]] const VulkanBarrierPlan& LoweredBarriers() const noexcept {
        return *lowered_vulkan_barriers;
    }

    [[nodiscard]] const VulkanCommandReadyPlan& CommandReadyBarriers() const noexcept {
        return *command_ready_vulkan_barriers;
    }

    [[nodiscard]] const PhysicalTextureRecord* FindTexture(const ResourceHandle logical_) const noexcept {
        return physical_resources->FindTexture(logical_);
    }

    [[nodiscard]] const PhysicalBufferRecord* FindBuffer(const ResourceHandle logical_) const noexcept {
        return physical_resources->FindBuffer(logical_);
    }

    [[nodiscard]] render::RenderTargetHandle ResolveTextureTarget(const ResourceHandle logical_) const noexcept {
        const auto* texture_ = FindTexture(logical_);
        return (texture_ != nullptr) ? texture_->render_target : render::invalid_render_target_handle;
    }

    [[nodiscard]] render::RenderTargetResolvedView ResolveTextureView(const ResourceHandle logical_) const {
        return render_target_host->ResolveView(ResolveTextureTarget(logical_));
    }

    [[nodiscard]] render::RenderTargetRenderingInfo BuildRenderingInfo(
        const VkRect2D render_area_,
        const render::AttachmentRef* color_attachments_,
        const std::uint32_t color_attachment_count_,
        const render::AttachmentRef* depth_attachment_ = nullptr,
        const render::AttachmentRef* stencil_attachment_ = nullptr,
        const std::uint32_t layer_count_ = 1U) const {
        return render_target_host->BuildRenderingInfo(render_area_,
                                                      layer_count_,
                                                      color_attachments_,
                                                      color_attachment_count_,
                                                      depth_attachment_,
                                                      stencil_attachment_);
    }

    [[nodiscard]] render::RenderTargetRenderingInfo BuildRenderingInfo(
        const RasterPassDesc& raster_pass_) const {
        std::vector<render::AttachmentRef> color_attachments{};
        color_attachments.reserve(raster_pass_.color_attachments.size());

        render::AttachmentRef depth_attachment{};
        const render::AttachmentRef* depth_attachment_ptr = nullptr;
        const render::AttachmentRef* stencil_attachment_ptr = nullptr;
        VkRect2D render_area{};
        bool render_area_initialized = false;

        auto set_render_area_from = [&](const ResourceHandle target_) {
            if (render_area_initialized) {
                return;
            }
            const auto resolved = ResolveTextureView(target_);
            render_area.offset = VkOffset2D{0, 0};
            render_area.extent = VkExtent2D{resolved.extent.width, resolved.extent.height};
            render_area_initialized = true;
        };

        for (const auto& color_attachment_ : raster_pass_.color_attachments) {
            render::AttachmentRef attachment{};
            attachment.target = ResolveTextureTarget(color_attachment_.target);
            attachment.load_op = ToVkAttachmentLoadOp(color_attachment_.load_op);
            attachment.store_op = ToVkAttachmentStoreOp(color_attachment_.store_op);
            attachment.clear_value.color = VkClearColorValue{{
                color_attachment_.clear_value.red,
                color_attachment_.clear_value.green,
                color_attachment_.clear_value.blue,
                color_attachment_.clear_value.alpha,
            }};
            attachment.expected_state = render::RenderTargetStateKind::color_attachment;
            color_attachments.push_back(attachment);
            set_render_area_from(color_attachment_.target);
        }

        if (raster_pass_.has_depth_attachment) {
            depth_attachment.target = ResolveTextureTarget(raster_pass_.depth_attachment.target);
            depth_attachment.load_op = ToVkAttachmentLoadOp(raster_pass_.depth_attachment.load_op);
            depth_attachment.store_op = ToVkAttachmentStoreOp(raster_pass_.depth_attachment.store_op);
            depth_attachment.stencil_load_op = ToVkAttachmentLoadOp(raster_pass_.depth_attachment.stencil_load_op);
            depth_attachment.stencil_store_op = ToVkAttachmentStoreOp(raster_pass_.depth_attachment.stencil_store_op);
            depth_attachment.clear_value.depthStencil = VkClearDepthStencilValue{
                raster_pass_.depth_attachment.clear_value.depth,
                raster_pass_.depth_attachment.clear_value.stencil,
            };
            depth_attachment.expected_state = raster_pass_.depth_attachment.read_only
                ? render::RenderTargetStateKind::depth_read_only
                : render::RenderTargetStateKind::depth_attachment;
            depth_attachment_ptr = &depth_attachment;
            set_render_area_from(raster_pass_.depth_attachment.target);
        }

        return BuildRenderingInfo(render_area,
                                  color_attachments.data(),
                                  static_cast<std::uint32_t>(color_attachments.size()),
                                  depth_attachment_ptr,
                                  stencil_attachment_ptr,
                                  raster_pass_.layer_count);
    }

    [[nodiscard]] render::RenderTargetRenderingInfo BuildRenderingInfo(
        const NativePassGroup& native_pass_group_) const {
        const auto& attachments_ = native_pass_group_.attachments;
        std::vector<render::AttachmentRef> color_attachments{};
        color_attachments.reserve(attachments_.color_attachments.size());

        render::AttachmentRef depth_attachment{};
        const render::AttachmentRef* depth_attachment_ptr = nullptr;
        const render::AttachmentRef* stencil_attachment_ptr = nullptr;
        VkRect2D render_area{};
        bool render_area_initialized = false;

        const auto set_render_area_from = [&](const ResourceHandle target_) {
            if (render_area_initialized) {
                return;
            }
            const auto resolved = ResolveTextureView(target_);
            render_area.offset = VkOffset2D{0, 0};
            render_area.extent = VkExtent2D{resolved.extent.width, resolved.extent.height};
            render_area_initialized = true;
        };

        for (std::size_t color_index = 0U;
             color_index < attachments_.color_attachments.size();
             ++color_index) {
            const auto& group_attachment_ =
                attachments_.color_attachments[color_index];

            render::AttachmentRef attachment{};
            attachment.target = ResolveTextureTarget(group_attachment_.target);
            attachment.load_op =
                ToVkAttachmentLoadOp(group_attachment_.effective_load_op);
            attachment.store_op =
                ToVkAttachmentStoreOp(group_attachment_.effective_store_op);
            attachment.clear_value.color = VkClearColorValue{{
                group_attachment_.clear_value.red,
                group_attachment_.clear_value.green,
                group_attachment_.clear_value.blue,
                group_attachment_.clear_value.alpha,
            }};
            attachment.expected_state = render::RenderTargetStateKind::color_attachment;
            color_attachments.push_back(attachment);
            set_render_area_from(group_attachment_.target);
        }

        if (attachments_.has_depth_attachment) {
            depth_attachment.target = ResolveTextureTarget(attachments_.depth_attachment.target);
            depth_attachment.load_op =
                ToVkAttachmentLoadOp(
                    attachments_.depth_attachment.effective_load_op);
            depth_attachment.store_op =
                ToVkAttachmentStoreOp(
                    attachments_.depth_attachment.effective_store_op);
            depth_attachment.stencil_load_op =
                ToVkAttachmentLoadOp(
                    attachments_.depth_attachment.effective_stencil_load_op);
            depth_attachment.stencil_store_op =
                ToVkAttachmentStoreOp(
                    attachments_.depth_attachment.effective_stencil_store_op);
            depth_attachment.clear_value.depthStencil = VkClearDepthStencilValue{
                attachments_.depth_attachment.clear_value.depth,
                attachments_.depth_attachment.clear_value.stencil,
            };
            depth_attachment.expected_state = attachments_.depth_attachment.read_only
                ? render::RenderTargetStateKind::depth_read_only
                : render::RenderTargetStateKind::depth_attachment;
            depth_attachment_ptr = &depth_attachment;
            set_render_area_from(attachments_.depth_attachment.target);
        }

        return BuildRenderingInfo(render_area,
                                  color_attachments.data(),
                                  static_cast<std::uint32_t>(color_attachments.size()),
                                  depth_attachment_ptr,
                                  stencil_attachment_ptr,
                                  attachments_.layer_count);
    }

    void BeginRendering(const render::RenderTargetRenderingInfo& rendering_info_) const noexcept {
        ++rendering_scope_count;
        vkCmdBeginRendering(command_buffer, rendering_info_.VkInfoPtr());
    }

    void EndRendering() const noexcept {
        vkCmdEndRendering(command_buffer);
    }

    void SetCurrentPass(const PassHandle handle_) noexcept {
        current_pass = handle_;
        current_pass_descriptor_sets.clear();
    }

    void ClearCurrentPass() noexcept {
        current_pass = invalid_pass_handle;
        current_pass_descriptor_sets.clear();
    }

    [[nodiscard]] PassHandle CurrentPass() const noexcept {
        return current_pass;
    }

    [[nodiscard]] std::uint32_t RenderingScopeCount() const noexcept {
        return rendering_scope_count;
    }

    void SetCurrentPassDescriptorSets(std::vector<VkDescriptorSet> descriptor_sets_) {
        current_pass_descriptor_sets = std::move(descriptor_sets_);
    }

    [[nodiscard]] std::uint32_t CurrentPassDescriptorSetCount() const noexcept {
        return static_cast<std::uint32_t>(current_pass_descriptor_sets.size());
    }

    [[nodiscard]] VkDescriptorSet CurrentPassDescriptorSet(const std::uint32_t set_index_) const {
        if (set_index_ >= current_pass_descriptor_sets.size()) {
            throw std::out_of_range(
                "GraphCommandContext current pass descriptor set index is out of range");
        }
        return current_pass_descriptor_sets[set_index_];
    }

    void BindCurrentPassDescriptorSets(const VkPipelineBindPoint bind_point_,
                                       const VkPipelineLayout pipeline_layout_,
                                       const std::uint32_t first_set_,
                                       const std::uint32_t set_count_) const {
        if (!IsValidPassHandle(current_pass)) {
            throw std::runtime_error(
                "GraphCommandContext::BindCurrentPassDescriptorSets requires an active pass");
        }
        if (set_count_ == 0U) {
            return;
        }
        if (first_set_ + set_count_ > current_pass_descriptor_sets.size()) {
            throw std::out_of_range(
                "GraphCommandContext::BindCurrentPassDescriptorSets range exceeds prepared descriptor sets");
        }
        for (std::uint32_t set_offset = 0U; set_offset < set_count_; ++set_offset) {
            if (current_pass_descriptor_sets[first_set_ + set_offset] == VK_NULL_HANDLE) {
                throw std::runtime_error(
                    "GraphCommandContext::BindCurrentPassDescriptorSets encountered an unprepared descriptor set");
            }
        }
        vkCmdBindDescriptorSets(command_buffer,
                                bind_point_,
                                pipeline_layout_,
                                first_set_,
                                set_count_,
                                current_pass_descriptor_sets.data() + first_set_,
                                0U,
                                nullptr);
    }

private:
    VulkanContext* device = nullptr;
    std::uint32_t frame_index = 0U;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    const CompiledRenderGraph* compiled_graph = nullptr;
    const VulkanResourceTable* physical_resources = nullptr;
    render::RenderTargetHost* render_target_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    const VulkanBarrierPlan* lowered_vulkan_barriers = nullptr;
    const VulkanCommandReadyPlan* command_ready_vulkan_barriers = nullptr;
    PassHandle current_pass = invalid_pass_handle;
    std::vector<VkDescriptorSet> current_pass_descriptor_sets{};
    mutable std::uint32_t rendering_scope_count = 0U;
};

} // namespace vr::render_graph
