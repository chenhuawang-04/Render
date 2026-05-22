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

    [[nodiscard]] typename InternalRuntimeHost::SwapchainType& Swapchain() noexcept {
        return host.Swapchain();
    }

    [[nodiscard]] const typename InternalRuntimeHost::SwapchainType& Swapchain() const noexcept {
        return host.Swapchain();
    }

    [[nodiscard]] VulkanContext& Context() noexcept {
        return host.Context();
    }

    [[nodiscard]] const VulkanContext& Context() const noexcept {
        return host.Context();
    }

    [[nodiscard]] resource::GpuMemoryHost& GpuMemory() {
        return host.GpuMemory();
    }

    [[nodiscard]] const resource::GpuMemoryHost& GpuMemory() const noexcept {
        return host.GpuMemory();
    }

    [[nodiscard]] vr::render::BindlessResourceSystem& BindlessResources() noexcept {
        return host.BindlessResources();
    }

    [[nodiscard]] const vr::render::BindlessResourceSystem& BindlessResources() const noexcept {
        return host.BindlessResources();
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

    [[nodiscard]] resource::BufferResource CreateBuffer(
        const resource::BufferCreateInfo& create_info_) {
        return host.CreateBuffer(create_info_);
    }

    [[nodiscard]] resource::ImageResource CreateImage(
        const resource::ImageCreateInfo& create_info_) {
        return host.CreateImage(create_info_);
    }

    void DestroyBuffer(resource::BufferResource& resource_) noexcept {
        host.DestroyBuffer(resource_);
    }

    void DestroyImage(resource::ImageResource& resource_) noexcept {
        host.DestroyImage(resource_);
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
