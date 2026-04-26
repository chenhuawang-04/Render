#pragma once

#include "vr/platform/render_host.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_context.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"
#include "vr/text/glyph_upload_host.hpp"

#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vr::render {

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
    ~RenderRuntimeHost() {
        Shutdown();
    }

    RenderRuntimeHost(const RenderRuntimeHost&) = delete;
    RenderRuntimeHost& operator=(const RenderRuntimeHost&) = delete;

    RenderRuntimeHost(RenderRuntimeHost&&) = delete;
    RenderRuntimeHost& operator=(RenderRuntimeHost&&) = delete;

    void Initialize(const CreateInfo& create_info_ = {}) {
        Shutdown();

        create_info_cache = create_info_;
        bool platform_initialized = false;

        try {
            platform_host.Initialize(create_info_cache.platform);
            platform_initialized = true;

            gpu_memory_host.Initialize(platform_host.Context(), create_info_cache.gpu_memory);
            gpu_memory_initialized = true;

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

            if (create_info_cache.modules.enable_pipeline_host) {
                pipeline_host.Initialize(platform_host.Context(), create_info_cache.pipeline);
                pipeline_initialized = true;
            }

            if (create_info_cache.modules.enable_upload_host) {
                UploadHostCreateInfo upload_info = create_info_cache.upload;
                upload_info.frames_in_flight = frames_in_flight_v;
                upload_host.Initialize(platform_host.Context(), gpu_memory_host, upload_info);
                upload_initialized = true;
                InitializeUploadSyncObjects(platform_host.Context());
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
            }

            render_loop.Initialize(platform_host.Context(),
                                   platform_host.SurfaceHost(),
                                   swapchain,
                                   create_info_cache.render_loop);
            loop_initialized = true;
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

            if (glyph_upload_initialized) {
                glyph_upload_host.Shutdown(platform_host.Context());
                glyph_upload_initialized = false;
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

        DestroyUploadSyncObjects(device);

        if (glyph_upload_initialized) {
            glyph_upload_host.Shutdown(platform_host.Context());
            glyph_upload_initialized = false;
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
        create_info_cache = {};
        initialized = false;
    }

    [[nodiscard]] bool PollEvents() {
        return platform_host.PollAndHandleCloseEvents();
    }

    template<typename RecorderT>
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
    [[nodiscard]] RuntimeTickResult Tick(RecorderT& recorder_) {
        if (!initialized) {
            throw std::runtime_error("RenderRuntimeHost::Tick called before Initialize");
        }

        RuntimeTickResult result{};
        if (create_info_cache.poll_events_each_tick) {
            result.events_polled = platform_host.PollAndHandleCloseEvents();
        }

        result.running = platform_host.IsRunning();
        if (!result.running) {
            result.render = {
                .code = TickCode::SkippedWindowHidden,
                .frame_index = render_loop.Sync().CurrentFrameIndex(),
                .image_index = 0U
            };
            FillPipelineQueueStats(result);
            return result;
        }

        if (pipeline_initialized && create_info_cache.pipeline_warmup.compile_before_render) {
            result.compiled_pipeline_count += pipeline_host.ProcessPendingCompiles(
                platform_host.Context(),
                create_info_cache.pipeline_warmup.max_graphics_compiles_per_tick,
                create_info_cache.pipeline_warmup.max_compute_compiles_per_tick);
        }

        render_loop.Sync().PrepareCurrentFrame(platform_host.Context());

        const uint32_t frame_index = render_loop.Sync().CurrentFrameIndex();
        if (descriptor_initialized) {
            descriptor_host.BeginFrame(platform_host.Context(), frame_index);
        }

        if (upload_initialized) {
            upload_host.BeginFrame(platform_host.Context(), frame_index);
        }

        if constexpr (RuntimeFramePreparer<RecorderT>) {
            RuntimePrepareContext prepare_context{};
            prepare_context.context = &platform_host.Context();
            prepare_context.frame_index = frame_index;
            prepare_context.gpu_memory_host = &gpu_memory_host;
            prepare_context.upload_host = upload_initialized ? &upload_host : nullptr;
            prepare_context.descriptor_host = descriptor_initialized ? &descriptor_host : nullptr;
            prepare_context.pipeline_host = pipeline_initialized ? &pipeline_host : nullptr;
            prepare_context.sampler_host = sampler_initialized ? &sampler_host : nullptr;
            prepare_context.freetype_host = freetype_initialized ? &freetype_host : nullptr;
            prepare_context.glyph_atlas_host = glyph_atlas_initialized ? &glyph_atlas_host : nullptr;
            prepare_context.glyph_upload_host = glyph_upload_initialized ? &glyph_upload_host : nullptr;
            recorder_.PrepareFrame(prepare_context);
        }

        if (upload_initialized && glyph_upload_initialized && glyph_atlas_initialized) {
            glyph_upload_host.UploadDirtyPages(platform_host.Context(),
                                               upload_host,
                                               frame_index,
                                               glyph_atlas_host);
        }

        FrameSubmitWait extra_wait{};
        const FrameSubmitWait* extra_wait_ptr = nullptr;
        uint32_t extra_wait_count = 0U;

        if (upload_initialized) {
            UploadSubmitInfo upload_submit{};
            if (upload_wait_required) {
                upload_submit.signal_semaphore = UploadCompleteSemaphore(frame_index);
            }
            const UploadEndFrameResult upload_end_result = upload_host.EndFrameAndSubmit(
                platform_host.Context(),
                frame_index,
                upload_submit);

            result.upload_submitted = upload_end_result.submitted;
            if (upload_end_result.submitted && upload_wait_required) {
                extra_wait.semaphore = upload_submit.signal_semaphore;
                extra_wait.stage_mask = create_info_cache.upload_wait_stage_mask;
                extra_wait_ptr = &extra_wait;
                extra_wait_count = 1U;
                result.upload_cross_queue_wait = true;
            }
        }

        result.render = render_loop.Tick(platform_host.Context(),
                                         platform_host.SurfaceHost(),
                                         swapchain,
                                         recorder_,
                                         extra_wait_ptr,
                                         extra_wait_count);

        if (pipeline_initialized && create_info_cache.pipeline_warmup.compile_after_render) {
            result.compiled_pipeline_count += pipeline_host.ProcessPendingCompiles(
                platform_host.Context(),
                create_info_cache.pipeline_warmup.max_graphics_compiles_per_tick,
                create_info_cache.pipeline_warmup.max_compute_compiles_per_tick);
        }

        FillPipelineQueueStats(result);
        result.running = platform_host.IsRunning();
        return result;
    }

    [[nodiscard]] bool IsInitialized() const noexcept {
        return initialized;
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return platform_host.IsRunning();
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

    void RequestClose() noexcept {
        platform_host.RequestClose();
    }

    [[nodiscard]] const CreateInfo& Config() const noexcept {
        return create_info_cache;
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

private:
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

    void FillPipelineQueueStats(RuntimeTickResult& result_) const noexcept {
        if (!pipeline_initialized) {
            result_.pending_graphics_compile_count = 0U;
            result_.pending_compute_compile_count = 0U;
            return;
        }
        result_.pending_graphics_compile_count = pipeline_host.PendingGraphicsCompileCount();
        result_.pending_compute_compile_count = pipeline_host.PendingComputeCompileCount();
    }

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
