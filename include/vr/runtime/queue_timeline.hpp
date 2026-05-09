#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vr::runtime {

enum class QueueKind : std::uint8_t {
    graphics = 0U,
    transfer = 1U,
    compute = 2U,
};

struct QueueTimeline final {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::uint64_t next_value = 1U;
    std::uint64_t submitted_value = 0U;
    std::uint64_t completed_value = 0U;

    [[nodiscard]] constexpr bool IsAvailable() const noexcept {
        return next_value > 1U || submitted_value > 0U || completed_value > 0U || semaphore != VK_NULL_HANDLE;
    }
};

struct QueueDependency final {
    QueueKind source_queue = QueueKind::graphics;
    std::uint64_t value = 0U;
    VkPipelineStageFlags2 wait_stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 visible_access = VK_ACCESS_2_NONE;
};

struct QueueTimelineSet final {
    QueueTimeline graphics{};
    QueueTimeline transfer{};
    QueueTimeline compute{};

    [[nodiscard]] constexpr const QueueTimeline& Get(const QueueKind kind_) const noexcept {
        switch (kind_) {
        case QueueKind::graphics:
            return graphics;
        case QueueKind::transfer:
            return transfer;
        case QueueKind::compute:
            return compute;
        default:
            return graphics;
        }
    }
};

[[nodiscard]] constexpr QueueTimeline MakeLegacyGraphicsTimeline(
    const std::uint64_t submitted_value_,
    const std::uint64_t completed_value_) noexcept {
    return {
        .semaphore = VK_NULL_HANDLE,
        .next_value = submitted_value_ + 1U,
        .submitted_value = submitted_value_,
        .completed_value = completed_value_,
    };
}

[[nodiscard]] constexpr QueueDependency MakeDependencyFromTimeline(
    const QueueKind source_queue_,
    const QueueTimeline& timeline_,
    const VkPipelineStageFlags2 wait_stage_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    const VkAccessFlags2 visible_access_ = VK_ACCESS_2_NONE) noexcept {
    return {
        .source_queue = source_queue_,
        .value = timeline_.submitted_value,
        .wait_stage = wait_stage_,
        .visible_access = visible_access_,
    };
}

} // namespace vr::runtime
