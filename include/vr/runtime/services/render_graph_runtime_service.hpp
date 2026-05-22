#pragma once

#include "vr/render/frame_sync_host.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/queue_execution_policy.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render_graph/render_graph_executor.hpp"
#include "vr/render_graph/vulkan_barrier_plan.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace vr::runtime::services {

class RenderGraphRuntimeService final {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<GpuMemoryService, RenderTargetService, DescriptorService>;
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

private:
    struct ImportedBufferBinding final {
        render_graph::ResourceHandle logical{};
        render_graph::ImportedBufferBinding imported_buffer{};
    };

    struct ExternalQueueWait final {
        render_graph::QueueClass queue = render_graph::QueueClass::graphics;
        VkSemaphore semaphore = VK_NULL_HANDLE;
    };

public:
    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        const std::uint32_t current_frame_index =
            vr::runtime::detail::ResolveFrameIndex(context_);
        auto& device = vr::runtime::detail::ResolveDevice(context_);
        const std::uint64_t graphics_completed =
            vr::runtime::detail::ResolveGraphicsCompleted(context_);
        BeginFrame(current_frame_index);
        BeginFrameMultiQueue(device, current_frame_index, graphics_completed);
        auto& services = vr::runtime::detail::ResolveServices(context_);
        physical_resources.BeginFrame(device,
                                      services.template Get<RenderTargetService>().Host(),
                                      vr::runtime::detail::ResolveGraphicsSubmitted(context_),
                                      graphics_completed);
    }

    void BeginFrame(const std::uint32_t frame_index_) noexcept;

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
        direct_imported_buffers.clear();
        external_queue_waits.clear();

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
                        graph_build_callback_2d(builder_ref_,
                                                snapshot_ref_,
                                                build_result_ref_,
                                                color_chain_ref_);
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
                        graph_build_callback_3d(builder_ref_,
                                                snapshot_ref_,
                                                build_result_ref_,
                                                color_chain_ref_);
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
            const auto& local_read_caps =
                vr::runtime::detail::ResolveDevice(context_)
                    .DynamicRenderingLocalReadCapsInfo();
            builder.SetNativePassPlannerConfig(
                render_graph::NativePassPlannerConfig{
                    .request_dynamic_rendering_local_read =
                        local_read_caps.requested,
                    .dynamic_rendering_local_read_supported =
                        local_read_caps.supported,
                    .dynamic_rendering_local_read_enabled =
                        local_read_caps.enabled,
                });
            SetCompiledGraph(builder.Compile());
            ResolvePhysicalResources(context_);
            const auto capabilities = render_graph::InspectQueueExecutionCapabilities(
                vr::runtime::detail::ResolveDevice(context_));
            queue_execution_policy = render_graph::ResolveQueueExecutionPolicy(
                compiled_graph,
                capabilities,
                kRenderGraphMultiQueueSubmitEnabled);
            if (queue_execution_policy.multi_queue_enabled) {
                std::string unsupported_reason =
                    ResolveUnsupportedMultiQueueTopologyReason(compiled_graph.PlannedBarriers());
                if (!unsupported_reason.empty()) {
                    ApplyGraphicsFallback(capabilities.queue_families,
                                          std::move(unsupported_reason));
                }
            }
            lowered_vulkan_barriers = render_graph::LowerToVulkanBarrierPlan(
                compiled_graph,
                queue_execution_policy.effective_queue_families);
            auto& services = vr::runtime::detail::ResolveServices(context_);
            command_ready_vulkan_barriers = render_graph::BuildCommandReadyVulkanBarrierPlan(
                lowered_vulkan_barriers,
                physical_resources,
                services.template Get<RenderTargetService>().Host());
        }
        RefreshDiagnostics(vr::runtime::detail::ResolveDevice(context_));
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
        auto* descriptor_service = services.template TryGet<DescriptorService>();
        auto* descriptor_host =
            descriptor_service != nullptr ? descriptor_service->HostPtr() : nullptr;
        auto graph_context = render_graph::GraphCommandContext{
            device,
            frame_index,
            command_buffer,
            compiled_graph,
            physical_resources,
            services.template Get<RenderTargetService>().Host(),
            descriptor_host,
            lowered_vulkan_barriers,
            command_ready_vulkan_barriers,
        };
        if (queue_execution_policy.multi_queue_enabled) {
            record_stats = RecordPreparedMultiQueueGraph(device,
                                                         services.template Get<RenderTargetService>().Host(),
                                                         descriptor_host,
                                                         command_buffer);
        } else {
            record_stats = render_graph::RenderGraphExecutor::Record(graph_context);
        }
        RefreshDiagnostics(device);
    }

    template<typename ContextT>
    void Shutdown(ContextT& context_) noexcept {
        auto&& resolved_device = vr::runtime::detail::ResolveDevice(context_);
        if constexpr (std::is_pointer_v<std::remove_reference_t<decltype(resolved_device)>>) {
            if (resolved_device != nullptr) {
                Shutdown(*resolved_device);
            }
        } else {
            Shutdown(resolved_device);
        }
    }

    void Shutdown(VulkanContext& device_) noexcept;

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
        } else {
            graph_build_callback_3d = std::move(callback_);
        }
    }

    void SetDirectGraphBuildCallback(DirectGraphBuildCallback callback_) {
        direct_graph_build_callback = std::move(callback_);
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

    [[nodiscard]] bool SupportsGraphExecution(const VulkanContext& device_) const noexcept {
        return
               device_.EnabledVulkan13Features().synchronization2 == VK_TRUE &&
               device_.EnabledVulkan13Features().dynamicRendering == VK_TRUE;
    }

    [[nodiscard]] bool CanExecuteGraphRecord(const VulkanContext& device_) const noexcept {
        return SupportsGraphExecution(device_) &&
               has_compiled_graph &&
               compiled_graph.HasExecutablePasses() &&
               !compiled_graph.Passes().empty();
    }

    [[nodiscard]] bool HasGraphRecordWorkSource() const noexcept {
        return HasFrameSnapshot() || static_cast<bool>(direct_graph_build_callback);
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

    [[nodiscard]] const vr::runtime::RenderGraphRuntimeDiagnostics& LastDiagnostics() const noexcept {
        return last_diagnostics;
    }

    [[nodiscard]] bool UsesMultiQueueSubmitPath() const noexcept {
        return queue_execution_policy.multi_queue_enabled;
    }

    [[nodiscard]] bool HasPreparedMultiQueueSubmission() const noexcept {
        return prepared_multi_queue_submission.active;
    }

    [[nodiscard]] const std::vector<vr::render::FrameSubmitWait>& PendingGraphicsSubmitWaits() const noexcept {
        return prepared_multi_queue_submission.graphics_waits;
    }

    void RegisterDirectImportedBuffer(
        const render_graph::ResourceHandle logical_,
        const render_graph::ImportedBufferBinding& imported_buffer_);

    void RegisterExternalQueueSubmitWait(const render_graph::QueueClass queue_,
                                         const VkSemaphore semaphore_);

    [[nodiscard]] bool HasTransferQueueProgress() const noexcept {
        return queue_execution_policy.transfer_enabled ||
               last_transfer_submitted_value != 0U ||
               completed_transfer_submit_value != 0U;
    }

    [[nodiscard]] bool HasComputeQueueProgress() const noexcept {
        return queue_execution_policy.compute_enabled ||
               last_compute_submitted_value != 0U ||
               completed_compute_submit_value != 0U;
    }

    [[nodiscard]] std::uint64_t TransferSubmittedValue() const noexcept {
        return last_transfer_submitted_value;
    }

    [[nodiscard]] std::uint64_t CompletedTransferValue() const noexcept {
        return completed_transfer_submit_value;
    }

    [[nodiscard]] std::uint64_t NextTransferSignalValue() const noexcept {
        return next_transfer_submit_value;
    }

    [[nodiscard]] std::uint64_t ComputeSubmittedValue() const noexcept {
        return last_compute_submitted_value;
    }

    [[nodiscard]] std::uint64_t CompletedComputeValue() const noexcept {
        return completed_compute_submit_value;
    }

    [[nodiscard]] std::uint64_t NextComputeSignalValue() const noexcept {
        return next_compute_submit_value;
    }

    [[nodiscard]] VkResult SubmitPreparedMultiQueueWork(VulkanContext& device_);

    void MarkGraphicsSubmissionEnqueued(const vr::render::FrameToken& token_) noexcept;

private:
    struct QueueCommandResources final {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> primary_buffers{};
        std::uint32_t used_primary_count = 0U;
        std::uint32_t queue_family_index = VK_QUEUE_FAMILY_IGNORED;
    };

    struct DependencySemaphoreState final {
        VkSemaphore semaphore = VK_NULL_HANDLE;
    };

    struct MultiQueueFrameSlot final {
        QueueCommandResources graphics{};
        QueueCommandResources transfer{};
        QueueCommandResources compute{};
        std::vector<DependencySemaphoreState> dependency_semaphores{};
        std::uint64_t pending_transfer_value = 0U;
        std::uint64_t pending_compute_value = 0U;
        std::uint64_t completion_graphics_value = 0U;
        bool pending_completion = false;
    };

    struct PreparedQueueSubmitBatch final {
        std::uint32_t batch_index = render_graph::invalid_render_graph_index;
        render_graph::QueueClass queue = render_graph::QueueClass::graphics;
        VkQueue submit_queue = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        std::vector<VkSemaphore> wait_semaphores{};
        std::vector<VkSemaphore> signal_semaphores{};
    };

    struct PreparedMultiQueueSubmission final {
        bool active = false;
        bool submitted = false;
        std::uint32_t graphics_batch_index = render_graph::invalid_render_graph_index;
        std::vector<PreparedQueueSubmitBatch> owned_submit_batches{};
        std::vector<vr::render::FrameSubmitWait> graphics_waits{};
        std::uint64_t transfer_submitted_value = 0U;
        std::uint64_t compute_submitted_value = 0U;
    };

    struct BatchDependencyAggregation final {
        render_graph::VulkanDependencyInfoData begin_dependency{};
        render_graph::VulkanDependencyInfoData end_dependency{};
    };

    [[nodiscard]] const render_graph::CompiledPass* TryFindPass(
        const render_graph::PassHandle pass_) const noexcept;

    [[nodiscard]] std::string ResolvePassDebugName(
        const render_graph::PassHandle pass_) const;

    [[nodiscard]] const PreparedQueueSubmitBatch* TryFindPreparedSubmitBatch(
        const std::uint32_t batch_index_) const noexcept;

    void BuildEffectiveQueueDiagnostics(
        vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) const;

    [[nodiscard]] std::string BuildEffectiveQueueTimelineDebugString(
        const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) const;

    [[nodiscard]] std::string ResolveUnsupportedMultiQueueTopologyReason(
        const render_graph::BarrierPlan& barrier_plan_) const;

    void ApplyGraphicsFallback(const QueueFamilyIndices& queue_families_,
                               std::string reason_);

    void BeginFrameMultiQueue(VulkanContext& device_,
                              const std::uint32_t frame_index_,
                              const std::uint64_t graphics_completed_);

    void DestroyMultiQueueResources(VulkanContext& device_) noexcept;

    static void DestroyQueueCommandResources(const VkDevice device_,
                                             QueueCommandResources& resources_) noexcept;

    void EnsureMultiQueueFrameSlotCapacity(const std::uint32_t required_size_) {
        if (required_size_ <= multi_queue_frame_slots.size()) {
            return;
        }
        multi_queue_frame_slots.resize(required_size_);
    }

    void ResetQueueCommandResources(VulkanContext& device_,
                                    QueueCommandResources& resources_);

    [[nodiscard]] std::uint32_t ResolveQueueFamilyIndex(const VulkanContext& device_,
                                                        const render_graph::QueueClass queue_) const;

    [[nodiscard]] VkQueue ResolveSubmitQueue(const VulkanContext& device_,
                                             const render_graph::QueueClass queue_) const noexcept;

    QueueCommandResources& EnsureQueueCommandResources(VulkanContext& device_,
                                                       MultiQueueFrameSlot& slot_,
                                                       const render_graph::QueueClass queue_);

    [[nodiscard]] VkCommandBuffer BeginOwnedCommandBuffer(VulkanContext& device_,
                                                          QueueCommandResources& resources_);

    void EnsureDependencySemaphores(VulkanContext& device_,
                                    MultiQueueFrameSlot& slot_,
                                    const render_graph::BarrierPlan& barrier_plan_);

    [[nodiscard]] std::vector<std::uint32_t> BuildBatchIndexByPass() const;

    [[nodiscard]] render_graph::RenderGraphRecordStats RecordPreparedMultiQueueGraph(
        VulkanContext& device_,
        render::RenderTargetHost& render_target_host_,
        render::DescriptorHost* descriptor_host_,
        VkCommandBuffer graphics_command_buffer_);

    void RefreshDiagnostics(const VulkanContext& device_);

    template<typename ContextT>
    void ResolvePhysicalResources(ContextT& context_) {
        auto& services = vr::runtime::detail::ResolveServices(context_);
        RegisterImportedPresentTarget(context_);
        RegisterPendingImportedTextures();
        RegisterPendingImportedBuffers();
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
                                       const render::RenderTargetHandle render_target_);

    void RegisterPendingImportedTextures();

    void RegisterPendingImportedBuffers();

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
    render_graph::QueueExecutionPolicy queue_execution_policy{};
    render_graph::RenderGraphRecordStats record_stats{};
    GraphBuildCallback2D graph_build_callback_2d{};
    GraphBuildCallback3D graph_build_callback_3d{};
    DirectGraphBuildCallback direct_graph_build_callback{};
    render_graph::VulkanResourceTable physical_resources{};
    bool has_compiled_graph = false;
    vr::runtime::RenderGraphRuntimeDiagnostics last_diagnostics{};
    FrameSnapshotVariant frame_snapshot{};
    std::vector<ImportedTextureBinding> direct_imported_textures{};
    std::vector<ImportedBufferBinding> direct_imported_buffers{};
    std::vector<ExternalQueueWait> external_queue_waits{};
    std::vector<MultiQueueFrameSlot> multi_queue_frame_slots{};
    PreparedMultiQueueSubmission prepared_multi_queue_submission{};
    std::uint64_t next_transfer_submit_value = 1U;
    std::uint64_t last_transfer_submitted_value = 0U;
    std::uint64_t completed_transfer_submit_value = 0U;
    std::uint64_t next_compute_submit_value = 1U;
    std::uint64_t last_compute_submitted_value = 0U;
    std::uint64_t completed_compute_submit_value = 0U;

    static constexpr bool kRenderGraphMultiQueueSubmitEnabled = true;
};

} // namespace vr::runtime::services
