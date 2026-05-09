#include "vr/render/swapchain_target_set.hpp"

namespace vr::render {

void SwapchainTargetSet::Reset() noexcept {
    handles.clear();
    generation_cache = 0U;
}

void SwapchainTargetSet::Invalidate(VulkanContext& context_,
                                    RenderTargetHost& render_target_host_,
                                    std::uint64_t last_submitted_value_,
                                    std::uint64_t completed_submit_value_) {
    for (auto& handle : handles) {
        if (IsValidRenderTargetHandle(handle)) {
            (void)render_target_host_.DestroyTarget(context_,
                                                    handle,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
        }
        handle = invalid_render_target_handle;
    }
    Reset();
}

RenderTargetHandle SwapchainTargetSet::Get(std::uint32_t image_index_) const noexcept {
    if (image_index_ >= handles.size()) {
        return invalid_render_target_handle;
    }
    return handles[image_index_];
}

std::uint64_t SwapchainTargetSet::Generation() const noexcept {
    return generation_cache;
}

bool SwapchainTargetSet::Empty() const noexcept {
    return handles.empty();
}

} // namespace vr::render
