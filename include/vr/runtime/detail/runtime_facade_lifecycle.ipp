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
        auto init_context = RuntimeInitContext<typename InternalRuntimeHost::DefaultProfile,
                                               BackendTag,
                                               frames_in_flight_v>{
            .services = host.Services(),
            .kernel = kernel,
            .device = host.IsInitialized() ? &host.Context() : nullptr,
        };
        host.Services().Initialize(init_context);
        auto post_init_context = RuntimePostInitContext<typename InternalRuntimeHost::DefaultProfile,
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
            auto shutdown_context = RuntimeShutdownContext<typename InternalRuntimeHost::DefaultProfile,
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
        if (!host.IsInitialized()) {
            throw std::runtime_error("Runtime::Tick called before Initialize");
        }
        typename InternalRuntimeHost::RuntimeTickResult tick_state{};
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

        auto* graph_service =
            host.Services().template TryGet<services::RenderGraphRuntimeService>();
        if (graph_service != nullptr) {
            graph_service->SetDiagnosticsLevel(host.Config().diagnostics.level);
        }
        phase_driver.OnPreRecord(frame.token.frame_index,
                                 host.Loop().Sync().LastSubmittedValue(),
                                 host.Loop().Sync().CompletedSubmitValue());
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

