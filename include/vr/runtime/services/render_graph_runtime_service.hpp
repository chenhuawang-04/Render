#pragma once

#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"

#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace vr::runtime::services {

class RenderGraphRuntimeService final {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "RenderGraphRuntimeService";
    static constexpr std::uint32_t invalid_frame_index =
        (std::numeric_limits<std::uint32_t>::max)();

    template<typename ContextT>
    void BeginFrame(ContextT& context_) noexcept {
        BeginFrame(vr::runtime::detail::ResolveFrameIndex(context_));
    }

    void BeginFrame(const std::uint32_t frame_index_) noexcept {
        frame_index = frame_index_;
        builder.Reset();
        compiled_graph = {};
        has_compiled_graph = false;
        frame_snapshot = std::monostate{};
    }

    template<typename ContextT>
    void PrepareFrame(ContextT&) noexcept {}

    template<typename ContextT>
    void PreRecord(ContextT&) {
        builder.Reset();
        compiled_graph = {};
        has_compiled_graph = false;

        if (const auto* snapshot_2d = TryGetFrameSnapshot<ecs::Dim2>();
            snapshot_2d != nullptr) {
            (void)render_graph::BuildMinimalFrameGraph(builder, *snapshot_2d);
        } else if (const auto* snapshot_3d = TryGetFrameSnapshot<ecs::Dim3>();
                   snapshot_3d != nullptr) {
            (void)render_graph::BuildMinimalFrameGraph(builder, *snapshot_3d);
        }

        if (builder.PassCount() != 0U) {
            SetCompiledGraph(builder.Compile());
        }
    }

    template<typename ContextT>
    void Retire(ContextT&) noexcept {}

    [[nodiscard]] std::uint32_t FrameIndex() const noexcept {
        return frame_index;
    }

    [[nodiscard]] render_graph::RenderGraphBuilder& Builder() noexcept {
        return builder;
    }

    [[nodiscard]] const render_graph::RenderGraphBuilder& Builder() const noexcept {
        return builder;
    }

    void SetCompiledGraph(render_graph::CompiledRenderGraph compiled_graph_) {
        compiled_graph = std::move(compiled_graph_);
        has_compiled_graph = true;
    }

    [[nodiscard]] const render_graph::CompiledRenderGraph* TryGetCompiledGraph() const noexcept {
        return has_compiled_graph ? &compiled_graph : nullptr;
    }

    [[nodiscard]] bool HasFrameSnapshot() const noexcept {
        return !std::holds_alternative<std::monostate>(frame_snapshot);
    }

    template<ecs::DimensionTag DimensionT>
    void SetFrameSnapshot(render_graph::FrameSnapshot<DimensionT> snapshot_) {
        frame_snapshot = std::move(snapshot_);
    }

    template<ecs::DimensionTag DimensionT>
    [[nodiscard]] const render_graph::FrameSnapshot<DimensionT>* TryGetFrameSnapshot() const noexcept {
        if constexpr (std::is_same_v<DimensionT, ecs::Dim2>) {
            return std::get_if<render_graph::FrameSnapshot2D>(&frame_snapshot);
        } else {
            return std::get_if<render_graph::FrameSnapshot3D>(&frame_snapshot);
        }
    }

private:
    using FrameSnapshotVariant = std::variant<
        std::monostate,
        render_graph::FrameSnapshot2D,
        render_graph::FrameSnapshot3D>;

    std::uint32_t frame_index = invalid_frame_index;
    render_graph::RenderGraphBuilder builder{};
    render_graph::CompiledRenderGraph compiled_graph{};
    bool has_compiled_graph = false;
    FrameSnapshotVariant frame_snapshot{};
};

} // namespace vr::runtime::services
