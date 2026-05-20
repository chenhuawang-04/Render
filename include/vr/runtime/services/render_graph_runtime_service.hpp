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
#include <cstddef>
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <sstream>
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

    void BeginFrame(const std::uint32_t frame_index_) noexcept {
        frame_index = frame_index_;
        builder.Reset();
        compiled_graph = {};
        lowered_vulkan_barriers = {};
        command_ready_vulkan_barriers = {};
        record_stats = {};
        prepared_multi_queue_submission = {};
        graph_build_callback_2d = {};
        graph_build_callback_3d = {};
        has_compiled_graph = false;
        queue_execution_policy = {};
        frame_snapshot = std::monostate{};
        direct_imported_textures.clear();
        direct_imported_buffers.clear();
        external_queue_waits.clear();
        strict_graph_only_record_required = false;
        last_diagnostics = {};
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

    void Shutdown(VulkanContext& device_) noexcept {
        DestroyMultiQueueResources(device_);
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

    void RequireStrictGraphOnlyRecord(const bool value_ = true) noexcept {
        strict_graph_only_record_required = value_;
    }

    [[nodiscard]] bool StrictGraphOnlyRecordRequired() const noexcept {
        return strict_graph_only_record_required;
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
        const render_graph::ImportedBufferBinding& imported_buffer_) {
        if (!render_graph::IsValidResourceHandle(logical_) ||
            imported_buffer_.buffer == VK_NULL_HANDLE ||
            imported_buffer_.size_bytes == 0U) {
            return;
        }
        const auto existing = std::find_if(
            direct_imported_buffers.begin(),
            direct_imported_buffers.end(),
            [&](const ImportedBufferBinding& binding_) {
                return binding_.logical.index == logical_.index;
            });
        if (existing != direct_imported_buffers.end()) {
            existing->logical = logical_;
            existing->imported_buffer = imported_buffer_;
            return;
        }
        direct_imported_buffers.push_back(ImportedBufferBinding{
            .logical = logical_,
            .imported_buffer = imported_buffer_,
        });
    }

    void RegisterExternalQueueSubmitWait(const render_graph::QueueClass queue_,
                                         const VkSemaphore semaphore_) {
        if (semaphore_ == VK_NULL_HANDLE) {
            return;
        }
        const auto existing = std::find_if(
            external_queue_waits.begin(),
            external_queue_waits.end(),
            [&](const ExternalQueueWait& wait_) {
                return wait_.queue == queue_ && wait_.semaphore == semaphore_;
            });
        if (existing == external_queue_waits.end()) {
            external_queue_waits.push_back(ExternalQueueWait{
                .queue = queue_,
                .semaphore = semaphore_,
            });
        }
    }

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

    [[nodiscard]] VkResult SubmitPreparedMultiQueueWork(VulkanContext& device_) {
        if (!prepared_multi_queue_submission.active ||
            prepared_multi_queue_submission.submitted) {
            return VK_SUCCESS;
        }
        if (device_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
            throw std::runtime_error(
                "RenderGraphRuntimeService multi-queue submit requires Vulkan synchronization2");
        }

        VkResult last_result = VK_SUCCESS;
        for (auto& batch_ : prepared_multi_queue_submission.owned_submit_batches) {
            std::vector<VkSemaphoreSubmitInfo> wait_infos(batch_.wait_semaphores.size());
            std::vector<VkSemaphoreSubmitInfo> signal_infos(batch_.signal_semaphores.size());

            for (std::size_t wait_index = 0; wait_index < batch_.wait_semaphores.size(); ++wait_index) {
                wait_infos[wait_index].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                wait_infos[wait_index].semaphore = batch_.wait_semaphores[wait_index];
                wait_infos[wait_index].value = 0U;
                wait_infos[wait_index].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                wait_infos[wait_index].deviceIndex = 0U;
            }
            for (std::size_t signal_index = 0; signal_index < batch_.signal_semaphores.size(); ++signal_index) {
                signal_infos[signal_index].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                signal_infos[signal_index].semaphore = batch_.signal_semaphores[signal_index];
                signal_infos[signal_index].value = 0U;
                signal_infos[signal_index].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                signal_infos[signal_index].deviceIndex = 0U;
            }

            VkCommandBufferSubmitInfo command_buffer_info{};
            command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command_buffer_info.commandBuffer = batch_.command_buffer;
            command_buffer_info.deviceMask = 0U;

            VkSubmitInfo2 submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount =
                static_cast<std::uint32_t>(batch_.wait_semaphores.size());
            submit_info.pWaitSemaphoreInfos =
                submit_info.waitSemaphoreInfoCount != 0U ? wait_infos.data() : nullptr;
            submit_info.commandBufferInfoCount = 1U;
            submit_info.pCommandBufferInfos = &command_buffer_info;
            submit_info.signalSemaphoreInfoCount =
                static_cast<std::uint32_t>(batch_.signal_semaphores.size());
            submit_info.pSignalSemaphoreInfos =
                submit_info.signalSemaphoreInfoCount != 0U ? signal_infos.data() : nullptr;

            last_result = vkQueueSubmit2(batch_.submit_queue, 1U, &submit_info, VK_NULL_HANDLE);
            if (last_result != VK_SUCCESS) {
                throw std::runtime_error("RenderGraphRuntimeService multi-queue vkQueueSubmit2 failed");
            }

            if (batch_.queue == render_graph::QueueClass::transfer) {
                const std::uint64_t submit_value = next_transfer_submit_value++;
                last_transfer_submitted_value =
                    (std::max)(last_transfer_submitted_value, submit_value);
                prepared_multi_queue_submission.transfer_submitted_value =
                    (std::max)(prepared_multi_queue_submission.transfer_submitted_value, submit_value);
            } else if (batch_.queue == render_graph::QueueClass::compute) {
                const std::uint64_t submit_value = next_compute_submit_value++;
                last_compute_submitted_value =
                    (std::max)(last_compute_submitted_value, submit_value);
                prepared_multi_queue_submission.compute_submitted_value =
                    (std::max)(prepared_multi_queue_submission.compute_submitted_value, submit_value);
            }
        }

        prepared_multi_queue_submission.submitted = true;
        return last_result;
    }

    void MarkGraphicsSubmissionEnqueued(const vr::render::FrameToken& token_) noexcept {
        if (!prepared_multi_queue_submission.active ||
            !prepared_multi_queue_submission.submitted ||
            token_.frame_index >= multi_queue_frame_slots.size()) {
            prepared_multi_queue_submission = {};
            return;
        }

        auto& slot = multi_queue_frame_slots[token_.frame_index];
        slot.pending_transfer_value =
            (std::max)(slot.pending_transfer_value,
                       prepared_multi_queue_submission.transfer_submitted_value);
        slot.pending_compute_value =
            (std::max)(slot.pending_compute_value,
                       prepared_multi_queue_submission.compute_submitted_value);
        slot.completion_graphics_value = token_.graphics_signal_value;
        slot.pending_completion =
            slot.pending_transfer_value != 0U ||
            slot.pending_compute_value != 0U;
        prepared_multi_queue_submission = {};
    }

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

    static void AppendDependencyInfoData(render_graph::VulkanDependencyInfoData& destination_,
                                         const render_graph::VulkanDependencyInfoData& source_) {
        destination_.memory_barriers.insert(destination_.memory_barriers.end(),
                                            source_.memory_barriers.begin(),
                                            source_.memory_barriers.end());
        destination_.buffer_barriers.insert(destination_.buffer_barriers.end(),
                                            source_.buffer_barriers.begin(),
                                            source_.buffer_barriers.end());
        destination_.image_barriers.insert(destination_.image_barriers.end(),
                                           source_.image_barriers.begin(),
                                           source_.image_barriers.end());
    }

    static void AppendSemaphoreUnique(std::vector<VkSemaphore>& semaphores_,
                                      const VkSemaphore semaphore_) {
        if (semaphore_ == VK_NULL_HANDLE) {
            return;
        }
        if (std::find(semaphores_.begin(), semaphores_.end(), semaphore_) == semaphores_.end()) {
            semaphores_.push_back(semaphore_);
        }
    }

    static void AppendFrameSubmitWaitUnique(std::vector<vr::render::FrameSubmitWait>& waits_,
                                            const vr::render::FrameSubmitWait& wait_) {
        if (wait_.semaphore == VK_NULL_HANDLE) {
            return;
        }
        const auto existing = std::find_if(
            waits_.begin(),
            waits_.end(),
            [&](const vr::render::FrameSubmitWait& candidate_) {
                return candidate_.semaphore == wait_.semaphore &&
                       candidate_.stage_mask == wait_.stage_mask;
            });
        if (existing == waits_.end()) {
            waits_.push_back(wait_);
        }
    }

    [[nodiscard]] static std::string_view QueueClassName(
        const render_graph::QueueClass queue_) noexcept {
        switch (queue_) {
        case render_graph::QueueClass::graphics:
            return "graphics";
        case render_graph::QueueClass::compute:
            return "compute";
        case render_graph::QueueClass::transfer:
            return "transfer";
        default:
            break;
        }
        return "unknown";
    }

    [[nodiscard]] const render_graph::CompiledPass* TryFindPass(
        const render_graph::PassHandle pass_) const noexcept {
        if (pass_.index >= compiled_graph.Passes().size()) {
            return nullptr;
        }
        return &compiled_graph.Passes()[pass_.index];
    }

    [[nodiscard]] std::string ResolvePassDebugName(
        const render_graph::PassHandle pass_) const {
        if (const auto* compiled_pass = TryFindPass(pass_);
            compiled_pass != nullptr) {
            return compiled_pass->debug_name;
        }
        return {};
    }

    [[nodiscard]] const PreparedQueueSubmitBatch* TryFindPreparedSubmitBatch(
        const std::uint32_t batch_index_) const noexcept {
        const auto existing = std::find_if(
            prepared_multi_queue_submission.owned_submit_batches.begin(),
            prepared_multi_queue_submission.owned_submit_batches.end(),
            [&](const PreparedQueueSubmitBatch& batch_) {
                return batch_.batch_index == batch_index_;
            });
        return existing != prepared_multi_queue_submission.owned_submit_batches.end()
            ? &(*existing)
            : nullptr;
    }

    static void AppendIndexListToStream(std::ostringstream& oss_,
                                        const std::vector<std::uint32_t>& values_) {
        oss_ << '[';
        for (std::size_t index = 0U; index < values_.size(); ++index) {
            if (index != 0U) {
                oss_ << ',';
            }
            oss_ << values_[index];
        }
        oss_ << ']';
    }

    static void AppendStringListToStream(std::ostringstream& oss_,
                                         const std::vector<std::string>& values_) {
        oss_ << '[';
        for (std::size_t index = 0U; index < values_.size(); ++index) {
            if (index != 0U) {
                oss_ << ',';
            }
            oss_ << values_[index];
        }
        oss_ << ']';
    }

    [[nodiscard]] static bool BarrierPlanContainsHostBoundary(
        const render_graph::BarrierPlan& barrier_plan_) noexcept {
        return std::any_of(
                   barrier_plan_.queue_batches.begin(),
                   barrier_plan_.queue_batches.end(),
                   [](const render_graph::QueueSubmitBatch& batch_) {
                       return batch_.contains_host_boundary;
                   }) ||
               std::any_of(
                   barrier_plan_.queue_dependencies.begin(),
                   barrier_plan_.queue_dependencies.end(),
                   [](const render_graph::QueueDependencyPlan& dependency_) {
                       return dependency_.host_boundary;
                   });
    }

    void BuildEffectiveQueueDiagnostics(
        vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) const {
        diagnostics_.graphics_submit_wait_count = static_cast<std::uint32_t>(
            prepared_multi_queue_submission.graphics_waits.size());
        diagnostics_.non_graphics_submit_batch_count = static_cast<std::uint32_t>(
            prepared_multi_queue_submission.owned_submit_batches.size());

        if (!has_compiled_graph) {
            return;
        }

        const auto& barrier_plan = compiled_graph.PlannedBarriers();
        const bool preserve_host_boundary_fallback_topology =
            queue_execution_policy.graphics_fallback_active &&
            !queue_execution_policy.multi_queue_enabled &&
            !barrier_plan.queue_batches.empty() &&
            BarrierPlanContainsHostBoundary(barrier_plan);
        const bool preserve_effective_queue_topology =
            (queue_execution_policy.multi_queue_enabled ||
             preserve_host_boundary_fallback_topology) &&
            !barrier_plan.queue_batches.empty();
        const auto resolve_effective_queue_name =
            [&](const render_graph::QueueClass queue_) -> std::string {
                return preserve_host_boundary_fallback_topology
                    ? std::string("graphics")
                    : std::string(QueueClassName(queue_));
            };

        if (preserve_effective_queue_topology) {
            diagnostics_.effective_queue_batches.reserve(barrier_plan.queue_batches.size());
            diagnostics_.effective_queue_dependencies.reserve(barrier_plan.queue_dependencies.size());

            for (std::uint32_t dependency_index = 0U;
                 dependency_index < static_cast<std::uint32_t>(barrier_plan.queue_dependencies.size());
                 ++dependency_index) {
                const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
                diagnostics_.effective_queue_dependencies.push_back(
                    vr::runtime::RenderGraphQueueDependencyDiagnostics{
                        .dependency_index = dependency_index,
                        .source_queue_name = resolve_effective_queue_name(dependency_.source_queue),
                        .target_queue_name = resolve_effective_queue_name(dependency_.target_queue),
                        .source_batch_index = dependency_.source_batch_index,
                        .target_batch_index = dependency_.target_batch_index,
                        .source_pass_index = dependency_.source_pass.index,
                        .target_pass_index = dependency_.target_pass.index,
                        .source_pass_debug_name = ResolvePassDebugName(dependency_.source_pass),
                        .target_pass_debug_name = ResolvePassDebugName(dependency_.target_pass),
                        .resource_count = static_cast<std::uint32_t>(dependency_.resources.size()),
                        .queue_transfer = dependency_.queue_transfer,
                        .host_boundary = dependency_.host_boundary,
                    });
            }

            for (std::uint32_t batch_index = 0U;
                 batch_index < static_cast<std::uint32_t>(barrier_plan.queue_batches.size());
                 ++batch_index) {
                const auto& batch_ = barrier_plan.queue_batches[batch_index];
                vr::runtime::RenderGraphQueueBatchDiagnostics batch_diagnostics{};
                batch_diagnostics.batch_index = batch_index;
                batch_diagnostics.queue_name = resolve_effective_queue_name(batch_.queue);
                batch_diagnostics.wait_dependency_indices = batch_.wait_dependency_indices;
                batch_diagnostics.signal_dependency_indices = batch_.signal_dependency_indices;
                batch_diagnostics.barrier_batch_indices = batch_.barrier_batch_indices;
                batch_diagnostics.contains_host_boundary = batch_.contains_host_boundary;
                batch_diagnostics.pass_indices.reserve(batch_.passes.size());
                batch_diagnostics.pass_debug_names.reserve(batch_.passes.size());
                for (const auto pass_handle_ : batch_.passes) {
                    batch_diagnostics.pass_indices.push_back(pass_handle_.index);
                    batch_diagnostics.pass_debug_names.push_back(
                        ResolvePassDebugName(pass_handle_));
                }

                if (!preserve_host_boundary_fallback_topology) {
                    if (const auto* prepared_batch = TryFindPreparedSubmitBatch(batch_index);
                        prepared_batch != nullptr) {
                        batch_diagnostics.submit_wait_count =
                            static_cast<std::uint32_t>(prepared_batch->wait_semaphores.size());
                        batch_diagnostics.submit_signal_count =
                            static_cast<std::uint32_t>(prepared_batch->signal_semaphores.size());
                        batch_diagnostics.submitted_on_owned_queue = true;
                    } else if (batch_index == prepared_multi_queue_submission.graphics_batch_index) {
                        batch_diagnostics.submit_wait_count = diagnostics_.graphics_submit_wait_count;
                    }
                }

                diagnostics_.effective_queue_batches.push_back(std::move(batch_diagnostics));
            }
        } else {
            vr::runtime::RenderGraphQueueBatchDiagnostics batch_diagnostics{};
            batch_diagnostics.batch_index = 0U;
            batch_diagnostics.queue_name = "graphics";
            for (const auto& pass_ : compiled_graph.Passes()) {
                if (!pass_.executable) {
                    continue;
                }
                batch_diagnostics.pass_indices.push_back(pass_.handle.index);
                batch_diagnostics.pass_debug_names.push_back(pass_.debug_name);
            }
            if (!batch_diagnostics.pass_indices.empty()) {
                diagnostics_.effective_queue_batches.push_back(std::move(batch_diagnostics));
            }
        }

        diagnostics_.effective_queue_batch_count = static_cast<std::uint32_t>(
            diagnostics_.effective_queue_batches.size());
        diagnostics_.effective_queue_dependency_count = static_cast<std::uint32_t>(
            diagnostics_.effective_queue_dependencies.size());
    }

    [[nodiscard]] std::string BuildEffectiveQueueTimelineDebugString(
        const vr::runtime::RenderGraphRuntimeDiagnostics& diagnostics_) const {
        std::ostringstream oss{};
        oss << "requested transfer=" << (diagnostics_.transfer_queue_requested ? 1 : 0)
            << " compute=" << (diagnostics_.compute_queue_requested ? 1 : 0)
            << " multi=" << (diagnostics_.multi_queue_requested ? 1 : 0) << '\n';
        oss << "enabled transfer=" << (diagnostics_.transfer_queue_enabled ? 1 : 0)
            << " compute=" << (diagnostics_.compute_queue_enabled ? 1 : 0)
            << " multi=" << (diagnostics_.multi_queue_enabled ? 1 : 0)
            << " graphics_fallback=" << (diagnostics_.graphics_fallback_active ? 1 : 0)
            << '\n';
        oss << "effective_batches=" << diagnostics_.effective_queue_batch_count
            << " effective_dependencies=" << diagnostics_.effective_queue_dependency_count
            << " graphics_submit_waits=" << diagnostics_.graphics_submit_wait_count
            << " owned_submit_batches=" << diagnostics_.non_graphics_submit_batch_count
            << '\n';
        for (const auto& batch_ : diagnostics_.effective_queue_batches) {
            oss << "batch[" << batch_.batch_index << "] queue=" << batch_.queue_name
                << " passes=";
            AppendStringListToStream(oss, batch_.pass_debug_names);
            oss << " wait_deps=";
            AppendIndexListToStream(oss, batch_.wait_dependency_indices);
            oss << " signal_deps=";
            AppendIndexListToStream(oss, batch_.signal_dependency_indices);
            oss << " submit_waits=" << batch_.submit_wait_count
                << " submit_signals=" << batch_.submit_signal_count
                << " host_boundary=" << (batch_.contains_host_boundary ? 1 : 0)
                << " owned_submit=" << (batch_.submitted_on_owned_queue ? 1 : 0)
                << '\n';
        }
        for (const auto& dependency_ : diagnostics_.effective_queue_dependencies) {
            oss << "dependency[" << dependency_.dependency_index << "] "
                << dependency_.source_queue_name << '[' << dependency_.source_batch_index << ']'
                << " -> " << dependency_.target_queue_name << '[' << dependency_.target_batch_index << ']'
                << " source_pass=" << dependency_.source_pass_debug_name
                << " target_pass=" << dependency_.target_pass_debug_name
                << " resources=" << dependency_.resource_count
                << " queue_transfer=" << (dependency_.queue_transfer ? 1 : 0)
                << " host_boundary=" << (dependency_.host_boundary ? 1 : 0)
                << '\n';
        }
        return oss.str();
    }

    [[nodiscard]] std::string ResolveUnsupportedMultiQueueTopologyReason(
        const render_graph::BarrierPlan& barrier_plan_) const {
        if (barrier_plan_.queue_batches.empty()) {
            return "RenderGraph multi-queue submit requires at least one queue batch";
        }

        for (std::uint32_t batch_index = 0U;
             batch_index < static_cast<std::uint32_t>(barrier_plan_.queue_batches.size());
             ++batch_index) {
            const auto& batch_ = barrier_plan_.queue_batches[batch_index];
            if (batch_.contains_host_boundary) {
                return "RenderGraph multi-queue submit does not yet support host boundary queue batches";
            }
        }

        if (barrier_plan_.queue_batches.back().queue != render_graph::QueueClass::graphics) {
            return "RenderGraph multi-queue submit requires a terminal graphics batch for present submission";
        }

        const auto host_boundary = std::find_if(
            barrier_plan_.queue_dependencies.begin(),
            barrier_plan_.queue_dependencies.end(),
            [](const render_graph::QueueDependencyPlan& dependency_) {
                return dependency_.host_boundary;
            });
        if (host_boundary != barrier_plan_.queue_dependencies.end()) {
            return "RenderGraph multi-queue submit does not yet support host boundary dependencies";
        }

        return {};
    }

    void ApplyGraphicsFallback(const QueueFamilyIndices& queue_families_,
                               std::string reason_) {
        queue_execution_policy.effective_queue_families =
            render_graph::BuildGraphicsOnlyQueueFamilies(queue_families_);
        queue_execution_policy.transfer_enabled = false;
        queue_execution_policy.compute_enabled = false;
        queue_execution_policy.multi_queue_enabled = false;
        queue_execution_policy.graphics_fallback_active =
            queue_execution_policy.multi_queue_requested;
        queue_execution_policy.fallback_reason = std::move(reason_);
    }

    void BeginFrameMultiQueue(VulkanContext& device_,
                              const std::uint32_t frame_index_,
                              const std::uint64_t graphics_completed_) {
        prepared_multi_queue_submission = {};
        if (frame_index_ == invalid_frame_index) {
            return;
        }

        EnsureMultiQueueFrameSlotCapacity(frame_index_ + 1U);
        MultiQueueFrameSlot& slot = multi_queue_frame_slots[frame_index_];
        if (slot.pending_completion &&
            slot.completion_graphics_value != 0U &&
            graphics_completed_ >= slot.completion_graphics_value) {
            completed_transfer_submit_value =
                (std::max)(completed_transfer_submit_value, slot.pending_transfer_value);
            completed_compute_submit_value =
                (std::max)(completed_compute_submit_value, slot.pending_compute_value);
            slot.pending_transfer_value = 0U;
            slot.pending_compute_value = 0U;
            slot.completion_graphics_value = 0U;
            slot.pending_completion = false;
        }

        ResetQueueCommandResources(device_, slot.graphics);
        ResetQueueCommandResources(device_, slot.transfer);
        ResetQueueCommandResources(device_, slot.compute);
    }

    void DestroyMultiQueueResources(VulkanContext& device_) noexcept {
        const VkDevice vk_device = device_.Device();
        for (auto& slot : multi_queue_frame_slots) {
            DestroyQueueCommandResources(vk_device, slot.graphics);
            DestroyQueueCommandResources(vk_device, slot.transfer);
            DestroyQueueCommandResources(vk_device, slot.compute);
            for (auto& dependency_ : slot.dependency_semaphores) {
                if (vk_device != VK_NULL_HANDLE && dependency_.semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(vk_device, dependency_.semaphore, nullptr);
                }
                dependency_.semaphore = VK_NULL_HANDLE;
            }
            slot.dependency_semaphores.clear();
            slot.pending_transfer_value = 0U;
            slot.pending_compute_value = 0U;
            slot.completion_graphics_value = 0U;
            slot.pending_completion = false;
        }
        multi_queue_frame_slots.clear();
        prepared_multi_queue_submission = {};
        next_transfer_submit_value = 1U;
        last_transfer_submitted_value = 0U;
        completed_transfer_submit_value = 0U;
        next_compute_submit_value = 1U;
        last_compute_submitted_value = 0U;
        completed_compute_submit_value = 0U;
    }

    static void DestroyQueueCommandResources(const VkDevice device_,
                                             QueueCommandResources& resources_) noexcept {
        resources_.primary_buffers.clear();
        resources_.used_primary_count = 0U;
        resources_.queue_family_index = VK_QUEUE_FAMILY_IGNORED;
        if (device_ != VK_NULL_HANDLE && resources_.pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, resources_.pool, nullptr);
        }
        resources_.pool = VK_NULL_HANDLE;
    }

    void EnsureMultiQueueFrameSlotCapacity(const std::uint32_t required_size_) {
        if (required_size_ <= multi_queue_frame_slots.size()) {
            return;
        }
        multi_queue_frame_slots.resize(required_size_);
    }

    void ResetQueueCommandResources(VulkanContext& device_,
                                    QueueCommandResources& resources_) {
        if (resources_.pool == VK_NULL_HANDLE) {
            resources_.used_primary_count = 0U;
            return;
        }
        const VkResult reset_result =
            vkResetCommandPool(device_.Device(), resources_.pool, 0U);
        if (reset_result != VK_SUCCESS) {
            throw std::runtime_error(
                "RenderGraphRuntimeService failed to reset a multi-queue command pool");
        }
        resources_.used_primary_count = 0U;
    }

    [[nodiscard]] std::uint32_t ResolveQueueFamilyIndex(const VulkanContext& device_,
                                                        const render_graph::QueueClass queue_) const {
        switch (queue_) {
        case render_graph::QueueClass::transfer:
            if (!device_.QueueFamilies().transfer.has_value()) {
                break;
            }
            return device_.QueueFamilies().transfer.value();
        case render_graph::QueueClass::compute:
            if (!device_.QueueFamilies().compute.has_value()) {
                break;
            }
            return device_.QueueFamilies().compute.value();
        case render_graph::QueueClass::graphics:
            if (!device_.QueueFamilies().graphics.has_value()) {
                break;
            }
            return device_.QueueFamilies().graphics.value();
        default:
            break;
        }
        throw std::runtime_error(
            "RenderGraphRuntimeService could not resolve a queue family index for the requested queue class");
    }

    [[nodiscard]] VkQueue ResolveSubmitQueue(const VulkanContext& device_,
                                             const render_graph::QueueClass queue_) const noexcept {
        switch (queue_) {
        case render_graph::QueueClass::transfer:
            return device_.TransferQueue();
        case render_graph::QueueClass::compute:
            return device_.ComputeQueue();
        case render_graph::QueueClass::graphics:
            return device_.GraphicsQueue();
        default:
            break;
        }
        return VK_NULL_HANDLE;
    }

    QueueCommandResources& EnsureQueueCommandResources(VulkanContext& device_,
                                                       MultiQueueFrameSlot& slot_,
                                                       const render_graph::QueueClass queue_) {
        QueueCommandResources* resources = nullptr;
        switch (queue_) {
        case render_graph::QueueClass::graphics:
            resources = &slot_.graphics;
            break;
        case render_graph::QueueClass::transfer:
            resources = &slot_.transfer;
            break;
        case render_graph::QueueClass::compute:
            resources = &slot_.compute;
            break;
        default:
            throw std::runtime_error(
                "RenderGraphRuntimeService could not resolve owned command resources for the requested queue");
        }

        const std::uint32_t queue_family_index = ResolveQueueFamilyIndex(device_, queue_);
        if (resources->pool != VK_NULL_HANDLE &&
            resources->queue_family_index != queue_family_index) {
            DestroyQueueCommandResources(device_.Device(), *resources);
        }

        if (resources->pool == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo pool_create_info{};
            pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool_create_info.queueFamilyIndex = queue_family_index;
            const VkResult create_result =
                vkCreateCommandPool(device_.Device(), &pool_create_info, nullptr, &resources->pool);
            if (create_result != VK_SUCCESS) {
                throw std::runtime_error(
                    "RenderGraphRuntimeService failed to create a multi-queue command pool");
            }
            resources->queue_family_index = queue_family_index;
            resources->used_primary_count = 0U;
        }

        return *resources;
    }

    [[nodiscard]] VkCommandBuffer BeginOwnedCommandBuffer(VulkanContext& device_,
                                                          QueueCommandResources& resources_) {
        if (resources_.used_primary_count >= resources_.primary_buffers.size()) {
            VkCommandBuffer new_command_buffer = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo allocate_info{};
            allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocate_info.commandPool = resources_.pool;
            allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount = 1U;
            const VkResult allocate_result =
                vkAllocateCommandBuffers(device_.Device(), &allocate_info, &new_command_buffer);
            if (allocate_result != VK_SUCCESS) {
                throw std::runtime_error(
                    "RenderGraphRuntimeService failed to allocate a multi-queue command buffer");
            }
            resources_.primary_buffers.push_back(new_command_buffer);
        }

        const VkCommandBuffer command_buffer =
            resources_.primary_buffers[resources_.used_primary_count++];
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        const VkResult begin_result =
            vkBeginCommandBuffer(command_buffer, &begin_info);
        if (begin_result != VK_SUCCESS) {
            throw std::runtime_error(
                "RenderGraphRuntimeService failed to begin a multi-queue command buffer");
        }
        return command_buffer;
    }

    void EnsureDependencySemaphores(VulkanContext& device_,
                                    MultiQueueFrameSlot& slot_,
                                    const render_graph::BarrierPlan& barrier_plan_) {
        if (slot_.dependency_semaphores.size() < barrier_plan_.queue_dependencies.size()) {
            slot_.dependency_semaphores.resize(barrier_plan_.queue_dependencies.size());
        }

        for (std::size_t dependency_index = 0U;
             dependency_index < barrier_plan_.queue_dependencies.size();
             ++dependency_index) {
            const auto& dependency_ = barrier_plan_.queue_dependencies[dependency_index];
            if (dependency_.host_boundary ||
                dependency_.source_queue == dependency_.target_queue) {
                continue;
            }
            if (slot_.dependency_semaphores[dependency_index].semaphore != VK_NULL_HANDLE) {
                continue;
            }

            VkSemaphoreCreateInfo semaphore_create_info{};
            semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            const VkResult create_result =
                vkCreateSemaphore(device_.Device(),
                                  &semaphore_create_info,
                                  nullptr,
                                  &slot_.dependency_semaphores[dependency_index].semaphore);
            if (create_result != VK_SUCCESS) {
                throw std::runtime_error(
                    "RenderGraphRuntimeService failed to allocate a cross-queue dependency semaphore");
            }
        }
    }

    [[nodiscard]] std::vector<std::uint32_t> BuildBatchIndexByPass() const {
        std::vector<std::uint32_t> batch_index_by_pass(
            compiled_graph.Passes().size(),
            render_graph::invalid_render_graph_index);
        const auto& queue_batches = compiled_graph.PlannedBarriers().queue_batches;
        for (std::uint32_t batch_index = 0U;
             batch_index < static_cast<std::uint32_t>(queue_batches.size());
             ++batch_index) {
            for (const auto pass_handle_ : queue_batches[batch_index].passes) {
                if (pass_handle_.index < batch_index_by_pass.size()) {
                    batch_index_by_pass[pass_handle_.index] = batch_index;
                }
            }
        }
        return batch_index_by_pass;
    }

    [[nodiscard]] render_graph::RenderGraphRecordStats RecordPreparedMultiQueueGraph(
        VulkanContext& device_,
        render::RenderTargetHost& render_target_host_,
        render::DescriptorHost* descriptor_host_,
        const VkCommandBuffer graphics_command_buffer_) {
        if (graphics_command_buffer_ == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "RenderGraphRuntimeService multi-queue path requires a graphics command buffer");
        }

        const auto& barrier_plan = compiled_graph.PlannedBarriers();
        const auto& queue_batches = barrier_plan.queue_batches;
        if (queue_batches.empty()) {
            return {};
        }

        EnsureMultiQueueFrameSlotCapacity(frame_index + 1U);
        MultiQueueFrameSlot& frame_slot_ref = multi_queue_frame_slots[frame_index];
        EnsureDependencySemaphores(device_, frame_slot_ref, barrier_plan);

        const auto batch_index_by_pass = BuildBatchIndexByPass();
        std::vector<BatchDependencyAggregation> batch_dependencies(queue_batches.size());
        for (const auto& transfer_batch_ : command_ready_vulkan_barriers.queue_transfer_batches) {
            if (transfer_batch_.source_pass.index >= batch_index_by_pass.size() ||
                transfer_batch_.target_pass.index >= batch_index_by_pass.size()) {
                continue;
            }

            const std::uint32_t source_batch_index =
                batch_index_by_pass[transfer_batch_.source_pass.index];
            const std::uint32_t target_batch_index =
                batch_index_by_pass[transfer_batch_.target_pass.index];
            if (source_batch_index >= batch_dependencies.size() ||
                target_batch_index >= batch_dependencies.size()) {
                continue;
            }

            AppendDependencyInfoData(batch_dependencies[source_batch_index].end_dependency,
                                     transfer_batch_.release_dependency);
            AppendDependencyInfoData(batch_dependencies[target_batch_index].begin_dependency,
                                     transfer_batch_.acquire_dependency);
        }

        PreparedMultiQueueSubmission prepared{};
        prepared.graphics_batch_index =
            static_cast<std::uint32_t>(queue_batches.size() - 1U);
        std::array<bool, 3U> external_wait_consumed_by_queue{};

        const auto queue_class_index =
            [](const render_graph::QueueClass queue_) constexpr noexcept -> std::size_t {
                switch (queue_) {
                case render_graph::QueueClass::graphics:
                    return 0U;
                case render_graph::QueueClass::compute:
                    return 1U;
                case render_graph::QueueClass::transfer:
                    return 2U;
                default:
                    return 0U;
                }
            };

        const auto append_external_submit_waits =
            [&](std::vector<VkSemaphore>& waits_,
                const render_graph::QueueClass queue_) {
                bool& consumed = external_wait_consumed_by_queue[queue_class_index(queue_)];
                if (consumed) {
                    return;
                }
                for (const ExternalQueueWait& wait_ : external_queue_waits) {
                    if (wait_.queue != queue_) {
                        continue;
                    }
                    AppendSemaphoreUnique(waits_, wait_.semaphore);
                }
                consumed = true;
            };

        const auto append_external_graphics_waits =
            [&](std::vector<vr::render::FrameSubmitWait>& waits_,
                const render_graph::QueueClass queue_) {
                bool& consumed = external_wait_consumed_by_queue[queue_class_index(queue_)];
                if (consumed) {
                    return;
                }
                for (const ExternalQueueWait& wait_ : external_queue_waits) {
                    if (wait_.queue != queue_) {
                        continue;
                    }
                    AppendFrameSubmitWaitUnique(waits_,
                                                vr::render::FrameSubmitWait{
                                                    .semaphore = wait_.semaphore,
                                                    .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                                });
                }
                consumed = true;
            };

        const auto accumulate_stats =
            [](render_graph::RenderGraphRecordStats& destination_,
               const render_graph::RenderGraphRecordStats& source_) {
                destination_.pass_count += source_.pass_count;
                destination_.rendering_scope_count += source_.rendering_scope_count;
                destination_.command_batch_count += source_.command_batch_count;
                destination_.memory_barrier_count += source_.memory_barrier_count;
                destination_.buffer_barrier_count += source_.buffer_barrier_count;
                destination_.image_barrier_count += source_.image_barrier_count;
            };

        render_graph::RenderGraphRecordStats total_stats{};
        for (std::uint32_t batch_index = 0U;
             batch_index < static_cast<std::uint32_t>(queue_batches.size());
             ++batch_index) {
            const auto& queue_batch_ = queue_batches[batch_index];
            const auto* begin_dependency =
                batch_dependencies[batch_index].begin_dependency.Empty()
                    ? nullptr
                    : &batch_dependencies[batch_index].begin_dependency;
            const auto* end_dependency =
                batch_dependencies[batch_index].end_dependency.Empty()
                    ? nullptr
                    : &batch_dependencies[batch_index].end_dependency;
            const bool terminal_graphics_batch =
                queue_batch_.queue == render_graph::QueueClass::graphics &&
                batch_index == prepared.graphics_batch_index;

            if (terminal_graphics_batch) {
                render_graph::GraphCommandContext graphics_context{
                    device_,
                    frame_index,
                    graphics_command_buffer_,
                    compiled_graph,
                    physical_resources,
                    render_target_host_,
                    descriptor_host_,
                    lowered_vulkan_barriers,
                    command_ready_vulkan_barriers,
                };
                accumulate_stats(total_stats,
                                 render_graph::RenderGraphExecutor::RecordQueueBatch(
                                     graphics_context,
                                     queue_batch_,
                                     begin_dependency,
                                     end_dependency));

                for (const auto dependency_index : queue_batch_.wait_dependency_indices) {
                    if (dependency_index >= barrier_plan.queue_dependencies.size() ||
                        dependency_index >= frame_slot_ref.dependency_semaphores.size()) {
                        continue;
                    }
                    const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
                    if (dependency_.host_boundary ||
                        dependency_.source_queue == dependency_.target_queue) {
                        continue;
                    }

                    const VkSemaphore semaphore =
                        frame_slot_ref.dependency_semaphores[dependency_index].semaphore;
                    if (semaphore == VK_NULL_HANDLE) {
                        continue;
                    }
                    AppendFrameSubmitWaitUnique(prepared.graphics_waits,
                                                vr::render::FrameSubmitWait{
                        .semaphore = semaphore,
                        .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    });
                }
                append_external_graphics_waits(prepared.graphics_waits,
                                               render_graph::QueueClass::graphics);
                continue;
            }

            QueueCommandResources& command_resources =
                EnsureQueueCommandResources(device_, frame_slot_ref, queue_batch_.queue);
            const VkCommandBuffer owned_command_buffer =
                BeginOwnedCommandBuffer(device_, command_resources);
            render_graph::GraphCommandContext batch_context{
                device_,
                frame_index,
                owned_command_buffer,
                compiled_graph,
                physical_resources,
                render_target_host_,
                descriptor_host_,
                lowered_vulkan_barriers,
                command_ready_vulkan_barriers,
            };
            accumulate_stats(total_stats,
                             render_graph::RenderGraphExecutor::RecordQueueBatch(
                                 batch_context,
                                 queue_batch_,
                                 begin_dependency,
                                 end_dependency));
            const VkResult end_result = vkEndCommandBuffer(owned_command_buffer);
            if (end_result != VK_SUCCESS) {
                throw std::runtime_error(
                    "RenderGraphRuntimeService failed to end a multi-queue command buffer");
            }

            PreparedQueueSubmitBatch submit_batch{};
            submit_batch.batch_index = batch_index;
            submit_batch.queue = queue_batch_.queue;
            submit_batch.submit_queue = ResolveSubmitQueue(device_, queue_batch_.queue);
            submit_batch.command_buffer = owned_command_buffer;

            for (const auto dependency_index : queue_batch_.wait_dependency_indices) {
                if (dependency_index >= barrier_plan.queue_dependencies.size() ||
                    dependency_index >= frame_slot_ref.dependency_semaphores.size()) {
                    continue;
                }
                const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
                if (dependency_.host_boundary ||
                    dependency_.source_queue == dependency_.target_queue) {
                    continue;
                }
                const VkSemaphore semaphore =
                    frame_slot_ref.dependency_semaphores[dependency_index].semaphore;
                if (semaphore != VK_NULL_HANDLE) {
                    AppendSemaphoreUnique(submit_batch.wait_semaphores, semaphore);
                }
            }
            append_external_submit_waits(submit_batch.wait_semaphores, queue_batch_.queue);
            for (const auto dependency_index : queue_batch_.signal_dependency_indices) {
                if (dependency_index >= barrier_plan.queue_dependencies.size() ||
                    dependency_index >= frame_slot_ref.dependency_semaphores.size()) {
                    continue;
                }
                const auto& dependency_ = barrier_plan.queue_dependencies[dependency_index];
                if (dependency_.host_boundary ||
                    dependency_.source_queue == dependency_.target_queue) {
                    continue;
                }
                const VkSemaphore semaphore =
                    frame_slot_ref.dependency_semaphores[dependency_index].semaphore;
                if (semaphore != VK_NULL_HANDLE) {
                    AppendSemaphoreUnique(submit_batch.signal_semaphores, semaphore);
                }
            }
            prepared.owned_submit_batches.push_back(std::move(submit_batch));
        }

        total_stats.queue_transfer_batch_count =
            static_cast<std::uint32_t>(command_ready_vulkan_barriers.queue_transfer_batches.size());
        prepared.active = !prepared.owned_submit_batches.empty() ||
                          !prepared.graphics_waits.empty();
        prepared_multi_queue_submission = std::move(prepared);
        return total_stats;
    }

    void RefreshDiagnostics(const VulkanContext& device_) {
        vr::runtime::RenderGraphRuntimeDiagnostics diagnostics{};
        diagnostics.available = has_compiled_graph;
        diagnostics.frame_compiled = has_compiled_graph && !compiled_graph.Empty();
        diagnostics.graph_only_path_enabled = graph_only_record_path_enabled;
        diagnostics.graph_only_supported = SupportsGraphOnlyRecord(device_);
        diagnostics.graph_only_active =
            diagnostics.graph_only_supported &&
            CanExecuteGraphRecord(device_) &&
            record_stats.pass_count > 0U;
        diagnostics.strict_graph_only_required = strict_graph_only_record_required;
        diagnostics.transfer_queue_requested = queue_execution_policy.transfer_requested;
        diagnostics.compute_queue_requested = queue_execution_policy.compute_requested;
        diagnostics.multi_queue_requested = queue_execution_policy.multi_queue_requested;
        diagnostics.transfer_queue_enabled = queue_execution_policy.transfer_enabled;
        diagnostics.compute_queue_enabled = queue_execution_policy.compute_enabled;
        diagnostics.multi_queue_enabled = queue_execution_policy.multi_queue_enabled;
        diagnostics.graphics_fallback_active = queue_execution_policy.graphics_fallback_active;
        diagnostics.queue_fallback_reason = queue_execution_policy.fallback_reason;
        const auto& local_read_caps = device_.DynamicRenderingLocalReadCapsInfo();
        diagnostics.dynamic_rendering_local_read_supported =
            local_read_caps.supported;
        diagnostics.dynamic_rendering_local_read_requested =
            local_read_caps.requested;
        diagnostics.dynamic_rendering_local_read_enabled =
            local_read_caps.enabled;
        if (has_compiled_graph) {
            diagnostics.compiled_pass_count =
                static_cast<std::uint32_t>(compiled_graph.Passes().size());
            diagnostics.executable_pass_count = static_cast<std::uint32_t>(
                std::count_if(compiled_graph.Passes().begin(),
                              compiled_graph.Passes().end(),
                              [](const render_graph::CompiledPass& pass_) {
                                  return pass_.executable;
                              }));
            diagnostics.logical_raster_pass_count =
                compiled_graph.NativePasses().summary.logical_raster_pass_count;
            diagnostics.native_pass_group_count =
                compiled_graph.NativePasses().summary.native_pass_group_count;
            diagnostics.fused_raster_pass_count =
                compiled_graph.NativePasses().summary.fused_raster_pass_count;
            diagnostics.store_elision_count =
                compiled_graph.NativePasses().summary.store_elision_count;
            diagnostics.load_inference_count =
                compiled_graph.NativePasses().summary.load_inference_count;
            diagnostics.effective_clear_attachment_count =
                compiled_graph.NativePasses().summary.clear_attachment_count;
            diagnostics.local_read_candidate_count =
                compiled_graph.NativePasses().summary.local_read_candidate_count;
            diagnostics.dynamic_rendering_local_read_requested =
                compiled_graph.NativePasses().local_read.requested;
            diagnostics.dynamic_rendering_local_read_supported =
                compiled_graph.NativePasses().local_read.supported;
            diagnostics.dynamic_rendering_local_read_enabled =
                compiled_graph.NativePasses().local_read.device_enabled;
            diagnostics.dynamic_rendering_local_read_status = std::string(
                render_graph::NativePassLocalReadStatusName(
                    compiled_graph.NativePasses().local_read.status));
            diagnostics.dynamic_rendering_local_read_reason = std::string(
                render_graph::NativePassLocalReadReasonName(
                    compiled_graph.NativePasses().local_read.reason));
            const auto& timeline = compiled_graph.TransientAllocations().timeline;
            diagnostics.transient_logical_total_bytes = timeline.logical_total_bytes;
            diagnostics.transient_physical_total_bytes = timeline.physical_total_bytes;
            diagnostics.transient_peak_live_bytes = timeline.peak_live_bytes;
            diagnostics.transient_saved_bytes = timeline.saved_bytes;
            diagnostics.transient_page_count = timeline.page_count;
            diagnostics.alias_barrier_count = timeline.alias_barrier_count;
        } else {
            diagnostics.dynamic_rendering_local_read_status = std::string(
                render_graph::NativePassLocalReadStatusName(
                    render_graph::NativePassLocalReadStatus::not_applicable));
            diagnostics.dynamic_rendering_local_read_reason = std::string(
                render_graph::NativePassLocalReadReasonName(
                    render_graph::NativePassLocalReadReason::none));
        }
        diagnostics.recorded_pass_count = record_stats.pass_count;
        diagnostics.recorded_rendering_scope_count =
            record_stats.rendering_scope_count;
        diagnostics.recorded_command_batch_count = record_stats.command_batch_count;
        diagnostics.recorded_image_barrier_count = record_stats.image_barrier_count;
        diagnostics.recorded_buffer_barrier_count = record_stats.buffer_barrier_count;
        diagnostics.recorded_queue_transfer_batch_count = record_stats.queue_transfer_batch_count;
        diagnostics.lazy_memory_requested_count =
            physical_resources.Stats().lazy_memory_requested_count;
        diagnostics.lazy_memory_realized_count =
            physical_resources.Stats().lazy_memory_realized_count;
        diagnostics.lazy_memory_unavailable_count =
            physical_resources.Stats().lazy_memory_unavailable_count;
        diagnostics.lazy_memory_resources.reserve(
            physical_resources.LazyMemoryResolutions().size());
        for (const auto& resolution_ : physical_resources.LazyMemoryResolutions()) {
            diagnostics.lazy_memory_resources.push_back(
                vr::runtime::RenderGraphLazyMemoryResourceDiagnostics{
                    .logical_resource_index = resolution_.logical.index,
                    .debug_name = resolution_.debug_name,
                    .requested = resolution_.requested,
                    .realized = resolution_.realized,
                    .unavailable_reason = resolution_.unavailable_reason,
                });
        }
        BuildEffectiveQueueDiagnostics(diagnostics);
        diagnostics.effective_queue_timeline_debug_string =
            BuildEffectiveQueueTimelineDebugString(diagnostics);
        diagnostics.effective_queue_timeline_json =
            vr::runtime::BuildRenderGraphQueueTimelineJson(diagnostics);
        last_diagnostics = std::move(diagnostics);
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

    void RegisterPendingImportedBuffers() {
        for (const ImportedBufferBinding& binding_ : direct_imported_buffers) {
            if (binding_.imported_buffer.buffer == VK_NULL_HANDLE ||
                binding_.imported_buffer.size_bytes == 0U) {
                continue;
            }
            physical_resources.RegisterImportedBuffer(binding_.logical,
                                                      binding_.imported_buffer);
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
    render_graph::QueueExecutionPolicy queue_execution_policy{};
    render_graph::RenderGraphRecordStats record_stats{};
    GraphBuildCallback2D graph_build_callback_2d{};
    GraphBuildCallback3D graph_build_callback_3d{};
    DirectGraphBuildCallback direct_graph_build_callback{};
    render_graph::VulkanResourceTable physical_resources{};
    bool has_compiled_graph = false;
    bool record_execution_enabled = false;
    bool graph_only_record_path_enabled = false;
    bool strict_graph_only_record_required = false;
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
