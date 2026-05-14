#pragma once

#include "vr/render/render_loop_host.hpp"
#include "vr/render/render_target_host.hpp"

namespace vr::render {

struct RenderTargetColorOutputConfig final {
    RenderTargetHandle color_target{};
    RenderTargetStateKind final_state = RenderTargetStateKind::present_src;
    bool use_explicit_load_op = false;
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearColorValue clear_color{};
};

struct RenderTargetDepthOutputConfig final {
    RenderTargetHandle depth_target{};
    RenderTargetStateKind final_state = RenderTargetStateKind::depth_attachment;
    bool use_explicit_load_op = false;
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkClearDepthStencilValue clear_depth_stencil{1.0F, 0U};
};

struct ResolvedColorRenderTarget final {
    RenderTargetHandle handle{};
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    bool using_render_target_host = false;
    bool present_after_pass = false;
};

struct ResolvedDepthRenderTarget final {
    RenderTargetHandle handle{};
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    bool using_render_target_host = false;
};

struct ResolvedColorRenderPass final {
    ResolvedColorRenderTarget target{};
    ResolvedDepthRenderTarget depth_target{};
    RenderTargetRenderingInfo rendering_info{};
    VkAttachmentLoadOp effective_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentLoadOp effective_depth_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    bool has_depth_target = false;
};

[[nodiscard]] ResolvedColorRenderTarget ResolveColorRenderTarget(
    const FrameRecordContext& record_context_,
    const RenderTargetColorOutputConfig& output_config_);

[[nodiscard]] ResolvedColorRenderPass BuildColorRenderPass(
    const FrameRecordContext& record_context_,
    const RenderTargetColorOutputConfig& output_config_,
    bool default_clear_target_,
    const VkClearColorValue& default_clear_color_,
    bool has_previous_content_);

[[nodiscard]] ResolvedColorRenderPass BuildColorDepthRenderPass(
    const FrameRecordContext& record_context_,
    const RenderTargetColorOutputConfig& color_output_config_,
    const RenderTargetDepthOutputConfig& depth_output_config_,
    bool default_clear_color_target_,
    const VkClearColorValue& default_clear_color_,
    bool has_previous_color_content_,
    bool has_previous_depth_content_);

void RecordEndColorPass(const FrameRecordContext& record_context_,
                        const RenderTargetColorOutputConfig& output_config_);

void RecordEndColorDepthPass(const FrameRecordContext& record_context_,
                             const RenderTargetColorOutputConfig& color_output_config_,
                             const RenderTargetDepthOutputConfig& depth_output_config_);

} // namespace vr::render

