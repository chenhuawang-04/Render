#pragma once

#include "vr/render/render_target_view.hpp"

#include <array>

namespace vr::render {

struct RenderTargetPipelineSignature final {
    std::array<VkFormat, k_max_render_target_color_attachments> color_formats{};
    std::uint32_t color_attachment_count = 0U;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkFormat stencil_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};

struct AttachmentRef final {
    RenderTargetHandle target{};
    RenderTargetViewDesc view{};
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkClearValue clear_value{};
    RenderTargetStateKind expected_state = RenderTargetStateKind::color_attachment;
    bool use_resolve = false;
    RenderTargetHandle resolve_target{};
};

struct RenderPassPreset final {
    RenderTargetPipelineSignature pipeline_signature{};
    std::array<VkAttachmentLoadOp, k_max_render_target_color_attachments> color_load_ops{};
    std::array<VkAttachmentStoreOp, k_max_render_target_color_attachments> color_store_ops{};
    std::uint32_t color_attachment_count = 0U;
    bool has_depth_attachment = false;
    bool has_stencil_attachment = false;
    bool depth_read_only = false;
    VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp depth_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
};

} // namespace vr::render
