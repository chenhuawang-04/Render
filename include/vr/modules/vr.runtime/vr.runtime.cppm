module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

export module vr.runtime;
import vr.types;
import vr.context;
import vr.platform;
import vr.resource;
import vr.render;
import vr.text;

export {
namespace vr::render {

// --- render_runtime_host.hpp --------------------------------------------------

struct RuntimeModulesCreateInfo {
    bool enable_upload_host = true;
    bool enable_descriptor_host = true;
    bool enable_pipeline_host = true;
    bool enable_sampler_host = true;
    bool enable_freetype_host = true;
    bool enable_glyph_atlas_host = true;
    bool enable_glyph_upload_host = true;
};

template<typename RecorderT>
concept RuntimeFramePreparer = requires(RecorderT& recorder_,
                                        const RuntimePrepareContext& prepare_context_) {
    { recorder_.PrepareFrame(prepare_context_) };
};

template<typename BackendTagT = platform::ActiveBackendTag,
         uint32_t frames_in_flight_v = 2U>
class RenderRuntimeHost final {
public:
    static_assert(frames_in_flight_v > 0U, "frames_in_flight_v must be >= 1");

    using BackendTag = BackendTagT;
    using PlatformHostType = platform::RenderHost<BackendTag>;
    using WindowSurfaceType = typename PlatformHostType::WindowSurfaceType;
    using SwapchainType = SwapchainHost<WindowSurfaceType>;
    using LoopType = RenderLoopHost<WindowSurfaceType, SwapchainType, frames_in_flight_v>;

    struct PipelineWarmupCreateInfo {
        uint32_t max_graphics_compiles_per_tick = 0U;
        uint32_t max_compute_compiles_per_tick = 0U;
        bool compile_before_render = true;
        bool compile_after_render = false;
    };

    struct CreateInfo {
        platform::RenderHostCreateInfo platform{};
        RenderLoopCreateInfo render_loop{};

        resource::GpuMemoryHostCreateInfo gpu_memory{};
        UploadHostCreateInfo upload{};
        DescriptorHostCreateInfo descriptor{};
        PipelineHostCreateInfo pipeline{};
        resource::SamplerHostCreateInfo sampler{};
        text::FreeTypeHostCreateInfo freetype{};
        text::GlyphAtlasCreateInfo glyph_atlas{};
        text::GlyphUploadHostCreateInfo glyph_upload{};

        RuntimeModulesCreateInfo modules{};
        PipelineWarmupCreateInfo pipeline_warmup{};

        bool poll_events_each_tick = true;
        VkPipelineStageFlags upload_wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    };

    struct RuntimeTickResult {
        TickResult render{};
        uint32_t compiled_pipeline_count = 0U;
        uint32_t pending_graphics_compile_count = 0U;
        uint32_t pending_compute_compile_count = 0U;
        bool upload_submitted = false;
        bool upload_cross_queue_wait = false;
        bool events_polled = false;
        bool running = true;
    };

    RenderRuntimeHost() = default;
    ~RenderRuntimeHost() { Shutdown(); }

    RenderRuntimeHost(const RenderRuntimeHost&) = delete;
    RenderRuntimeHost& operator=(const RenderRuntimeHost&) = delete;
    RenderRuntimeHost(RenderRuntimeHost&&) = delete;
    RenderRuntimeHost& operator=(RenderRuntimeHost&&) = delete;

    void Initialize(const CreateInfo& create_info_ = {});
    void Shutdown();

    [[nodiscard]] bool PollEvents();

    template<typename RecorderT>
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
    [[nodiscard]] RuntimeTickResult Tick(RecorderT& recorder_);

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized; }
    [[nodiscard]] bool IsRunning() const noexcept { return platform_host.IsRunning(); }
    [[nodiscard]] bool HasUploadHost() const noexcept { return upload_initialized; }
    [[nodiscard]] bool HasDescriptorHost() const noexcept { return descriptor_initialized; }
    [[nodiscard]] bool HasPipelineHost() const noexcept { return pipeline_initialized; }
    [[nodiscard]] bool HasSamplerHost() const noexcept { return sampler_initialized; }
    [[nodiscard]] bool HasFreeTypeHost() const noexcept { return freetype_initialized; }
    [[nodiscard]] bool HasGlyphAtlasHost() const noexcept { return glyph_atlas_initialized; }
    [[nodiscard]] bool HasGlyphUploadHost() const noexcept { return glyph_upload_initialized; }

    void RequestClose() noexcept { platform_host.RequestClose(); }

    [[nodiscard]] const CreateInfo& Config() const noexcept { return create_info_cache; }
    [[nodiscard]] PlatformHostType& PlatformHost() noexcept { return platform_host; }
    [[nodiscard]] const PlatformHostType& PlatformHost() const noexcept { return platform_host; }
    [[nodiscard]] VulkanContext& Context() noexcept { return platform_host.Context(); }
    [[nodiscard]] const VulkanContext& Context() const noexcept { return platform_host.Context(); }
    [[nodiscard]] WindowSurfaceType& SurfaceHost() noexcept { return platform_host.SurfaceHost(); }
    [[nodiscard]] const WindowSurfaceType& SurfaceHost() const noexcept { return platform_host.SurfaceHost(); }
    [[nodiscard]] SwapchainType& Swapchain() noexcept { return swapchain; }
    [[nodiscard]] const SwapchainType& Swapchain() const noexcept { return swapchain; }
    [[nodiscard]] LoopType& Loop() noexcept { return render_loop; }
    [[nodiscard]] const LoopType& Loop() const noexcept { return render_loop; }

    [[nodiscard]] resource::GpuMemoryHost& GpuMemory();
    [[nodiscard]] const resource::GpuMemoryHost& GpuMemory() const noexcept { return gpu_memory_host; }
    [[nodiscard]] UploadHost& Upload();
    [[nodiscard]] DescriptorHost& Descriptor();
    [[nodiscard]] PipelineHost& Pipeline();
    [[nodiscard]] resource::SamplerHost& Sampler();
    [[nodiscard]] text::FreeTypeHost& FreeType();
    [[nodiscard]] text::GlyphAtlasHost& GlyphAtlas();
    [[nodiscard]] text::GlyphUploadHost& GlyphUpload();

    [[nodiscard]] resource::BufferResource CreateBuffer(const resource::BufferCreateInfo& create_info_);
    void DestroyBuffer(resource::BufferResource& resource_);
    [[nodiscard]] resource::ImageResource CreateImage(const resource::ImageCreateInfo& create_info_);
    void DestroyImage(resource::ImageResource& resource_);

private:
    static void ThrowVk(const char* stage_, VkResult result_);
    static void CheckVk(const char* stage_, VkResult result_);
    void InitializeUploadSyncObjects(VulkanContext& context_);
    void DestroyUploadSyncObjects(VkDevice device_) noexcept;
    [[nodiscard]] VkSemaphore UploadCompleteSemaphore(uint32_t frame_index_) const;
    void FillPipelineQueueStats(RuntimeTickResult& result_) const noexcept;

private:
    PlatformHostType platform_host{};
    SwapchainType swapchain{};
    LoopType render_loop{};

    resource::GpuMemoryHost gpu_memory_host{};
    UploadHost upload_host{};
    DescriptorHost descriptor_host{};
    PipelineHost pipeline_host{};
    resource::SamplerHost sampler_host{};
    text::FreeTypeHost freetype_host{};
    text::GlyphAtlasHost glyph_atlas_host{};
    text::GlyphUploadHost glyph_upload_host{};

    vr::McVector<VkSemaphore> upload_complete_semaphores{};

    CreateInfo create_info_cache{};
    bool gpu_memory_initialized = false;
    bool upload_initialized = false;
    bool descriptor_initialized = false;
    bool pipeline_initialized = false;
    bool sampler_initialized = false;
    bool freetype_initialized = false;
    bool glyph_atlas_initialized = false;
    bool glyph_upload_initialized = false;
    bool loop_initialized = false;
    bool upload_wait_required = false;
    bool initialized = false;
};

} // namespace vr::render
} // export
