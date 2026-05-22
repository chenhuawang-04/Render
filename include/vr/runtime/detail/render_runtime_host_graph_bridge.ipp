    struct ServicePhaseFrameContext final {
        struct FrameInfo final {
            std::uint32_t frame_index = 0U;
            std::uint32_t image_index = 0U;
        };

        struct ProgressInfo final {
            std::uint64_t graphics_submitted = 0U;
            std::uint64_t graphics_completed = 0U;
        };

        VulkanContext& device;
        RuntimeServicesType& services;
        FrameInfo frame{};
        ProgressInfo progress{};
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        SwapchainTargetSet* swapchain_targets = nullptr;
    };

    struct ServicePhaseContext final {
        ServicePhaseFrameContext& frame_context;
        vr::runtime::RuntimeExecutionTrace& execution;
    };

    struct DefaultPhaseDriver final {
        explicit DefaultPhaseDriver(RenderRuntimeHost& runtime_) noexcept
            : runtime(runtime_) {}

        void OnServiceBeginFrame(const std::uint32_t frame_index_,
                                 const std::uint64_t graphics_submitted_,
                                 const std::uint64_t graphics_completed_) {
            auto frame_context = runtime.BuildServicePhaseFrameContext(frame_index_,
                                                                      graphics_submitted_,
                                                                      graphics_completed_);
            auto phase_context = ServicePhaseContext{
                .frame_context = frame_context,
                .execution = execution,
            };
            runtime.services_ref.BeginFrame(phase_context);
            execution.Mark(vr::runtime::RuntimeExecutionStage::ServiceBeginFrame);
        }

        void OnPrepare(const std::uint32_t frame_index_,
                       const std::uint64_t graphics_submitted_,
                       const std::uint64_t graphics_completed_) {
            auto frame_context = runtime.BuildServicePhaseFrameContext(frame_index_,
                                                                      graphics_submitted_,
                                                                      graphics_completed_);
            auto phase_context = ServicePhaseContext{
                .frame_context = frame_context,
                .execution = execution,
            };
            runtime.services_ref.PrepareFrame(phase_context);
            execution.Mark(vr::runtime::RuntimeExecutionStage::Prepare);
        }

        void OnFlushUploads() noexcept {
            execution.Mark(vr::runtime::RuntimeExecutionStage::FlushUploads);
        }

        void OnPreRecord(const std::uint32_t frame_index_,
                         const std::uint64_t graphics_submitted_,
                         const std::uint64_t graphics_completed_) {
            auto frame_context = runtime.BuildServicePhaseFrameContext(frame_index_,
                                                                      graphics_submitted_,
                                                                      graphics_completed_);
            auto phase_context = ServicePhaseContext{
                .frame_context = frame_context,
                .execution = execution,
            };
            runtime.services_ref.PreRecord(phase_context);
            execution.Mark(vr::runtime::RuntimeExecutionStage::PreRecord);
        }

        void OnRecord(const std::uint32_t frame_index_,
                      const std::uint64_t graphics_submitted_,
                      const std::uint64_t graphics_completed_,
                      const VkCommandBuffer command_buffer_) {
            auto frame_context = runtime.BuildServicePhaseFrameContext(frame_index_,
                                                                      graphics_submitted_,
                                                                      graphics_completed_);
            frame_context.command_buffer = command_buffer_;
            auto phase_context = ServicePhaseContext{
                .frame_context = frame_context,
                .execution = execution,
            };
            runtime.services_ref.Record(phase_context);
            execution.Mark(vr::runtime::RuntimeExecutionStage::Record);
        }

        void OnSubmit() noexcept {
            execution.Mark(vr::runtime::RuntimeExecutionStage::Submit);
        }

        void OnPostRecord(const std::uint32_t frame_index_,
                          const std::uint64_t graphics_submitted_,
                          const std::uint64_t graphics_completed_) {
            auto frame_context = runtime.BuildServicePhaseFrameContext(frame_index_,
                                                                      graphics_submitted_,
                                                                      graphics_completed_);
            auto phase_context = ServicePhaseContext{
                .frame_context = frame_context,
                .execution = execution,
            };
            runtime.services_ref.PostRecord(phase_context);
        }

        void OnPresent() noexcept {
            execution.Mark(vr::runtime::RuntimeExecutionStage::Present);
        }

        void OnEndFrame(const std::uint32_t frame_index_,
                        const std::uint64_t graphics_submitted_,
                        const std::uint64_t graphics_completed_) {
            auto frame_context = runtime.BuildServicePhaseFrameContext(frame_index_,
                                                                      graphics_submitted_,
                                                                      graphics_completed_);
            auto phase_context = ServicePhaseContext{
                .frame_context = frame_context,
                .execution = execution,
            };
            runtime.services_ref.EndFrame(phase_context);
            execution.Mark(vr::runtime::RuntimeExecutionStage::EndFrame);
        }

        void OnRetire(const std::uint32_t frame_index_,
                      const std::uint64_t graphics_submitted_,
                      const std::uint64_t graphics_completed_) {
            auto frame_context = runtime.BuildServicePhaseFrameContext(frame_index_,
                                                                      graphics_submitted_,
                                                                      graphics_completed_);
            auto phase_context = ServicePhaseContext{
                .frame_context = frame_context,
                .execution = execution,
            };
            runtime.services_ref.Retire(phase_context);
            execution.Mark(vr::runtime::RuntimeExecutionStage::Retire);
        }

        RenderRuntimeHost& runtime;
        vr::runtime::RuntimeExecutionTrace execution{};
    };

    void RefreshServiceBindings() noexcept {
        command_service_ref.BindAvailable(loop_initialized);

        if (gpu_memory_initialized) {
            gpu_memory_service_ref.Bind(gpu_memory_host);
        } else {
            gpu_memory_service_ref.Reset();
        }

        if (texture_initialized) {
            texture_service_ref.Bind(texture_host);
        } else {
            texture_service_ref.Reset();
        }

        if (upload_initialized) {
            upload_service_ref.Bind(upload_host);
        } else {
            upload_service_ref.Reset();
        }

        if (descriptor_initialized) {
            descriptor_service_ref.Bind(descriptor_host);
        } else {
            descriptor_service_ref.Reset();
        }

        if (pipeline_initialized) {
            pipeline_service_ref.Bind(pipeline_host);
            pipeline_service_ref.ConfigureWarmup({
                .max_graphics_compiles_per_tick =
                    create_info_cache.pipeline_warmup.max_graphics_compiles_per_tick,
                .max_compute_compiles_per_tick =
                    create_info_cache.pipeline_warmup.max_compute_compiles_per_tick,
                .compile_before_render = create_info_cache.pipeline_warmup.compile_before_render,
                .compile_after_render = create_info_cache.pipeline_warmup.compile_after_render,
            });
        } else {
            pipeline_service_ref.Reset();
        }

        if (render_target_initialized) {
            render_target_service_ref.Bind(render_target_host);
        } else {
            render_target_service_ref.Reset();
        }

        if (sampler_initialized) {
            sampler_service_ref.Bind(sampler_host);
        } else {
            sampler_service_ref.Reset();
        }

        if (frame_composer_initialized) {
            frame_composer_service_ref.Bind(frame_composer_host);
        } else {
            frame_composer_service_ref.Reset();
        }

        if (ibl_initialized) {
            ibl_service_ref.Bind(ibl_host);
        } else {
            ibl_service_ref.Reset();
        }

        if (ibl_bake_initialized) {
            ibl_bake_service_ref.Bind(ibl_bake_host);
        } else {
            ibl_bake_service_ref.Reset();
        }

        if (sky_environment_initialized) {
            sky_environment_service_ref.Bind(sky_environment_gpu_host);
        } else {
            sky_environment_service_ref.Reset();
        }

        if (freetype_initialized) {
            freetype_service_ref.Bind(freetype_host);
        } else {
            freetype_service_ref.Reset();
        }

        if (glyph_atlas_initialized) {
            glyph_atlas_service_ref.Bind(glyph_atlas_host);
        } else {
            glyph_atlas_service_ref.Reset();
        }

        if (glyph_upload_initialized) {
            glyph_upload_service_ref.Bind(glyph_upload_host);
        } else {
            glyph_upload_service_ref.Reset();
        }

        if (particle_upload_initialized) {
            particle_upload_service_ref.Bind(particle_upload_host);
        } else {
            particle_upload_service_ref.Reset();
        }

        if (particle_simulation_initialized) {
            particle_simulation_service_ref.Bind(particle_simulation_host);
        } else {
            particle_simulation_service_ref.Reset();
        }

        particle_render_service_ref.Bind(particle_upload_service_ref,
                                         particle_simulation_service_ref,
                                         texture_initialized ? &texture_service_ref : nullptr);

        services_ref.Bind(command_service_ref,
                          gpu_memory_service_ref,
                          upload_service_ref,
                          descriptor_service_ref,
                          pipeline_service_ref,
                          sampler_service_ref,
                          texture_service_ref,
                          render_target_service_ref,
                          render_graph_runtime_service_ref,
                          frame_composer_service_ref,
                          ibl_service_ref,
                          sky_environment_service_ref,
                          ibl_bake_service_ref,
                          freetype_service_ref,
                          glyph_atlas_service_ref,
                          glyph_upload_service_ref,
                          particle_upload_service_ref,
                          particle_simulation_service_ref,
                          particle_render_service_ref);
    }

    [[nodiscard]] FrameStaticContext BuildFrameStaticContext(std::uint32_t frame_index_) const noexcept {
        return {
            .frame_index = frame_index_,
            .swapchain_extent = swapchain.Extent(),
            .swapchain_format = swapchain.Format(),
        };
    }

    [[nodiscard]] FrameGpuProgressContext BuildFrameGpuProgressContext() const noexcept {
        return {
            .last_submitted_value = render_loop.Sync().LastSubmittedValue(),
            .completed_submit_value = render_loop.Sync().CompletedSubmitValue(),
        };
    }

    template<typename RecorderT>
    static void RegisterOptionalDirectGraphImportedResources(
        RecorderT& recorder_,
        runtime::services::RenderGraphRuntimeService& render_graph_service_) {
        if constexpr (RuntimeDirectGraphImportedResourceRecorder<RecorderT>) {
            recorder_.RegisterGraphImportedResources(render_graph_service_);
        }
    }

    template<typename RecorderT>
    static void SetDirectGraphBuildCallback(
        RecorderT& recorder_,
        runtime::services::RenderGraphRuntimeService& render_graph_service_) {
        render_graph_service_.SetDirectGraphBuildCallback(
            [&recorder_, &render_graph_service_](render_graph::RenderGraphBuilder& builder_ref_,
                                                 const render_graph::ResourceHandle present_target_ref_,
                                                 const render_graph::Extent3D& reference_extent_ref_,
                                                 render_graph::ResourceVersionHandle& present_ready_version_ref_,
                                                 const runtime::services::RenderGraphRuntimeService::ImportedTextureRegisterFn& register_imported_texture_ref_) {
                recorder_.BuildDirectRuntimeGraph(RuntimeDirectGraphBuildView{
                    .builder = builder_ref_,
                    .present_target = present_target_ref_,
                    .reference_extent = reference_extent_ref_,
                    .present_ready_version = present_ready_version_ref_,
                    .register_imported_texture = register_imported_texture_ref_,
                });
                RegisterOptionalDirectGraphImportedResources(recorder_, render_graph_service_);
            });
    }

    template<typename RecorderT>
    void DispatchPrepareFrame(RecorderT& recorder_, std::uint32_t frame_index_) {
        const FrameStaticContext frame = BuildFrameStaticContext(frame_index_);
        const FrameGpuProgressContext progress = BuildFrameGpuProgressContext();
        VulkanContext& device = platform_host.Context();

        if constexpr (requires(RecorderT& recorder_ref_,
                               const SceneRecorder3DPrepareView& prepare_view_) {
                          recorder_ref_.PrepareFrame(prepare_view_);
                      }) {
            if constexpr (requires(RecorderT& recorder_ref_) {
                              recorder_ref_.FramePacket();
                          }) {
                if (const auto* frame_packet = recorder_.FramePacket();
                    frame_packet != nullptr) {
                    auto& render_graph_service =
                        services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
                    render_graph_service.template SetFrameSnapshot<ecs::Dim3>(
                        render_graph::MakeFrameSnapshot(
                            *frame_packet,
                            frame.frame_index,
                            render_graph::Extent3D{
                                .width = frame.swapchain_extent.width,
                                .height = frame.swapchain_extent.height,
                                .depth = 1U,
                            }));
                    if constexpr (requires(RecorderT& recorder_ref_,
                                           render_graph::RenderGraphBuilder& builder_ref_,
                                           const render_graph::FrameSnapshot3D& snapshot_ref_,
                                           const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_ref_,
                                           render_graph::ResourceVersionHandle& color_chain_ref_) {
                                      recorder_ref_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                                  }) {
                        render_graph_service.template SetGraphBuildCallback<ecs::Dim3>(
                            [&recorder_](render_graph::RenderGraphBuilder& builder_ref_,
                                         const render_graph::FrameSnapshot3D& snapshot_ref_,
                                         const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_ref_,
                                         render_graph::ResourceVersionHandle& color_chain_ref_) {
                                recorder_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                            });
                    }
                }
            }
            recorder_.PrepareFrame(SceneRecorder3DPrepareView{
                .device = device,
                .gpu_memory = &gpu_memory_host,
                .texture = texture_initialized ? &texture_host : nullptr,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .upload = upload_initialized ? &upload_host : nullptr,
                .descriptor = descriptor_initialized ? &descriptor_host : nullptr,
                .frame_composer = frame_composer_initialized ? &frame_composer_host : nullptr,
                .ibl = ibl_initialized ? &ibl_host : nullptr,
                .ibl_bake = ibl_bake_initialized ? &ibl_bake_host : nullptr,
                .sky_environment = sky_environment_initialized ? &sky_environment_gpu_host : nullptr,
                .pipeline = pipeline_initialized ? &pipeline_host : nullptr,
                .render_target = render_target_host,
                .sampler = sampler_initialized ? &sampler_host : nullptr,
                .freetype = freetype_initialized ? &freetype_host : nullptr,
                .glyph_atlas = glyph_atlas_initialized ? &glyph_atlas_host : nullptr,
                .glyph_upload = glyph_upload_initialized ? &glyph_upload_host : nullptr,
                .particle_upload = particle_upload_initialized ? &particle_upload_host : nullptr,
                .particle_simulation = particle_simulation_initialized ? &particle_simulation_host : nullptr,
                .frame = frame,
                .progress = progress,
            });
            if constexpr (requires(RecorderT& recorder_ref_) {
                              recorder_ref_.FramePacket();
                          }) {
                if (const auto* frame_packet = recorder_.FramePacket();
                    frame_packet != nullptr) {
                    auto& render_graph_service =
                        services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
                    render_graph_service.template SetFrameSnapshot<ecs::Dim3>(
                        render_graph::MakeFrameSnapshot(
                            *frame_packet,
                            frame.frame_index,
                            render_graph::Extent3D{
                                .width = frame.swapchain_extent.width,
                                .height = frame.swapchain_extent.height,
                                .depth = 1U,
                            }));
                    if constexpr (requires(RecorderT& recorder_ref_,
                                           render_graph::RenderGraphBuilder& builder_ref_,
                                           const render_graph::FrameSnapshot3D& snapshot_ref_,
                                           const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_ref_,
                                           render_graph::ResourceVersionHandle& color_chain_ref_) {
                                      recorder_ref_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                                  }) {
                        render_graph_service.template SetGraphBuildCallback<ecs::Dim3>(
                            [&recorder_](render_graph::RenderGraphBuilder& builder_ref_,
                                         const render_graph::FrameSnapshot3D& snapshot_ref_,
                                         const render_graph::MinimalFrameGraphBuildResult<ecs::Dim3>& build_result_ref_,
                                         render_graph::ResourceVersionHandle& color_chain_ref_) {
                                recorder_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                            });
                    }
                }
            }
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const SceneRecorder2DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            if constexpr (requires(RecorderT& recorder_ref_) {
                              recorder_ref_.FramePacket();
                          }) {
                if (const auto* frame_packet = recorder_.FramePacket();
                    frame_packet != nullptr) {
                    auto& render_graph_service =
                        services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
                    render_graph_service.template SetFrameSnapshot<ecs::Dim2>(
                        render_graph::MakeFrameSnapshot(
                            *frame_packet,
                            frame.frame_index,
                            render_graph::Extent3D{
                                .width = frame.swapchain_extent.width,
                                .height = frame.swapchain_extent.height,
                                .depth = 1U,
                            }));
                    if constexpr (requires(RecorderT& recorder_ref_,
                                           render_graph::RenderGraphBuilder& builder_ref_,
                                           const render_graph::FrameSnapshot2D& snapshot_ref_,
                                           const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_ref_,
                                           render_graph::ResourceVersionHandle& color_chain_ref_) {
                                      recorder_ref_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                                  }) {
                        render_graph_service.template SetGraphBuildCallback<ecs::Dim2>(
                            [&recorder_](render_graph::RenderGraphBuilder& builder_ref_,
                                         const render_graph::FrameSnapshot2D& snapshot_ref_,
                                         const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_ref_,
                                         render_graph::ResourceVersionHandle& color_chain_ref_) {
                                recorder_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                            });
                    }
                }
            }
            recorder_.PrepareFrame(SceneRecorder2DPrepareView{
                .device = device,
                .gpu_memory = &gpu_memory_host,
                .texture = texture_initialized ? &texture_host : nullptr,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .upload = upload_initialized ? &upload_host : nullptr,
                .descriptor = descriptor_initialized ? &descriptor_host : nullptr,
                .frame_composer = frame_composer_initialized ? &frame_composer_host : nullptr,
                .ibl = ibl_initialized ? &ibl_host : nullptr,
                .ibl_bake = ibl_bake_initialized ? &ibl_bake_host : nullptr,
                .pipeline = pipeline_initialized ? &pipeline_host : nullptr,
                .render_target = render_target_host,
                .sampler = sampler_initialized ? &sampler_host : nullptr,
                .freetype = freetype_initialized ? &freetype_host : nullptr,
                .glyph_atlas = glyph_atlas_initialized ? &glyph_atlas_host : nullptr,
                .glyph_upload = glyph_upload_initialized ? &glyph_upload_host : nullptr,
                .particle_upload = particle_upload_initialized ? &particle_upload_host : nullptr,
                .particle_simulation = particle_simulation_initialized ? &particle_simulation_host : nullptr,
                .frame = frame,
                .progress = progress,
            });
            if constexpr (requires(RecorderT& recorder_ref_) {
                              recorder_ref_.FramePacket();
                          }) {
                if (const auto* frame_packet = recorder_.FramePacket();
                    frame_packet != nullptr) {
                    auto& render_graph_service =
                        services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
                    render_graph_service.template SetFrameSnapshot<ecs::Dim2>(
                        render_graph::MakeFrameSnapshot(
                            *frame_packet,
                            frame.frame_index,
                            render_graph::Extent3D{
                                .width = frame.swapchain_extent.width,
                                .height = frame.swapchain_extent.height,
                                .depth = 1U,
                            }));
                    if constexpr (requires(RecorderT& recorder_ref_,
                                           render_graph::RenderGraphBuilder& builder_ref_,
                                           const render_graph::FrameSnapshot2D& snapshot_ref_,
                                           const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_ref_,
                                           render_graph::ResourceVersionHandle& color_chain_ref_) {
                                      recorder_ref_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                                  }) {
                        render_graph_service.template SetGraphBuildCallback<ecs::Dim2>(
                            [&recorder_](render_graph::RenderGraphBuilder& builder_ref_,
                                         const render_graph::FrameSnapshot2D& snapshot_ref_,
                                         const render_graph::MinimalFrameGraphBuildResult<ecs::Dim2>& build_result_ref_,
                                         render_graph::ResourceVersionHandle& color_chain_ref_) {
                                recorder_.BuildRenderGraph(builder_ref_, snapshot_ref_, build_result_ref_, color_chain_ref_);
                            });
                    }
                }
            }
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const FrameComposerPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(FrameComposerPrepareView{
                .device = device,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .sampler = sampler_host,
                .render_target = render_target_host,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .frame = frame,
                .progress = progress,
            });
            if constexpr (requires(RecorderT& recorder_ref_,
                                   render_graph::RenderGraphBuilder& builder_ref_,
                                   render_graph::ResourceHandle present_target_ref_,
                                   const render_graph::Extent3D& reference_extent_ref_,
                                   render_graph::ResourceVersionHandle& present_ready_version_ref_,
                                   const runtime::services::RenderGraphRuntimeService::ImportedTextureRegisterFn& register_imported_texture_ref_) {
                              recorder_ref_.BuildRenderGraph(builder_ref_,
                                                             present_target_ref_,
                                                             reference_extent_ref_,
                                                             present_ready_version_ref_,
                                                             register_imported_texture_ref_);
                          }) {
                auto& render_graph_service =
                    services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
                render_graph_service.SetDirectGraphBuildCallback(
                    [&recorder_](render_graph::RenderGraphBuilder& builder_ref_,
                                 const render_graph::ResourceHandle present_target_ref_,
                                 const render_graph::Extent3D& reference_extent_ref_,
                                 render_graph::ResourceVersionHandle& present_ready_version_ref_,
                                 const runtime::services::RenderGraphRuntimeService::ImportedTextureRegisterFn& register_imported_texture_ref_) {
                        recorder_.BuildRenderGraph(builder_ref_,
                                                   present_target_ref_,
                                                   reference_extent_ref_,
                                                  present_ready_version_ref_,
                                                   register_imported_texture_ref_);
                    });
            }
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const RenderTargetCompositeRendererPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(RenderTargetCompositeRendererPrepareView{
                .device = device,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .render_target = render_target_host,
                .sampler = sampler_host,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                 TextRenderer2DPrepareView>) {
            recorder_.PrepareFrame(TextRenderer2DPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .freetype = freetype_host,
                .glyph_atlas = glyph_atlas_host,
                .glyph_upload = glyph_upload_host,
                .render_graph_upload_active = true,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                 TextRenderer3DPrepareView>) {
            recorder_.PrepareFrame(TextRenderer3DPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .freetype = freetype_host,
                .glyph_atlas = glyph_atlas_host,
                .glyph_upload = glyph_upload_host,
                .render_graph_upload_active = true,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (RuntimeDirectGraphRecorder<RecorderT,
                                                       GeometryRenderer2DPrepareView>) {
            recorder_.PrepareFrame(GeometryRenderer2DPrepareView{
                .device = device,
                .upload = upload_host,
                .pipeline = pipeline_host,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                 GeometryRenderer3DPrepareView>) {
            recorder_.PrepareFrame(GeometryRenderer3DPrepareView{
                .device = device,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .gpu_memory = gpu_memory_host,
                .ibl = ibl_host,
                .sampler = sampler_host,
                .texture = texture_initialized ? &texture_host : nullptr,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .render_target = render_target_initialized ? &render_target_host : nullptr,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                 SurfaceRenderer2DPrepareView>) {
            recorder_.PrepareFrame(SurfaceRenderer2DPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .sampler = sampler_host,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                 SurfaceRenderer3DPrepareView>) {
            recorder_.PrepareFrame(SurfaceRenderer3DPrepareView{
                .device = device,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .gpu_memory = gpu_memory_host,
                .ibl = ibl_host,
                .sampler = sampler_host,
                .texture = texture_initialized ? &texture_host : nullptr,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .render_target = render_target_initialized ? &render_target_host : nullptr,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                 ParticleRenderer2DPrepareView>) {
            recorder_.PrepareFrame(ParticleRenderer2DPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .sampler = sampler_host,
                .texture = texture_initialized ? &texture_host : nullptr,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .particle_upload = particle_upload_initialized ? &particle_upload_host : nullptr,
                .particle_simulation = particle_simulation_initialized ? &particle_simulation_host : nullptr,
                .render_target = render_target_initialized ? &render_target_host : nullptr,
                .render_graph_compute_active = true,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                 ParticleRenderer3DPrepareView>) {
            recorder_.PrepareFrame(ParticleRenderer3DPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .sampler = sampler_host,
                .texture = texture_initialized ? &texture_host : nullptr,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .particle_upload = particle_upload_initialized ? &particle_upload_host : nullptr,
                .particle_simulation = particle_simulation_initialized ? &particle_simulation_host : nullptr,
                .render_target = render_target_initialized ? &render_target_host : nullptr,
                .render_graph_compute_active = true,
                .frame = frame,
                .progress = progress,
            });
            auto& render_graph_service =
                services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            SetDirectGraphBuildCallback(recorder_, render_graph_service);
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const ShadowRenderer2DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(ShadowRenderer2DPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .pipeline = pipeline_host,
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const ShadowRenderer3DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(ShadowRenderer3DPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const IblBakeCoordinatorPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(IblBakeCoordinatorPrepareView{
                .device = device,
                .upload = upload_host,
                .ibl_bake = ibl_bake_host,
                .ibl = ibl_initialized ? &ibl_host : nullptr,
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const IblHostPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(IblHostPrepareView{
                .device = device,
                .gpu_memory = gpu_memory_host,
                .upload = upload_host,
                .descriptor = descriptor_host,
                .frame = frame,
                .progress = progress,
            });
        }
    }

    template<typename RecorderT>
    class RuntimeRecordAdapter final {
    public:
        RuntimeRecordAdapter(RenderRuntimeHost& runtime_,
                             RecorderT& recorder_,
                             std::uint64_t last_submitted_value_,
                             std::uint64_t completed_submit_value_) noexcept
            : runtime(runtime_),
              recorder(recorder_),
              last_submitted_value(last_submitted_value_),
              completed_submit_value(completed_submit_value_) {}

        void Record(const FrameRecordContext& record_context_) {
            FrameRecordContext augmented = record_context_;
            runtime.EnsureSwapchainTargets(last_submitted_value, completed_submit_value);
            augmented.render_target_host = runtime.render_target_initialized ? &runtime.render_target_host : nullptr;
            augmented.swapchain_targets = runtime.render_target_initialized ? &runtime.swapchain_targets : nullptr;

            const auto& graph_service = runtime.services_ref.template Get<runtime::services::RenderGraphRuntimeService>();
            if (graph_service.HasGraphRecordWorkSource()) {
                if (!graph_service.SupportsGraphExecution(runtime.platform_host.Context())) {
                    throw std::runtime_error(
                        "RenderRuntimeHost graph-only scheduling requires synchronization2 and dynamicRendering support");
                }
                return;
            }

            if constexpr (FrameContextRecorder<RecorderT>) {
                recorder.Record(augmented);
            } else if constexpr (FrameRecorder<RecorderT>) {
                recorder.Record(augmented.command_buffer,
                                augmented.frame_index,
                                augmented.image_index,
                                augmented.extent,
                                augmented.format,
                                augmented.image,
                                augmented.image_view);
            } else {
                throw std::runtime_error(
                    "RenderRuntimeHost requires graph work source when recorder omits legacy Record entry points");
            }
        }

        void OnSwapchainRecreated(std::uint32_t image_count_,
                                  VkExtent2D extent_,
                                  VkFormat format_,
                                  std::uint64_t last_submitted_value_,
                                  std::uint64_t completed_submit_value_) {
            runtime.InvalidateSwapchainTargets(last_submitted_value_, completed_submit_value_);
            if (runtime.frame_composer_initialized && runtime.render_target_initialized) {
                (void)runtime.frame_composer_host.OnSwapchainRecreated(
                    runtime.platform_host.Context(),
                    runtime.render_target_host,
                    extent_,
                    last_submitted_value_,
                    completed_submit_value_);
            }

            if constexpr (requires(RecorderT& recorder__,
                                   std::uint32_t image_count__,
                                   VkExtent2D extent__,
                                   VkFormat format__,
                                   std::uint64_t last_submitted_value__,
                                   std::uint64_t completed_submit_value__) {
                              recorder__.OnSwapchainRecreated(image_count__,
                                                              extent__,
                                                              format__,
                                                              last_submitted_value__,
                                                              completed_submit_value__);
                          }) {
                recorder.OnSwapchainRecreated(image_count_,
                                              extent_,
                                              format_,
                                              last_submitted_value_,
                                              completed_submit_value_);
            } else if constexpr (requires(RecorderT& recorder__,
                                          std::uint32_t image_count__,
                                          VkExtent2D extent__,
                                          VkFormat format__) {
                                     recorder__.OnSwapchainRecreated(image_count__, extent__, format__);
                                 }) {
                recorder.OnSwapchainRecreated(image_count_, extent_, format_);
            } else if constexpr (requires(RecorderT& recorder__,
                                          std::uint32_t image_count__,
                                          VkExtent2D extent__) {
                                     recorder__.OnSwapchainRecreated(image_count__, extent__);
                                 }) {
                recorder.OnSwapchainRecreated(image_count_, extent_);
            } else if constexpr (requires(RecorderT& recorder__, std::uint32_t image_count__) {
                                     recorder__.OnSwapchainRecreated(image_count__);
                                 }) {
                recorder.OnSwapchainRecreated(image_count_);
            }
        }

    private:
        RenderRuntimeHost& runtime;
        RecorderT& recorder;
        std::uint64_t last_submitted_value = 0U;
        std::uint64_t completed_submit_value = 0U;
    };

    static void ThrowVk(const char* stage_, VkResult result_) {
        std::ostringstream oss;
        oss << stage_ << " failed: VkResult(" << static_cast<int>(result_) << ")";
        throw std::runtime_error(oss.str());
    }

    static void CheckVk(const char* stage_, VkResult result_) {
        if (result_ != VK_SUCCESS) {
            ThrowVk(stage_, result_);
        }
    }

    void InitializeUploadSyncObjects(VulkanContext& context_) {
        upload_wait_required = upload_host.SubmitQueue() != context_.GraphicsQueue();
        if (!upload_wait_required) {
            upload_complete_semaphores.clear();
            return;
        }

        VkSemaphoreCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        upload_complete_semaphores.resize(frames_in_flight_v);
        uint32_t created_count = 0U;
        try {
            for (; created_count < frames_in_flight_v; ++created_count) {
                CheckVk("vkCreateSemaphore(runtime upload complete)",
                        vkCreateSemaphore(context_.Device(),
                                          &create_info,
                                          nullptr,
                                          &upload_complete_semaphores[created_count]));
            }
        } catch (...) {
            for (uint32_t i = 0U; i < created_count; ++i) {
                if (upload_complete_semaphores[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(context_.Device(), upload_complete_semaphores[i], nullptr);
                    upload_complete_semaphores[i] = VK_NULL_HANDLE;
                }
            }
            upload_complete_semaphores.clear();
            upload_wait_required = false;
            throw;
        }
    }

    void DestroyUploadSyncObjects(VkDevice device_) noexcept {
        if (device_ != VK_NULL_HANDLE) {
            for (auto& semaphore : upload_complete_semaphores) {
                if (semaphore != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            }
        } else {
            for (auto& semaphore : upload_complete_semaphores) {
                semaphore = VK_NULL_HANDLE;
            }
        }
        upload_complete_semaphores.clear();
        upload_wait_required = false;
    }

    [[nodiscard]] VkSemaphore UploadCompleteSemaphore(uint32_t frame_index_) const {
        if (!upload_wait_required) {
            return VK_NULL_HANDLE;
        }
        if (frame_index_ >= upload_complete_semaphores.size()) {
            throw std::out_of_range("RenderRuntimeHost upload semaphore frame index out of range");
        }
        return upload_complete_semaphores[frame_index_];
    }

    [[nodiscard]] ServicePhaseFrameContext BuildServicePhaseFrameContext(
        const std::uint32_t frame_index_,
        const std::uint64_t graphics_submitted_,
        const std::uint64_t graphics_completed_) {
        EnsureSwapchainTargets(graphics_submitted_, graphics_completed_);
        return ServicePhaseFrameContext{
            .device = platform_host.Context(),
            .services = services_ref,
            .frame = {
                .frame_index = frame_index_,
                .image_index = last_tick_image_index,
            },
            .progress = {
                .graphics_submitted = graphics_submitted_,
                .graphics_completed = graphics_completed_,
            },
            .swapchain_targets = render_target_initialized ? &swapchain_targets : nullptr,
        };
    }

    void FillPipelineQueueStats(RuntimeTickResult& result_) const noexcept {
        if (!pipeline_initialized) {
            result_.pending_graphics_compile_count = 0U;
            result_.pending_compute_compile_count = 0U;
            return;
        }
        result_.pending_graphics_compile_count = pipeline_host.PendingGraphicsCompileCount();
        result_.pending_compute_compile_count = pipeline_host.PendingComputeCompileCount();
    }

    void FillFrameDiagnostics(RuntimeTickResult& result_,
                              std::uint64_t frame_id_) const noexcept {
        const auto diagnostics_level = create_info_cache.diagnostics.level;
        if (!vr::runtime::DiagnosticsCollectsFrameData(diagnostics_level)) {
            result_.diagnostics = {};
            return;
        }

        RuntimeFrameDiagnostics diagnostics{};
        diagnostics.collected = true;
        diagnostics.level = diagnostics_level;

        diagnostics.frame.frame_id = frame_id_;
        diagnostics.frame.frame_index = result_.render.frame_index;
        diagnostics.frame.image_index = result_.render.image_index;
        diagnostics.frame.upload_submitted = result_.upload_submitted;
        diagnostics.frame.upload_cross_queue_wait = result_.upload_cross_queue_wait;

        diagnostics.swapchain.valid = swapchain.IsValid();
        diagnostics.swapchain.generation = swapchain.Generation();
        diagnostics.swapchain.image_count = swapchain.ImageCount();
        diagnostics.swapchain.extent = swapchain.Extent();
        diagnostics.swapchain.format = swapchain.Format();
        diagnostics.swapchain.color_space = swapchain.ColorSpace();
        diagnostics.swapchain.present_mode = swapchain.PresentMode();

        diagnostics.queues.graphics_submitted = render_loop.Sync().LastSubmittedValue();
        diagnostics.queues.graphics_completed = render_loop.Sync().CompletedSubmitValue();
        if (upload_initialized && upload_host.UsesCrossQueueSubmit()) {
            diagnostics.queues.transfer_submitted = upload_host.LastSubmittedValue();
            diagnostics.queues.transfer_completed = upload_host.CompletedSubmitValue();
        }
        if (const auto* graph_service =
                services_ref.template TryGet<runtime::services::RenderGraphRuntimeService>();
            graph_service != nullptr) {
            if (graph_service->HasTransferQueueProgress()) {
                diagnostics.queues.transfer_submitted =
                    (std::max)(diagnostics.queues.transfer_submitted,
                               graph_service->TransferSubmittedValue());
                diagnostics.queues.transfer_completed =
                    (std::max)(diagnostics.queues.transfer_completed,
                               graph_service->CompletedTransferValue());
            }
            if (graph_service->HasComputeQueueProgress()) {
                diagnostics.queues.compute_submitted =
                    (std::max)(diagnostics.queues.compute_submitted,
                               graph_service->ComputeSubmittedValue());
                diagnostics.queues.compute_completed =
                    (std::max)(diagnostics.queues.compute_completed,
                               graph_service->CompletedComputeValue());
            }
        }

        diagnostics.commands.frame_slot_count = frames_in_flight_v;
        if (result_.render.frame_index < render_loop.Commands().FramesInFlight()) {
            diagnostics.commands.used_primary_count =
                render_loop.Commands().UsedPrimaryCount(result_.render.frame_index);
        }

        if (!vr::runtime::DiagnosticsCollectsServiceCounters(diagnostics_level)) {
            result_.diagnostics = diagnostics;
            return;
        }

        if (upload_initialized && result_.render.frame_index < upload_host.FramesInFlight()) {
            diagnostics.upload = upload_host.FrameStats(result_.render.frame_index);
            diagnostics.allocations.upload_capacity_bytes = diagnostics.upload.capacity_bytes;
            diagnostics.allocations.upload_staging_page_growth_count =
                diagnostics.upload.staging_page_growth_count;
        }
        if (texture_initialized) {
            diagnostics.texture = texture_host.Stats();
        }
        if (frame_composer_initialized) {
            diagnostics.frame_composer = frame_composer_host.Stats();
        }
        if (ibl_initialized) {
            diagnostics.ibl = ibl_host.Stats();
        }
        if (ibl_bake_initialized) {
            diagnostics.ibl_bake = ibl_bake_host.Stats();
        }
        if (descriptor_initialized) {
            diagnostics.descriptor.total_pool_count = descriptor_host.TotalPoolCount();
            diagnostics.descriptor.frame_pool_count = descriptor_host.FramePoolCount(result_.render.frame_index);
            diagnostics.descriptor.total_allocated_set_count = descriptor_host.TotalAllocatedSetCount();
            diagnostics.descriptor.frame_allocated_set_count =
                descriptor_host.FrameAllocatedSetCount(result_.render.frame_index);
            diagnostics.descriptor.descriptor_indexing = platform_host.Context().DescriptorIndexingCapsInfo();
            diagnostics.descriptor.host = descriptor_host.Stats();
            diagnostics.descriptor.validation = descriptor_host.ValidationStats();
            diagnostics.allocations.descriptor_total_pool_count = diagnostics.descriptor.total_pool_count;
        }
        if (bindless_resources_initialized) {
            diagnostics.bindless.resources = bindless_resource_system.Stats();
        }
        if (pipeline_initialized) {
            diagnostics.pipeline = pipeline_host.Stats();
        }
        if (render_target_initialized) {
            diagnostics.render_target = render_target_host.Stats();
        }
        diagnostics.render_graph = render_graph_runtime_service_ref.LastDiagnostics();
        if (glyph_atlas_initialized) {
            diagnostics.glyph_atlas = glyph_atlas_host.Stats();
        }
        if (glyph_upload_initialized) {
            diagnostics.glyph_upload = glyph_upload_host.Stats();
        }
        if (particle_upload_initialized) {
            diagnostics.particle_upload = particle_upload_host.Stats();
        }
        if (particle_simulation_initialized) {
            diagnostics.particle_simulation = particle_simulation_host.Stats();
        }
        diagnostics.particle_render.service_available = particle_render_service_ref.IsAvailable();
        result_.diagnostics = diagnostics;
    }

    void InvalidateSwapchainTargets(std::uint64_t last_submitted_value_,
                                    std::uint64_t completed_submit_value_) {
        if (!render_target_initialized) {
            swapchain_targets.Reset();
            return;
        }
        swapchain_targets.Invalidate(platform_host.Context(),
                                     render_target_host,
                                     last_submitted_value_,
                                     completed_submit_value_);
    }

    void EnsureSwapchainTargets(std::uint64_t last_submitted_value_,
                                std::uint64_t completed_submit_value_) {
        if (!render_target_initialized || !swapchain.IsValid()) {
            return;
        }

        const SwapchainTargetSetImportConfig import_config{
            .debug_name = "SwapchainImportedTarget",
            .usage = create_info_cache.render_loop.swapchain.image_usage,
        };
        swapchain_targets.ImportOrRefresh(platform_host.Context(),
                                          swapchain,
                                          render_target_host,
                                          import_config,
                                          last_submitted_value_,
                                          completed_submit_value_);
    }

private:
