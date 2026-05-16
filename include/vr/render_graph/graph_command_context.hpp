#pragma once

#include "vr/render/render_pass_preset.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/render_graph/vulkan_barrier_plan.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"
#include "vr/vulkan_context.hpp"

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
                        const VkCommandBuffer command_buffer_,
                        const CompiledRenderGraph& compiled_graph_,
                        const VulkanResourceTable& physical_resources_,
                        render::RenderTargetHost& render_target_host_,
                        const VulkanBarrierPlan& lowered_vulkan_barriers_,
                        const VulkanCommandReadyPlan& command_ready_vulkan_barriers_) noexcept
        : device(&device_),
          command_buffer(command_buffer_),
          compiled_graph(&compiled_graph_),
          physical_resources(&physical_resources_),
          render_target_host(&render_target_host_),
          lowered_vulkan_barriers(&lowered_vulkan_barriers_),
          command_ready_vulkan_barriers(&command_ready_vulkan_barriers_) {}

    [[nodiscard]] VulkanContext& Device() const noexcept {
        return *device;
    }

    [[nodiscard]] VkCommandBuffer CommandBuffer() const noexcept {
        return command_buffer;
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

    void BeginRendering(const render::RenderTargetRenderingInfo& rendering_info_) const noexcept {
        vkCmdBeginRendering(command_buffer, rendering_info_.VkInfoPtr());
    }

    void EndRendering() const noexcept {
        vkCmdEndRendering(command_buffer);
    }

private:
    VulkanContext* device = nullptr;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    const CompiledRenderGraph* compiled_graph = nullptr;
    const VulkanResourceTable* physical_resources = nullptr;
    render::RenderTargetHost* render_target_host = nullptr;
    const VulkanBarrierPlan* lowered_vulkan_barriers = nullptr;
    const VulkanCommandReadyPlan* command_ready_vulkan_barriers = nullptr;
};

} // namespace vr::render_graph
