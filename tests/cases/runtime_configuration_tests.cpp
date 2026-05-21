#include "support/test_framework.hpp"
#include "vr/runtime/runtime.hpp"
#include "vr/runtime/profiles/minimal_profile.hpp"
#include "vr/runtime/profiles/runtime_2d_profile.hpp"
#include "vr/runtime/profiles/runtime_3d_profile.hpp"
#include "vr/runtime/runtime_service.hpp"
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
#include "vr/runtime/services/render_target_service.hpp"
#include "vr/runtime/services/sampler_service.hpp"
#include "vr/runtime/services/sky_environment_service.hpp"
#include "vr/runtime/services/texture_service.hpp"
#include "vr/runtime/services/upload_service.hpp"
#include "vr/text/text_runtime_contract.hpp"
#include "vr/vulkan_context.hpp"

#include <functional>
#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;

class NoopRecorder final {
public:
    void Record(const vr::render::FrameRecordContext& record_context_) {
        (void)record_context_;
    }
};

template<typename FnT>
[[nodiscard]] bool ThrowsAnyException(FnT&& function_) {
    try {
        std::invoke(std::forward<FnT>(function_));
    } catch (...) {
        return true;
    }
    return false;
}

struct LifecycleEventLog final {
    std::array<int, 16> events{};
    std::uint32_t count = 0U;

    void Push(const int value_) noexcept {
        if (count < events.size()) {
            events[count++] = value_;
        }
    }
};

inline LifecycleEventLog* g_lifecycle_log = nullptr;

struct MockLifecycleServiceA final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "MockLifecycleServiceA";

    template<typename ContextT>
    void Initialize(ContextT&) {
        g_lifecycle_log->Push(1);
    }

    template<typename ContextT>
    void PostInitialize(ContextT&) {
        g_lifecycle_log->Push(4);
    }

    template<typename ContextT>
    void Shutdown(ContextT&) {
        g_lifecycle_log->Push(9);
    }
};

struct MockLifecycleServiceB final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<MockLifecycleServiceA>;
    static constexpr std::string_view Name = "MockLifecycleServiceB";

    template<typename ContextT>
    void Initialize(ContextT&) {
        g_lifecycle_log->Push(2);
    }

    template<typename ContextT>
    void PostInitialize(ContextT&) {
        g_lifecycle_log->Push(5);
    }

    template<typename ContextT>
    void Shutdown(ContextT&) {
        g_lifecycle_log->Push(8);
    }
};

struct MockLifecycleServiceC final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<MockLifecycleServiceA, MockLifecycleServiceB>;
    static constexpr std::string_view Name = "MockLifecycleServiceC";

    template<typename ContextT>
    void Initialize(ContextT&) {
        g_lifecycle_log->Push(3);
    }

    template<typename ContextT>
    void PostInitialize(ContextT&) {
        g_lifecycle_log->Push(6);
    }

    template<typename ContextT>
    void Shutdown(ContextT&) {
        g_lifecycle_log->Push(7);
    }
};

struct MockPhaseServiceA final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "MockPhaseServiceA";

    template<typename ContextT>
    void BeginFrame(ContextT&) {
        g_lifecycle_log->Push(1);
    }

    template<typename ContextT>
    void PrepareFrame(ContextT&) {
        g_lifecycle_log->Push(3);
    }

    template<typename ContextT>
    void PreRecord(ContextT&) {
        g_lifecycle_log->Push(5);
    }

    template<typename ContextT>
    void PostRecord(ContextT&) {
        g_lifecycle_log->Push(6);
    }

    template<typename ContextT>
    void EndFrame(ContextT&) {
        g_lifecycle_log->Push(9);
    }

    template<typename ContextT>
    void Retire(ContextT&) {
        g_lifecycle_log->Push(11);
    }
};

struct MockPhaseServiceB final {
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<MockPhaseServiceA>;
    static constexpr std::string_view Name = "MockPhaseServiceB";

    template<typename ContextT>
    void BeginFrame(ContextT&) {
        g_lifecycle_log->Push(2);
    }

    template<typename ContextT>
    void PrepareFrame(ContextT&) {
        g_lifecycle_log->Push(4);
    }

    template<typename ContextT>
    void PreRecord(ContextT&) {
        g_lifecycle_log->Push(7);
    }

    template<typename ContextT>
    void PostRecord(ContextT&) {
        g_lifecycle_log->Push(8);
    }

    template<typename ContextT>
    void EndFrame(ContextT&) {
        g_lifecycle_log->Push(10);
    }

    template<typename ContextT>
    void Retire(ContextT&) {
        g_lifecycle_log->Push(12);
    }
};

VR_TEST_CASE(RuntimeConfig_modules_default_to_enabled, "unit;core;runtime") {
    vr::runtime::RuntimeModulesCreateInfo modules{};
    VR_CHECK(modules.enable_texture_host);
    VR_CHECK(modules.enable_frame_composer_host);
    VR_CHECK(modules.enable_ibl_host);
    VR_CHECK(!modules.enable_ibl_bake_host);
    VR_CHECK(modules.enable_sky_environment_gpu_host);
    VR_CHECK(modules.enable_upload_host);
    VR_CHECK(modules.enable_descriptor_host);
    VR_CHECK(modules.enable_pipeline_host);
    VR_CHECK(modules.enable_render_target_host);
    VR_CHECK(modules.enable_sampler_host);
    VR_CHECK(modules.enable_freetype_host);
    VR_CHECK(modules.enable_glyph_atlas_host);
    VR_CHECK(modules.enable_glyph_upload_host);
    VR_CHECK(modules.enable_particle_upload_host);
    VR_CHECK(modules.enable_particle_simulation_host);
}

VR_TEST_CASE(RuntimeConfig_default_state_before_initialize_is_safe, "unit;core;runtime") {
    Runtime runtime{};

    VR_CHECK(!runtime.IsInitialized());
    VR_CHECK(!runtime.IsRunning());
    VR_CHECK(!runtime.HasTextureHost());
    VR_CHECK(!runtime.HasFrameComposerHost());
    VR_CHECK(!runtime.HasIblHost());
    VR_CHECK(!runtime.HasIblBakeHost());
    VR_CHECK(!runtime.HasSkyEnvironmentHost());
    VR_CHECK(!runtime.HasUploadHost());
    VR_CHECK(!runtime.HasDescriptorHost());
    VR_CHECK(!runtime.HasPipelineHost());
    VR_CHECK(!runtime.HasRenderTargetHost());
    VR_CHECK(!runtime.HasSamplerHost());
    VR_CHECK(!runtime.HasFreeTypeHost());
    VR_CHECK(!runtime.HasGlyphAtlasHost());
    VR_CHECK(!runtime.HasGlyphUploadHost());
    VR_CHECK(!runtime.HasParticleUploadHost());
    VR_CHECK(!runtime.HasParticleSimulationHost());
    VR_CHECK(!runtime.ParticleUploadService().IsAvailable());
    VR_CHECK(!runtime.ParticleSimulationService().IsAvailable());
    VR_CHECK(!runtime.SkyEnvironmentService().IsAvailable());
    VR_CHECK(!runtime.Particles().IsAvailable());

    const Runtime::CreateInfo& config = runtime.Config();
    VR_CHECK(config.modules.enable_upload_host);
    VR_CHECK(config.modules.enable_texture_host);
    VR_CHECK(config.modules.enable_frame_composer_host);
    VR_CHECK(config.modules.enable_ibl_host);
    VR_CHECK(!config.modules.enable_ibl_bake_host);
    VR_CHECK(config.modules.enable_sky_environment_gpu_host);
    VR_CHECK(config.modules.enable_descriptor_host);
    VR_CHECK(config.modules.enable_pipeline_host);
    VR_CHECK(config.modules.enable_render_target_host);
    VR_CHECK(config.modules.enable_sampler_host);
    VR_CHECK(config.modules.enable_freetype_host);
    VR_CHECK(config.modules.enable_glyph_atlas_host);
    VR_CHECK(config.modules.enable_glyph_upload_host);
    VR_CHECK(config.modules.enable_particle_upload_host);
    VR_CHECK(config.modules.enable_particle_simulation_host);
    VR_CHECK(config.diagnostics.level == vr::runtime::DiagnosticsLevel::Off);
}

VR_TEST_CASE(RuntimeConfig_text_runtime_feature_contract_enables_dynamic_rendering_and_sync2,
             "unit;core;runtime;text") {
    Runtime::CreateInfo create_info{};
    VR_CHECK(create_info.bindless.update_after_bind_policy ==
             vr::render::BindlessUpdateAfterBindPolicy::disabled);
    vr::text::ApplyTextRuntimeFeatureContract(create_info);

    VR_CHECK(create_info.platform.device.required_vulkan13_features.dynamicRendering == VK_TRUE);
    VR_CHECK(create_info.platform.device.required_vulkan13_features.synchronization2 == VK_TRUE);
    VR_CHECK(create_info.bindless.update_after_bind_policy ==
             vr::render::BindlessUpdateAfterBindPolicy::disabled);

    const auto default_text_create_info =
        vr::text::MakeDefaultTextRuntimeCreateInfo<Runtime::CreateInfo>();
    VR_CHECK(default_text_create_info.platform.device.required_vulkan13_features.dynamicRendering == VK_TRUE);
    VR_CHECK(default_text_create_info.platform.device.required_vulkan13_features.synchronization2 == VK_TRUE);
    VR_CHECK(default_text_create_info.bindless.update_after_bind_policy ==
             vr::render::BindlessUpdateAfterBindPolicy::disabled);
    VR_CHECK(default_text_create_info.platform.device.feature_chain_policy ==
             vr::VulkanFeatureChainPolicy::minimal_required);
}

VR_TEST_CASE(RuntimeConfig_vulkan_device_feature_chain_policy_defaults_to_minimal_required,
             "unit;core;runtime;vulkan") {
    vr::VulkanDeviceCreateInfo device_create_info{};
    VR_CHECK(device_create_info.feature_chain_policy ==
             vr::VulkanFeatureChainPolicy::minimal_required);
    VR_CHECK(!device_create_info.request_dynamic_rendering_local_read);

    device_create_info.feature_chain_policy =
        vr::VulkanFeatureChainPolicy::explicit_vulkan12_vulkan13;
    VR_CHECK(device_create_info.feature_chain_policy ==
             vr::VulkanFeatureChainPolicy::explicit_vulkan12_vulkan13);
    device_create_info.request_dynamic_rendering_local_read = true;
    VR_CHECK(device_create_info.request_dynamic_rendering_local_read);
}

VR_TEST_CASE(RuntimeConfig_unavailable_modules_throw_before_initialize, "unit;core;runtime") {
    Runtime runtime{};

    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GpuMemory(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Texture(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.FrameComposer(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Ibl(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.IblBake(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.SkyEnvironment(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Upload(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Descriptor(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Pipeline(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.RenderTarget(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Sampler(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.FreeType(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GlyphAtlas(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.GlyphUpload(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.ParticleUpload(); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.ParticleSimulation(); }));
}

VR_TEST_CASE(RuntimeConfig_particle_service_dependency_contract_matches_runtime_design,
             "unit;core;runtime;particle") {
    using namespace vr::runtime;
    using namespace vr::runtime::profiles;
    using namespace vr::runtime::services;

    static_assert(RuntimeService<GpuMemoryService>);
    static_assert(RuntimeService<ParticleRenderService>);
    static_assert(RuntimeService<SkyEnvironmentService>);

    static_assert(service_depends_on_v<UploadService, GpuMemoryService>);
    static_assert(service_depends_on_v<TextureService, GpuMemoryService>);
    static_assert(service_depends_on_v<TextureService, UploadService>);
    static_assert(service_depends_on_v<RenderTargetService, GpuMemoryService>);
    static_assert(service_depends_on_v<FrameComposerService, RenderTargetService>);
    static_assert(service_depends_on_v<IblService, TextureService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, TextureService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, DescriptorService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, PipelineService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, SamplerService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, UploadService>);
    static_assert(service_depends_on_v<SkyEnvironmentService, GpuMemoryService>);
    static_assert(service_depends_on_v<IblBakeService, IblService>);
    static_assert(service_depends_on_v<GlyphAtlasService, FreeTypeService>);
    static_assert(service_depends_on_v<GlyphUploadService, GlyphAtlasService>);
    static_assert(service_depends_on_v<ParticleUploadService, GpuMemoryService>);
    static_assert(service_depends_on_v<ParticleUploadService, UploadService>);
    static_assert(service_depends_on_v<ParticleSimulationService, GpuMemoryService>);
    static_assert(service_depends_on_v<ParticleSimulationService, UploadService>);
    static_assert(service_depends_on_v<ParticleSimulationService, DescriptorService>);
    static_assert(service_depends_on_v<ParticleSimulationService, PipelineService>);
    static_assert(service_depends_on_v<ParticleRenderService, ParticleUploadService>);
    static_assert(service_depends_on_v<ParticleRenderService, ParticleSimulationService>);
    static_assert(service_depends_on_v<ParticleRenderService, TextureService>);
    static_assert(service_depends_on_v<ParticleRenderService, DescriptorService>);
    static_assert(service_depends_on_v<ParticleRenderService, PipelineService>);
    static_assert(service_depends_on_v<ParticleRenderService, SamplerService>);
    static_assert(service_depends_on_v<ParticleRenderService, RenderTargetService>);
    static_assert(service_depends_on_v<ParticleRenderService, UploadService>);

    static_assert(profile_contains_v<MinimalProfile, services::CommandService>);
    static_assert(profile_contains_v<Runtime2DProfile, ParticleUploadService>);
    static_assert(profile_contains_v<Runtime2DProfile, ParticleSimulationService>);
    static_assert(profile_contains_v<Runtime2DProfile, ParticleRenderService>);
    static_assert(!profile_contains_v<Runtime2DProfile, SkyEnvironmentService>);
    static_assert(profile_contains_v<Runtime3DProfile, FrameComposerService>);
    static_assert(profile_contains_v<Runtime3DProfile, IblService>);
    static_assert(profile_contains_v<Runtime3DProfile, SkyEnvironmentService>);
    static_assert(profile_contains_v<Runtime3DProfile, IblBakeService>);
    static_assert(profile_contains_v<Runtime3DProfile, ParticleUploadService>);
    static_assert(profile_contains_v<Runtime3DProfile, ParticleSimulationService>);
    static_assert(profile_contains_v<Runtime3DProfile, ParticleRenderService>);

    static_assert(profile_satisfies_service_dependencies_v<Runtime2DProfile, ParticleUploadService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime2DProfile, ParticleSimulationService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime2DProfile, ParticleRenderService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime3DProfile, IblBakeService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime3DProfile, SkyEnvironmentService>);
    static_assert(profile_satisfies_service_dependencies_v<Runtime3DProfile, ParticleRenderService>);

    VR_CHECK(true);
}

VR_TEST_CASE(RuntimeConfig_runtime_services_registry_exposes_profiled_typed_access,
             "unit;core;runtime;services") {
    using namespace vr::runtime;
    using namespace vr::runtime::services;

    Runtime runtime{};
    auto& services = runtime.Services();

    static_assert(Runtime::RuntimeServicesType::Contains<ParticleRenderService>());
    static_assert(Runtime::RuntimeServicesType::Contains<FrameComposerService>());
    static_assert(Runtime::RuntimeServicesType::Contains<SkyEnvironmentService>());
    static_assert(!Runtime::RuntimeServicesType::Contains<int>());

    VR_CHECK(services.TryGet<ParticleRenderService>() != nullptr);
    VR_CHECK(services.TryGet<ParticleSimulationService>() != nullptr);
    VR_CHECK(services.TryGet<FrameComposerService>() != nullptr);
    VR_CHECK(services.TryGet<IblService>() != nullptr);
    VR_CHECK(services.TryGet<SkyEnvironmentService>() != nullptr);

    VR_CHECK(!services.Get<ParticleRenderService>().IsAvailable());
    VR_CHECK(!services.Get<ParticleSimulationService>().IsAvailable());
    VR_CHECK(!services.Get<FrameComposerService>().IsAvailable());
    VR_CHECK(!services.Get<IblService>().IsAvailable());
    VR_CHECK(!services.Get<SkyEnvironmentService>().IsAvailable());
}

VR_TEST_CASE(RuntimeConfig_runtime_services_lifecycle_dispatch_uses_forward_init_and_reverse_shutdown,
             "unit;core;runtime;services") {
    using MockProfile = vr::runtime::RuntimeProfile<
        MockLifecycleServiceA,
        MockLifecycleServiceB,
        MockLifecycleServiceC>;
    using Services = vr::runtime::RuntimeServices<MockProfile>;
    using Kernel = vr::runtime::RuntimeKernel<vr::platform::ActiveBackendTag, 2U>;

    Services services{};
    Kernel kernel{};
    MockLifecycleServiceA service_a{};
    MockLifecycleServiceB service_b{};
    MockLifecycleServiceC service_c{};
    services.Bind(service_a, service_b, service_c);

    LifecycleEventLog log{};
    g_lifecycle_log = &log;

    auto init_context = vr::runtime::RuntimeInitContext<MockProfile, vr::platform::ActiveBackendTag, 2U>{
        .services = services,
        .kernel = kernel,
        .device = nullptr,
    };
    auto post_init_context =
        vr::runtime::RuntimePostInitContext<MockProfile, vr::platform::ActiveBackendTag, 2U>{
            .services = services,
            .kernel = kernel,
            .device = nullptr,
        };
    auto shutdown_context =
        vr::runtime::RuntimeShutdownContext<MockProfile, vr::platform::ActiveBackendTag, 2U>{
            .services = services,
            .kernel = kernel,
            .device = nullptr,
        };

    services.Initialize(init_context);
    services.PostInitialize(post_init_context);
    services.Shutdown(shutdown_context);
    g_lifecycle_log = nullptr;

    VR_REQUIRE(log.count == 9U);
    VR_CHECK(log.events[0] == 1);
    VR_CHECK(log.events[1] == 2);
    VR_CHECK(log.events[2] == 3);
    VR_CHECK(log.events[3] == 4);
    VR_CHECK(log.events[4] == 5);
    VR_CHECK(log.events[5] == 6);
    VR_CHECK(log.events[6] == 7);
    VR_CHECK(log.events[7] == 8);
    VR_CHECK(log.events[8] == 9);
}

VR_TEST_CASE(RuntimeConfig_runtime_execution_dispatches_service_phases_in_document_order,
             "unit;core;runtime;services") {
    using MockProfile = vr::runtime::RuntimeProfile<MockPhaseServiceA, MockPhaseServiceB>;
    using Services = vr::runtime::RuntimeServices<MockProfile>;
    using Kernel = vr::runtime::RuntimeKernel<vr::platform::ActiveBackendTag, 2U>;
    using FrameContext = vr::runtime::RuntimeFrameContext<MockProfile, vr::platform::ActiveBackendTag, 2U>;
    using Execution = vr::runtime::RuntimeExecution<FrameContext>;

    Services services{};
    Kernel kernel{};
    vr::runtime::CommandService commands{};
    MockPhaseServiceA service_a{};
    MockPhaseServiceB service_b{};
    services.Bind(service_a, service_b);

    FrameContext frame_context{
        .frame = {},
        .progress = {},
        .timelines = {},
        .services = services,
        .kernel = kernel,
        .commands = commands,
        .swapchain_targets = nullptr,
    };

    LifecycleEventLog log{};
    g_lifecycle_log = &log;

    Execution execution{frame_context};
    execution.MarkBeginFrame();
    execution.ServiceBeginFrame(services);
    execution.Prepare(services);
    execution.MarkFlushUploads();
    execution.PreRecord(services);
    execution.MarkRecord();
    execution.MarkSubmit();
    execution.PostRecord(services);
    execution.MarkPresent();
    execution.EndFrame(services);
    execution.Retire(services);
    execution.MarkDiagnostics();
    g_lifecycle_log = nullptr;

    VR_REQUIRE(log.count == 12U);
    constexpr std::array<int, 12> expected_order = {1, 2, 3, 4, 5, 7, 6, 8, 10, 9, 12, 11};
    for (std::uint32_t index = 0U; index < log.count; ++index) {
        VR_CHECK(log.events[index] == expected_order[index]);
    }
    VR_CHECK(execution.Trace().last_completed_stage == vr::runtime::RuntimeExecutionStage::Diagnostics);
    VR_CHECK(execution.Trace().completed_stage_count == 11U);
}

VR_TEST_CASE(RuntimeConfig_runtime_root_wraps_legacy_facade_with_typed_status,
             "unit;core;runtime;services") {
    using RuntimeRoot = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;
    using ParticleRenderService = vr::runtime::services::ParticleRenderService;
    using ParticleSimulationService = vr::runtime::services::ParticleSimulationService;

    static_assert(std::is_same_v<RuntimeRoot::CreateInfo,
                                 vr::runtime::RuntimeCreateInfo<vr::platform::ActiveBackendTag, 2U>>);

    RuntimeRoot runtime{};
    VR_CHECK(runtime.Kernel().IsBound());
    VR_CHECK(runtime.Frames().IsBound());
    VR_CHECK(runtime.Frames().FramesInFlight() == 2U);
    VR_CHECK(runtime.Commands().IsBound());
    VR_CHECK(runtime.Retire().IsBound());
    VR_CHECK(runtime.Kernel().Clock().frame_id == 0U);
    VR_CHECK(runtime.LastExecutionTrace().completed_stage_count == 0U);
    const auto timelines = runtime.Kernel().BuildQueueTimelines();
    VR_CHECK(timelines.graphics.next_value == 1U);
    VR_CHECK(timelines.graphics.submitted_value == 0U);
    VR_CHECK(timelines.graphics.completed_value == 0U);
    VR_CHECK(!timelines.graphics.IsAvailable());
    VR_CHECK(!timelines.transfer.IsAvailable());
    VR_CHECK(!timelines.compute.IsAvailable());
    const auto dependency = runtime.Kernel().BuildGraphicsDependency();
    VR_CHECK(dependency.source_queue == vr::runtime::QueueKind::graphics);
    VR_CHECK(dependency.value == 0U);
    const auto transfer_dependency = runtime.Kernel().BuildTransferDependency();
    VR_CHECK(transfer_dependency.source_queue == vr::runtime::QueueKind::transfer);
    VR_CHECK(transfer_dependency.value == 0U);
    const auto compute_dependency = runtime.Kernel().BuildComputeDependency();
    VR_CHECK(compute_dependency.source_queue == vr::runtime::QueueKind::compute);
    VR_CHECK(compute_dependency.value == 0U);
    VR_CHECK(runtime.Services().TryGet<ParticleRenderService>() != nullptr);
    VR_CHECK(runtime.Services().TryGet<ParticleSimulationService>() != nullptr);
    VR_CHECK(!runtime.Services().Get<ParticleSimulationService>().HasComputeTimelineProgress());
    VR_CHECK(runtime.Services().Get<ParticleSimulationService>().LastSubmittedValue() == 0U);
    VR_CHECK(runtime.Services().Get<ParticleSimulationService>().CompletedSubmitValue() == 0U);
    VR_CHECK(runtime.Config().diagnostics.level == vr::runtime::DiagnosticsLevel::Off);

    VR_CHECK(vr::runtime::ToRuntimeStatusCode(vr::render::TickCode::Submitted) ==
             vr::runtime::RuntimeStatusCode::Ok);
    VR_CHECK(vr::runtime::ToRuntimeStatusCode(vr::render::TickCode::SkippedWindowHidden) ==
             vr::runtime::RuntimeStatusCode::WindowHidden);
    VR_CHECK(vr::runtime::ToRuntimeStatusCode(vr::render::TickCode::RecreateRequested) ==
             vr::runtime::RuntimeStatusCode::SwapchainRecreated);
}

VR_TEST_CASE(RuntimeConfig_tick_requires_initialize, "unit;core;runtime") {
    Runtime runtime{};
    NoopRecorder recorder{};

    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.Tick(recorder); }));
}

VR_TEST_CASE(RuntimeConfig_resource_creation_requires_initialize, "unit;core;runtime") {
    Runtime runtime{};

    vr::resource::BufferCreateInfo buffer_create_info{};
    buffer_create_info.size = 1024U;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    vr::resource::ImageCreateInfo image_create_info{};
    image_create_info.extent = VkExtent3D{64U, 64U, 1U};
    image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.CreateBuffer(buffer_create_info); }));
    VR_CHECK(ThrowsAnyException([&]() { (void)runtime.CreateImage(image_create_info); }));
}

} // namespace

