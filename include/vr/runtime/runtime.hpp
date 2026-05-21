#pragma once

#include "vr/runtime/runtime_kernel.hpp"
#include "vr/runtime/runtime_context.hpp"
#include "vr/render/render_runtime_host.hpp"
#include "vr/runtime/runtime_create_info.hpp"
#include "vr/runtime/runtime_status.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace vr::runtime {

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
class Runtime final {
public:
    using BackendTag = BackendTagT;
    using HostType = vr::render::RenderRuntimeHost<BackendTag, frames_in_flight_v>;
    using CreateInfo = RuntimeCreateInfo<BackendTag, frames_in_flight_v>;
    using PipelineWarmupCreateInfo = RuntimePipelineWarmupCreateInfo<BackendTag, frames_in_flight_v>;
    using RuntimeServicesType = typename HostType::RuntimeServicesType;
    using KernelType = RuntimeKernel<BackendTag, frames_in_flight_v>;
    using RuntimeFrameType = typename KernelType::FrameType;
    using FrameContext = RuntimeFrameContext<typename HostType::DefaultProfile,
                                             BackendTag,
                                             frames_in_flight_v>;
    using TickResult = RuntimeTickResult;

    Runtime() {
        kernel.Bind(host);
    }
    ~Runtime() = default;

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    void Initialize(const CreateInfo& create_info_ = {}) {
        host.Initialize(create_info_);
        kernel.Bind(host);
        last_execution_trace = {};
        auto init_context = RuntimeInitContext<typename HostType::DefaultProfile,
                                               BackendTag,
                                               frames_in_flight_v>{
            .services = host.Services(),
            .kernel = kernel,
            .device = host.IsInitialized() ? &host.Context() : nullptr,
        };
        host.Services().Initialize(init_context);
        auto post_init_context = RuntimePostInitContext<typename HostType::DefaultProfile,
                                                        BackendTag,
                                                        frames_in_flight_v>{
            .services = host.Services(),
            .kernel = kernel,
            .device = host.IsInitialized() ? &host.Context() : nullptr,
        };
        host.Services().PostInitialize(post_init_context);
    }

    void Shutdown() {
        if (host.IsInitialized()) {
            auto shutdown_context = RuntimeShutdownContext<typename HostType::DefaultProfile,
                                                          BackendTag,
                                                          frames_in_flight_v>{
                .services = host.Services(),
                .kernel = kernel,
                .device = &host.Context(),
            };
            host.Services().Shutdown(shutdown_context);
        }
        host.Shutdown();
        kernel.Bind(host);
        last_execution_trace = {};
    }

    template<typename RecorderT>
    [[nodiscard]] TickResult Tick(RecorderT& recorder_) {
        typename HostType::RuntimeTickResult tick_state{};
        const auto prelude = kernel.BeginPrelude(host.Config().poll_events_each_tick);
        const std::uint64_t frame_id = prelude.frame_id;
        tick_state.frame_id = frame_id;
        tick_state.events_polled = prelude.events_polled;
        tick_state.running = prelude.running;
        if (!tick_state.running) {
            tick_state.render = {
                .code = vr::render::TickCode::SkippedWindowHidden,
                .frame_index = prelude.frame_index,
                .image_index = 0U,
            };
            kernel.UpdateLastTickFrame(tick_state.render.frame_index, tick_state.render.image_index);
            FrameContext skipped_frame_context = BuildFrameContext();
            RuntimeExecution<FrameContext> execution{skipped_frame_context};
            execution.MarkBeginFrame();
            execution.MarkDiagnostics();
            last_execution_trace = execution.Trace();
            CollectTickPostState(tick_state, frame_id);
            TickResult result = Convert(tick_state);
            result.execution = last_execution_trace;
            return result;
        }

        auto frame = kernel.BeginFrame(recorder_, frame_id);
        if (frame.token.frame_index == 0U && prelude.frame_index < kernel.Frames().FramesInFlight()) {
            frame.token.frame_index = prelude.frame_index;
        }
        FrameContext execution_frame_context = BuildFrameContext(frame);
        RuntimeExecution<FrameContext> execution{execution_frame_context};
        execution.MarkBeginFrame();
        ExecutionPhaseDriver phase_driver{*this, execution, execution_frame_context};

        if (!frame.ready) {
            tick_state.render = {
                .code = frame.code,
                .frame_index = frame.token.frame_index,
                .image_index = frame.token.image_index,
            };
            kernel.UpdateLastTickFrame(frame.token.frame_index, frame.token.image_index);
            CollectTickPostState(tick_state, frame_id);
            const auto legacy_result = tick_state;
            execution.MarkDiagnostics();
            last_execution_trace = execution.Trace();
            TickResult result = Convert(legacy_result);
            result.execution = last_execution_trace;
            return result;
        }

        phase_driver.OnServiceBeginFrame(frame.token.frame_index,
                                         host.Loop().Sync().LastSubmittedValue(),
                                         host.Loop().Sync().CompletedSubmitValue());
        tick_state.compiled_pipeline_count +=
            host.Services().template Get<services::PipelineService>().LastBeginFrameCompileCount();

        host.PrepareTickFrame(recorder_, frame.token.frame_index);
        phase_driver.OnPrepare(frame.token.frame_index,
                               host.Loop().Sync().LastSubmittedValue(),
                               host.Loop().Sync().CompletedSubmitValue());

        const auto upload_flush =
            host.Services().template Get<services::UploadService>().Flush(execution_frame_context);
        phase_driver.OnFlushUploads();

        tick_state.upload_submitted = upload_flush.submitted;
        tick_state.upload_cross_queue_wait = upload_flush.cross_queue_wait;
        frame.token.transfer_wait_value = upload_flush.transfer_wait_value;

        phase_driver.OnPreRecord(frame.token.frame_index,
                                 host.Loop().Sync().LastSubmittedValue(),
                                 host.Loop().Sync().CompletedSubmitValue());
        auto* graph_service =
            host.Services().template TryGet<services::RenderGraphRuntimeService>();
        if (graph_service != nullptr &&
            upload_flush.extra_wait_count != 0U &&
            upload_flush.extra_wait.semaphore != VK_NULL_HANDLE) {
            graph_service->RegisterExternalQueueSubmitWait(render_graph::QueueClass::compute,
                                                           upload_flush.extra_wait.semaphore);
        }

        kernel.Commands().BeginFrame(frame.token.frame_index);
        const VkCommandBuffer command_buffer = kernel.Commands().BeginPrimaryGraphics(
            frame.token.frame_index,
            host.Config().render_loop.command_usage_flags);
        host.RecordTickFrame(recorder_,
                             {
                                 .code = frame.code,
                                 .token = frame.token,
                                 .extent = frame.extent,
                                 .format = frame.format,
                                 .image = frame.image,
                                 .image_view = frame.image_view,
                             },
                             command_buffer);
        phase_driver.OnRecord(frame.token.frame_index,
                              host.Loop().Sync().LastSubmittedValue(),
                              host.Loop().Sync().CompletedSubmitValue(),
                              command_buffer);
        kernel.Commands().End(command_buffer);

        std::vector<vr::render::FrameSubmitWait> submit_waits{};
        if (graph_service != nullptr &&
            graph_service->HasPreparedMultiQueueSubmission()) {
            (void)graph_service->SubmitPreparedMultiQueueWork(host.Context());
            for (const auto& wait_ : graph_service->PendingGraphicsSubmitWaits()) {
                submit_waits.push_back(wait_);
            }
        }
        for (std::uint32_t wait_index = 0U;
             wait_index < upload_flush.extra_wait_count;
             ++wait_index) {
            (void)wait_index;
            submit_waits.push_back(upload_flush.extra_wait);
        }

        const GraphicsSubmitResult submit_result = kernel.SubmitGraphics({
            .token = frame.token,
            .command_buffer = command_buffer,
            .wait_stage_mask = host.Config().render_loop.submit_wait_stage_mask,
            .extra_waits = submit_waits.empty() ? nullptr : submit_waits.data(),
            .extra_wait_count = static_cast<std::uint32_t>(submit_waits.size()),
        });
        (void)submit_result;
        if (graph_service != nullptr) {
            graph_service->MarkGraphicsSubmissionEnqueued(frame.token);
        }
        phase_driver.OnSubmit();

        phase_driver.OnPostRecord(frame.token.frame_index,
                                  host.Loop().Sync().LastSubmittedValue(),
                                  host.Loop().Sync().CompletedSubmitValue());

        const PresentResult present_result = kernel.Present(frame.token);
        tick_state.render = {
            .code = present_result.recreate_requested
                ? vr::render::TickCode::RecreateRequested
                : vr::render::TickCode::Submitted,
            .frame_index = frame.token.frame_index,
            .image_index = frame.token.image_index,
        };
        kernel.UpdateLastTickFrame(frame.token.frame_index, frame.token.image_index);
        phase_driver.OnPresent();

        phase_driver.OnEndFrame(frame.token.frame_index,
                                host.Loop().Sync().LastSubmittedValue(),
                                host.Loop().Sync().CompletedSubmitValue());
        tick_state.compiled_pipeline_count +=
            host.Services().template Get<services::PipelineService>().LastEndFrameCompileCount();
        phase_driver.OnRetire(frame.token.frame_index,
                              host.Loop().Sync().LastSubmittedValue(),
                              host.Loop().Sync().CompletedSubmitValue());
        kernel.EndFrame(frame.token);

        CollectTickPostState(tick_state, frame_id);
        const auto legacy_result = tick_state;
        execution.MarkDiagnostics();
        last_execution_trace = execution.Trace();
        TickResult result = Convert(legacy_result);
        result.execution = last_execution_trace;
        return result;
    }

    [[nodiscard]] bool PollEvents() {
        return host.PollEvents();
    }

    [[nodiscard]] bool IsInitialized() const noexcept {
        return host.IsInitialized();
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return host.IsRunning();
    }

    void RequestClose() noexcept {
        host.RequestClose();
    }

    [[nodiscard]] const CreateInfo& Config() const noexcept {
        return host.Config();
    }

    [[nodiscard]] HostType& Host() noexcept {
        return host;
    }

    [[nodiscard]] const HostType& Host() const noexcept {
        return host;
    }

    [[nodiscard]] KernelType& Kernel() noexcept {
        return kernel;
    }

    [[nodiscard]] const KernelType& Kernel() const noexcept {
        return kernel;
    }

    [[nodiscard]] typename KernelType::SchedulerType& Frames() noexcept {
        return kernel.Frames();
    }

    [[nodiscard]] const typename KernelType::SchedulerType& Frames() const noexcept {
        return kernel.Frames();
    }

    [[nodiscard]] CommandService& Commands() noexcept {
        return kernel.Commands();
    }

    [[nodiscard]] const CommandService& Commands() const noexcept {
        return kernel.Commands();
    }

    [[nodiscard]] FrameRetireService& Retire() noexcept {
        return kernel.Retire();
    }

    [[nodiscard]] const FrameRetireService& Retire() const noexcept {
        return kernel.Retire();
    }

    [[nodiscard]] RuntimeServicesType& Services() noexcept {
        return host.Services();
    }

    [[nodiscard]] const RuntimeServicesType& Services() const noexcept {
        return host.Services();
    }

    [[nodiscard]] FrameContext BuildFrameContext() {
        const auto frame = kernel.BuildFrameStaticContext();
        const auto progress = kernel.BuildFrameGpuProgressContext();
        if (host.HasRenderTargetHost()) {
            host.EnsureSwapchainTargetsForFrame(progress.graphics_submitted,
                                                progress.graphics_completed);
        }
        return FrameContext{
            .frame = frame,
            .progress = progress,
            .timelines = kernel.BuildQueueTimelines(),
            .services = host.Services(),
            .kernel = kernel,
            .commands = kernel.Commands(),
            .swapchain_targets = host.HasRenderTargetHost() ? &host.SwapchainTargets() : nullptr,
        };
    }

    [[nodiscard]] FrameContext BuildFrameContext(const RuntimeFrameType& frame_) {
        const auto frame = kernel.BuildFrameStaticContext(frame_);
        const auto progress = kernel.BuildFrameGpuProgressContext();
        if (host.HasRenderTargetHost()) {
            host.EnsureSwapchainTargetsForFrame(progress.graphics_submitted,
                                                progress.graphics_completed);
        }
        return FrameContext{
            .frame = frame,
            .progress = progress,
            .timelines = kernel.BuildQueueTimelines(),
            .services = host.Services(),
            .kernel = kernel,
            .commands = kernel.Commands(),
            .swapchain_targets = host.HasRenderTargetHost() ? &host.SwapchainTargets() : nullptr,
        };
    }

    [[nodiscard]] const RuntimeExecutionTrace& LastExecutionTrace() const noexcept {
        return last_execution_trace;
    }

    [[nodiscard]] typename HostType::PlatformHostType& PlatformHost() noexcept {
        return host.PlatformHost();
    }

    [[nodiscard]] const typename HostType::PlatformHostType& PlatformHost() const noexcept {
        return host.PlatformHost();
    }

    [[nodiscard]] VulkanContext& Context() noexcept {
        return host.Context();
    }

    [[nodiscard]] const VulkanContext& Context() const noexcept {
        return host.Context();
    }

    [[nodiscard]] asset::TextureHost& Texture() {
        return host.Texture();
    }

    [[nodiscard]] vr::render::FrameComposerHost& FrameComposer() {
        return host.FrameComposer();
    }

    [[nodiscard]] vr::render::IblHost& Ibl() {
        return host.Ibl();
    }

    [[nodiscard]] vr::render::IblBakeHost& IblBake() {
        return host.IblBake();
    }

    [[nodiscard]] vr::render::SkyEnvironmentGpuHost& SkyEnvironment() {
        return host.SkyEnvironment();
    }

    [[nodiscard]] vr::render::UploadHost& Upload() {
        return host.Upload();
    }

    [[nodiscard]] vr::render::DescriptorHost& Descriptor() {
        return host.Descriptor();
    }

    [[nodiscard]] vr::render::PipelineHost& Pipeline() {
        return host.Pipeline();
    }

    [[nodiscard]] vr::render::RenderTargetHost& RenderTarget() {
        return host.RenderTarget();
    }

    [[nodiscard]] const vr::render::RenderTargetPoolStats& RenderTargetPoolStats() const noexcept {
        return host.RenderTargetPoolStats();
    }

    [[nodiscard]] resource::SamplerHost& Sampler() {
        return host.Sampler();
    }

    [[nodiscard]] text::FreeTypeHost& FreeType() {
        return host.FreeType();
    }

    [[nodiscard]] text::GlyphAtlasHost& GlyphAtlas() {
        return host.GlyphAtlas();
    }

    [[nodiscard]] text::GlyphUploadHost& GlyphUpload() {
        return host.GlyphUpload();
    }

    [[nodiscard]] particle::ParticleUploadHost& ParticleUpload() {
        return host.ParticleUpload();
    }

    [[nodiscard]] particle::ParticleSimulationHost& ParticleSimulation() {
        return host.ParticleSimulation();
    }

    [[nodiscard]] runtime::services::ParticleUploadService& ParticleUploadService() noexcept {
        return host.ParticleUploadService();
    }

    [[nodiscard]] runtime::services::ParticleSimulationService& ParticleSimulationService() noexcept {
        return host.ParticleSimulationService();
    }

    [[nodiscard]] runtime::services::SkyEnvironmentService& SkyEnvironmentService() noexcept {
        return host.SkyEnvironmentService();
    }

    [[nodiscard]] runtime::services::ParticleRenderService& Particles() noexcept {
        return host.Particles();
    }

    [[nodiscard]] bool HasTextureHost() const noexcept {
        return host.HasTextureHost();
    }

    [[nodiscard]] bool HasFrameComposerHost() const noexcept {
        return host.HasFrameComposerHost();
    }

    [[nodiscard]] bool HasIblHost() const noexcept {
        return host.HasIblHost();
    }

    [[nodiscard]] bool HasIblBakeHost() const noexcept {
        return host.HasIblBakeHost();
    }

    [[nodiscard]] bool HasSkyEnvironmentHost() const noexcept {
        return host.HasSkyEnvironmentHost();
    }

    [[nodiscard]] bool HasUploadHost() const noexcept {
        return host.HasUploadHost();
    }

    [[nodiscard]] bool HasDescriptorHost() const noexcept {
        return host.HasDescriptorHost();
    }

    [[nodiscard]] bool HasPipelineHost() const noexcept {
        return host.HasPipelineHost();
    }

    [[nodiscard]] bool HasRenderTargetHost() const noexcept {
        return host.HasRenderTargetHost();
    }

    [[nodiscard]] bool HasRenderTargetPool() const noexcept {
        return host.HasRenderTargetPool();
    }

    [[nodiscard]] bool HasSamplerHost() const noexcept {
        return host.HasSamplerHost();
    }

    [[nodiscard]] bool HasFreeTypeHost() const noexcept {
        return host.HasFreeTypeHost();
    }

    [[nodiscard]] bool HasGlyphAtlasHost() const noexcept {
        return host.HasGlyphAtlasHost();
    }

    [[nodiscard]] bool HasGlyphUploadHost() const noexcept {
        return host.HasGlyphUploadHost();
    }

    [[nodiscard]] bool HasParticleUploadHost() const noexcept {
        return host.HasParticleUploadHost();
    }

    [[nodiscard]] bool HasParticleSimulationHost() const noexcept {
        return host.HasParticleSimulationHost();
    }

private:
    class ExecutionPhaseDriver final {
    public:
        ExecutionPhaseDriver(Runtime& runtime_,
                             RuntimeExecution<FrameContext>& execution_,
                             FrameContext& phase_frame_context_) noexcept
            : runtime(runtime_),
              execution(execution_),
              phase_frame_context(phase_frame_context_) {
            execution.RebindFrame(phase_frame_context);
        }

        void OnServiceBeginFrame(const std::uint32_t frame_index_,
                                 const std::uint64_t graphics_submitted_,
                                 const std::uint64_t graphics_completed_) {
            runtime.UpdatePhaseFrameContext(phase_frame_context,
                                            frame_index_,
                                            graphics_submitted_,
                                            graphics_completed_);
            execution.ServiceBeginFrame(runtime.Services());
        }

        void OnPrepare(const std::uint32_t frame_index_,
                       const std::uint64_t graphics_submitted_,
                       const std::uint64_t graphics_completed_) {
            runtime.UpdatePhaseFrameContext(phase_frame_context,
                                            frame_index_,
                                            graphics_submitted_,
                                            graphics_completed_);
            execution.Prepare(runtime.Services());
        }

        void OnFlushUploads() noexcept {
            execution.MarkFlushUploads();
        }

        void OnPreRecord(const std::uint32_t frame_index_,
                         const std::uint64_t graphics_submitted_,
                         const std::uint64_t graphics_completed_) {
            runtime.UpdatePhaseFrameContext(phase_frame_context,
                                            frame_index_,
                                            graphics_submitted_,
                                            graphics_completed_);
            execution.PreRecord(runtime.Services());
        }

        void OnRecord(const std::uint32_t frame_index_,
                      const std::uint64_t graphics_submitted_,
                      const std::uint64_t graphics_completed_,
                      const VkCommandBuffer command_buffer_) {
            runtime.UpdatePhaseFrameContext(phase_frame_context,
                                            frame_index_,
                                            graphics_submitted_,
                                            graphics_completed_,
                                            command_buffer_);
            execution.Record(runtime.Services());
        }

        void OnSubmit() noexcept {
            execution.MarkSubmit();
        }

        void OnPostRecord(const std::uint32_t frame_index_,
                          const std::uint64_t graphics_submitted_,
                          const std::uint64_t graphics_completed_) {
            runtime.UpdatePhaseFrameContext(phase_frame_context,
                                            frame_index_,
                                            graphics_submitted_,
                                            graphics_completed_);
            execution.PostRecord(runtime.Services());
        }

        void OnPresent() noexcept {
            execution.MarkPresent();
        }

        void OnEndFrame(const std::uint32_t frame_index_,
                        const std::uint64_t graphics_submitted_,
                        const std::uint64_t graphics_completed_) {
            runtime.UpdatePhaseFrameContext(phase_frame_context,
                                            frame_index_,
                                            graphics_submitted_,
                                            graphics_completed_);
            execution.EndFrame(runtime.Services());
        }

        void OnRetire(const std::uint32_t frame_index_,
                      const std::uint64_t graphics_submitted_,
                      const std::uint64_t graphics_completed_) {
            runtime.UpdatePhaseFrameContext(phase_frame_context,
                                            frame_index_,
                                            graphics_submitted_,
                                            graphics_completed_);
            execution.Retire(runtime.Services());
        }

    private:
        Runtime& runtime;
        RuntimeExecution<FrameContext>& execution;
        FrameContext& phase_frame_context;
    };

    [[nodiscard]] static TickResult Convert(const typename HostType::RuntimeTickResult& legacy_) {
        TickResult result{};
        result.code = ToRuntimeStatusCode(legacy_.render.code);
        result.frame_id = legacy_.frame_id;
        result.frame_index = legacy_.render.frame_index;
        result.image_index = legacy_.render.image_index;
        result.running = legacy_.running;
        result.compiled_pipeline_count = legacy_.compiled_pipeline_count;
        result.pending_graphics_compile_count = legacy_.pending_graphics_compile_count;
        result.pending_compute_compile_count = legacy_.pending_compute_compile_count;
        result.upload_submitted = legacy_.upload_submitted;
        result.upload_cross_queue_wait = legacy_.upload_cross_queue_wait;
        result.events_polled = legacy_.events_polled;
        result.render = legacy_.render;
        result.diagnostics = legacy_.diagnostics;
        return result;
    }

    void UpdatePhaseFrameContext(FrameContext& frame_context_,
                                 const std::uint32_t frame_index_,
                                 const std::uint64_t graphics_submitted_,
                                 const std::uint64_t graphics_completed_,
                                 const VkCommandBuffer command_buffer_ = VK_NULL_HANDLE) {
        frame_context_.frame.frame_id = host.CurrentFrameId();
        frame_context_.frame.frame_index = frame_index_;

        frame_context_.progress = kernel.BuildFrameGpuProgressContext();
        frame_context_.progress.graphics_submitted = graphics_submitted_;
        frame_context_.progress.graphics_completed = graphics_completed_;

        frame_context_.timelines = kernel.BuildQueueTimelines();
        frame_context_.timelines.graphics.submitted_value = graphics_submitted_;
        frame_context_.timelines.graphics.completed_value = graphics_completed_;
        if (frame_context_.timelines.graphics.next_value <= graphics_submitted_) {
            frame_context_.timelines.graphics.next_value = graphics_submitted_ + 1U;
        }
        frame_context_.command_buffer = command_buffer_;
        frame_context_.swapchain_targets = host.HasRenderTargetHost() ? &host.SwapchainTargets() : nullptr;
    }

    void CollectTickPostState(typename HostType::RuntimeTickResult& result_,
                              const std::uint64_t frame_id_) {
        if (!host.HasPipelineHost()) {
            result_.pending_graphics_compile_count = 0U;
            result_.pending_compute_compile_count = 0U;
        } else {
            result_.pending_graphics_compile_count = host.Pipeline().PendingGraphicsCompileCount();
            result_.pending_compute_compile_count = host.Pipeline().PendingComputeCompileCount();
        }

        const auto diagnostics_level = host.Config().diagnostics.level;
        if (!DiagnosticsCollectsFrameData(diagnostics_level)) {
            result_.diagnostics = {};
            result_.running = kernel.IsRunning();
            return;
        }

        RuntimeFrameDiagnosticsV2 diagnostics{};
        diagnostics.collected = true;
        diagnostics.level = diagnostics_level;

        diagnostics.frame.frame_id = frame_id_;
        diagnostics.frame.frame_index = result_.render.frame_index;
        diagnostics.frame.image_index = result_.render.image_index;
        diagnostics.frame.upload_submitted = result_.upload_submitted;
        diagnostics.frame.upload_cross_queue_wait = result_.upload_cross_queue_wait;

        diagnostics.swapchain.valid = host.Swapchain().IsValid();
        diagnostics.swapchain.generation = host.Swapchain().Generation();
        diagnostics.swapchain.image_count = host.Swapchain().ImageCount();
        diagnostics.swapchain.extent = host.Swapchain().Extent();
        diagnostics.swapchain.format = host.Swapchain().Format();
        diagnostics.swapchain.color_space = host.Swapchain().ColorSpace();
        diagnostics.swapchain.present_mode = host.Swapchain().PresentMode();

        diagnostics.queues.graphics_submitted = kernel.Frames().LastSubmittedValue();
        diagnostics.queues.graphics_completed = kernel.Frames().CompletedSubmitValue();
        if (host.HasUploadHost() && host.Upload().UsesCrossQueueSubmit()) {
            diagnostics.queues.transfer_submitted = host.Upload().LastSubmittedValue();
            diagnostics.queues.transfer_completed = host.Upload().CompletedSubmitValue();
        }
        if (host.HasParticleSimulationHost() && host.ParticleSimulationService().HasComputeTimelineProgress()) {
            diagnostics.queues.compute_submitted = host.ParticleSimulationService().LastSubmittedValue();
            diagnostics.queues.compute_completed = host.ParticleSimulationService().CompletedSubmitValue();
        }

        {
            const auto command_stats = kernel.Commands().Stats(result_.render.frame_index);
            diagnostics.commands.frame_slot_count = command_stats.frame_slot_count;
            diagnostics.commands.used_primary_count = command_stats.used_primary_count;
        }

        if (!DiagnosticsCollectsServiceCounters(diagnostics_level)) {
            result_.diagnostics = diagnostics;
            result_.running = kernel.IsRunning();
            return;
        }

        if (host.HasUploadHost() && result_.render.frame_index < host.Upload().FramesInFlight()) {
            diagnostics.upload = host.Upload().FrameStats(result_.render.frame_index);
            diagnostics.allocations.upload_capacity_bytes = diagnostics.upload.capacity_bytes;
            diagnostics.allocations.upload_staging_page_growth_count =
                diagnostics.upload.staging_page_growth_count;
        }
        if (host.HasTextureHost()) {
            diagnostics.texture = host.Texture().Stats();
        }
        if (host.HasFrameComposerHost()) {
            diagnostics.frame_composer = host.FrameComposer().Stats();
        }
        if (host.HasIblHost()) {
            diagnostics.ibl = host.Ibl().Stats();
        }
        if (host.HasIblBakeHost()) {
            diagnostics.ibl_bake = host.IblBake().Stats();
        }
        if (host.HasDescriptorHost()) {
            diagnostics.descriptor.total_pool_count = host.Descriptor().TotalPoolCount();
            diagnostics.descriptor.frame_pool_count = host.Descriptor().FramePoolCount(result_.render.frame_index);
            diagnostics.descriptor.total_allocated_set_count = host.Descriptor().TotalAllocatedSetCount();
            diagnostics.descriptor.frame_allocated_set_count =
                host.Descriptor().FrameAllocatedSetCount(result_.render.frame_index);
            diagnostics.descriptor.validation = host.Descriptor().ValidationStats();
            diagnostics.allocations.descriptor_total_pool_count = diagnostics.descriptor.total_pool_count;
        }
        if (host.HasPipelineHost()) {
            diagnostics.pipeline = host.Pipeline().Stats();
        }
        if (host.HasRenderTargetHost()) {
            diagnostics.render_target = host.RenderTarget().Stats();
        }
        if (host.HasRenderTargetPool()) {
            diagnostics.render_target_pool = host.RenderTargetPoolStats();
            diagnostics.allocations.render_target_transient_acquired_count =
                static_cast<std::uint32_t>(diagnostics.render_target_pool.acquire_count);
        }
        if (const auto* render_graph_service =
                host.Services().template TryGet<services::RenderGraphRuntimeService>();
            render_graph_service != nullptr) {
            diagnostics.render_graph = render_graph_service->LastDiagnostics();
        }
        if (host.HasGlyphAtlasHost()) {
            diagnostics.glyph_atlas = host.GlyphAtlas().Stats();
        }
        if (host.HasGlyphUploadHost()) {
            diagnostics.glyph_upload = host.GlyphUpload().Stats();
        }
        if (host.HasParticleUploadHost()) {
            diagnostics.particle_upload = host.ParticleUpload().Stats();
        }
        if (host.HasParticleSimulationHost()) {
            diagnostics.particle_simulation = host.ParticleSimulation().Stats();
        }
        diagnostics.particle_render.service_available = host.Particles().IsAvailable();
        result_.diagnostics = diagnostics;
        result_.running = kernel.IsRunning();
    }

    HostType host{};
    KernelType kernel{};
    RuntimeExecutionTrace last_execution_trace{};
};

} // namespace vr::runtime

