#pragma once

#include "vr/render/pipeline_host.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/bound_host_service.hpp"

#include <cstdint>
#include <string_view>

namespace vr::runtime::services {

class PipelineService final : public detail::BoundHostService<vr::render::PipelineHost> {
public:
    struct WarmupPolicy final {
        std::uint32_t max_graphics_compiles_per_tick = 0U;
        std::uint32_t max_compute_compiles_per_tick = 0U;
        bool compile_before_render = true;
        bool compile_after_render = false;
    };

    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "PipelineService";

    [[nodiscard]] vr::render::PipelineHost& Host() {
        return this->RequireHost(Name);
    }

    [[nodiscard]] const vr::render::PipelineHost& Host() const {
        return this->RequireHost(Name);
    }

    void ConfigureWarmup(const WarmupPolicy& policy_) noexcept {
        warmup_policy = policy_;
    }

    [[nodiscard]] const WarmupPolicy& Warmup() const noexcept {
        return warmup_policy;
    }

    [[nodiscard]] std::uint32_t LastBeginFrameCompileCount() const noexcept {
        return last_begin_frame_compile_count;
    }

    [[nodiscard]] std::uint32_t LastEndFrameCompileCount() const noexcept {
        return last_end_frame_compile_count;
    }

    [[nodiscard]] std::uint32_t LastFrameCompileCount() const noexcept {
        return last_begin_frame_compile_count + last_end_frame_compile_count;
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        last_begin_frame_compile_count = 0U;
        if (!warmup_policy.compile_before_render) {
            return;
        }
        if (auto* host_ = this->HostPtr()) {
            last_begin_frame_compile_count = host_->ProcessPendingCompiles(
                vr::runtime::detail::ResolveDevice(context_),
                warmup_policy.max_graphics_compiles_per_tick,
                warmup_policy.max_compute_compiles_per_tick);
        }
    }

    template<typename ContextT>
    void EndFrame(ContextT& context_) {
        last_end_frame_compile_count = 0U;
        if (!warmup_policy.compile_after_render) {
            return;
        }
        if (auto* host_ = this->HostPtr()) {
            last_end_frame_compile_count = host_->ProcessPendingCompiles(
                vr::runtime::detail::ResolveDevice(context_),
                warmup_policy.max_graphics_compiles_per_tick,
                warmup_policy.max_compute_compiles_per_tick);
        }
    }

private:
    WarmupPolicy warmup_policy{};
    std::uint32_t last_begin_frame_compile_count = 0U;
    std::uint32_t last_end_frame_compile_count = 0U;
};

} // namespace vr::runtime::services
