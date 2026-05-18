#pragma once

#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render_graph/render_graph_executor.hpp"
#include "vr/render_graph/vulkan_barrier_plan.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"

#include <cstdint>
#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace vr::runtime::services {

class RenderGraphRuntimeService final {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService, RenderTargetService>;
    static constexpr std::string_view Name = "RenderGraphRuntimeService";
    static constexpr std::uint32_t invalid_frame_index =
        (std::numeric_limits<std::uint32_t>::max)();
    using ImportedTextureRegisterFn = std::function<void(
        render_graph::ResourceHandle,
        render::RenderTargetHandle)>;
    using DirectGraphBuildCallback = std::function<void(
        render_graph::RenderGraphBuilder&,
        render_graph::ResourceHandle,
        const render_graph::Extent3D&,
        render_graph::ResourceVersionHandle&,
        const ImportedTextureRegisterFn&)>;

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
        lowered_vulkan_barriers = {};
        command_ready_vulkan_barriers = {};
        record_stats = {};
        graph_build_callback_2d = {};
        graph_build_callback_3d = {};
        direct_graph_build_callback = {};
        has_compiled_graph = false;
        frame_snapshot = std::monostate{};
        direct_imported_textures.clear();
    }

    template<typename ContextT>
    void PrepareFrame(ContextT&) noexcept {}

    template<typename ContextT>
    void PreRecord(ContextT& context_) {
        builder.Reset();
        compiled_graph = {};
        lowered_vulkan_barriers = {};
        command_ready_vulkan_barriers = {};
        record_stats = {};
        has_compiled_graph = false;
        direct_imported_textures.clear();

        if (const auto* snapshot_2d = TryGetFrameSnapshot<ecs::Dim2>();
            snapshot_2d != nullptr) {
            const auto build_result = render_graph::BuildMinimalFrameGraph(
                builder,
                *snapshot_2d,
                [this](render_graph::RenderGraphBuilder& builder_ref_,
                       const render_graph::FrameSnapshot2D& snapshot_ref_,
                       render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_ref_,
                       render_graph::ResourceVersionHandle& color_chain_ref_) {
                    if (graph_build_callback_2d) {
                        graph_build_callback_2d(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                    }
                });
            (void)build_result;
        } else if (const auto* snapshot_3d = TryGetFrameSnapshot<ecs::Dim3>();
                   snapshot_3d != nullptr) {
            const auto build_result = render_graph::BuildMinimalFrameGraph(
                builder,
                *snapshot_3d,
                [this](render_graph::RenderGraphBuilder& builder_ref_,
                       const render_graph::FrameSnapshot3D& snapshot_ref_,
                       render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_ref_,
                       render_graph::ResourceVersionHandle& color_chain_ref_) {
                    if (graph_build_callback_3d) {
                        graph_build_callback_3d(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                    }
                });
            (void)build_result;
        } else if (direct_graph_build_callback) {
            const auto& frame_context = vr::runtime::detail::ResolveFrameContext(context_);
            render_graph::Extent3D reference_extent{
                .width = 0U,
                .height = 0U,
                .depth = 1U,
            };
            if constexpr (requires { frame_context.frame.swapchain_extent; }) {
                reference_extent.width = frame_context.frame.swapchain_extent.width;
                reference_extent.height = frame_context.frame.swapchain_extent.height;
            }
            if constexpr (requires { frame_context.swapchain_targets; frame_context.frame.image_index; }) {
                if (reference_extent.width == 0U || reference_extent.height == 0U) {
                    if (frame_context.swapchain_targets != nullptr) {
                        const render::RenderTargetHandle imported_target =
                            frame_context.swapchain_targets->Get(frame_context.frame.image_index);
                        if (render::IsValidRenderTargetHandle(imported_target)) {
                            auto& services = vr::runtime::detail::ResolveServices(context_);
                            const auto imported_view =
                                services.template Get<RenderTargetService>().Host().ResolveView(imported_target);
                            reference_extent.width = imported_view.extent.width;
                            reference_extent.height = imported_view.extent.height;
                            reference_extent.depth = imported_view.extent.depth;
                        }
                    }
                }
            }
            const auto present_target = builder.CreateTexture(
                "present_target",
                render_graph::TextureDesc{
                    .dimension = render_graph::TextureDimension::image_2d,
                    .format = render_graph::TextureFormat::unknown,
                    .extent = reference_extent,
                    .usage = render_graph::texture_usage_color_attachment_flag |
                             render_graph::texture_usage_present_flag,
                    .mip_level_count = 1U,
                    .array_layer_count = 1U,
                    .sample_count = render_graph::SampleCount::x1,
                },
                render_graph::ResourceLifetime::imported);

            render_graph::ResourceVersionHandle present_ready_version =
                render_graph::invalid_resource_version;
            const ImportedTextureRegisterFn register_imported_texture =
                [this](const render_graph::ResourceHandle logical_,
                       const render::RenderTargetHandle render_target_) {
                    RegisterDirectImportedTexture(logical_, render_target_);
                };

            direct_graph_build_callback(builder,
                                        present_target,
                                        reference_extent,
                                        present_ready_version,
                                        register_imported_texture);

            if (!render_graph::IsValidResourceVersionHandle(present_ready_version)) {
                throw std::runtime_error(
                    "RenderGraphRuntimeService direct graph build callback did not produce a present-ready resource version");
            }

            const auto present_transition = builder.AddPass("present_transition", true);
            (void)builder.Read(present_transition,
                               present_ready_version,
                               render_graph::AccessDesc{
                                   .access = render_graph::AccessKind::present,
                               });
        }

        if (builder.PassCount() != 0U) {
            SetCompiledGraph(builder.Compile());
            ResolvePhysicalResources(context_);
            lowered_vulkan_barriers = render_graph::LowerToVulkanBarrierPlan(
                compiled_graph,
                vr::runtime::detail::ResolveDevice(context_).QueueFamilies());
            auto& services = vr::runtime::detail::ResolveServices(context_);
            command_ready_vulkan_barriers = render_graph::BuildCommandReadyVulkanBarrierPlan(
                lowered_vulkan_barriers,
                physical_resources,
                services.template Get<RenderTargetService>().Host());
        }
    }

    template<typename ContextT>
    void Record(ContextT& context_) {
        auto& device = vr::runtime::detail::ResolveDevice(context_);
        if (!CanExecuteGraphRecord(device)) {
            return;
        }
        const VkCommandBuffer command_buffer = vr::runtime::detail::ResolveCommandBuffer(context_);
        if (command_buffer == VK_NULL_HANDLE) {
            return;
        }

        auto& services = vr::runtime::detail::ResolveServices(context_);
        record_stats = render_graph::RenderGraphExecutor::Record(
            render_graph::GraphCommandContext{
                device,
                command_buffer,
                compiled_graph,
                physical_resources,
                services.template Get<RenderTargetService>().Host(),
                lowered_vulkan_barriers,
                command_ready_vulkan_barriers,
            });
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
    void SetGraphBuildCallback(std::function<void(render_graph::RenderGraphBuilder&,
                                                  const render_graph::FrameSnapshot<DimensionT>&,
                                                  const render_graph::MinimalFrameGraphBuildResult<DimensionT>&,
                                                  render_graph::ResourceVersionHandle&)> callback_) {
        if constexpr (std::is_same_v<DimensionT, ecs::Dim2>) {
            graph_build_callback_2d = std::move(callback_);
            if (graph_build_callback_2d) {
                EnableRecordExecution(true);
                EnableGraphOnlyRecordPath(true);
            }
        } else {
            graph_build_callback_3d = std::move(callback_);
            if (graph_build_callback_3d) {
                EnableRecordExecution(true);
                EnableGraphOnlyRecordPath(true);
            }
        }
    }

    void SetDirectGraphBuildCallback(DirectGraphBuildCallback callback_) {
        direct_graph_build_callback = std::move(callback_);
        if (direct_graph_build_callback) {
            EnableRecordExecution(true);
            EnableGraphOnlyRecordPath(true);
        }
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

    void EnableRecordExecution(const bool value_ = true) noexcept {
        record_execution_enabled = value_;
    }

    void EnableGraphOnlyRecordPath(const bool value_ = true) noexcept {
        graph_only_record_path_enabled = value_;
    }

    [[nodiscard]] bool RecordExecutionEnabled() const noexcept {
        return record_execution_enabled;
    }

    [[nodiscard]] bool GraphOnlyRecordPathEnabled() const noexcept {
        return graph_only_record_path_enabled;
    }

    [[nodiscard]] bool SupportsGraphOnlyRecord(const VulkanContext& device_) const noexcept {
        return record_execution_enabled &&
               graph_only_record_path_enabled &&
               device_.EnabledVulkan13Features().synchronization2 == VK_TRUE &&
               device_.EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    }

    [[nodiscard]] bool CanExecuteGraphRecord(const VulkanContext& device_) const noexcept {
        return SupportsGraphOnlyRecord(device_) &&
               has_compiled_graph &&
               compiled_graph.HasExecutablePasses() &&
               !compiled_graph.Passes().empty();
    }

    [[nodiscard]] const render_graph::VulkanBarrierPlan& PlannedVulkanBarriers() const noexcept {
        return lowered_vulkan_barriers;
    }

    [[nodiscard]] const render_graph::VulkanCommandReadyPlan& PlannedCommandReadyVulkanBarriers() const noexcept {
        return command_ready_vulkan_barriers;
    }

    [[nodiscard]] const render_graph::RenderGraphRecordStats& LastRecordStats() const noexcept {
        return record_stats;
    }

private:
    template<typename ContextT>
    void ResolvePhysicalResources(ContextT& context_) {
        auto& services = vr::runtime::detail::ResolveServices(context_);
        RegisterImportedPresentTarget(context_);
        RegisterPendingImportedTextures();
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

    void RegisterDirectImportedTexture(const render_graph::ResourceHandle logical_,
                                       const render::RenderTargetHandle render_target_) {
        if (!render_graph::IsValidResourceHandle(logical_)) {
            return;
        }
        const auto existing = std::find_if(
            direct_imported_textures.begin(),
            direct_imported_textures.end(),
            [&](const ImportedTextureBinding& binding_) {
                return binding_.logical.index == logical_.index;
            });
        if (existing != direct_imported_textures.end()) {
            existing->logical = logical_;
            existing->render_target = render_target_;
            return;
        }
        direct_imported_textures.push_back(ImportedTextureBinding{
            .logical = logical_,
            .render_target = render_target_,
        });
    }

    void RegisterPendingImportedTextures() {
        for (const ImportedTextureBinding& binding_ : direct_imported_textures) {
            if (!render::IsValidRenderTargetHandle(binding_.render_target)) {
                continue;
            }
            physical_resources.RegisterImportedTexture(binding_.logical,
                                                       binding_.render_target);
        }
    }

private:
    using FrameSnapshotVariant = std::variant<
        std::monostate,
        render_graph::FrameSnapshot2D,
        render_graph::FrameSnapshot3D>;
    struct ImportedTextureBinding final {
        render_graph::ResourceHandle logical{};
        render::RenderTargetHandle render_target =
            render::invalid_render_target_handle;
    };
    using GraphBuildCallback2D = std::function<void(
        render_graph::RenderGraphBuilder&,
        const render_graph::FrameSnapshot2D&,
        const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>&,
        render_graph::ResourceVersionHandle&)>;
    using GraphBuildCallback3D = std::function<void(
        render_graph::RenderGraphBuilder&,
        const render_graph::FrameSnapshot3D&,
        const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>&,
        render_graph::ResourceVersionHandle&)>;

    std::uint32_t frame_index = invalid_frame_index;
    render_graph::RenderGraphBuilder builder{};
    render_graph::CompiledRenderGraph compiled_graph{};
    render_graph::VulkanBarrierPlan lowered_vulkan_barriers{};
    render_graph::VulkanCommandReadyPlan command_ready_vulkan_barriers{};
    render_graph::RenderGraphRecordStats record_stats{};
    GraphBuildCallback2D graph_build_callback_2d{};
    GraphBuildCallback3D graph_build_callback_3d{};
    DirectGraphBuildCallback direct_graph_build_callback{};
    render_graph::VulkanResourceTable physical_resources{};
    bool has_compiled_graph = false;
    bool record_execution_enabled = false;
    bool graph_only_record_path_enabled = false;
    FrameSnapshotVariant frame_snapshot{};
    std::vector<ImportedTextureBinding> direct_imported_textures{};
};

} // namespace vr::runtime::services
