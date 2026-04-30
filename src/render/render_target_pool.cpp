#include "vr/render/render_target_pool.hpp"

namespace vr::render {

void RenderTargetPool::Initialize(const RenderTargetPoolCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    initialized = true;
}

void RenderTargetPool::Shutdown() noexcept {
    create_info_cache = {};
    stats = {};
    initialized = false;
}

void RenderTargetPool::BeginFrame(std::uint32_t frame_index_,
                                  std::uint64_t completed_submit_value_) noexcept {
    (void)frame_index_;
    (void)completed_submit_value_;
    if (!initialized) {
        return;
    }
    ++stats.frame_revision;
}

void RenderTargetPool::EndFrame(std::uint32_t frame_index_,
                                std::uint64_t last_submitted_value_) noexcept {
    (void)frame_index_;
    (void)last_submitted_value_;
}

bool RenderTargetPool::IsInitialized() const noexcept {
    return initialized;
}

const RenderTargetPoolStats& RenderTargetPool::Stats() const noexcept {
    return stats;
}

} // namespace vr::render
