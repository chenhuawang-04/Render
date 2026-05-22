    template<typename RecorderT>
    requires RuntimeTickRecorder<RecorderT>
    void PrepareTickFrame(RecorderT& recorder_, const std::uint32_t frame_index_) {
        DispatchPrepareFrame(recorder_, frame_index_);
    }

    [[nodiscard]] TickUploadFlushResult FlushTickUploads(const std::uint32_t frame_index_) {
        TickUploadFlushResult flush{};

        if (!upload_initialized) {
            return flush;
        }

        UploadSubmitInfo upload_submit{};
        if (upload_wait_required) {
            upload_submit.signal_semaphore = UploadCompleteSemaphore(frame_index_);
        }

        const UploadEndFrameResult upload_end_result = upload_host.EndFrameAndSubmit(
            platform_host.Context(),
            frame_index_,
            upload_submit);
        flush.submitted = upload_end_result.submitted;

        if (upload_end_result.submitted && upload_wait_required) {
            flush.extra_wait.semaphore = upload_submit.signal_semaphore;
            flush.extra_wait.stage_mask = create_info_cache.upload_wait_stage_mask;
            flush.extra_wait_count = 1U;
            flush.cross_queue_wait = true;
            flush.transfer_wait_value = upload_host.LastSubmittedValue();
        }

        return flush;
    }

    template<typename RecorderT>
    requires RuntimeTickRecorder<RecorderT>
    [[nodiscard]] AcquiredFrame AcquireTickFrame(RecorderT& recorder_,
                                                 const std::uint64_t frame_id_,
                                                 const TickUploadFlushResult& upload_flush_) {
        return render_loop.AcquireFrame(platform_host.Context(),
                                        platform_host.SurfaceHost(),
                                        swapchain,
                                        frame_id_,
                                        upload_flush_.transfer_wait_value,
                                        0U,
                                        recorder_);
    }

    template<typename RecorderT>
    requires RuntimeTickRecorder<RecorderT>
    void RecordTickFrame(RecorderT& recorder_,
                         const AcquiredFrame& acquired_frame_,
                         const VkCommandBuffer command_buffer_) {
        RuntimeRecordAdapter<RecorderT> record_adapter{
            *this,
            recorder_,
            render_loop.Sync().LastSubmittedValue(),
            render_loop.Sync().CompletedSubmitValue()
        };
        FrameRecordContext context{};
        context.command_buffer = command_buffer_;
        context.frame_index = acquired_frame_.token.frame_index;
        context.image_index = acquired_frame_.token.image_index;
        context.extent = acquired_frame_.extent;
        context.format = acquired_frame_.format;
        context.image = acquired_frame_.image;
        context.image_view = acquired_frame_.image_view;
        record_adapter.Record(context);
    }

    [[nodiscard]] TickResult SubmitPresentTickFrame(const AcquiredFrame& acquired_frame_,
                                                    const VkCommandBuffer command_buffer_,
                                                    const TickUploadFlushResult& upload_flush_) {
        return SubmitPresentTickFrame(acquired_frame_,
                                      command_buffer_,
                                      upload_flush_.ExtraWaits(),
                                      upload_flush_.extra_wait_count);
    }

    [[nodiscard]] TickResult SubmitPresentTickFrame(const AcquiredFrame& acquired_frame_,
                                                    const VkCommandBuffer command_buffer_,
                                                    const FrameSubmitWait* extra_waits_,
                                                    const std::uint32_t extra_wait_count_) {
        const auto render_result = render_loop.SubmitPresentAndAdvance(platform_host.Context(),
                                                                       swapchain,
                                                                       acquired_frame_.token,
                                                                       command_buffer_,
                                                                       extra_waits_,
                                                                       extra_wait_count_);
        last_tick_frame_index = render_result.frame_index;
        last_tick_image_index = render_result.image_index;
        return render_result;
    }

    void CollectTickPostState(RuntimeTickResult& result_,
                              const std::uint64_t frame_id_) {
        FillPipelineQueueStats(result_);
        FillFrameDiagnostics(result_, frame_id_);
        result_.running = platform_host.IsRunning();
    }

    void FinalizeTick(RuntimeTickResult& result_, const std::uint64_t frame_id_) {
        CollectTickPostState(result_, frame_id_);
    }

    template<typename RecorderT>
    requires RuntimeTickRecorder<RecorderT>
    [[nodiscard]] RuntimeTickResult Tick(RecorderT& recorder_) {
        DefaultPhaseDriver phase_driver{*this};
        return TickWithPhaseDriver(recorder_, phase_driver);
    }

    template<typename RecorderT, typename PhaseDriverT>
    requires RuntimeTickRecorder<RecorderT>
    [[nodiscard]] RuntimeTickResult TickWithPhaseDriver(RecorderT& recorder_,
                                                        PhaseDriverT& phase_driver_) {
        auto begin = BeginTickFrame();
        if (!begin.ready) {
            return begin.result;
        }

        RuntimeTickResult& result = begin.result;
        const std::uint64_t frame_id = begin.frame_id;
        const std::uint32_t frame_index = begin.frame_index;
        phase_driver_.OnServiceBeginFrame(frame_index,
                                          render_loop.Sync().LastSubmittedValue(),
                                          render_loop.Sync().CompletedSubmitValue());
        result.compiled_pipeline_count += pipeline_service_ref.LastBeginFrameCompileCount();

        PrepareTickFrame(recorder_, frame_index);
        phase_driver_.OnPrepare(frame_index,
                                render_loop.Sync().LastSubmittedValue(),
                                render_loop.Sync().CompletedSubmitValue());

        const TickUploadFlushResult upload_flush = FlushTickUploads(frame_index);
        result.upload_submitted = upload_flush.submitted;
        result.upload_cross_queue_wait = upload_flush.cross_queue_wait;

        phase_driver_.OnFlushUploads();
        phase_driver_.OnPreRecord(frame_index,
                                  render_loop.Sync().LastSubmittedValue(),
                                  render_loop.Sync().CompletedSubmitValue());
        auto* graph_service =
            services_ref.template TryGet<runtime::services::RenderGraphRuntimeService>();
        if (graph_service != nullptr &&
            upload_flush.extra_wait_count != 0U &&
            upload_flush.extra_wait.semaphore != VK_NULL_HANDLE) {
            graph_service->RegisterExternalQueueSubmitWait(render_graph::QueueClass::compute,
                                                           upload_flush.extra_wait.semaphore);
        }

        const AcquiredFrame acquired_frame = AcquireTickFrame(recorder_, frame_id, upload_flush);
        if (acquired_frame.code != TickCode::Submitted) {
            result.render = {
                .code = acquired_frame.code,
                .frame_index = acquired_frame.token.frame_index,
                .image_index = acquired_frame.token.image_index,
            };
            last_tick_frame_index = result.render.frame_index;
            last_tick_image_index = result.render.image_index;
            FinalizeTick(result, frame_id);
            return result;
        }

        render_loop.Commands().ResetFrame(platform_host.Context(), acquired_frame.token.frame_index);
        VkCommandBuffer command_buffer = render_loop.Commands().BeginPrimary(platform_host.Context(),
                                                                            acquired_frame.token.frame_index,
                                                                            create_info_cache.render_loop.command_usage_flags);
        RecordTickFrame(recorder_, acquired_frame, command_buffer);
        phase_driver_.OnRecord(frame_index,
                               render_loop.Sync().LastSubmittedValue(),
                               render_loop.Sync().CompletedSubmitValue(),
                               command_buffer);
        render_loop.Commands().EndCommandBuffer(command_buffer);

        std::vector<FrameSubmitWait> submit_waits{};
        if (graph_service != nullptr &&
            graph_service->HasPreparedMultiQueueSubmission()) {
            (void)graph_service->SubmitPreparedMultiQueueWork(platform_host.Context());
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

        result.render = SubmitPresentTickFrame(acquired_frame,
                                               command_buffer,
                                               submit_waits.empty() ? nullptr : submit_waits.data(),
                                               static_cast<std::uint32_t>(submit_waits.size()));
        if (graph_service != nullptr) {
            graph_service->MarkGraphicsSubmissionEnqueued(acquired_frame.token);
        }
        phase_driver_.OnSubmit();

        phase_driver_.OnPostRecord(frame_index,
                                   render_loop.Sync().LastSubmittedValue(),
                                   render_loop.Sync().CompletedSubmitValue());
        phase_driver_.OnEndFrame(frame_index,
                                 render_loop.Sync().LastSubmittedValue(),
                                 render_loop.Sync().CompletedSubmitValue());
        result.compiled_pipeline_count += pipeline_service_ref.LastEndFrameCompileCount();
        phase_driver_.OnPresent();
        phase_driver_.OnRetire(frame_index,
                               render_loop.Sync().LastSubmittedValue(),
                               render_loop.Sync().CompletedSubmitValue());

        FinalizeTick(result, frame_id);
        return result;
    }

