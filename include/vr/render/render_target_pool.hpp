#pragma once

#include <cstdint>

namespace vr::render {

struct RenderTargetPoolCreateInfo final {
    std::uint32_t reserve_bucket_count = 32U;
    std::uint32_t reserve_live_target_count = 64U;
};

struct RenderTargetPoolStats final {
    std::uint32_t live_transient_target_count = 0U;
    std::uint32_t reusable_target_count = 0U;
    std::uint32_t frame_revision = 0U;
};

class RenderTargetPool final {
public:
    RenderTargetPool() = default;
    ~RenderTargetPool() = default;

    RenderTargetPool(const RenderTargetPool&) = delete;
    RenderTargetPool& operator=(const RenderTargetPool&) = delete;

    RenderTargetPool(RenderTargetPool&&) = delete;
    RenderTargetPool& operator=(RenderTargetPool&&) = delete;

    void Initialize(const RenderTargetPoolCreateInfo& create_info_ = {});
    void Shutdown() noexcept;
    void BeginFrame(std::uint32_t frame_index_,
                    std::uint64_t completed_submit_value_) noexcept;
    void EndFrame(std::uint32_t frame_index_,
                  std::uint64_t last_submitted_value_) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RenderTargetPoolStats& Stats() const noexcept;

private:
    RenderTargetPoolCreateInfo create_info_cache{};
    RenderTargetPoolStats stats{};
    bool initialized = false;
};

} // namespace vr::render
