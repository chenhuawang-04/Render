#pragma once

#include "vr/render/frame_sync_host.hpp"
#include "vr/runtime/queue_timeline.hpp"

#include <cstdint>

namespace vr::runtime {

template<std::uint32_t frames_in_flight_v>
class FrameScheduler final {
public:
    using SyncType = vr::render::FrameSyncHost<frames_in_flight_v>;

    FrameScheduler() = default;

    explicit FrameScheduler(SyncType& sync_) noexcept
        : sync(&sync_) {}

    void Bind(SyncType& sync_) noexcept {
        sync = &sync_;
    }

    void Reset() noexcept {
        sync = nullptr;
    }

    [[nodiscard]] bool IsBound() const noexcept {
        return sync != nullptr;
    }

    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept {
        return sync != nullptr ? sync->FramesInFlight() : frames_in_flight_v;
    }

    [[nodiscard]] std::uint32_t CurrentFrameIndex() const noexcept {
        return sync != nullptr ? sync->CurrentFrameIndex() : 0U;
    }

    [[nodiscard]] std::uint64_t LastSubmittedValue() const noexcept {
        return LastGraphicsSubmittedValue();
    }

    [[nodiscard]] std::uint64_t CompletedSubmitValue() const noexcept {
        return CompletedGraphicsValue();
    }

    [[nodiscard]] std::uint64_t LastGraphicsSubmittedValue() const noexcept {
        return sync != nullptr ? sync->LastGraphicsSubmittedValue() : 0U;
    }

    [[nodiscard]] std::uint64_t CompletedGraphicsValue() const noexcept {
        return sync != nullptr ? sync->CompletedGraphicsValue() : 0U;
    }

    [[nodiscard]] std::uint64_t NextGraphicsSignalValue() const noexcept {
        return sync != nullptr ? sync->NextGraphicsSignalValue() : 1U;
    }

    [[nodiscard]] QueueTimelineSet MakeTimelineSet() const noexcept {
        QueueTimelineSet timelines{};
        timelines.graphics = {
            .semaphore = VK_NULL_HANDLE,
            .next_value = NextGraphicsSignalValue(),
            .submitted_value = LastGraphicsSubmittedValue(),
            .completed_value = CompletedGraphicsValue(),
        };
        timelines.transfer = {};
        timelines.compute = {};
        return timelines;
    }

    [[nodiscard]] QueueDependency MakeGraphicsDependency(
        const VkPipelineStageFlags2 wait_stage_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        const VkAccessFlags2 visible_access_ = VK_ACCESS_2_NONE) const noexcept {
        return MakeDependencyFromTimeline(QueueKind::graphics,
                                          MakeTimelineSet().graphics,
                                          wait_stage_,
                                          visible_access_);
    }

    [[nodiscard]] QueueDependency MakeTransferDependency(
        const VkPipelineStageFlags2 wait_stage_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        const VkAccessFlags2 visible_access_ = VK_ACCESS_2_NONE) const noexcept {
        return MakeDependencyFromTimeline(QueueKind::transfer,
                                          MakeTimelineSet().transfer,
                                          wait_stage_,
                                          visible_access_);
    }

    [[nodiscard]] QueueDependency MakeComputeDependency(
        const VkPipelineStageFlags2 wait_stage_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        const VkAccessFlags2 visible_access_ = VK_ACCESS_2_NONE) const noexcept {
        return MakeDependencyFromTimeline(QueueKind::compute,
                                          MakeTimelineSet().compute,
                                          wait_stage_,
                                          visible_access_);
    }

private:
    SyncType* sync = nullptr;
};

} // namespace vr::runtime

