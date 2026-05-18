#pragma once

#include "vr/asset/texture_host.hpp"
#include "vr/render/frame_composer_host.hpp"
#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/ibl_bake_host.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/platform/render_host.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/runtime/runtime_diagnostics.hpp"
#include "vr/runtime/runtime_execution.hpp"
#include "vr/runtime/profiles/runtime_3d_profile.hpp"
#include "vr/runtime/runtime_services.hpp"
#include "vr/runtime/services/command_service.hpp"
#include "vr/runtime/services/descriptor_service.hpp"
#include "vr/runtime/services/frame_composer_service.hpp"
#include "vr/runtime/services/freetype_service.hpp"
#include "vr/runtime/services/glyph_atlas_service.hpp"
#include "vr/runtime/services/glyph_upload_service.hpp"
#include "vr/runtime/services/gpu_memory_service.hpp"
#include "vr/runtime/services/ibl_bake_service.hpp"
#include "vr/runtime/services/ibl_service.hpp"
#include "vr/runtime/services/particle_render_service.hpp"
#include "vr/runtime/services/particle_simulation_service.hpp"
#include "vr/runtime/services/particle_upload_service.hpp"
#include "vr/runtime/services/pipeline_service.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/runtime/services/render_target_pool_service.hpp"
#include "vr/runtime/services/render_target_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/sky_environment_service.hpp"
#include "vr/runtime/services/texture_service.hpp"
#include "vr/runtime/services/upload_service.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_target_pool.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/swapchain_target_set.hpp"
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
    bool enable_texture_host = true;
    bool enable_frame_composer_host = true;
    bool enable_ibl_host = true;
    bool enable_ibl_bake_host = false;
    bool enable_sky_environment_gpu_host = true;
    bool enable_upload_host = true;
    bool enable_descriptor_host = true;
    bool enable_pipeline_host = true;
    bool enable_render_target_host = true;
    bool enable_render_target_pool = true;
    bool enable_sampler_host = true;
    bool enable_freetype_host = true;
    bool enable_glyph_atlas_host = true;
    bool enable_glyph_upload_host = true;
    bool enable_particle_upload_host = true;
    bool enable_particle_simulation_host = true;
};

using RuntimeDiagnosticsCreateInfo = vr::runtime::RuntimeDiagnosticsCreateInfo;
using RuntimeFrameDiagnostics = vr::runtime::RuntimeFrameDiagnosticsV2;

template<typename BackendTagT = platform::ActiveBackendTag,
         uint32_t frames_in_flight_v = 2U>
// Legacy runtime facade retained during the runtime-core refactor.
// Prefer vr::runtime::Runtime for new public entry points.
class RenderRuntimeHost final {
public:
    static_assert(frames_in_flight_v > 0U, "frames_in_flight_v must be >= 1");

    using BackendTag = BackendTagT;
    using PlatformHostType = platform::RenderHost<BackendTag>;
    using WindowSurfaceType = typename PlatformHostType::WindowSurfaceType;
    using SwapchainType = SwapchainHost<WindowSurfaceType>;
    using LoopType = RenderLoopHost<WindowSurfaceType, SwapchainType, frames_in_flight_v>;
    using DefaultProfile = vr::runtime::profiles::Runtime3DProfile;
    using RuntimeServicesType = vr::runtime::RuntimeServices<DefaultProfile>;

private:
    struct DefaultPhaseDriver;

    [[nodiscard]] static bool RequiresBindlessEngineContract(
        const RuntimeModulesCreateInfo& modules_) noexcept {
        return modules_.enable_descriptor_host &&
               modules_.enable_sampler_host &&
               (modules_.enable_texture_host ||
                modules_.enable_render_target_host ||
                modules_.enable_frame_composer_host ||
                modules_.enable_ibl_host ||
                modules_.enable_sky_environment_gpu_host ||
                modules_.enable_glyph_upload_host);
    }

    static void RequireBindlessEngineContract(const VulkanContext& context_,
                                              const RuntimeModulesCreateInfo& modules_,
                                              const BindlessResourceSystemCreateInfo& bindless_) {
        if (!RequiresBindlessEngineContract(modules_)) {
            return;
        }

        const auto& caps = context_.DescriptorIndexingCapsInfo();
        std::ostringstream missing_stream{};
        bool missing_any = false;
        auto append_missing = [&](const char* name_) {
            if (missing_any) {
                missing_stream << ", ";
            }
            missing_stream << name_;
            missing_any = true;
        };

        if (!caps.sampled_image_array_dynamic_indexing) {
            append_missing("shaderSampledImageArrayDynamicIndexing");
        }
        if (!caps.runtime_descriptor_array) {
            append_missing("runtimeDescriptorArray");
        }
        if (!caps.descriptor_binding_partially_bound) {
            append_missing("descriptorBindingPartiallyBound");
        }
        if (!caps.descriptor_binding_variable_descriptor_count) {
            append_missing("descriptorBindingVariableDescriptorCount");
        }
        if (!caps.sampled_image_array_non_uniform_indexing) {
            append_missing("shaderSampledImageArrayNonUniformIndexing");
        }
        if (!caps.sampler_array_non_uniform_indexing) {
            append_missing("samplerArrayNonUniformIndexing");
        }
        if (bindless_.update_after_bind_policy != BindlessUpdateAfterBindPolicy::disabled) {
            if (!caps.sampled_image_update_after_bind) {
                append_missing("descriptorBindingSampledImageUpdateAfterBind");
            }
            if (!caps.sampler_update_after_bind) {
                append_missing("samplerUpdateAfterBindSupport");
            }
            if (!caps.update_unused_while_pending) {
                append_missing("descriptorBindingUpdateUnusedWhilePending");
            }
        }

        if (!missing_any && caps.enabled) {
            return;
        }
        if (!missing_any) {
            append_missing("descriptorIndexingCaps(enabled=false)");
        }

        std::ostringstream oss{};
        oss << "RenderRuntimeHost requires bindless descriptor indexing: "
            << missing_stream.str();
        throw std::runtime_error(oss.str());
    }

public:

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
        asset::TextureHostCreateInfo texture{};
        FrameComposerHostCreateInfo frame_composer{};
        IblHostCreateInfo ibl{};
        IblBakeHostCreateInfo ibl_bake{};
        SkyEnvironmentGpuHostCreateInfo sky_environment{};
        UploadHostCreateInfo upload{};
        DescriptorHostCreateInfo descriptor{};
        BindlessResourceSystemCreateInfo bindless{};
        PipelineHostCreateInfo pipeline{};
        RenderTargetHostCreateInfo render_target{};
        RenderTargetPoolCreateInfo render_target_pool{};
        resource::SamplerHostCreateInfo sampler{};
        text::FreeTypeHostCreateInfo freetype{};
        text::GlyphAtlasCreateInfo glyph_atlas{};
        text::GlyphUploadHostCreateInfo glyph_upload{};
        particle::ParticleUploadHostCreateInfo particle_upload{};
        particle::ParticleSimulationHostCreateInfo particle_simulation{};

        RuntimeModulesCreateInfo modules{};
        RuntimeDiagnosticsCreateInfo diagnostics{};
        PipelineWarmupCreateInfo pipeline_warmup{};

        bool poll_events_each_tick = true;
        VkPipelineStageFlags upload_wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    };

    struct RuntimeTickResult {
        std::uint64_t frame_id = 0U;
        TickResult render{};
        uint32_t compiled_pipeline_count = 0U;
        uint32_t pending_graphics_compile_count = 0U;
        uint32_t pending_compute_compile_count = 0U;
        bool upload_submitted = false;
        bool upload_cross_queue_wait = false;
        bool events_polled = false;
        bool running = true;
        RuntimeFrameDiagnostics diagnostics{};
    };

    struct TickBeginFrameResult {
        RuntimeTickResult result{};
        std::uint64_t frame_id = 0U;
        std::uint32_t frame_index = 0U;
        bool ready = false;
    };

    struct TickUploadFlushResult {
        bool submitted = false;
        bool cross_queue_wait = false;
        FrameSubmitWait extra_wait{};
        std::uint32_t extra_wait_count = 0U;
        std::uint64_t transfer_wait_value = 0U;

        [[nodiscard]] const FrameSubmitWait* ExtraWaits() const noexcept {
            return extra_wait_count > 0U ? &extra_wait : nullptr;
        }
    };

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

            if (create_info_cache.modules.enable_render_target_pool) {
                if (!render_target_initialized) {
                    throw std::runtime_error(
                        "RenderRuntimeHost::Initialize requires RenderTargetHost when render_target_pool is enabled");
                }
                render_target_pool.Initialize(create_info_cache.render_target_pool);
                render_target_pool_initialized = true;
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
            if (render_target_pool_initialized) {
                if (render_target_initialized) {
                    render_target_pool.InvalidateAll(platform_host.Context(),
                                                    render_target_host,
                                                    0U,
                                                    0U);
                }
                render_target_pool.Shutdown();
                render_target_pool_initialized = false;
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

        if (render_target_pool_initialized) {
            if (render_target_initialized) {
                const std::uint64_t last_submitted_value =
                    loop_initialized ? render_loop.Sync().LastSubmittedValue() : 0U;
                const std::uint64_t completed_submit_value =
                    loop_initialized ? render_loop.Sync().CompletedSubmitValue() : 0U;
                render_target_pool.InvalidateAll(platform_host.Context(),
                                                render_target_host,
                                                last_submitted_value,
                                                completed_submit_value);
            }
            render_target_pool.Shutdown();
            render_target_pool_initialized = false;
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

    template<typename RecorderT>
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
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
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
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
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
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
        const auto render_result = render_loop.SubmitPresentAndAdvance(platform_host.Context(),
                                                                       swapchain,
                                                                       acquired_frame_.token,
                                                                       command_buffer_,
                                                                       upload_flush_.ExtraWaits(),
                                                                       upload_flush_.extra_wait_count);
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
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
    [[nodiscard]] RuntimeTickResult Tick(RecorderT& recorder_) {
        DefaultPhaseDriver phase_driver{*this};
        return TickWithPhaseDriver(recorder_, phase_driver);
    }

    template<typename RecorderT, typename PhaseDriverT>
    requires (FrameRecorder<RecorderT> || FrameContextRecorder<RecorderT>)
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

        result.render = SubmitPresentTickFrame(acquired_frame, command_buffer, upload_flush);
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

    [[nodiscard]] bool HasRenderTargetPool() const noexcept {
        return render_target_pool_initialized;
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

    [[nodiscard]] RenderTargetPool& TargetPool() {
        if (!render_target_pool_initialized) {
            throw std::runtime_error("RenderRuntimeHost::RenderTargetPool requested but pool is not initialized");
        }
        return render_target_pool;
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

private:
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

        if (render_target_pool_initialized) {
            render_target_pool_service_ref.Bind(render_target_pool);
        } else {
            render_target_pool_service_ref.Reset();
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
                          render_target_pool_service_ref,
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
    void DispatchPrepareFrame(RecorderT& recorder_, std::uint32_t frame_index_) {
        const FrameStaticContext frame = BuildFrameStaticContext(frame_index_);
        const FrameGpuProgressContext progress = BuildFrameGpuProgressContext();
        VulkanContext& device = platform_host.Context();

        if constexpr (requires(RecorderT& recorder_ref_,
                               const SceneRecorder3DPrepareView& prepare_view_) {
                          recorder_ref_.PrepareFrame(prepare_view_);
                      }) {
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
                .render_target_pool = render_target_pool_initialized ? &render_target_pool : nullptr,
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
                .render_target_pool = render_target_pool_initialized ? &render_target_pool : nullptr,
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
                        render_graph_service.RequireStrictGraphOnlyRecord(true);
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
                .render_target_pool = render_target_pool_initialized ? &render_target_pool : nullptr,
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
                render_graph_service.RequireStrictGraphOnlyRecord(true);
            }
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const SceneRenderTargetSetPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(SceneRenderTargetSetPrepareView{
                .device = device,
                .render_target = render_target_host,
                .render_target_pool = render_target_pool_initialized ? &render_target_pool : nullptr,
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const SceneBloomPostStackPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(SceneBloomPostStackPrepareView{
                .device = device,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .render_target = render_target_host,
                .render_target_pool = render_target_pool_initialized ? &render_target_pool : nullptr,
                .sampler = sampler_host,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const RenderTargetBloomRendererPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(RenderTargetBloomRendererPrepareView{
                .device = device,
                .descriptor = descriptor_host,
                .pipeline = pipeline_host,
                .render_target = render_target_host,
                .render_target_pool = render_target_pool,
                .sampler = sampler_host,
                .bindless = bindless_resources_initialized ? &bindless_resource_system : nullptr,
                .frame = frame,
                .progress = progress,
            });
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
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const TextRenderer2DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
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
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const TextRenderer3DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
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
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const GeometryRenderer2DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
            recorder_.PrepareFrame(GeometryRenderer2DPrepareView{
                .device = device,
                .upload = upload_host,
                .pipeline = pipeline_host,
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const GeometryRenderer3DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
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
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const SurfaceRenderer2DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
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
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const SurfaceRenderer3DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
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
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const ParticleRenderer2DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
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
                .frame = frame,
                .progress = progress,
            });
        } else if constexpr (requires(RecorderT& recorder_ref_,
                                      const ParticleRenderer3DPrepareView& prepare_view_) {
                                 recorder_ref_.PrepareFrame(prepare_view_);
                             }) {
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
                .frame = frame,
                .progress = progress,
            });
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
            if (graph_service.StrictGraphOnlyRecordRequired()) {
                if (!graph_service.CanExecuteGraphRecord(runtime.platform_host.Context())) {
                    throw std::runtime_error(
                        "RenderRuntimeHost strict graph-only record path requires an executable compiled render graph");
                }
                return;
            }
            if (graph_service.SupportsGraphOnlyRecord(runtime.platform_host.Context())) {
                if (!graph_service.CanExecuteGraphRecord(runtime.platform_host.Context())) {
                    throw std::runtime_error(
                        "RenderRuntimeHost graph-only record path enabled without an executable compiled graph");
                }
                return;
            }

            if constexpr (FrameContextRecorder<RecorderT>) {
                recorder.Record(augmented);
            } else {
                recorder.Record(augmented.command_buffer,
                                augmented.frame_index,
                                augmented.image_index,
                                augmented.extent,
                                augmented.format,
                                augmented.image,
                                augmented.image_view);
            }
        }

        void OnSwapchainRecreated(std::uint32_t image_count_,
                                  VkExtent2D extent_,
                                  VkFormat format_,
                                  std::uint64_t last_submitted_value_,
                                  std::uint64_t completed_submit_value_) {
            runtime.InvalidateSwapchainTargets(last_submitted_value_, completed_submit_value_);
            if (runtime.render_target_pool_initialized && runtime.render_target_initialized) {
                runtime.render_target_pool.InvalidateAll(runtime.platform_host.Context(),
                                                        runtime.render_target_host,
                                                        last_submitted_value_,
                                                        completed_submit_value_);
            }
            if (runtime.frame_composer_initialized && runtime.render_target_initialized) {
                (void)runtime.frame_composer_host.OnSwapchainRecreated(
                    runtime.platform_host.Context(),
                    runtime.render_target_host,
                    runtime.render_target_pool_initialized
                        ? &runtime.render_target_pool
                        : nullptr,
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
        if (render_target_pool_initialized) {
            diagnostics.render_target_pool = render_target_pool.Stats();
            diagnostics.allocations.render_target_transient_acquired_count =
                static_cast<std::uint32_t>(diagnostics.render_target_pool.acquire_count);
        }
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
    PlatformHostType platform_host{};
    SwapchainType swapchain{};
    LoopType render_loop{};

    resource::GpuMemoryHost gpu_memory_host{};
    asset::TextureHost texture_host{};
    FrameComposerHost frame_composer_host{};
    IblHost ibl_host{};
    IblBakeHost ibl_bake_host{};
    SkyEnvironmentGpuHost sky_environment_gpu_host{};
    UploadHost upload_host{};
    DescriptorHost descriptor_host{};
    BindlessResourceSystem bindless_resource_system{};
    PipelineHost pipeline_host{};
    RenderTargetHost render_target_host{};
    RenderTargetPool render_target_pool{};
    resource::SamplerHost sampler_host{};
    text::FreeTypeHost freetype_host{};
    text::GlyphAtlasHost glyph_atlas_host{};
    text::GlyphUploadHost glyph_upload_host{};
    particle::ParticleUploadHost particle_upload_host{};
    particle::ParticleSimulationHost particle_simulation_host{};

    runtime::services::CommandService command_service_ref{};
    runtime::services::GpuMemoryService gpu_memory_service_ref{};
    runtime::services::TextureService texture_service_ref{};
    runtime::services::UploadService upload_service_ref{};
    runtime::services::RenderGraphRuntimeService render_graph_runtime_service_ref{};
    runtime::services::DescriptorService descriptor_service_ref{};
    runtime::services::PipelineService pipeline_service_ref{};
    runtime::services::RenderTargetService render_target_service_ref{};
    runtime::services::RenderTargetPoolService render_target_pool_service_ref{};
    runtime::services::SamplerService sampler_service_ref{};
    runtime::services::FrameComposerService frame_composer_service_ref{};
    runtime::services::IblService ibl_service_ref{};
    runtime::services::SkyEnvironmentService sky_environment_service_ref{};
    runtime::services::IblBakeService ibl_bake_service_ref{};
    runtime::services::FreeTypeService freetype_service_ref{};
    runtime::services::GlyphAtlasService glyph_atlas_service_ref{};
    runtime::services::GlyphUploadService glyph_upload_service_ref{};
    runtime::services::ParticleUploadService particle_upload_service_ref{};
    runtime::services::ParticleSimulationService particle_simulation_service_ref{};
    runtime::services::ParticleRenderService particle_render_service_ref{};
    RuntimeServicesType services_ref{};

    vr::McVector<VkSemaphore> upload_complete_semaphores{};
    SwapchainTargetSet swapchain_targets{};

    CreateInfo create_info_cache{};
    bool gpu_memory_initialized = false;
    bool texture_initialized = false;
    bool frame_composer_initialized = false;
    bool ibl_initialized = false;
    bool ibl_bake_initialized = false;
    bool sky_environment_initialized = false;
    bool upload_initialized = false;
    bool descriptor_initialized = false;
    bool bindless_resources_initialized = false;
    bool pipeline_initialized = false;
    bool render_target_initialized = false;
    bool render_target_pool_initialized = false;
    bool sampler_initialized = false;
    bool freetype_initialized = false;
    bool glyph_atlas_initialized = false;
    bool glyph_upload_initialized = false;
    bool particle_upload_initialized = false;
    bool particle_simulation_initialized = false;
    bool loop_initialized = false;
    bool upload_wait_required = false;
    bool initialized = false;
    std::uint64_t runtime_frame_id = 0U;
    std::uint32_t last_tick_frame_index = 0U;
    std::uint32_t last_tick_image_index = 0U;
};

} // namespace vr::render

