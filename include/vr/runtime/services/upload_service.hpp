#pragma once

#include "vr/render/upload_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"

#include <string_view>

namespace vr::runtime::services {

class UploadService final : public detail::BoundHostService<vr::render::UploadHost> {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService>;
    static constexpr std::string_view Name = "UploadService";

    [[nodiscard]] vr::render::UploadHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::UploadHost& Host() const {
        return this->RequireHost(Name);
    }

    [[nodiscard]] bool UsesCrossQueueSubmit() const noexcept {
        return this->HostPtr() != nullptr && this->HostPtr()->UsesCrossQueueSubmit();
    }

    [[nodiscard]] std::uint64_t LastSubmittedValue() const noexcept {
        return this->HostPtr() != nullptr ? this->HostPtr()->LastSubmittedValue() : 0U;
    }

    [[nodiscard]] std::uint64_t CompletedSubmitValue() const noexcept {
        return this->HostPtr() != nullptr ? this->HostPtr()->CompletedSubmitValue() : 0U;
    }

    [[nodiscard]] std::uint64_t NextSignalValue() const noexcept {
        return this->HostPtr() != nullptr ? this->HostPtr()->NextSignalValue() : 1U;
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        if (auto* host_ = this->HostPtr()) {
            host_->BeginFrame(vr::runtime::detail::ResolveDevice(context_),
                              vr::runtime::detail::ResolveFrameIndex(context_));
        }
    }

    template<typename ContextT>
    [[nodiscard]] auto Flush(ContextT& context_) {
        auto& frame_context = vr::runtime::detail::ResolveFrameContext(context_);
        return frame_context.kernel.FlushUploads(frame_context.frame.frame_index);
    }
};

} // namespace vr::runtime::services
