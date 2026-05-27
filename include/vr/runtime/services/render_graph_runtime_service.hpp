#pragma once

#include "vr/render/frame_sync_host.hpp"
#include "vr/render_graph/frame_graph_build.hpp"
#include "vr/render_graph/frame_snapshot.hpp"
#include "vr/render_graph/frame_temporal_consumer.hpp"
#include "vr/render_graph/queue_execution_policy.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/render_graph/render_graph_executor.hpp"
#include "vr/render_graph/vulkan_barrier_plan.hpp"
#include "vr/render_graph/vulkan_resource_table.hpp"
#include "vr/ecs/system/spatial_math.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"

#include <cstdint>
#include <functional>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <array>
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
#if VR_ENABLE_DEBUG_OBSERVABILITY
        ResetRecordedTimingSamples();
#endif

        if (const auto* snapshot_2d = TryGetFrameSnapshot<ecs::Dim2>();
            snapshot_2d != nullptr) {
            auto prepared_snapshot = *snapshot_2d;
            PrepareFrameTemporalContracts(context_, prepared_snapshot);
            SetFrameSnapshot<ecs::Dim2>(prepared_snapshot);
            const auto build_result = render_graph::BuildMinimalFrameGraph(
                builder,
                prepared_snapshot,
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
            AppendFrameColorHistoryCapturePass(prepared_snapshot, build_result);
            AppendFrameDepthHistoryCapturePass(prepared_snapshot, build_result);
            (void)build_result;
        } else if (const auto* snapshot_3d = TryGetFrameSnapshot<ecs::Dim3>();
                   snapshot_3d != nullptr) {
            auto prepared_snapshot = *snapshot_3d;
            PrepareFrameTemporalContracts(context_, prepared_snapshot);
            SetFrameSnapshot<ecs::Dim3>(prepared_snapshot);
            const auto build_result = render_graph::BuildMinimalFrameGraph(
                builder,
                prepared_snapshot,
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
            AppendFrameColorHistoryCapturePass(prepared_snapshot, build_result);
            AppendFrameDepthHistoryCapturePass(prepared_snapshot, build_result);
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
#if VR_ENABLE_DEBUG_OBSERVABILITY
        RefreshDiagnostics(vr::runtime::detail::ResolveDevice(context_));
#endif
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
#if VR_ENABLE_DEBUG_OBSERVABILITY
        if (CollectsTimingOrCapture()) {
            if (queue_execution_policy.multi_queue_enabled) {
                record_stats = RecordPreparedMultiQueueGraph(device,
                                                             services.template Get<RenderTargetService>().Host(),
                                                             descriptor_host,
                                                             command_buffer);
            } else {
                record_stats = RecordSingleQueueGraphByQueueBatch(
                    device,
                    services.template Get<RenderTargetService>().Host(),
                    descriptor_host,
                    command_buffer);
            }
        } else if (queue_execution_policy.multi_queue_enabled) {
            record_stats = RecordPreparedMultiQueueGraph(device,
                                                         services.template Get<RenderTargetService>().Host(),
                                                         descriptor_host,
                                                         command_buffer);
        } else {
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
            record_stats = render_graph::RenderGraphExecutor::Record(graph_context);
        }
#else
        if (queue_execution_policy.multi_queue_enabled) {
            record_stats = RecordPreparedMultiQueueGraph(device,
                                                         services.template Get<RenderTargetService>().Host(),
                                                         descriptor_host,
                                                         command_buffer);
        } else {
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
            record_stats = render_graph::RenderGraphExecutor::Record(graph_context);
        }
#endif
#if VR_ENABLE_DEBUG_OBSERVABILITY
        RefreshDiagnostics(device);
#endif
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

    [[nodiscard]] bool IsGraphOnlyRecordActive(
        const VulkanContext& device_) const noexcept {
        return CanExecuteGraphRecord(device_) &&
               record_stats.pass_count > 0U;
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

    void RegisterImportedTexture(
        const render_graph::ResourceHandle logical_,
        const render::RenderTargetHandle render_target_);

    void SetDiagnosticsLevel(const vr::runtime::DiagnosticsLevel level_) noexcept {
        diagnostics_level =
            vr::runtime::ResolveRuntimeDiagnosticsLevelForBuild(level_);
    }

    [[nodiscard]] vr::runtime::DiagnosticsLevel DiagnosticsLevel() const noexcept {
        return diagnostics_level;
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

    void RequestFrameColorHistoryReset() noexcept;
    void QueueFrameMotionHistoryPublish(std::uint64_t frame_index_,
                                        render::SceneSubmissionId submission_id_) noexcept;

private:
    struct FrameHistorySlot final {
        render::RenderTargetHandle handle =
            render::invalid_render_target_handle;
        std::uint32_t resource_revision = 0U;
    };

    struct FrameHistoryState final {
        std::array<FrameHistorySlot, 2U> slots{};
        render_graph::TextureDesc desc{};
        std::uint32_t published_slot = invalid_frame_index;
        std::uint32_t write_slot = 0U;
        std::uint32_t pending_publish_slot = invalid_frame_index;
        std::uint64_t previous_frame_index = 0U;
        std::uint64_t pending_frame_index = 0U;
        render::SceneSubmissionId previous_submission_id{};
        render::SceneSubmissionId pending_submission_id{};
        render_graph::FrameHistoryInvalidationReason last_invalidation_reason =
            render_graph::FrameHistoryInvalidationReason::first_frame;
        bool initialized = false;
        bool reset_requested = false;
        std::uint8_t active_dimension = 0U;
    };

    struct FrameTemporalReprojectionState final {
        ecs::Matrix4x4 previous_view_projection =
            ecs::spatial_math::IdentityMatrix4x4();
        ecs::Matrix4x4 pending_view_projection =
            ecs::spatial_math::IdentityMatrix4x4();
        std::uint64_t previous_frame_index = 0U;
        std::uint64_t pending_frame_index = 0U;
        render::SceneSubmissionId previous_submission_id{};
        render::SceneSubmissionId pending_submission_id{};
        render_graph::FrameHistoryInvalidationReason last_invalidation_reason =
            render_graph::FrameHistoryInvalidationReason::first_frame;
        bool previous_available = false;
        bool pending_available = false;
        bool reset_requested = false;
        std::uint8_t active_dimension = 0U;
    };

    struct FrameTemporalJitterState final {
        float previous_uv_x = 0.0F;
        float previous_uv_y = 0.0F;
        float pending_uv_x = 0.0F;
        float pending_uv_y = 0.0F;
        std::uint64_t previous_frame_index = 0U;
        std::uint64_t pending_frame_index = 0U;
        render::SceneSubmissionId previous_submission_id{};
        render::SceneSubmissionId pending_submission_id{};
        render_graph::FrameHistoryInvalidationReason last_invalidation_reason =
            render_graph::FrameHistoryInvalidationReason::first_frame;
        bool previous_available = false;
        bool pending_available = false;
        bool reset_requested = false;
        std::uint8_t active_dimension = 0U;
    };

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
        vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_,
        bool include_detailed_) const;

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

#if VR_ENABLE_DEBUG_OBSERVABILITY
    void ResetRecordedTimingSamples() noexcept;

    [[nodiscard]] bool CollectsTimingOrCapture() const noexcept;

    void AppendRecordedQueueBatchTimingSample(
        render_graph::QueueClass effective_queue_,
        const render_graph::QueueSubmitBatch& queue_batch_,
        std::uint32_t batch_index_,
        std::uint64_t relative_begin_ns_,
        std::uint64_t relative_end_ns_);

    [[nodiscard]] render_graph::RenderGraphRecordStats RecordSingleQueueGraphByQueueBatch(
        VulkanContext& device_,
        render::RenderTargetHost& render_target_host_,
        render::DescriptorHost* descriptor_host_,
        VkCommandBuffer graphics_command_buffer_);
#endif

    void RefreshDiagnostics(const VulkanContext& device_);

    void ResetPendingHistoryPublish(FrameHistoryState& history_) noexcept {
        history_.pending_publish_slot = invalid_frame_index;
        history_.pending_frame_index = 0U;
        history_.pending_submission_id = {};
    }

    void ResetPendingReprojectionPublish() noexcept {
        frame_reprojection.pending_available = false;
        frame_reprojection.pending_frame_index = 0U;
        frame_reprojection.pending_submission_id = {};
        frame_reprojection.pending_view_projection =
            ecs::spatial_math::IdentityMatrix4x4();
    }

    void ResetPendingJitterPublish() noexcept {
        frame_jitter.pending_available = false;
        frame_jitter.pending_uv_x = 0.0F;
        frame_jitter.pending_uv_y = 0.0F;
        frame_jitter.pending_frame_index = 0U;
        frame_jitter.pending_submission_id = {};
    }

    [[nodiscard]] static float BuildHaltonSequenceSample(std::uint32_t index_,
                                                         const std::uint32_t base_) noexcept {
        float result = 0.0F;
        float fraction = 1.0F / static_cast<float>(base_);
        while (index_ != 0U) {
            result += fraction * static_cast<float>(index_ % base_);
            index_ /= base_;
            fraction /= static_cast<float>(base_);
        }
        return result;
    }

    [[nodiscard]] static std::array<float, 2U> BuildTemporalJitterUv(
        const std::uint64_t frame_index_,
        const render_graph::Extent3D& extent_) noexcept {
        if (extent_.width == 0U || extent_.height == 0U) {
            return {0.0F, 0.0F};
        }

        constexpr std::uint32_t k_jitter_phase_count = 8U;
        const std::uint64_t extent_seed =
            (static_cast<std::uint64_t>(extent_.width) * 73856093ULL) ^
            (static_cast<std::uint64_t>(extent_.height) * 19349663ULL);
        const auto sequence_index = static_cast<std::uint32_t>(
            ((frame_index_ + extent_seed) % k_jitter_phase_count) + 1ULL);
        return {
            (BuildHaltonSequenceSample(sequence_index, 2U) - 0.5F) /
                static_cast<float>(extent_.width),
            (BuildHaltonSequenceSample(sequence_index, 3U) - 0.5F) /
                static_cast<float>(extent_.height),
        };
    }

    template<ecs::DimensionTag DimensionT>
    void PrepareTemporalReprojectionContract(
        render_graph::FrameSnapshot<DimensionT>& snapshot_,
        const render_graph::FrameHistoryInvalidationReason fallback_invalidation_reason_,
        const std::uint8_t desired_dimension) {
        auto& contract = snapshot_.temporal.reprojection;
        contract = {};
        ResetPendingReprojectionPublish();

        if constexpr (!std::is_same_v<DimensionT, ecs::Dim3>) {
            frame_reprojection.previous_available = false;
            frame_reprojection.reset_requested = false;
            frame_reprojection.active_dimension = desired_dimension;
            frame_reprojection.last_invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            contract.invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            return;
        }

        render_graph::FrameHistoryInvalidationReason invalidation_reason =
            fallback_invalidation_reason_;
        const auto* scene_view = snapshot_.SceneView();
        if (scene_view == nullptr || scene_view->has_camera == 0U) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            frame_reprojection.previous_available = false;
            frame_reprojection.reset_requested = false;
            frame_reprojection.active_dimension = desired_dimension;
            frame_reprojection.last_invalidation_reason = invalidation_reason;
            contract.invalidation_reason = invalidation_reason;
            return;
        }

        if (invalidation_reason ==
            render_graph::FrameHistoryInvalidationReason::none) {
            if (frame_reprojection.reset_requested) {
                invalidation_reason =
                    render_graph::FrameHistoryInvalidationReason::reset_requested;
            } else if (frame_reprojection.active_dimension != desired_dimension) {
                invalidation_reason =
                    render_graph::FrameHistoryInvalidationReason::dimension_changed;
            } else if (!frame_reprojection.previous_available) {
                invalidation_reason =
                    render_graph::FrameHistoryInvalidationReason::first_frame;
            }
        }

        contract.current_available = true;
        frame_reprojection.pending_frame_index = snapshot_.frame_index;
        frame_reprojection.pending_submission_id = snapshot_.submission_id;
        frame_reprojection.pending_view_projection =
            scene_view->camera.runtime.view_projection_matrix;

        ecs::Matrix4x4 current_inverse_view_projection =
            ecs::spatial_math::IdentityMatrix4x4();
        if (!ecs::spatial_math::InvertAffineMatrix4x4(
                scene_view->camera.runtime.view_projection_matrix,
                current_inverse_view_projection)) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            frame_reprojection.previous_available = false;
            frame_reprojection.pending_available = false;
            frame_reprojection.reset_requested = false;
            frame_reprojection.active_dimension = desired_dimension;
            frame_reprojection.last_invalidation_reason = invalidation_reason;
            contract.current_available = false;
            contract.invalidation_reason = invalidation_reason;
            return;
        }

        frame_reprojection.pending_available = true;

        const bool previous_available =
            frame_reprojection.previous_available &&
            invalidation_reason ==
                render_graph::FrameHistoryInvalidationReason::none;
        contract.previous_available = previous_available;
        contract.invalidation_reason = previous_available
            ? render_graph::FrameHistoryInvalidationReason::none
            : invalidation_reason;
        if (previous_available) {
            contract.current_clip_to_previous_clip =
                ecs::spatial_math::MultiplyMatrix4x4(
                    frame_reprojection.previous_view_projection,
                    current_inverse_view_projection);
            contract.previous_frame_index = frame_reprojection.previous_frame_index;
            contract.previous_submission_id =
                frame_reprojection.previous_submission_id;
        }

        frame_reprojection.reset_requested = false;
        frame_reprojection.active_dimension = desired_dimension;
        frame_reprojection.last_invalidation_reason =
            contract.invalidation_reason;
    }

    template<ecs::DimensionTag DimensionT>
    void PrepareTemporalJitterContract(
        render_graph::FrameSnapshot<DimensionT>& snapshot_,
        const render_graph::FrameHistoryInvalidationReason fallback_invalidation_reason_,
        const std::uint8_t desired_dimension) {
        auto& contract = snapshot_.temporal.jitter;
        contract = {};
        ResetPendingJitterPublish();

        if constexpr (!std::is_same_v<DimensionT, ecs::Dim3>) {
            frame_jitter.previous_available = false;
            frame_jitter.reset_requested = false;
            frame_jitter.active_dimension = desired_dimension;
            frame_jitter.last_invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            contract.invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            return;
        }

        render_graph::FrameHistoryInvalidationReason invalidation_reason =
            fallback_invalidation_reason_;
        const auto* scene_view = snapshot_.SceneView();
        const bool valid_extent =
            snapshot_.reference_extent.width != 0U &&
            snapshot_.reference_extent.height != 0U &&
            snapshot_.reference_extent.depth != 0U;
        if (!valid_extent || scene_view == nullptr || scene_view->has_camera == 0U) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            frame_jitter.previous_available = false;
            frame_jitter.reset_requested = false;
            frame_jitter.active_dimension = desired_dimension;
            frame_jitter.last_invalidation_reason = invalidation_reason;
            contract.invalidation_reason = invalidation_reason;
            return;
        }

        if (invalidation_reason == render_graph::FrameHistoryInvalidationReason::none) {
            if (frame_jitter.reset_requested) {
                invalidation_reason =
                    render_graph::FrameHistoryInvalidationReason::reset_requested;
            } else if (frame_jitter.active_dimension != desired_dimension) {
                invalidation_reason =
                    render_graph::FrameHistoryInvalidationReason::dimension_changed;
            } else if (!frame_jitter.previous_available) {
                invalidation_reason =
                    render_graph::FrameHistoryInvalidationReason::first_frame;
            }
        }

        const auto jitter_uv =
            BuildTemporalJitterUv(snapshot_.frame_index, snapshot_.reference_extent);
        contract.current_uv_x = jitter_uv[0U];
        contract.current_uv_y = jitter_uv[1U];
        contract.current_available = true;

        frame_jitter.pending_uv_x = jitter_uv[0U];
        frame_jitter.pending_uv_y = jitter_uv[1U];
        frame_jitter.pending_frame_index = snapshot_.frame_index;
        frame_jitter.pending_submission_id = snapshot_.submission_id;
        frame_jitter.pending_available = true;

        const bool previous_available =
            frame_jitter.previous_available &&
            invalidation_reason == render_graph::FrameHistoryInvalidationReason::none;
        contract.previous_available = previous_available;
        contract.invalidation_reason = previous_available
            ? render_graph::FrameHistoryInvalidationReason::none
            : invalidation_reason;
        if (previous_available) {
            contract.previous_uv_x = frame_jitter.previous_uv_x;
            contract.previous_uv_y = frame_jitter.previous_uv_y;
            contract.previous_frame_index = frame_jitter.previous_frame_index;
            contract.previous_submission_id = frame_jitter.previous_submission_id;
        }

        frame_jitter.reset_requested = false;
        frame_jitter.active_dimension = desired_dimension;
        frame_jitter.last_invalidation_reason = contract.invalidation_reason;
    }

    template<typename ContextT>
    void PrepareSurfaceHistoryContract(ContextT& context_,
                                       FrameHistoryState& history_,
                                       render_graph::FrameHistorySurfaceContract& contract_,
                                       const render_graph::TextureDesc& desired_desc_,
                                       const std::string_view target_debug_name_,
                                       const bool source_available,
                                       const std::uint8_t desired_dimension) {
        auto& device = vr::runtime::detail::ResolveDevice(context_);
        auto& services = vr::runtime::detail::ResolveServices(context_);
        auto& render_target_host = services.template Get<RenderTargetService>().Host();
        contract_ = {};
        contract_.desc = desired_desc_;
        ResetPendingHistoryPublish(history_);

        const bool valid_extent =
            desired_desc_.extent.width != 0U &&
            desired_desc_.extent.height != 0U &&
            desired_desc_.extent.depth != 0U;

        render_graph::FrameHistoryInvalidationReason invalidation_reason =
            render_graph::FrameHistoryInvalidationReason::none;
        if (!source_available || !valid_extent) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
        } else if (!history_.initialized) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::first_frame;
        } else if (history_.reset_requested) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::reset_requested;
        } else if (history_.active_dimension != desired_dimension) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::dimension_changed;
        } else if (history_.desc.extent.width != desired_desc_.extent.width ||
                   history_.desc.extent.height != desired_desc_.extent.height ||
                   history_.desc.extent.depth != desired_desc_.extent.depth) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::extent_changed;
        }

        if (invalidation_reason ==
            render_graph::FrameHistoryInvalidationReason::source_unavailable) {
            history_.initialized = false;
            history_.published_slot = invalid_frame_index;
            history_.active_dimension = desired_dimension;
            history_.desc = desired_desc_;
            history_.last_invalidation_reason = invalidation_reason;
            history_.reset_requested = false;
            contract_.invalidation_reason = invalidation_reason;
            return;
        }

        const auto target_resource_desc =
            render_graph::VulkanResourceTable::BuildRenderTargetDesc(desired_desc_);
        render::RenderTargetDesc target_desc{};
        target_desc.debug_name = target_debug_name_.data();
        target_desc.dimension = render::RenderTargetDimension::image_2d;
        target_desc.lifetime = render::RenderTargetLifetime::history;
        target_desc.scale_mode = render::RenderTargetScaleMode::absolute;
        target_desc.width = desired_desc_.extent.width;
        target_desc.height = desired_desc_.extent.height;
        target_desc.depth = desired_desc_.extent.depth;
        target_desc.format = target_resource_desc.format;
        target_desc.samples = target_resource_desc.samples;
        target_desc.usage = target_resource_desc.usage;
        target_desc.aspect = target_resource_desc.aspect;
        target_desc.mip_levels = desired_desc_.mip_level_count;
        target_desc.array_layers = desired_desc_.array_layer_count;
        target_desc.color_encoding = render::RenderTargetColorEncoding::linear;
        target_desc.memory_policy = render::RenderTargetMemoryPolicy::auto_select;
        target_desc.allow_uav = false;
        target_desc.allow_alias = false;
        target_desc.allow_history = true;

        const std::uint64_t last_submitted =
            vr::runtime::detail::ResolveGraphicsSubmitted(context_);
        const std::uint64_t completed_submitted =
            vr::runtime::detail::ResolveGraphicsCompleted(context_);

        bool revision_changed = false;
        for (std::uint32_t slot_index = 0U; slot_index < history_.slots.size(); ++slot_index) {
            const auto ensure = render_target_host.EnsurePersistentTarget(
                device,
                history_.slots[slot_index].handle,
                target_desc,
                VkExtent2D{desired_desc_.extent.width, desired_desc_.extent.height},
                last_submitted,
                completed_submitted);
            history_.slots[slot_index].handle = ensure.handle;
            if (const auto resolved_view =
                    render_target_host.Resolve(ensure.handle);
                resolved_view != nullptr) {
                history_.slots[slot_index].resource_revision =
                    resolved_view->resource_revision;
            } else {
                history_.slots[slot_index].resource_revision = 0U;
            }
            revision_changed =
                revision_changed || ensure.created || ensure.recreated ||
                ensure.revision_changed;
        }

        if (!history_.initialized) {
            history_.published_slot = invalid_frame_index;
            history_.write_slot = 0U;
        } else if (history_.published_slot == invalid_frame_index) {
            history_.write_slot = 0U;
        } else {
            history_.write_slot = history_.published_slot ^ 1U;
        }

        if (revision_changed &&
            invalidation_reason ==
                render_graph::FrameHistoryInvalidationReason::none) {
            invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::extent_changed;
        }
        if (history_.published_slot == invalid_frame_index &&
            invalidation_reason ==
                render_graph::FrameHistoryInvalidationReason::none) {
            invalidation_reason =
                history_.last_invalidation_reason ==
                    render_graph::FrameHistoryInvalidationReason::none
                ? render_graph::FrameHistoryInvalidationReason::first_frame
                : history_.last_invalidation_reason;
        }

        const bool previous_available =
            history_.published_slot != invalid_frame_index &&
            invalidation_reason ==
                render_graph::FrameHistoryInvalidationReason::none &&
            history_.slots[history_.published_slot].resource_revision != 0U;

        contract_.current = render_graph::FrameHistoryResourceIdentity{
            .handle = history_.slots[history_.write_slot].handle,
            .resource_revision = history_.slots[history_.write_slot].resource_revision,
        };
        contract_.current_writable =
            render_graph::IsValidFrameHistoryResourceIdentity(
                contract_.current);
        contract_.previous_available = previous_available;
        contract_.invalidation_reason = previous_available
            ? render_graph::FrameHistoryInvalidationReason::none
            : invalidation_reason;
        if (previous_available) {
            contract_.previous =
                render_graph::FrameHistoryResourceIdentity{
                    .handle = history_.slots[history_.published_slot].handle,
                    .resource_revision =
                        history_.slots[history_.published_slot].resource_revision,
                };
            contract_.previous_frame_index = history_.previous_frame_index;
            contract_.previous_submission_id = history_.previous_submission_id;
        }

        history_.initialized = true;
        history_.desc = desired_desc_;
        history_.active_dimension = desired_dimension;
        history_.last_invalidation_reason = contract_.invalidation_reason;
        history_.reset_requested = false;
        if (!previous_available) {
            history_.published_slot = invalid_frame_index;
        }
    }

    template<typename ContextT, ecs::DimensionTag DimensionT>
    void PrepareFrameTemporalContracts(ContextT& context_,
                                       render_graph::FrameSnapshot<DimensionT>& snapshot_) {
        const bool has_active_view =
            snapshot_.ViewCount() != 0U && snapshot_.ActiveView() != nullptr;
        const auto* scene_view = snapshot_.SceneView();
        const bool has_scene_view = snapshot_.HasSceneView() && scene_view != nullptr;
        const bool has_scene_camera =
            has_scene_view && scene_view->has_camera != 0U;
        const std::uint8_t desired_dimension =
            std::is_same_v<DimensionT, ecs::Dim3> ? 3U : 2U;
        snapshot_.temporal = {};

        PrepareSurfaceHistoryContract(
            context_,
            frame_color_history,
            snapshot_.temporal.color,
            render_graph::detail::MakeFrameColorHistoryDesc(snapshot_),
            "frame_color_history",
            has_active_view,
            desired_dimension);

        if constexpr (std::is_same_v<DimensionT, ecs::Dim3>) {
            PrepareSurfaceHistoryContract(
                context_,
                frame_depth_history,
                snapshot_.temporal.depth,
                render_graph::detail::MakeFrameDepthHistoryDesc(snapshot_),
                "frame_depth_history",
                has_active_view && has_scene_view,
                desired_dimension);
            PrepareSurfaceHistoryContract(
                context_,
                frame_motion_history,
                snapshot_.temporal.motion,
                render_graph::detail::MakeFrameMotionHistoryDesc(snapshot_),
                "frame_motion_history",
                has_active_view && has_scene_camera,
                desired_dimension);
            PrepareTemporalJitterContract(
                snapshot_,
                snapshot_.temporal.color.invalidation_reason,
                desired_dimension);
            PrepareTemporalReprojectionContract(
                snapshot_,
                snapshot_.temporal.color.invalidation_reason,
                desired_dimension);
        } else {
            ResetPendingHistoryPublish(frame_depth_history);
            frame_depth_history.initialized = false;
            frame_depth_history.published_slot = invalid_frame_index;
            frame_depth_history.reset_requested = false;
            frame_depth_history.last_invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            snapshot_.temporal.depth.desc =
                render_graph::detail::MakeFrameDepthHistoryDesc(snapshot_);
            snapshot_.temporal.depth.invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            ResetPendingHistoryPublish(frame_motion_history);
            frame_motion_history.initialized = false;
            frame_motion_history.published_slot = invalid_frame_index;
            frame_motion_history.reset_requested = false;
            frame_motion_history.last_invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            snapshot_.temporal.motion.desc =
                render_graph::detail::MakeFrameMotionHistoryDesc(snapshot_);
            snapshot_.temporal.motion.invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            ResetPendingReprojectionPublish();
            frame_reprojection.previous_available = false;
            frame_reprojection.reset_requested = false;
            frame_reprojection.active_dimension = desired_dimension;
            frame_reprojection.last_invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            snapshot_.temporal.reprojection.invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            ResetPendingJitterPublish();
            frame_jitter.previous_available = false;
            frame_jitter.reset_requested = false;
            frame_jitter.active_dimension = desired_dimension;
            frame_jitter.last_invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
            snapshot_.temporal.jitter.invalidation_reason =
                render_graph::FrameHistoryInvalidationReason::source_unavailable;
        }
    }

    template<ecs::DimensionTag DimensionT>
    void AppendFrameColorHistoryCapturePass(
        const render_graph::FrameSnapshot<DimensionT>& snapshot_,
        const render_graph::MinimalFrameGraphBuildResult<DimensionT>& build_result_) {
        if (!snapshot_.temporal.color.current_writable ||
            !render::IsValidRenderTargetHandle(
                snapshot_.temporal.color.current.handle) ||
            !render_graph::IsValidResourceVersionHandle(
                build_result_.final_color_version) ||
            !render_graph::IsValidResourceHandle(build_result_.final_color)) {
            return;
        }

        const auto history_target = builder.CreateTexture(
            "frame_color_history_current",
            snapshot_.temporal.color.desc,
            render_graph::ResourceLifetime::imported);
        RegisterImportedTexture(history_target, snapshot_.temporal.color.current.handle);

        const auto history_capture_pass =
            builder.AddPass("frame_color_history_capture", true);
        (void)builder.Read(history_capture_pass,
                           build_result_.final_color_version,
                           render_graph::AccessDesc{
                               .access =
                                   render_graph::AccessKind::transfer_read,
                           });
        (void)builder.Write(history_capture_pass,
                            history_target,
                            render_graph::AccessDesc{
                                .access =
                                    render_graph::AccessKind::transfer_write,
                            });
        builder.SetExecuteCallback(
            history_capture_pass,
            [source = build_result_.final_color,
             target = history_target](render_graph::GraphCommandContext& context_) {
                render_graph::detail::RecordTextureCopyOrBlit(
                    context_,
                    source,
                    target);
            });

        frame_color_history.pending_publish_slot = frame_color_history.write_slot;
        frame_color_history.pending_frame_index = snapshot_.frame_index;
        frame_color_history.pending_submission_id = snapshot_.submission_id;
    }

    template<ecs::DimensionTag DimensionT>
    void AppendFrameDepthHistoryCapturePass(
        const render_graph::FrameSnapshot<DimensionT>& snapshot_,
        const render_graph::MinimalFrameGraphBuildResult<DimensionT>& build_result_) {
        if (!snapshot_.temporal.depth.current_writable ||
            !render::IsValidRenderTargetHandle(
                snapshot_.temporal.depth.current.handle) ||
            !build_result_.has_depth ||
            !render_graph::IsValidResourceHandle(build_result_.scene_depth)) {
            return;
        }

        const auto history_target = builder.CreateTexture(
            "frame_depth_history_current",
            snapshot_.temporal.depth.desc,
            render_graph::ResourceLifetime::imported);
        RegisterImportedTexture(history_target, snapshot_.temporal.depth.current.handle);

        const auto history_capture_pass =
            builder.AddPass("frame_depth_history_capture", true);
        (void)builder.Read(history_capture_pass,
                           build_result_.scene_depth,
                           render_graph::AccessDesc{
                               .access =
                                   render_graph::AccessKind::transfer_read,
                           });
        (void)builder.Write(history_capture_pass,
                            history_target,
                            render_graph::AccessDesc{
                                .access =
                                    render_graph::AccessKind::transfer_write,
                            });
        builder.SetExecuteCallback(
            history_capture_pass,
            [source = build_result_.scene_depth,
             target = history_target](render_graph::GraphCommandContext& context_) {
                render_graph::detail::RecordTextureCopy(
                    context_,
                    source,
                    target,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
            });

        frame_depth_history.pending_publish_slot = frame_depth_history.write_slot;
        frame_depth_history.pending_frame_index = snapshot_.frame_index;
        frame_depth_history.pending_submission_id = snapshot_.submission_id;
    }

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
    vr::runtime::DiagnosticsLevel diagnostics_level =
        vr::runtime::DiagnosticsLevel::Detailed;
    vr::runtime::RenderGraphRuntimeDiagnostics last_diagnostics{};
#if VR_ENABLE_DEBUG_OBSERVABILITY
    std::vector<vr::runtime::RenderGraphQueueBatchTimingDiagnostics>
        last_recorded_queue_batch_timings{};
    std::uint64_t last_recorded_timing_total_duration_ns = 0U;
#endif
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
    FrameHistoryState frame_color_history{};
    FrameHistoryState frame_depth_history{};
    FrameHistoryState frame_motion_history{};
    FrameTemporalReprojectionState frame_reprojection{};
    FrameTemporalJitterState frame_jitter{};

    static constexpr bool kRenderGraphMultiQueueSubmitEnabled = true;
};

} // namespace vr::runtime::services
