#pragma once

#include "vr/runtime/runtime_context.hpp"

#include <cstdint>

namespace vr::runtime {

enum class RuntimeExecutionStage : std::uint8_t {
    BeginFrame = 0U,
    ServiceBeginFrame = 1U,
    Prepare = 2U,
    FlushUploads = 3U,
    PreRecord = 4U,
    Record = 5U,
    Submit = 6U,
    Present = 7U,
    EndFrame = 8U,
    Retire = 9U,
    Diagnostics = 10U,
};

struct RuntimeExecutionTrace final {
    RuntimeExecutionStage last_completed_stage = RuntimeExecutionStage::BeginFrame;
    std::uint32_t completed_stage_count = 0U;

    void Mark(const RuntimeExecutionStage stage_) noexcept {
        last_completed_stage = stage_;
        ++completed_stage_count;
    }
};

template<typename FrameContextT>
class RuntimeExecution final {
public:
    using FrameContextType = FrameContextT;
    using Profile = typename FrameContextType::Profile;
    using BackendTag = typename FrameContextType::BackendTag;
    using ServicesType = typename FrameContextType::ServicesType;
    static constexpr std::uint32_t FramesInFlight = FrameContextType::FramesInFlight;

    explicit RuntimeExecution(FrameContextType& frame_context_) noexcept
        : frame_context(&frame_context_) {}

    [[nodiscard]] FrameContextType& Frame() noexcept {
        return *frame_context;
    }

    [[nodiscard]] const FrameContextType& Frame() const noexcept {
        return *frame_context;
    }

    void RebindFrame(FrameContextType& frame_context_) noexcept {
        frame_context = &frame_context_;
    }

    [[nodiscard]] RuntimeExecutionTrace& Trace() noexcept {
        return trace;
    }

    [[nodiscard]] const RuntimeExecutionTrace& Trace() const noexcept {
        return trace;
    }

    void MarkBeginFrame() noexcept {
        trace.Mark(RuntimeExecutionStage::BeginFrame);
    }

    void MarkFlushUploads() noexcept {
        trace.Mark(RuntimeExecutionStage::FlushUploads);
    }

    void MarkRecord() noexcept {
        trace.Mark(RuntimeExecutionStage::Record);
    }

    void MarkSubmit() noexcept {
        trace.Mark(RuntimeExecutionStage::Submit);
    }

    void MarkPresent() noexcept {
        trace.Mark(RuntimeExecutionStage::Present);
    }

    void MarkDiagnostics() noexcept {
        trace.Mark(RuntimeExecutionStage::Diagnostics);
    }

    void ServiceBeginFrame(ServicesType& services_) {
        auto context = RuntimeBeginFrameContext<Profile, BackendTag, FramesInFlight>{
            .frame_context = *frame_context,
            .execution = trace,
        };
        services_.BeginFrame(context);
        trace.Mark(RuntimeExecutionStage::ServiceBeginFrame);
    }

    void Prepare(ServicesType& services_) {
        auto context = RuntimePreparePhaseContext<Profile, BackendTag, FramesInFlight>{
            .frame_context = *frame_context,
            .execution = trace,
        };
        services_.PrepareFrame(context);
        trace.Mark(RuntimeExecutionStage::Prepare);
    }

    void PreRecord(ServicesType& services_) {
        auto context = RuntimeRecordPhaseContext<Profile, BackendTag, FramesInFlight>{
            .frame_context = *frame_context,
            .execution = trace,
        };
        services_.PreRecord(context);
        trace.Mark(RuntimeExecutionStage::PreRecord);
    }

    void PostRecord(ServicesType& services_) {
        auto context = RuntimeRecordPhaseContext<Profile, BackendTag, FramesInFlight>{
            .frame_context = *frame_context,
            .execution = trace,
        };
        services_.PostRecord(context);
    }

    void EndFrame(ServicesType& services_) {
        auto context = RuntimeEndFrameContext<Profile, BackendTag, FramesInFlight>{
            .frame_context = *frame_context,
            .execution = trace,
        };
        services_.EndFrame(context);
        trace.Mark(RuntimeExecutionStage::EndFrame);
    }

    void Retire(ServicesType& services_) {
        auto context = RuntimeRetireContext<Profile, BackendTag, FramesInFlight>{
            .frame_context = *frame_context,
            .execution = trace,
        };
        services_.Retire(context);
        trace.Mark(RuntimeExecutionStage::Retire);
    }

private:
    FrameContextType* frame_context = nullptr;
    RuntimeExecutionTrace trace{};
};

} // namespace vr::runtime
