#pragma once

#include "vr/asset/texture_host.hpp"
#include "vr/render/frame_composer_host.hpp"
#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/ibl_bake_host.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/platform/render_host.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/runtime/runtime_create_info.hpp"
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
#include "vr/runtime/services/render_target_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/sky_environment_service.hpp"
#include "vr/runtime/services/texture_service.hpp"
#include "vr/runtime/services/upload_service.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/render_loop_host.hpp"

#include <vector>
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

#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vr::render {

using RuntimeDiagnosticsCreateInfo = vr::runtime::RuntimeDiagnosticsCreateInfo;
using RuntimeFrameDiagnostics = vr::runtime::RuntimeFrameDiagnosticsV2;

template<typename RecorderT, typename PrepareViewT>
concept RuntimeDirectGraphRecorder =
    requires(RecorderT& recorder_,
             const PrepareViewT& prepare_view_,
             const RuntimeDirectGraphBuildView& graph_view_) {
        recorder_.PrepareFrame(prepare_view_);
        recorder_.BuildDirectRuntimeGraph(graph_view_);
    };

template<typename RecorderT, typename PrepareViewT>
concept RuntimeDirectGraphDescriptorRecorder =
    RuntimeDirectGraphRecorder<RecorderT, PrepareViewT> &&
    requires(const RecorderT& recorder_,
             render_graph::RenderGraphBuilder& builder_,
             const render_graph::PassHandle pass_) {
        recorder_.DescribeGraphDescriptorBindings(builder_, pass_);
    };

template<typename RecorderT>
concept RuntimeDirectGraphImportedResourceRecorder =
    requires(RecorderT& recorder_,
             runtime::services::RenderGraphRuntimeService& graph_runtime_service_) {
        recorder_.RegisterGraphImportedResources(graph_runtime_service_);
    };

template<typename RecorderT>
concept RuntimeTickRecorder = FrameRecorder<RecorderT> ||
                              FrameContextRecorder<RecorderT> ||
                              requires(RecorderT& recorder_,
                                       const SceneRecorder2DPrepareView& prepare_view_) {
                                  recorder_.PrepareFrame(prepare_view_);
                              } ||
                              requires(RecorderT& recorder_,
                                       const SceneRecorder3DPrepareView& prepare_view_) {
                                  recorder_.PrepareFrame(prepare_view_);
                              } ||
                              requires(RecorderT& recorder_,
                                       const FrameComposerPrepareView& prepare_view_) {
                                  recorder_.PrepareFrame(prepare_view_);
                              } ||
                               requires(RecorderT& recorder_,
                                        const RenderTargetCompositeRendererPrepareView& prepare_view_) {
                                   recorder_.PrepareFrame(prepare_view_);
                               } ||
                               RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                    TextRenderer2DPrepareView> ||
                               RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                    TextRenderer3DPrepareView> ||
                               RuntimeDirectGraphRecorder<RecorderT,
                                                          GeometryRenderer2DPrepareView> ||
                               RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                    GeometryRenderer3DPrepareView> ||
                               RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                    SurfaceRenderer2DPrepareView> ||
                               RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                    SurfaceRenderer3DPrepareView> ||
                               RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                    ParticleRenderer2DPrepareView> ||
                               RuntimeDirectGraphDescriptorRecorder<RecorderT,
                                                                    ParticleRenderer3DPrepareView>;

template<typename BackendTagT = platform::ActiveBackendTag,
         uint32_t frames_in_flight_v = 2U>
// Internal runtime-core host implementation.
// Public entry points should route through vr::runtime::Runtime.
class RenderRuntimeHost final {
public:
    static_assert(frames_in_flight_v > 0U, "frames_in_flight_v must be >= 1");

    using BackendTag = BackendTagT;
    using CreateInfo = vr::runtime::RuntimeCreateInfo<BackendTagT, frames_in_flight_v>;
    using PipelineWarmupCreateInfo =
        vr::runtime::RuntimePipelineWarmupCreateInfo<BackendTagT, frames_in_flight_v>;
    using RuntimeModulesCreateInfo = vr::runtime::RuntimeModulesCreateInfo;
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

#include "vr/runtime/detail/render_runtime_host_lifecycle.ipp"

#include "vr/runtime/detail/render_runtime_host_tick.ipp"

#include "vr/runtime/detail/render_runtime_host_resources.ipp"

private:
#include "vr/runtime/detail/render_runtime_host_graph_bridge.ipp"

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

namespace vr::runtime::detail {

template<typename BackendTagT = vr::platform::ActiveBackendTag,
         std::uint32_t frames_in_flight_v = 2U>
using RuntimeHost = vr::render::RenderRuntimeHost<BackendTagT, frames_in_flight_v>;

} // namespace vr::runtime::detail

