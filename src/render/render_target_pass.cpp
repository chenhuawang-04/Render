#include "vr/render/render_target_pass.hpp"

#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] VkAttachmentLoadOp ResolveLoadOp(const RenderTargetColorOutputConfig& output_config_,
                                               bool default_clear_target_,
                                               bool has_previous_content_) noexcept {
    if (output_config_.use_explicit_load_op) {
        return output_config_.load_op;
    }
    return (default_clear_target_ || !has_previous_content_)
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
}

void RecordLegacySwapchainTransitionToColorAttachment(const FrameRecordContext& record_context_,
                                                      bool has_previous_content_) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = has_previous_content_ ? VK_ACCESS_MEMORY_READ_BIT : 0U;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = has_previous_content_ ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record_context_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(record_context_.command_buffer,
                         has_previous_content_
                             ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

void RecordLegacySwapchainTransitionToPresent(const FrameRecordContext& record_context_) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record_context_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = 1U;

    vkCmdPipelineBarrier(record_context_.command_buffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0U,
                         0U,
                         nullptr,
                         0U,
                         nullptr,
                         1U,
                         &barrier);
}

} // namespace

ResolvedColorRenderTarget ResolveColorRenderTarget(const FrameRecordContext& record_context_,
                                                   const RenderTargetColorOutputConfig& output_config_) {
    ResolvedColorRenderTarget resolved{};
    RenderTargetHost* render_target_host = record_context_.render_target_host;

    RenderTargetHandle handle = output_config_.color_target;
    if (!IsValidRenderTargetHandle(handle)) {
        handle = record_context_.swapchain_target_handle;
    }

    if (render_target_host != nullptr && IsValidRenderTargetHandle(handle)) {
        const RenderTargetResolvedView resolved_view = render_target_host->ResolveView(handle);
        resolved.handle = handle;
        resolved.extent = VkExtent2D{resolved_view.extent.width, resolved_view.extent.height};
        resolved.format = resolved_view.format;
        resolved.image = resolved_view.image;
        resolved.image_view = resolved_view.image_view;
        resolved.using_render_target_host = true;
        resolved.present_after_pass =
            !IsValidRenderTargetHandle(output_config_.color_target) &&
            handle.index == record_context_.swapchain_target_handle.index &&
            handle.generation == record_context_.swapchain_target_handle.generation;
        return resolved;
    }

    if (IsValidRenderTargetHandle(output_config_.color_target)) {
        throw std::runtime_error(
            "ResolveColorRenderTarget explicit color_target requires render_target_host in frame context");
    }

    resolved.handle = invalid_render_target_handle;
    resolved.extent = record_context_.extent;
    resolved.format = record_context_.format;
    resolved.image = record_context_.image;
    resolved.image_view = record_context_.image_view;
    resolved.using_render_target_host = false;
    resolved.present_after_pass = true;
    return resolved;
}

ResolvedColorRenderPass BuildColorRenderPass(const FrameRecordContext& record_context_,
                                             const RenderTargetColorOutputConfig& output_config_,
                                             bool default_clear_target_,
                                             const VkClearColorValue& default_clear_color_,
                                             bool has_previous_content_) {
    if (record_context_.command_buffer == VK_NULL_HANDLE) {
        throw std::invalid_argument("BuildColorRenderPass requires valid command buffer");
    }

    ResolvedColorRenderPass pass{};
    pass.target = ResolveColorRenderTarget(record_context_, output_config_);
    pass.effective_load_op = ResolveLoadOp(output_config_, default_clear_target_, has_previous_content_);

    if (pass.target.extent.width == 0U || pass.target.extent.height == 0U) {
        throw std::runtime_error("BuildColorRenderPass resolved zero-sized color target");
    }
    if (pass.target.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("BuildColorRenderPass resolved null color target view");
    }

    if (pass.target.using_render_target_host) {
        AttachmentRef color_attachment{};
        color_attachment.target = pass.target.handle;
        color_attachment.load_op = pass.effective_load_op;
        color_attachment.store_op = output_config_.store_op;
        color_attachment.clear_value.color = output_config_.use_explicit_load_op
            ? output_config_.clear_color
            : default_clear_color_;
        color_attachment.expected_state = RenderTargetStateKind::color_attachment;

        if (pass.effective_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            color_attachment.clear_value.color = output_config_.clear_color;
            if (!output_config_.use_explicit_load_op) {
                color_attachment.clear_value.color = default_clear_color_;
            }
        }

        record_context_.render_target_host->RecordTransitionsForRendering(
            record_context_.command_buffer,
            &color_attachment,
            1U);
        VkRect2D render_area{};
        render_area.offset = VkOffset2D{0, 0};
        render_area.extent = pass.target.extent;
        pass.rendering_info = record_context_.render_target_host->BuildRenderingInfo(
            render_area,
            1U,
            &color_attachment,
            1U);
        return pass;
    }

    RecordLegacySwapchainTransitionToColorAttachment(record_context_, has_previous_content_);

    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = pass.target.image_view;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
    color_attachment.resolveImageView = VK_NULL_HANDLE;
    color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.loadOp = pass.effective_load_op;
    color_attachment.storeOp = output_config_.store_op;
    color_attachment.clearValue.color = output_config_.use_explicit_load_op
        ? output_config_.clear_color
        : default_clear_color_;

    pass.rendering_info.color_attachment_count = 1U;
    pass.rendering_info.color_attachments[0] = color_attachment;
    pass.rendering_info.vk.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    pass.rendering_info.vk.renderArea.offset = VkOffset2D{0, 0};
    pass.rendering_info.vk.renderArea.extent = pass.target.extent;
    pass.rendering_info.vk.layerCount = 1U;
    pass.rendering_info.vk.viewMask = 0U;
    pass.rendering_info.vk.colorAttachmentCount = 1U;
    pass.rendering_info.vk.pColorAttachments = pass.rendering_info.color_attachments.data();
    pass.rendering_info.vk.pDepthAttachment = nullptr;
    pass.rendering_info.vk.pStencilAttachment = nullptr;
    return pass;
}

void RecordEndColorPass(const FrameRecordContext& record_context_,
                        const RenderTargetColorOutputConfig& output_config_) {
    const ResolvedColorRenderTarget target = ResolveColorRenderTarget(record_context_, output_config_);
    if (target.using_render_target_host) {
        const RenderTargetStateKind final_state = target.present_after_pass
            ? RenderTargetStateKind::present_src
            : output_config_.final_state;
        record_context_.render_target_host->RecordTransition(record_context_.command_buffer,
                                                             target.handle,
                                                             final_state);
        return;
    }

    if (target.present_after_pass) {
        RecordLegacySwapchainTransitionToPresent(record_context_);
    }
}

} // namespace vr::render
