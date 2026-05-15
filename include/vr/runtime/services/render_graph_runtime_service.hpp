#pragma once

#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"

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
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService, RenderTargetService>;
    static constexpr std::string_view Name = "RenderGraphRuntimeService";
    static constexpr std::uint32_t invalid_frame_index =
        (std::numeric_limits<std::uint32_t>::max)();

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        BeginFrame(vr::runtime::detail::ResolveFrameIndex(context_));
        auto& services = vr::runtime::detail::ResolveServices(context_);
        physical_resources.BeginFrame(vr::runtime::detail::ResolveDevice(context_),
                                      services.template Get<RenderTargetService>().Host(),
                                      vr::runtime::detail::ResolveGraphicsSubmitted(context_),
                                      vr::runtime::detail::ResolveGraphicsCompleted(context_));
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
    void PreRecord(ContextT& context_) {
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
            ResolvePhysicalResources(context_);
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

    [[nodiscard]] const render_graph::VulkanResourceTable& PhysicalResources() const noexcept {
        return physical_resources;
    }

private:
    template<typename ContextT>
    void ResolvePhysicalResources(ContextT& context_) {
        auto& services = vr::runtime::detail::ResolveServices(context_);
        RegisterImportedPresentTarget(context_);
        physical_resources.Resolve(vr::runtime::detail::ResolveDevice(context_),
                                   services.template Get<GpuMemoryService>().Host(),
                                   services.template Get<RenderTargetService>().Host(),
                                   compiled_graph,
                                   vr::runtime::detail::ResolveGraphicsSubmitted(context_),
                                   vr::runtime::detail::ResolveGraphicsCompleted(context_));
    }

    template<typename ContextT>
    void RegisterImportedPresentTarget(ContextT& context_) {
        auto& frame_context = vr::runtime::detail::ResolveFrameContext(context_);
        if constexpr (requires { frame_context.swapchain_targets; frame_context.frame.image_index; }) {
            if (frame_context.swapchain_targets == nullptr) {
                return;
            }
            const render::RenderTargetHandle imported_target =
                frame_context.swapchain_targets->Get(frame_context.frame.image_index);
            if (!render::IsValidRenderTargetHandle(imported_target)) {
                return;
            }
            for (const auto& resource_ : compiled_graph.Resources()) {
                if (resource_.kind == render_graph::ResourceKind::texture &&
                    resource_.lifetime == render_graph::ResourceLifetime::imported &&
                    resource_.debug_name == "present_target") {
                    physical_resources.RegisterImportedTexture(resource_.handle, imported_target);
                }
            }
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
    render_graph::VulkanResourceTable physical_resources{};
    bool has_compiled_graph = false;
    FrameSnapshotVariant frame_snapshot{};
};

} // namespace vr::runtime::services
