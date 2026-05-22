#include "vr/runtime/services/render_graph_runtime_service.hpp"

#include <algorithm>

namespace vr::runtime::services {

void RenderGraphRuntimeService::RegisterDirectImportedBuffer(
    const render_graph::ResourceHandle logical_,
    const render_graph::ImportedBufferBinding& imported_buffer_) {
    if (!render_graph::IsValidResourceHandle(logical_) ||
        imported_buffer_.buffer == VK_NULL_HANDLE ||
        imported_buffer_.size_bytes == 0U) {
        return;
    }
    const auto existing = std::find_if(
        direct_imported_buffers.begin(),
        direct_imported_buffers.end(),
        [&](const ImportedBufferBinding& binding_) {
            return binding_.logical.index == logical_.index;
        });
    if (existing != direct_imported_buffers.end()) {
        existing->logical = logical_;
        existing->imported_buffer = imported_buffer_;
        return;
    }
    direct_imported_buffers.push_back(ImportedBufferBinding{
        .logical = logical_,
        .imported_buffer = imported_buffer_,
    });
}

void RenderGraphRuntimeService::RegisterExternalQueueSubmitWait(
    const render_graph::QueueClass queue_,
    const VkSemaphore semaphore_) {
    if (semaphore_ == VK_NULL_HANDLE) {
        return;
    }
    const auto existing = std::find_if(
        external_queue_waits.begin(),
        external_queue_waits.end(),
        [&](const ExternalQueueWait& wait_) {
            return wait_.queue == queue_ && wait_.semaphore == semaphore_;
        });
    if (existing == external_queue_waits.end()) {
        external_queue_waits.push_back(ExternalQueueWait{
            .queue = queue_,
            .semaphore = semaphore_,
        });
    }
}

void RenderGraphRuntimeService::RegisterDirectImportedTexture(
    const render_graph::ResourceHandle logical_,
    const render::RenderTargetHandle render_target_) {
    if (!render_graph::IsValidResourceHandle(logical_)) {
        return;
    }
    const auto existing = std::find_if(
        direct_imported_textures.begin(),
        direct_imported_textures.end(),
        [&](const ImportedTextureBinding& binding_) {
            return binding_.logical.index == logical_.index;
        });
    if (existing != direct_imported_textures.end()) {
        existing->logical = logical_;
        existing->render_target = render_target_;
        return;
    }
    direct_imported_textures.push_back(ImportedTextureBinding{
        .logical = logical_,
        .render_target = render_target_,
    });
}

void RenderGraphRuntimeService::RegisterPendingImportedTextures() {
    for (const ImportedTextureBinding& binding_ : direct_imported_textures) {
        if (!render::IsValidRenderTargetHandle(binding_.render_target)) {
            continue;
        }
        physical_resources.RegisterImportedTexture(binding_.logical,
                                                   binding_.render_target);
    }
}

void RenderGraphRuntimeService::RegisterPendingImportedBuffers() {
    for (const ImportedBufferBinding& binding_ : direct_imported_buffers) {
        if (binding_.imported_buffer.buffer == VK_NULL_HANDLE ||
            binding_.imported_buffer.size_bytes == 0U) {
            continue;
        }
        physical_resources.RegisterImportedBuffer(binding_.logical,
                                                  binding_.imported_buffer);
    }
}

} // namespace vr::runtime::services
