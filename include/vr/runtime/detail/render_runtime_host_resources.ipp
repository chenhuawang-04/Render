    [[nodiscard]] bool IsInitialized() const noexcept {
        return initialized;
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return platform_host.IsRunning();
    }

    [[nodiscard]] bool HasTextureHost() const noexcept {
        return texture_initialized;
    }

    [[nodiscard]] bool HasFrameComposerHost() const noexcept {
        return frame_composer_initialized;
    }

    [[nodiscard]] bool HasIblHost() const noexcept {
        return ibl_initialized;
    }

    [[nodiscard]] bool HasIblBakeHost() const noexcept {
        return ibl_bake_initialized;
    }

    [[nodiscard]] bool HasSkyEnvironmentHost() const noexcept {
        return sky_environment_initialized;
    }

    [[nodiscard]] bool HasUploadHost() const noexcept {
        return upload_initialized;
    }

    [[nodiscard]] bool HasDescriptorHost() const noexcept {
        return descriptor_initialized;
    }

    [[nodiscard]] bool HasPipelineHost() const noexcept {
        return pipeline_initialized;
    }

    [[nodiscard]] bool HasRenderTargetHost() const noexcept {
        return render_target_initialized;
    }

    [[nodiscard]] bool HasSamplerHost() const noexcept {
        return sampler_initialized;
    }

    [[nodiscard]] bool HasFreeTypeHost() const noexcept {
        return freetype_initialized;
    }

    [[nodiscard]] bool HasGlyphAtlasHost() const noexcept {
        return glyph_atlas_initialized;
    }

    [[nodiscard]] bool HasGlyphUploadHost() const noexcept {
        return glyph_upload_initialized;
    }

    [[nodiscard]] bool HasParticleUploadHost() const noexcept {
        return particle_upload_initialized;
    }

    [[nodiscard]] bool HasParticleSimulationHost() const noexcept {
        return particle_simulation_initialized;
    }

    void RequestClose() noexcept {
        platform_host.RequestClose();
    }

    [[nodiscard]] const CreateInfo& Config() const noexcept {
        return create_info_cache;
    }

    [[nodiscard]] std::uint64_t CurrentFrameId() const noexcept {
        return runtime_frame_id;
    }

    [[nodiscard]] std::uint32_t LastTickFrameIndex() const noexcept {
        return last_tick_frame_index;
    }

    [[nodiscard]] std::uint32_t LastTickImageIndex() const noexcept {
        return last_tick_image_index;
    }

    void UpdateLastTickFrame(std::uint32_t frame_index_,
                             std::uint32_t image_index_) noexcept {
        last_tick_frame_index = frame_index_;
        last_tick_image_index = image_index_;
    }

    [[nodiscard]] SwapchainTargetSet& SwapchainTargets() noexcept {
        return swapchain_targets;
    }

    [[nodiscard]] const SwapchainTargetSet& SwapchainTargets() const noexcept {
        return swapchain_targets;
    }

    void EnsureSwapchainTargetsForFrame(std::uint64_t last_submitted_value_,
                                        std::uint64_t completed_submit_value_) {
        EnsureSwapchainTargets(last_submitted_value_, completed_submit_value_);
    }

    [[nodiscard]] PlatformHostType& PlatformHost() noexcept {
        return platform_host;
    }

    [[nodiscard]] const PlatformHostType& PlatformHost() const noexcept {
        return platform_host;
    }

    [[nodiscard]] VulkanContext& Context() noexcept {
        return platform_host.Context();
    }

    [[nodiscard]] const VulkanContext& Context() const noexcept {
        return platform_host.Context();
    }

    [[nodiscard]] WindowSurfaceType& SurfaceHost() noexcept {
        return platform_host.SurfaceHost();
    }

    [[nodiscard]] const WindowSurfaceType& SurfaceHost() const noexcept {
        return platform_host.SurfaceHost();
    }

    [[nodiscard]] SwapchainType& Swapchain() noexcept {
        return swapchain;
    }

    [[nodiscard]] const SwapchainType& Swapchain() const noexcept {
        return swapchain;
    }

    [[nodiscard]] LoopType& Loop() noexcept {
        return render_loop;
    }

    [[nodiscard]] const LoopType& Loop() const noexcept {
        return render_loop;
    }

    [[nodiscard]] resource::GpuMemoryHost& GpuMemory() {
        if (!gpu_memory_initialized) {
            throw std::runtime_error("RenderRuntimeHost::GpuMemory requested but host is not initialized");
        }
        return gpu_memory_host;
    }

    [[nodiscard]] const resource::GpuMemoryHost& GpuMemory() const noexcept {
        return gpu_memory_host;
    }

    [[nodiscard]] asset::TextureHost& Texture() {
        if (!texture_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Texture requested but host is not initialized");
        }
        return texture_host;
    }

    [[nodiscard]] const asset::TextureHost& Texture() const {
        if (!texture_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Texture requested but host is not initialized");
        }
        return texture_host;
    }

    [[nodiscard]] FrameComposerHost& FrameComposer() {
        if (!frame_composer_initialized) {
            throw std::runtime_error("RenderRuntimeHost::FrameComposer requested but host is not initialized");
        }
        return frame_composer_host;
    }

    [[nodiscard]] const FrameComposerHost& FrameComposer() const {
        if (!frame_composer_initialized) {
            throw std::runtime_error("RenderRuntimeHost::FrameComposer requested but host is not initialized");
        }
        return frame_composer_host;
    }

    [[nodiscard]] IblHost& Ibl() {
        if (!ibl_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Ibl requested but host is not initialized");
        }
        return ibl_host;
    }

    [[nodiscard]] const IblHost& Ibl() const {
        if (!ibl_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Ibl requested but host is not initialized");
        }
        return ibl_host;
    }

    [[nodiscard]] IblBakeHost& IblBake() {
        if (!ibl_bake_initialized) {
            throw std::runtime_error("RenderRuntimeHost::IblBake requested but host is not initialized");
        }
        return ibl_bake_host;
    }

    [[nodiscard]] const IblBakeHost& IblBake() const {
        if (!ibl_bake_initialized) {
            throw std::runtime_error("RenderRuntimeHost::IblBake requested but host is not initialized");
        }
        return ibl_bake_host;
    }

    [[nodiscard]] SkyEnvironmentGpuHost& SkyEnvironment() {
        if (!sky_environment_initialized) {
            throw std::runtime_error(
                "RenderRuntimeHost::SkyEnvironment requested but host is not initialized");
        }
        return sky_environment_gpu_host;
    }

    [[nodiscard]] const SkyEnvironmentGpuHost& SkyEnvironment() const {
        if (!sky_environment_initialized) {
            throw std::runtime_error(
                "RenderRuntimeHost::SkyEnvironment requested but host is not initialized");
        }
        return sky_environment_gpu_host;
    }

    [[nodiscard]] UploadHost& Upload() {
        if (!upload_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Upload requested but host is not initialized");
        }
        return upload_host;
    }

    [[nodiscard]] DescriptorHost& Descriptor() {
        if (!descriptor_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Descriptor requested but host is not initialized");
        }
        return descriptor_host;
    }

    [[nodiscard]] PipelineHost& Pipeline() {
        if (!pipeline_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Pipeline requested but host is not initialized");
        }
        return pipeline_host;
    }

    [[nodiscard]] RenderTargetHost& RenderTarget() {
        if (!render_target_initialized) {
            throw std::runtime_error("RenderRuntimeHost::RenderTarget requested but host is not initialized");
        }
        return render_target_host;
    }

    [[nodiscard]] resource::SamplerHost& Sampler() {
        if (!sampler_initialized) {
            throw std::runtime_error("RenderRuntimeHost::Sampler requested but host is not initialized");
        }
        return sampler_host;
    }

    [[nodiscard]] text::FreeTypeHost& FreeType() {
        if (!freetype_initialized) {
            throw std::runtime_error("RenderRuntimeHost::FreeType requested but host is not initialized");
        }
        return freetype_host;
    }

    [[nodiscard]] text::GlyphAtlasHost& GlyphAtlas() {
        if (!glyph_atlas_initialized) {
            throw std::runtime_error("RenderRuntimeHost::GlyphAtlas requested but host is not initialized");
        }
        return glyph_atlas_host;
    }

    [[nodiscard]] text::GlyphUploadHost& GlyphUpload() {
        if (!glyph_upload_initialized) {
            throw std::runtime_error("RenderRuntimeHost::GlyphUpload requested but host is not initialized");
        }
        return glyph_upload_host;
    }

    [[nodiscard]] particle::ParticleUploadHost& ParticleUpload() {
        if (!particle_upload_initialized) {
            throw std::runtime_error("RenderRuntimeHost::ParticleUpload requested but host is not initialized");
        }
        return particle_upload_host;
    }

    [[nodiscard]] const particle::ParticleUploadHost& ParticleUpload() const {
        if (!particle_upload_initialized) {
            throw std::runtime_error("RenderRuntimeHost::ParticleUpload requested but host is not initialized");
        }
        return particle_upload_host;
    }

    [[nodiscard]] particle::ParticleSimulationHost& ParticleSimulation() {
        if (!particle_simulation_initialized) {
            throw std::runtime_error("RenderRuntimeHost::ParticleSimulation requested but host is not initialized");
        }
        return particle_simulation_host;
    }

    [[nodiscard]] const particle::ParticleSimulationHost& ParticleSimulation() const {
        if (!particle_simulation_initialized) {
            throw std::runtime_error("RenderRuntimeHost::ParticleSimulation requested but host is not initialized");
        }
        return particle_simulation_host;
    }

    [[nodiscard]] runtime::services::ParticleUploadService& ParticleUploadService() noexcept {
        return particle_upload_service_ref;
    }

    [[nodiscard]] const runtime::services::ParticleUploadService& ParticleUploadService() const noexcept {
        return particle_upload_service_ref;
    }

    [[nodiscard]] runtime::services::ParticleSimulationService& ParticleSimulationService() noexcept {
        return particle_simulation_service_ref;
    }

    [[nodiscard]] const runtime::services::ParticleSimulationService& ParticleSimulationService() const noexcept {
        return particle_simulation_service_ref;
    }

    [[nodiscard]] runtime::services::SkyEnvironmentService& SkyEnvironmentService() noexcept {
        return sky_environment_service_ref;
    }

    [[nodiscard]] const runtime::services::SkyEnvironmentService& SkyEnvironmentService() const noexcept {
        return sky_environment_service_ref;
    }

    [[nodiscard]] runtime::services::ParticleRenderService& Particles() noexcept {
        return particle_render_service_ref;
    }

    [[nodiscard]] const runtime::services::ParticleRenderService& Particles() const noexcept {
        return particle_render_service_ref;
    }

    [[nodiscard]] RuntimeServicesType& Services() noexcept {
        return services_ref;
    }

    [[nodiscard]] const RuntimeServicesType& Services() const noexcept {
        return services_ref;
    }

    [[nodiscard]] resource::BufferResource CreateBuffer(const resource::BufferCreateInfo& create_info_) {
        if (!initialized || !gpu_memory_initialized) {
            throw std::runtime_error("RenderRuntimeHost::CreateBuffer requires initialized runtime and GpuMemoryHost");
        }
        return resource::BufferHost::CreateBuffer(platform_host.Context(), create_info_, gpu_memory_host);
    }

    void DestroyBuffer(resource::BufferResource& resource_) {
        if (!initialized) {
            throw std::runtime_error("RenderRuntimeHost::DestroyBuffer called before Initialize");
        }
        resource::BufferHost::DestroyBuffer(platform_host.Context(), resource_);
    }

    [[nodiscard]] resource::ImageResource CreateImage(const resource::ImageCreateInfo& create_info_) {
        if (!initialized || !gpu_memory_initialized) {
            throw std::runtime_error("RenderRuntimeHost::CreateImage requires initialized runtime and GpuMemoryHost");
        }
        return resource::ImageHost::CreateImage(platform_host.Context(), create_info_, gpu_memory_host);
    }

    void DestroyImage(resource::ImageResource& resource_) {
        if (!initialized) {
            throw std::runtime_error("RenderRuntimeHost::DestroyImage called before Initialize");
        }
        resource::ImageHost::DestroyImage(platform_host.Context(), resource_);
    }
