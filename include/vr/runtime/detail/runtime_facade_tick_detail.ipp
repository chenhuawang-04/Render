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

    [[nodiscard]] static TickResult Convert(const typename InternalRuntimeHost::RuntimeTickResult& legacy_) {
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

    [[nodiscard]] static QueueTimelineStats BuildQueueTimelineStats(
        const FrameGpuProgressContext& progress_) noexcept {
        return {
            .graphics_submitted = progress_.graphics_submitted,
            .graphics_completed = progress_.graphics_completed,
            .transfer_submitted = progress_.transfer_submitted,
            .transfer_completed = progress_.transfer_completed,
            .compute_submitted = progress_.compute_submitted,
            .compute_completed = progress_.compute_completed,
        };
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

    void CollectTickPostState(typename InternalRuntimeHost::RuntimeTickResult& result_,
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
        diagnostics.queues = BuildQueueTimelineStats(kernel.BuildFrameGpuProgressContext());

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

