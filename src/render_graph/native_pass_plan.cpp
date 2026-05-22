#include "native_pass_plan_internal.hpp"

#include <sstream>

namespace vr::render_graph::native_pass_detail {
[[nodiscard]] const char* QueueClassToString(const QueueClass queue_) noexcept {
    switch (queue_) {
    case QueueClass::graphics:
        return "graphics";
    case QueueClass::compute:
        return "compute";
    case QueueClass::transfer:
        return "transfer";
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
    }
    return "unknown";
}

[[nodiscard]] const char* AttachmentLoadOpToString(
    const AttachmentLoadOp load_op_) noexcept {
    switch (load_op_) {
    case AttachmentLoadOp::load:
        return "load";
    case AttachmentLoadOp::clear:
        return "clear";
    case AttachmentLoadOp::dont_care:
        return "dont_care";
    }
    return "unknown";
}

[[nodiscard]] const char* AttachmentStoreOpToString(
    const AttachmentStoreOp store_op_) noexcept {
    switch (store_op_) {
    case AttachmentStoreOp::store:
        return "store";
    case AttachmentStoreOp::dont_care:
        return "dont_care";
    }
    return "unknown";
}

} // namespace vr::render_graph::native_pass_detail
