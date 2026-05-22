    RenderRuntimeHost() {
        RefreshServiceBindings();
    }
    ~RenderRuntimeHost() {
        Shutdown();
    }

    RenderRuntimeHost(const RenderRuntimeHost&) = delete;
    RenderRuntimeHost& operator=(const RenderRuntimeHost&) = delete;

    RenderRuntimeHost(RenderRuntimeHost&&) = delete;
    RenderRuntimeHost& operator=(RenderRuntimeHost&&) = delete;

    [[nodiscard]] BindlessResourceSystem& BindlessResources() noexcept {
        return bindless_resource_system;
    }

    [[nodiscard]] const BindlessResourceSystem& BindlessResources() const noexcept {
        return bindless_resource_system;
    }

    void Initialize(const CreateInfo& create_info_ = {}) {
        Shutdown();

        create_info_cache = create_info_;
        EnableRecommendedBindlessOptionalFeatures(create_info_cache.platform.device);
        runtime_frame_id = 0U;
        last_tick_frame_index = 0U;
        last_tick_image_index = 0U;
        bool platform_initialized = false;

        try {
            platform_host.Initialize(create_info_cache.platform);
            platform_initialized = true;
            RequireBindlessEngineContract(platform_host.Context(),
                                          create_info_cache.modules,
                                          create_info_cache.bindless);

            gpu_memory_host.Initialize(platform_host.Context(), create_info_cache.gpu_memory);
            gpu_memory_initialized = true;

            if (create_info_cache.modules.enable_texture_host) {
                texture_host.Initialize(platform_host.Context(),
                                        gpu_memory_host,
                                        create_info_cache.texture);
                texture_initialized = true;
            }

            if (create_info_cache.modules.enable_sampler_host) {
                sampler_host.Initialize(platform_host.Context(), create_info_cache.sampler);
                sampler_initialized = true;
            }

            if (create_info_cache.modules.enable_freetype_host) {
                freetype_host.Initialize(create_info_cache.freetype);
                freetype_initialized = true;
            }

            if (create_info_cache.modules.enable_glyph_atlas_host) {
                if (!freetype_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires FreeTypeHost when glyph_atlas_host is enabled");
                }
                glyph_atlas_host.Initialize(freetype_host, create_info_cache.glyph_atlas);
                glyph_atlas_initialized = true;
            }

            if (create_info_cache.modules.enable_descriptor_host) {
                DescriptorHostCreateInfo descriptor_info = create_info_cache.descriptor;
                descriptor_info.frames_in_flight = frames_in_flight_v;
                descriptor_host.Initialize(platform_host.Context(), descriptor_info);
                descriptor_initialized = true;
            }

            if (descriptor_initialized &&
                sampler_initialized &&
                platform_host.Context().DescriptorIndexingCapsInfo().enabled) {
                bindless_resource_system.Initialize(platform_host.Context(),
                                                   gpu_memory_host,
                                                   descriptor_host,
                                                   sampler_host,
                                                   create_info_cache.bindless);
                bindless_resources_initialized = true;
                if (texture_initialized) {
                    bindless_resource_system.ConfigureTextureHost(texture_host);
                }
                if (render_target_initialized) {
                    bindless_resource_system.ConfigureRenderTargetHost(render_target_host);
                }
            }

            if (create_info_cache.modules.enable_ibl_host) {
                if (!texture_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires TextureHost when ibl_host is enabled");
                }
                if (!sampler_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires SamplerHost when ibl_host is enabled");
                }
                if (!descriptor_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires DescriptorHost when ibl_host is enabled");
                }
                IblHostCreateInfo ibl_info = create_info_cache.ibl;
                ibl_info.frames_in_flight = frames_in_flight_v;
                ibl_host.Initialize(platform_host.Context(),
                                    texture_host,
                                    descriptor_host,
                                    sampler_host,
                                    ibl_info);
                ibl_initialized = true;
            }

            if (create_info_cache.modules.enable_ibl_bake_host) {
                if (!texture_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires TextureHost when ibl_bake_host is enabled");
                }
                ibl_bake_host.Initialize(texture_host,
                                         ibl_initialized ? &ibl_host : nullptr,
                                         create_info_cache.ibl_bake);
                ibl_bake_initialized = true;
            }

            if (create_info_cache.modules.enable_sky_environment_gpu_host) {
                if (!texture_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires TextureHost when sky_environment_gpu_host is enabled");
                }
                if (!descriptor_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires DescriptorHost when sky_environment_gpu_host is enabled");
                }
                if (!sampler_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires SamplerHost when sky_environment_gpu_host is enabled");
                }
            }

            if (create_info_cache.modules.enable_pipeline_host) {
                pipeline_host.Initialize(platform_host.Context(), create_info_cache.pipeline);
                pipeline_initialized = true;
            }

            if (create_info_cache.modules.enable_frame_composer_host) {
                if (!render_target_initialized && !create_info_cache.modules.enable_render_target_host) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires RenderTargetHost when frame_composer_host is enabled");
                }
                if (!descriptor_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires DescriptorHost when frame_composer_host is enabled");
                }
                if (!pipeline_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires PipelineHost when frame_composer_host is enabled");
                }
                if (!sampler_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires SamplerHost when frame_composer_host is enabled");
                }
                frame_composer_host.Initialize(create_info_cache.frame_composer);
                frame_composer_initialized = true;
            }

            if (create_info_cache.modules.enable_render_target_host) {
                render_target_host.Initialize(platform_host.Context(),
                                              gpu_memory_host,
                                              create_info_cache.render_target);
                render_target_initialized = true;
                if (bindless_resources_initialized) {
                    bindless_resource_system.ConfigureRenderTargetHost(render_target_host);
                }
            }

            if (create_info_cache.modules.enable_upload_host) {
                UploadHostCreateInfo upload_info = create_info_cache.upload;
                upload_info.frames_in_flight = frames_in_flight_v;
                upload_host.Initialize(platform_host.Context(), gpu_memory_host, upload_info);
                upload_initialized = true;
                InitializeUploadSyncObjects(platform_host.Context());
            }

            if (create_info_cache.modules.enable_sky_environment_gpu_host) {
                if (!upload_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires UploadHost when sky_environment_gpu_host is enabled");
                }
                SkyEnvironmentGpuHostCreateInfo sky_environment_info = create_info_cache.sky_environment;
                sky_environment_info.frames_in_flight = frames_in_flight_v;
                sky_environment_gpu_host.Initialize(platform_host.Context(),
                                                    texture_host,
                                                    descriptor_host,
                                                    sampler_host,
                                                    sky_environment_info);
                sky_environment_initialized = true;
            }

            if (create_info_cache.modules.enable_glyph_upload_host) {
                if (!glyph_atlas_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires GlyphAtlasHost when glyph_upload_host is enabled");
                }
                if (!sampler_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires SamplerHost when glyph_upload_host is enabled");
                }
                if (!upload_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires UploadHost when glyph_upload_host is enabled");
                }
                glyph_upload_host.Initialize(platform_host.Context(),
                                             gpu_memory_host,
                                             sampler_host,
                                             create_info_cache.glyph_upload);
                glyph_upload_initialized = true;
                if (bindless_resources_initialized) {
                    bindless_resource_system.ConfigureGlyphUploadHost(glyph_upload_host);
                }
            }

            if (create_info_cache.modules.enable_particle_upload_host) {
                particle::ParticleUploadHostCreateInfo particle_upload_info = create_info_cache.particle_upload;
                particle_upload_info.frames_in_flight = frames_in_flight_v;
                particle_upload_host.Initialize(platform_host.Context(),
                                               gpu_memory_host,
                                               particle_upload_info);
                particle_upload_initialized = true;
            }

            if (create_info_cache.modules.enable_particle_simulation_host) {
                particle::ParticleSimulationHostCreateInfo particle_simulation_info =
                    create_info_cache.particle_simulation;
                particle_simulation_info.frames_in_flight = frames_in_flight_v;
                particle_simulation_host.Initialize(platform_host.Context(),
                                                   gpu_memory_host,
                                                   particle_simulation_info);
                particle_simulation_initialized = true;
            }

            render_loop.Initialize(platform_host.Context(),
                                   platform_host.SurfaceHost(),
                                   swapchain,
                                   create_info_cache.render_loop);
            loop_initialized = true;
            RefreshServiceBindings();
            initialized = true;
        } catch (...) {
            if (loop_initialized) {
                render_loop.Shutdown(platform_host.Context(), swapchain);
                loop_initialized = false;
            }

            const VkDevice device = platform_initialized
                ? platform_host.Context().Device()
                : VK_NULL_HANDLE;
            DestroyUploadSyncObjects(device);

            if (particle_simulation_initialized) {
                particle_simulation_host.Shutdown(platform_host.Context());
                particle_simulation_initialized = false;
            }
            if (sky_environment_initialized) {
                sky_environment_gpu_host.Shutdown(platform_host.Context());
                sky_environment_initialized = false;
            }
            if (particle_upload_initialized) {
                particle_upload_host.Shutdown(platform_host.Context());
                particle_upload_initialized = false;
            }

            if (glyph_upload_initialized) {
                glyph_upload_host.Shutdown(platform_host.Context());
                glyph_upload_initialized = false;
            }
            if (frame_composer_initialized) {
                frame_composer_host.Shutdown(platform_host.Context());
                frame_composer_initialized = false;
            }
            if (ibl_initialized) {
                ibl_host.Shutdown(platform_host.Context());
                ibl_initialized = false;
            }
            if (ibl_bake_initialized) {
                ibl_bake_host.Shutdown(platform_host.Context());
                ibl_bake_initialized = false;
            }
            if (bindless_resources_initialized) {
                bindless_resource_system.Shutdown(platform_host.Context());
                bindless_resources_initialized = false;
            }
            if (texture_initialized) {
                texture_host.Shutdown(platform_host.Context());
                texture_initialized = false;
            }
            if (render_target_initialized) {
                InvalidateSwapchainTargets(0U, 0U);
                render_target_host.Shutdown(platform_host.Context());
                render_target_initialized = false;
            }
            if (upload_initialized) {
                upload_host.Shutdown(platform_host.Context());
                upload_initialized = false;
            }
            if (glyph_atlas_initialized) {
                glyph_atlas_host.Shutdown();
                glyph_atlas_initialized = false;
            }
            if (pipeline_initialized) {
                pipeline_host.Shutdown(platform_host.Context());
                pipeline_initialized = false;
            }
            if (descriptor_initialized) {
                descriptor_host.Shutdown(platform_host.Context());
                descriptor_initialized = false;
            }
            if (sampler_initialized) {
                sampler_host.Shutdown(platform_host.Context());
                sampler_initialized = false;
            }
            if (freetype_initialized) {
                freetype_host.Shutdown();
                freetype_initialized = false;
            }
            if (gpu_memory_initialized) {
                gpu_memory_host.Shutdown();
                gpu_memory_initialized = false;
            }
            if (platform_initialized) {
                platform_host.Shutdown();
            }

            upload_wait_required = false;
            RefreshServiceBindings();
            initialized = false;
            throw;
        }
    }

    void Shutdown() {
        const bool context_alive = platform_host.Context().IsInstanceInitialized();
        VulkanContext* context = context_alive ? &platform_host.Context() : nullptr;
        VkDevice device = (context != nullptr) ? context->Device() : VK_NULL_HANDLE;

        if (loop_initialized) {
            render_loop.Shutdown(platform_host.Context(), swapchain);
            loop_initialized = false;
        }
        render_graph_runtime_service_ref.Shutdown(platform_host.Context());

        DestroyUploadSyncObjects(device);

        if (glyph_upload_initialized) {
            glyph_upload_host.Shutdown(platform_host.Context());
            glyph_upload_initialized = false;
        }

        if (particle_simulation_initialized) {
            particle_simulation_host.Shutdown(platform_host.Context());
            particle_simulation_initialized = false;
        }

        if (sky_environment_initialized) {
            sky_environment_gpu_host.Shutdown(platform_host.Context());
            sky_environment_initialized = false;
        }

        if (particle_upload_initialized) {
            particle_upload_host.Shutdown(platform_host.Context());
            particle_upload_initialized = false;
        }

        if (frame_composer_initialized) {
            frame_composer_host.Shutdown(platform_host.Context());
            frame_composer_initialized = false;
        }

        if (ibl_initialized) {
            ibl_host.Shutdown(platform_host.Context());
            ibl_initialized = false;
        }

        if (ibl_bake_initialized) {
            ibl_bake_host.Shutdown(platform_host.Context());
            ibl_bake_initialized = false;
        }

        if (bindless_resources_initialized) {
            bindless_resource_system.Shutdown(platform_host.Context());
            bindless_resources_initialized = false;
        }

        if (texture_initialized) {
            texture_host.Shutdown(platform_host.Context());
            texture_initialized = false;
        }

        if (render_target_initialized) {
            const std::uint64_t last_submitted_value =
                loop_initialized ? render_loop.Sync().LastSubmittedValue() : 0U;
            const std::uint64_t completed_submit_value =
                loop_initialized ? render_loop.Sync().CompletedSubmitValue() : 0U;
            InvalidateSwapchainTargets(last_submitted_value,
                                       completed_submit_value);
            render_target_host.Shutdown(platform_host.Context());
            render_target_initialized = false;
        }

        if (upload_initialized) {
            upload_host.Shutdown(platform_host.Context());
            upload_initialized = false;
        }

        if (glyph_atlas_initialized) {
            glyph_atlas_host.Shutdown();
            glyph_atlas_initialized = false;
        }

        if (pipeline_initialized) {
            pipeline_host.Shutdown(platform_host.Context());
            pipeline_initialized = false;
        }

        if (descriptor_initialized) {
            descriptor_host.Shutdown(platform_host.Context());
            descriptor_initialized = false;
        }

        if (sampler_initialized) {
            sampler_host.Shutdown(platform_host.Context());
            sampler_initialized = false;
        }

        if (freetype_initialized) {
            freetype_host.Shutdown();
            freetype_initialized = false;
        }

        if (gpu_memory_initialized) {
            gpu_memory_host.Shutdown();
            gpu_memory_initialized = false;
        }

        platform_host.Shutdown();

        upload_wait_required = false;
        runtime_frame_id = 0U;
        last_tick_frame_index = 0U;
        last_tick_image_index = 0U;
        create_info_cache = {};
        RefreshServiceBindings();
        initialized = false;
    }

    [[nodiscard]] bool PollEvents() {
        return platform_host.PollAndHandleCloseEvents();
    }

    [[nodiscard]] std::uint64_t AdvanceFrameId() noexcept {
        return ++runtime_frame_id;
    }

    [[nodiscard]] TickBeginFrameResult BeginTickFrame() {
        if (!initialized) {
            throw std::runtime_error("RenderRuntimeHost::BeginTickFrame called before Initialize");
        }

        TickBeginFrameResult begin{};
        begin.frame_id = ++runtime_frame_id;
        begin.result.frame_id = begin.frame_id;

        if (create_info_cache.poll_events_each_tick) {
            begin.result.events_polled = platform_host.PollAndHandleCloseEvents();
        }

        begin.result.running = platform_host.IsRunning();
        if (!begin.result.running) {
            begin.result.render = {
                .code = TickCode::SkippedWindowHidden,
                .frame_index = render_loop.Sync().CurrentFrameIndex(),
                .image_index = 0U
            };
            last_tick_frame_index = begin.result.render.frame_index;
            last_tick_image_index = begin.result.render.image_index;
            FillPipelineQueueStats(begin.result);
            FillFrameDiagnostics(begin.result, begin.frame_id);
            return begin;
        }

        render_loop.Sync().PrepareCurrentFrame(platform_host.Context());
        begin.frame_index = render_loop.Sync().CurrentFrameIndex();
        begin.ready = true;
        return begin;
    }

