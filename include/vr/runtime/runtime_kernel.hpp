#pragma once

#include "vr/platform/render_host.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/runtime/command_service.hpp"
#include "vr/runtime/frame_retire_service.hpp"
#include "vr/runtime/frame_scheduler.hpp"
#include "vr/runtime/runtime_context.hpp"

#include <cstdint>
#include <stdexcept>

namespace vr::runtime {

struct RuntimeClock final {
    std::uint64_t frame_id = 0U;
    bool running = false;
};

struct RuntimePreludeResult final {
    std::uint64_t frame_id = 0U;
    std::uint32_t frame_index = 0U;
    bool events_polled = false;
    bool running = false;
};

struct RuntimeFrame final {
    bool ready = false;
    vr::render::TickCode code = vr::render::TickCode::Submitted;
    vr::render::FrameToken token{};
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
};

struct GraphicsSubmitDesc final {
    vr::render::FrameToken token{};
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const vr::render::FrameSubmitWait* extra_waits = nullptr;
    std::uint32_t extra_wait_count = 0U;
};

struct GraphicsSubmitResult final {
    VkResult result = VK_SUCCESS;
    std::uint64_t submitted_value = 0U;
};

struct UploadFlushResult final {
    bool submitted = false;
    bool cross_queue_wait = false;
    vr::render::FrameSubmitWait extra_wait{};
    std::uint32_t extra_wait_count = 0U;
    std::uint64_t transfer_wait_value = 0U;

    [[nodiscard]] const vr::render::FrameSubmitWait* ExtraWaits() const noexcept {
        return extra_wait_count > 0U ? &extra_wait : nullptr;
    }
};

struct PresentResult final {
    bool recreate_requested = false;
};

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
class RuntimeKernel final {
public:
    using BackendTag = BackendTagT;
    using HostType = vr::render::RenderRuntimeHost<BackendTag, frames_in_flight_v>;
    using PlatformHostType = typename HostType::PlatformHostType;
    using WindowSurfaceType = typename HostType::WindowSurfaceType;
    using SwapchainType = typename HostType::SwapchainType;
    using LoopType = typename HostType::LoopType;
    using SchedulerType = FrameScheduler<frames_in_flight_v>;
    using FrameType = RuntimeFrame;

    RuntimeKernel() = default;

    explicit RuntimeKernel(HostType& host_) noexcept {
        Bind(host_);
    }

    void Bind(HostType& host_) noexcept {
        host = &host_;
        scheduler.Bind(host_.Loop().Sync());
        commands.Bind(host_.Context(), host_.Loop().Commands());
        retire.Bind(host_.Context(), host_.Loop().Retire());
    }

    void Reset() noexcept {
        host = nullptr;
        scheduler.Reset();
        commands.Reset();
        retire.Reset();
    }

    [[nodiscard]] bool IsBound() const noexcept {
        return host != nullptr;
    }

    [[nodiscard]] VulkanContext& Device() noexcept {
        return host->Context();
    }

    [[nodiscard]] const VulkanContext& Device() const noexcept {
        return host->Context();
    }

    [[nodiscard]] PlatformHostType& Platform() noexcept {
        return host->PlatformHost();
    }

    [[nodiscard]] const PlatformHostType& Platform() const noexcept {
        return host->PlatformHost();
    }

    [[nodiscard]] SwapchainType& Swapchain() noexcept {
        return host->Swapchain();
    }

    [[nodiscard]] const SwapchainType& Swapchain() const noexcept {
        return host->Swapchain();
    }

    [[nodiscard]] LoopType& Loop() noexcept {
        return host->Loop();
    }

    [[nodiscard]] const LoopType& Loop() const noexcept {
        return host->Loop();
    }

    [[nodiscard]] SchedulerType& Frames() noexcept {
        return scheduler;
    }

    [[nodiscard]] const SchedulerType& Frames() const noexcept {
        return scheduler;
    }

    [[nodiscard]] CommandService& Commands() noexcept {
        return commands;
    }

    [[nodiscard]] const CommandService& Commands() const noexcept {
        return commands;
    }

    [[nodiscard]] FrameRetireService& Retire() noexcept {
        return retire;
    }

    [[nodiscard]] const FrameRetireService& Retire() const noexcept {
        return retire;
    }

    [[nodiscard]] RuntimeClock Clock() const noexcept {
        return {
            .frame_id = host != nullptr ? host->CurrentFrameId() : 0U,
            .running = host != nullptr && host->IsRunning(),
        };
    }

    [[nodiscard]] RuntimePreludeResult BeginPrelude(const bool poll_events_) {
        if (host == nullptr) {
            throw std::runtime_error("RuntimeKernel::BeginPrelude requires bound host");
        }

        RuntimePreludeResult result{};
        result.frame_id = host->AdvanceFrameId();
        if (poll_events_) {
            result.events_polled = host->PollEvents();
        }
        result.running = host->IsRunning();
        if (result.running) {
            result.frame_index = PrepareFrameSlot();
        } else {
            result.frame_index = host->Loop().Sync().CurrentFrameIndex();
        }
        return result;
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return host != nullptr && host->IsRunning();
    }

    void UpdateLastTickFrame(const std::uint32_t frame_index_,
                             const std::uint32_t image_index_) noexcept {
        if (host != nullptr) {
            host->UpdateLastTickFrame(frame_index_, image_index_);
        }
    }

    [[nodiscard]] std::uint32_t PrepareFrameSlot() {
        if (host == nullptr) {
            throw std::runtime_error("RuntimeKernel::PrepareFrameSlot requires bound host");
        }
        host->Loop().Sync().PrepareCurrentFrame(host->Context());
        return host->Loop().Sync().CurrentFrameIndex();
    }

    template<typename RecorderT>
    [[nodiscard]] FrameType BeginFrame(RecorderT& recorder_,
                                       const std::uint64_t frame_id_) {
        if (host == nullptr) {
            throw std::runtime_error("RuntimeKernel::BeginFrame requires bound host");
        }

        const vr::render::AcquiredFrame acquired = host->Loop().AcquireFrame(host->Context(),
                                                                             host->PlatformHost().SurfaceHost(),
                                                                             host->Swapchain(),
                                                                             frame_id_,
                                                                             0U,
                                                                             0U,
                                                                             recorder_);
        return {
            .ready = acquired.code == vr::render::TickCode::Submitted,
            .code = acquired.code,
            .token = acquired.token,
            .extent = acquired.extent,
            .format = acquired.format,
            .image = acquired.image,
            .image_view = acquired.image_view,
        };
    }

    [[nodiscard]] UploadFlushResult FlushUploads(const std::uint32_t frame_index_) {
        if (host == nullptr) {
            throw std::runtime_error("RuntimeKernel::FlushUploads requires bound host");
        }
        const auto legacy = host->FlushTickUploads(frame_index_);
        return {
            .submitted = legacy.submitted,
            .cross_queue_wait = legacy.cross_queue_wait,
            .extra_wait = legacy.extra_wait,
            .extra_wait_count = legacy.extra_wait_count,
            .transfer_wait_value = legacy.transfer_wait_value,
        };
    }

    [[nodiscard]] FrameStaticContext BuildFrameStaticContext() const noexcept {
        if (host == nullptr) {
            return {};
        }
        return {
            .frame_id = host->CurrentFrameId(),
            .frame_index = host->LastTickFrameIndex(),
            .image_index = host->LastTickImageIndex(),
            .swapchain_extent = host->Swapchain().Extent(),
            .swapchain_format = host->Swapchain().Format(),
        };
    }

    [[nodiscard]] static FrameStaticContext BuildFrameStaticContext(const FrameType& frame_) noexcept {
        return {
            .frame_id = frame_.token.frame_id,
            .frame_index = frame_.token.frame_index,
            .image_index = frame_.token.image_index,
            .swapchain_extent = frame_.extent,
            .swapchain_format = frame_.format,
        };
    }

    [[nodiscard]] FrameGpuProgressContext BuildFrameGpuProgressContext() const noexcept {
        std::uint64_t transfer_submitted = 0U;
        std::uint64_t transfer_completed = 0U;
        std::uint64_t compute_submitted = 0U;
        std::uint64_t compute_completed = 0U;
        if (host != nullptr &&
            host->HasUploadHost() &&
            host->Upload().UsesCrossQueueSubmit()) {
            transfer_submitted = host->Upload().LastSubmittedValue();
            transfer_completed = host->Upload().CompletedSubmitValue();
        }
        if (host != nullptr &&
            host->HasParticleSimulationHost() &&
            host->ParticleSimulationService().HasComputeTimelineProgress()) {
            compute_submitted = host->ParticleSimulationService().LastSubmittedValue();
            compute_completed = host->ParticleSimulationService().CompletedSubmitValue();
        }
        return {
            .graphics_submitted = scheduler.LastSubmittedValue(),
            .graphics_completed = scheduler.CompletedSubmitValue(),
            .transfer_submitted = transfer_submitted,
            .transfer_completed = transfer_completed,
            .compute_submitted = compute_submitted,
            .compute_completed = compute_completed,
        };
    }

    [[nodiscard]] QueueTimelineSet BuildQueueTimelines() const noexcept {
        QueueTimelineSet timelines = scheduler.MakeTimelineSet();
        if (host != nullptr &&
            host->HasUploadHost() &&
            host->Upload().UsesCrossQueueSubmit()) {
            timelines.transfer = {
                .semaphore = VK_NULL_HANDLE,
                .next_value = host->Upload().NextSignalValue(),
                .submitted_value = host->Upload().LastSubmittedValue(),
                .completed_value = host->Upload().CompletedSubmitValue(),
            };
        }
        if (host != nullptr &&
            host->HasParticleSimulationHost() &&
            host->ParticleSimulationService().HasComputeTimelineProgress()) {
            timelines.compute = {
                .semaphore = VK_NULL_HANDLE,
                .next_value = host->ParticleSimulationService().NextSignalValue(),
                .submitted_value = host->ParticleSimulationService().LastSubmittedValue(),
                .completed_value = host->ParticleSimulationService().CompletedSubmitValue(),
            };
        }
        return timelines;
    }

    [[nodiscard]] QueueDependency BuildGraphicsDependency(
        const VkPipelineStageFlags2 wait_stage_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        const VkAccessFlags2 visible_access_ = VK_ACCESS_2_NONE) const noexcept {
        return scheduler.MakeGraphicsDependency(wait_stage_, visible_access_);
    }

    [[nodiscard]] QueueDependency BuildTransferDependency(
        const VkPipelineStageFlags2 wait_stage_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        const VkAccessFlags2 visible_access_ = VK_ACCESS_2_NONE) const noexcept {
        return MakeDependencyFromTimeline(QueueKind::transfer,
                                          BuildQueueTimelines().transfer,
                                          wait_stage_,
                                          visible_access_);
    }

    [[nodiscard]] QueueDependency BuildComputeDependency(
        const VkPipelineStageFlags2 wait_stage_ = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        const VkAccessFlags2 visible_access_ = VK_ACCESS_2_NONE) const noexcept {
        return MakeDependencyFromTimeline(QueueKind::compute,
                                          BuildQueueTimelines().compute,
                                          wait_stage_,
                                          visible_access_);
    }

    [[nodiscard]] GraphicsSubmitResult SubmitGraphics(const GraphicsSubmitDesc& desc_) {
        if (host == nullptr) {
            throw std::runtime_error("RuntimeKernel::SubmitGraphics requires bound host");
        }

        const VkResult submit_result = host->Loop().Sync().Submit(host->Context(),
                                                                  desc_.token,
                                                                  desc_.command_buffer,
                                                                  desc_.wait_stage_mask,
                                                                  desc_.extra_waits,
                                                                  desc_.extra_wait_count);
        return {
            .result = submit_result,
            .submitted_value = desc_.token.graphics_signal_value,
        };
    }

    [[nodiscard]] PresentResult Present(const vr::render::FrameToken& token_) {
        if (host == nullptr) {
            throw std::runtime_error("RuntimeKernel::Present requires bound host");
        }

        const bool recreate_requested = host->Loop().Sync().Present(host->Context(),
                                                                    host->Swapchain(),
                                                                    token_);
        if (recreate_requested) {
            host->Swapchain().MarkDirty();
        }
        return {
            .recreate_requested = recreate_requested,
        };
    }

    void EndFrame(const vr::render::FrameToken& token_) {
        if (host == nullptr) {
            throw std::runtime_error("RuntimeKernel::EndFrame requires bound host");
        }

        auto& loop = host->Loop();
        loop.Sync().AdvanceFrame();
        (void)loop.Retire().Collect(host->Context().Device(),
                                    loop.Sync().CompletedSubmitValue(),
                                    loop.Config().retire_collect_budget_per_type);
    }

private:
    HostType* host = nullptr;
    SchedulerType scheduler{};
    CommandService commands{};
    FrameRetireService retire{};
};

} // namespace vr::runtime

