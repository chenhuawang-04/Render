#include "support/test_framework.hpp"
#include "vr/runtime/detail/render_runtime_host.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef VR_TEST_SOURCE_DIR
#error "VR_TEST_SOURCE_DIR must be defined for runtime graph-only contract tests"
#endif

namespace {

template<typename PrepareViewT>
struct DescriptorlessDirectGraphWrapper final {
    void PrepareFrame(const PrepareViewT&) {}
    void BuildDirectRuntimeGraph(const vr::render::RuntimeDirectGraphBuildView&) {}
};

template<typename PrepareViewT>
struct DescriptorAwareDirectGraphWrapper final {
    void PrepareFrame(const PrepareViewT&) {}
    void BuildDirectRuntimeGraph(const vr::render::RuntimeDirectGraphBuildView&) {}
    void DescribeGraphDescriptorBindings(vr::render_graph::RenderGraphBuilder&,
                                         vr::render_graph::PassHandle) const {}
};

using Geometry2DMinimalWrapper =
    DescriptorlessDirectGraphWrapper<vr::render::GeometryRenderer2DPrepareView>;
using Text2DMissingDescriptorWrapper =
    DescriptorlessDirectGraphWrapper<vr::render::TextRenderer2DPrepareView>;
using Text2DFullWrapper =
    DescriptorAwareDirectGraphWrapper<vr::render::TextRenderer2DPrepareView>;
using Text3DMissingDescriptorWrapper =
    DescriptorlessDirectGraphWrapper<vr::render::TextRenderer3DPrepareView>;
using Surface3DMissingDescriptorWrapper =
    DescriptorlessDirectGraphWrapper<vr::render::SurfaceRenderer3DPrepareView>;
using Geometry3DMissingDescriptorWrapper =
    DescriptorlessDirectGraphWrapper<vr::render::GeometryRenderer3DPrepareView>;
using Particle2DMissingDescriptorWrapper =
    DescriptorlessDirectGraphWrapper<vr::render::ParticleRenderer2DPrepareView>;
using Particle3DMissingDescriptorWrapper =
    DescriptorlessDirectGraphWrapper<vr::render::ParticleRenderer3DPrepareView>;

static_assert(vr::render::RuntimeTickRecorder<Geometry2DMinimalWrapper>);
static_assert(!vr::render::RuntimeTickRecorder<Text2DMissingDescriptorWrapper>);
static_assert(vr::render::RuntimeTickRecorder<Text2DFullWrapper>);
static_assert(!vr::render::RuntimeTickRecorder<Text3DMissingDescriptorWrapper>);
static_assert(!vr::render::RuntimeTickRecorder<Surface3DMissingDescriptorWrapper>);
static_assert(!vr::render::RuntimeTickRecorder<Geometry3DMissingDescriptorWrapper>);
static_assert(!vr::render::RuntimeTickRecorder<Particle2DMissingDescriptorWrapper>);
static_assert(!vr::render::RuntimeTickRecorder<Particle3DMissingDescriptorWrapper>);

[[nodiscard]] std::filesystem::path SourceRoot() {
    return std::filesystem::path{VR_TEST_SOURCE_DIR};
}

[[nodiscard]] std::string ReadUtf8TextFile(const std::filesystem::path& path_) {
    std::ifstream stream(path_, std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Failed to open file: " + path_.string());
    }

    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string ReadConcatenatedUtf8TextFiles(
    const std::initializer_list<std::filesystem::path> paths_) {
    std::string combined{};
    for (const auto& path : paths_) {
        combined += ReadUtf8TextFile(path);
        combined.push_back('\n');
    }
    return combined;
}

[[nodiscard]] std::string ReadRuntimeHostCompositeSource() {
    return ReadConcatenatedUtf8TextFiles({
        SourceRoot() / "include" / "vr" / "runtime" / "detail" / "render_runtime_host.hpp",
        SourceRoot() / "include" / "vr" / "runtime" / "detail" / "render_runtime_host_lifecycle.ipp",
        SourceRoot() / "include" / "vr" / "runtime" / "detail" / "render_runtime_host_tick.ipp",
        SourceRoot() / "include" / "vr" / "runtime" / "detail" / "render_runtime_host_resources.ipp",
        SourceRoot() / "include" / "vr" / "runtime" / "detail" / "render_runtime_host_graph_bridge.ipp",
    });
}

[[nodiscard]] bool Contains(std::string_view haystack_,
                            std::string_view needle_) noexcept {
    return haystack_.find(needle_) != std::string_view::npos;
}

[[nodiscard]] std::size_t CountOccurrences(std::string_view haystack_,
                                           std::string_view needle_) noexcept {
    if (needle_.empty()) {
        return 0U;
    }

    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < haystack_.size()) {
        const std::size_t match = haystack_.find(needle_, offset);
        if (match == std::string_view::npos) {
            break;
        }
        ++count;
        offset = match + needle_.size();
    }
    return count;
}

} // namespace

VR_TEST_CASE(Runtime_graph_only_mainline_source_removes_long_lived_dual_track_gates,
             "unit;contract;runtime;render_graph") {
    const auto legacy_runtime_host_path =
        SourceRoot() / "include" / "vr" / "render" / "render_runtime_host.hpp";
    const auto internal_runtime_host_path =
        SourceRoot() / "include" / "vr" / "runtime" / "detail" / "render_runtime_host.hpp";
    const auto legacy_render_target_pool_header =
        SourceRoot() / "include" / "vr" / "render" / "render_target_pool.hpp";
    const auto legacy_render_target_pool_source =
        SourceRoot() / "src" / "render" / "render_target_pool.cpp";
    const auto legacy_render_target_pool_service =
        SourceRoot() / "include" / "vr" / "runtime" / "services" /
        "render_target_pool_service.hpp";
    const std::string runtime_service =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" / "services" /
                         "render_graph_runtime_service.hpp");
    const std::string runtime_host_root =
        ReadUtf8TextFile(internal_runtime_host_path);
    const std::string runtime_host = ReadRuntimeHostCompositeSource();
    const std::string runtime_public =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" /
                         "runtime.hpp");
    const std::string runtime_kernel =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" /
                         "runtime_kernel.hpp");
    const std::string runtime_create_info =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" /
                         "runtime_create_info.hpp");
    const std::string prepare_views =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "render" /
                         "runtime_prepare_views.hpp");
    const std::string frame_prepare_context =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" /
                         "frame_prepare_context.hpp");
    const std::string scene_prepare_views =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "render" /
                         "scene_prepare_views.hpp");
    const std::string renderer_prepare_views_2d =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "render" /
                         "renderer_prepare_views_2d.hpp");
    const std::string renderer_prepare_views_3d =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "render" /
                         "renderer_prepare_views_3d.hpp");
    const std::string runtime_views =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" /
                         "runtime_views.hpp");
    const std::string runtime_2d_profile =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" / "profiles" /
                         "runtime_2d_profile.hpp");
    const std::string runtime_3d_profile =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" / "profiles" /
                         "runtime_3d_profile.hpp");
    const std::string frame_composer_service =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" / "services" /
                         "frame_composer_service.hpp");

    VR_CHECK(!std::filesystem::exists(legacy_runtime_host_path));
    VR_CHECK(std::filesystem::exists(internal_runtime_host_path));
    VR_CHECK(!std::filesystem::exists(legacy_render_target_pool_header));
    VR_CHECK(!std::filesystem::exists(legacy_render_target_pool_source));
    VR_CHECK(!std::filesystem::exists(legacy_render_target_pool_service));

    VR_CHECK(Contains(runtime_service, "SupportsGraphExecution("));
    VR_CHECK(Contains(runtime_service, "HasGraphRecordWorkSource("));
    VR_CHECK(!Contains(runtime_service, "EnableRecordExecution("));
    VR_CHECK(!Contains(runtime_service, "EnableGraphOnlyRecordPath("));
    VR_CHECK(!Contains(runtime_service, "GraphOnlyRecordPathEnabled("));
    VR_CHECK(!Contains(runtime_service, "RequireStrictGraphOnlyRecord("));
    VR_CHECK(!Contains(runtime_service, "StrictGraphOnlyRecordRequired("));
    VR_CHECK(!Contains(runtime_service, "SupportsGraphOnlyRecord("));

    VR_CHECK(Contains(runtime_host, "HasGraphRecordWorkSource("));
    VR_CHECK(!Contains(runtime_host, "RequireStrictGraphOnlyRecord("));
    VR_CHECK(!Contains(runtime_host, "StrictGraphOnlyRecordRequired("));
    VR_CHECK(!Contains(runtime_host, "SupportsGraphOnlyRecord("));
    VR_CHECK(!Contains(runtime_host, "render_target_pool"));
    VR_CHECK(Contains(runtime_host_root,
                       "#include \"vr/runtime/detail/render_runtime_host_lifecycle.ipp\""));
    VR_CHECK(Contains(runtime_host_root,
                       "#include \"vr/runtime/detail/render_runtime_host_tick.ipp\""));
    VR_CHECK(Contains(runtime_host_root,
                       "#include \"vr/runtime/detail/render_runtime_host_resources.ipp\""));
    VR_CHECK(Contains(runtime_host_root,
                       "#include \"vr/runtime/detail/render_runtime_host_graph_bridge.ipp\""));
    VR_CHECK(Contains(runtime_public,
                       "#include \"vr/runtime/detail/runtime_facade_lifecycle.ipp\""));
    VR_CHECK(Contains(runtime_public,
                       "#include \"vr/runtime/detail/runtime_facade_accessors.ipp\""));
    VR_CHECK(Contains(runtime_public,
                       "#include \"vr/runtime/detail/runtime_facade_tick_detail.ipp\""));
    VR_CHECK(!Contains(runtime_public, " TargetPool("));
    VR_CHECK(!Contains(runtime_public, "HostType"));
    VR_CHECK(!Contains(runtime_public, " Host("));
    VR_CHECK(!Contains(runtime_public, "PlatformHost("));
    VR_CHECK(!Contains(runtime_public, "RenderTargetPoolStats("));
    VR_CHECK(!Contains(runtime_public, "HasRenderTargetPool("));
    VR_CHECK(!Contains(runtime_public, "vr/render/render_runtime_host.hpp"));
    VR_CHECK(!Contains(runtime_public, "vr/runtime/detail/render_runtime_host.hpp"));
    VR_CHECK(!Contains(runtime_public,
                       "detail::RuntimeHost<BackendTag, frames_in_flight_v>::RuntimeServicesType"));
    VR_CHECK(!Contains(runtime_public,
                       "RuntimeFrameContext<typename detail::RuntimeHost"));
    VR_CHECK(!Contains(runtime_public, "class ExecutionPhaseDriver final"));
    VR_CHECK(!Contains(runtime_public, "void CollectTickPostState("));
    VR_CHECK(!Contains(runtime_kernel,
                       "detail::RuntimeHost<BackendTag, frames_in_flight_v>::PlatformHostType"));
    VR_CHECK(!Contains(runtime_kernel,
                       "detail::RuntimeHost<BackendTag, frames_in_flight_v>::SwapchainType"));
    VR_CHECK(!Contains(runtime_kernel,
                       "detail::RuntimeHost<BackendTag, frames_in_flight_v>::LoopType"));
    VR_CHECK(!Contains(runtime_create_info, "RenderRuntimeHost<"));
    VR_CHECK(!Contains(runtime_create_info, "render_runtime_host.hpp"));
    VR_CHECK(!Contains(runtime_2d_profile, "RenderTargetPoolService"));
    VR_CHECK(!Contains(runtime_3d_profile, "RenderTargetPoolService"));
    VR_CHECK(!Contains(frame_composer_service, "RenderTargetPoolService"));
    VR_CHECK(!Contains(runtime_host, "SceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(runtime_host, "SceneBloomPostStackPrepareView"));

    VR_CHECK(Contains(prepare_views, "#include \"vr/runtime/frame_prepare_context.hpp\""));
    VR_CHECK(Contains(prepare_views, "#include \"vr/render/scene_prepare_views.hpp\""));
    VR_CHECK(Contains(prepare_views, "#include \"vr/render/renderer_prepare_views_2d.hpp\""));
    VR_CHECK(Contains(prepare_views, "#include \"vr/render/renderer_prepare_views_3d.hpp\""));
    VR_CHECK(!Contains(prepare_views, "render_graph_upload_active"));
    VR_CHECK(!Contains(prepare_views, "render_graph_compute_active"));
    VR_CHECK(!Contains(prepare_views, "prefer_graph_only_runtime_path"));
    VR_CHECK(!Contains(prepare_views, "SceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(prepare_views, "SceneBloomPostStackPrepareView"));
    VR_CHECK(Contains(frame_prepare_context, "struct FrameStaticContext final"));
    VR_CHECK(Contains(frame_prepare_context, "struct FrameGpuProgressContext final"));
    VR_CHECK(Contains(frame_prepare_context, "struct RuntimeDirectGraphBuildView final"));
    VR_CHECK(!Contains(frame_prepare_context, "struct SceneRecorder2DPrepareView final"));
    VR_CHECK(Contains(scene_prepare_views, "struct SceneRecorder2DPrepareView final"));
    VR_CHECK(Contains(scene_prepare_views, "struct SceneRecorder3DPrepareView final"));
    VR_CHECK(Contains(scene_prepare_views, "struct FrameComposerPrepareView final"));
    VR_CHECK(!Contains(scene_prepare_views, "struct GeometryRenderer2DPrepareView final"));
    VR_CHECK(!Contains(scene_prepare_views, "struct GeometryRenderer3DPrepareView final"));
    VR_CHECK(Contains(scene_prepare_views, "render_graph_upload_active"));
    VR_CHECK(Contains(scene_prepare_views, "render_graph_compute_active"));
    VR_CHECK(Contains(renderer_prepare_views_2d, "struct GeometryRenderer2DPrepareView final"));
    VR_CHECK(Contains(renderer_prepare_views_2d, "struct TextRenderer2DPrepareView final"));
    VR_CHECK(Contains(renderer_prepare_views_2d, "struct ParticleRenderer2DPrepareView final"));
    VR_CHECK(!Contains(renderer_prepare_views_2d, "struct GeometryRenderer3DPrepareView final"));
    VR_CHECK(Contains(renderer_prepare_views_2d, "render_graph_upload_active"));
    VR_CHECK(Contains(renderer_prepare_views_2d, "render_graph_compute_active"));
    VR_CHECK(Contains(renderer_prepare_views_3d, "struct GeometryRenderer3DPrepareView final"));
    VR_CHECK(Contains(renderer_prepare_views_3d, "struct TextRenderer3DPrepareView final"));
    VR_CHECK(Contains(renderer_prepare_views_3d, "struct ParticleRenderer3DPrepareView final"));
    VR_CHECK(!Contains(renderer_prepare_views_3d, "struct GeometryRenderer2DPrepareView final"));
    VR_CHECK(Contains(renderer_prepare_views_3d, "render_graph_upload_active"));
    VR_CHECK(Contains(renderer_prepare_views_3d, "render_graph_compute_active"));
    VR_CHECK(!Contains(runtime_views, "SceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(runtime_views, "SceneBloomPostStackPrepareView"));
    VR_CHECK(!Contains(runtime_views, "MakeSceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(runtime_views, "MakeSceneBloomPostStackPrepareView"));
}

VR_TEST_CASE(Runtime_graph_only_direct_graph_contract_rejects_partial_descriptor_wrappers,
             "unit;contract;runtime;render_graph") {
    VR_CHECK(vr::render::RuntimeTickRecorder<Geometry2DMinimalWrapper>);
    VR_CHECK(!vr::render::RuntimeTickRecorder<Text2DMissingDescriptorWrapper>);
    VR_CHECK(vr::render::RuntimeTickRecorder<Text2DFullWrapper>);
    VR_CHECK(!vr::render::RuntimeTickRecorder<Text3DMissingDescriptorWrapper>);
    VR_CHECK(!vr::render::RuntimeTickRecorder<Surface3DMissingDescriptorWrapper>);
    VR_CHECK(!vr::render::RuntimeTickRecorder<Geometry3DMissingDescriptorWrapper>);
    VR_CHECK(!vr::render::RuntimeTickRecorder<Particle2DMissingDescriptorWrapper>);
    VR_CHECK(!vr::render::RuntimeTickRecorder<Particle3DMissingDescriptorWrapper>);
}

VR_TEST_CASE(Runtime_graph_only_mainline_source_prefers_runtime_wrapper_in_canonical_lanes,
             "unit;contract;runtime;render_graph") {
    constexpr std::array<std::string_view, 10U> mainline_runtime_files{
        "examples/sdl_runtime_demo.cpp",
        "examples/sdl_text_demo.cpp",
        "bench/cases/runtime_text_renderer_bench.cpp",
        "bench/cases/runtime_steady_state_allocation_bench.cpp",
        "tests/cases/runtime_geometry_renderer_2d_integration_tests.cpp",
        "tests/cases/runtime_geometry_renderer_3d_integration_tests.cpp",
        "tests/cases/runtime_particle_renderer_2d_integration_tests.cpp",
        "tests/cases/runtime_particle_renderer_3d_integration_tests.cpp",
        "tests/cases/runtime_text_renderer_3d_integration_tests.cpp",
        "tests/cases/runtime_text_renderer_integration_tests.cpp",
    };
    constexpr std::string_view runtime_wrapper_alias =
        "using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;";

    for (const auto relative_path : mainline_runtime_files) {
        const std::string source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{relative_path});
        VR_CHECK(Contains(source, runtime_wrapper_alias));
        VR_CHECK(!Contains(source, "RenderRuntimeHost<"));
        VR_CHECK(!Contains(source, "RenderTargetPoolStats("));
        VR_CHECK(!Contains(source, "HasRenderTargetPool("));
    }
}

VR_TEST_CASE(Runtime_graph_only_secondary_demo_sources_follow_graph_only_recorder_surface,
             "unit;contract;runtime;render_graph") {
    constexpr std::array<std::string_view, 3U> specialty_demo_files{
        "examples/sdl_surface_unified_demo.cpp",
        "examples/sdl_text_3d_demo.cpp",
        "examples/sdl_pbr_material_grid_demo.cpp",
    };
    constexpr std::string_view runtime_wrapper_alias =
        "using Runtime = vr::runtime::Runtime<vr::platform::ActiveBackendTag, 2U>;";

    for (const auto relative_path : specialty_demo_files) {
        const std::string source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{relative_path});
        VR_CHECK(Contains(source, runtime_wrapper_alias));
        VR_CHECK(!Contains(source, "RenderRuntimeHost<"));
        VR_CHECK(!Contains(source, "PostStack("));
        VR_CHECK(Contains(source, "BloomStats("));
    }
}

VR_TEST_CASE(Runtime_graph_only_canonical_runtime_demo_uses_render_graph_clear_path,
             "unit;contract;runtime;render_graph") {
    const std::string runtime_demo =
        ReadUtf8TextFile(SourceRoot() / "examples" / "sdl_runtime_demo.cpp");

    VR_CHECK(Contains(runtime_demo, "BuildRenderGraph("));
    VR_CHECK(Contains(runtime_demo, "FrameComposerPrepareView"));
    VR_CHECK(!Contains(runtime_demo, "FrameRecordContext"));
    VR_CHECK(!Contains(runtime_demo, "vkCmdPipelineBarrier("));
    VR_CHECK(!Contains(runtime_demo, "vkCmdClearColorImage("));
}

VR_TEST_CASE(Runtime_graph_supported_renderer_source_prunes_legacy_target_and_record_entrypoints,
             "unit;contract;runtime;render_graph") {
    constexpr std::array<std::string_view, 10U> renderer_headers{
        "include/vr/geometry/geometry_renderer_2d.hpp",
        "include/vr/geometry/geometry_renderer_3d.hpp",
        "include/vr/surface/surface_renderer_2d.hpp",
        "include/vr/surface/surface_renderer_3d.hpp",
        "include/vr/text/text_renderer_2d.hpp",
        "include/vr/text/text_renderer_3d.hpp",
        "include/vr/particle/particle_renderer_2d.hpp",
        "include/vr/particle/particle_renderer_3d.hpp",
        "include/vr/render/render_target_composite_renderer.hpp",
        "include/vr/render/render_target_bloom_renderer.hpp",
    };
    constexpr std::array<std::string_view, 10U> forbidden_tokens{
        "SetOutputTargetConfig(",
        "ResetOutputTargetConfig(",
        "SetDepthTargetConfig(",
        "ResetDepthTargetConfig(",
        "SetSourceTarget(",
        "ClearSourceTarget(",
        "SetSceneSourceTarget(",
        "ClearSceneSourceTarget(",
        "Record(",
        "RecordSceneStage(",
    };

    for (const auto relative_path : renderer_headers) {
        const std::string source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{relative_path});
        for (const auto token : forbidden_tokens) {
            VR_CHECK(!Contains(source, token));
        }
    }
}

VR_TEST_CASE(Runtime_graph_only_direct_runtime_host_delegates_graph_policy_to_renderers,
             "unit;contract;runtime;render_graph") {
    const std::string runtime_host = ReadRuntimeHostCompositeSource();

    VR_CHECK(Contains(runtime_host, "RuntimeDirectGraphDescriptorRecorder<"));
    VR_CHECK(Contains(runtime_host, "recorder_.BuildDirectRuntimeGraph(RuntimeDirectGraphBuildView{"));
    VR_CHECK(Contains(runtime_host, "RegisterOptionalDirectGraphImportedResources(recorder_, render_graph_service_);"));
    VR_CHECK(CountOccurrences(runtime_host, ".render_graph_upload_active = true,") == 2U);
    VR_CHECK(CountOccurrences(runtime_host, ".render_graph_compute_active = true,") == 2U);
    VR_CHECK(!Contains(runtime_host, "SceneRenderStage::opaque"));
    VR_CHECK(!Contains(runtime_host, "SceneRenderStage::transparent"));
    VR_CHECK(!Contains(runtime_host, "AttachmentLoadOp::clear"));
    VR_CHECK(!Contains(runtime_host, "SetRasterPassDesc("));
    VR_CHECK(!Contains(runtime_host, "clear_depth_value"));
    VR_CHECK(!Contains(runtime_host, "clear_stencil_value"));
}

VR_TEST_CASE(Runtime_graph_only_direct_runtime_renderer_sources_define_explicit_graph_build_paths,
             "unit;contract;runtime;render_graph") {
    constexpr std::array<std::string_view, 8U> direct_renderer_headers{
        "include/vr/geometry/geometry_renderer_2d.hpp",
        "include/vr/geometry/geometry_renderer_3d.hpp",
        "include/vr/surface/surface_renderer_2d.hpp",
        "include/vr/surface/surface_renderer_3d.hpp",
        "include/vr/text/text_renderer_2d.hpp",
        "include/vr/text/text_renderer_3d.hpp",
        "include/vr/particle/particle_renderer_2d.hpp",
        "include/vr/particle/particle_renderer_3d.hpp",
    };
    for (const auto relative_path : direct_renderer_headers) {
        const std::string source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{relative_path});
        VR_CHECK(Contains(source, "BuildDirectRuntimeGraph("));
    }

    struct SplitRenderer3DSourceContract final {
        std::string_view root_source;
        std::string_view prepare_source;
        std::string_view graph_source;
        std::string_view resource_source;
        std::string_view extra_source;
        std::string_view extra_token;
    };

    constexpr std::array<SplitRenderer3DSourceContract, 4U> split_renderer_sources{{
        {
            "src/geometry/geometry_renderer_3d.cpp",
            "src/geometry/geometry_renderer_3d_prepare.cpp",
            "src/geometry/geometry_renderer_3d_graph.cpp",
            "src/geometry/geometry_renderer_3d_pipeline_resources.cpp",
            "src/geometry/geometry_renderer_3d_lighting.cpp",
            "EnsureLightingDescriptorObjects(",
        },
        {
            "src/surface/surface_renderer_3d.cpp",
            "src/surface/surface_renderer_3d_prepare.cpp",
            "src/surface/surface_renderer_3d_graph.cpp",
            "src/surface/surface_renderer_3d_pipeline_resources.cpp",
            "",
            "",
        },
        {
            "src/text/text_renderer_3d.cpp",
            "src/text/text_renderer_3d_prepare.cpp",
            "src/text/text_renderer_3d_graph.cpp",
            "src/text/text_renderer_3d_pipeline_resources.cpp",
            "src/text/text_renderer_3d_upload.cpp",
            "ScheduleGraphInstanceUpload(",
        },
        {
            "src/particle/particle_renderer_3d.cpp",
            "src/particle/particle_renderer_3d_prepare.cpp",
            "src/particle/particle_renderer_3d_graph.cpp",
            "src/particle/particle_renderer_3d_pipeline_resources.cpp",
            "",
            "ScheduleGraphComputeBuild(",
        },
    }};
    for (const auto& contract : split_renderer_sources) {
        const std::string root_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.root_source});
        const std::string prepare_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.prepare_source});
        const std::string graph_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.graph_source});
        const std::string resource_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.resource_source});

        VR_CHECK(!Contains(root_source, "BuildDirectRuntimeGraph("));
        VR_CHECK(!Contains(root_source, "RecordGraphSceneStage("));
        VR_CHECK(Contains(prepare_source, "PrepareFrame("));
        VR_CHECK(Contains(graph_source, "BuildDirectRuntimeGraph("));
        VR_CHECK(Contains(graph_source, "_direct_opaque"));
        VR_CHECK(Contains(graph_source, "_direct_transparent"));
        VR_CHECK(Contains(graph_source, "DescribeGraphDescriptorBindings(graph_view_.builder, pass);"));
        VR_CHECK(Contains(graph_source, "create_info_cache.clear_swapchain"));
        VR_CHECK(Contains(graph_source, "create_info_cache.clear_depth"));
        VR_CHECK(Contains(graph_source, "create_info_cache.clear_depth_value"));
        VR_CHECK(Contains(resource_source, "EnsurePipelineObjects("));

        if (!contract.extra_source.empty()) {
            const std::string extra_source =
                ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.extra_source});
            VR_CHECK(Contains(extra_source, contract.extra_token));
        } else if (!contract.extra_token.empty()) {
            VR_CHECK(Contains(graph_source, contract.extra_token));
        }
    }

    constexpr std::array<std::string_view, 4U> direct_renderer_3d_sources{
        "src/geometry/geometry_renderer_3d_graph.cpp",
        "src/surface/surface_renderer_3d_graph.cpp",
        "src/text/text_renderer_3d_graph.cpp",
        "src/particle/particle_renderer_3d_graph.cpp",
    };
    for (const auto relative_path : direct_renderer_3d_sources) {
        const std::string source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{relative_path});
        VR_CHECK(Contains(source, "BuildDirectRuntimeGraph("));
        VR_CHECK(Contains(source, "_direct_opaque"));
        VR_CHECK(Contains(source, "_direct_transparent"));
        VR_CHECK(Contains(source, "DescribeGraphDescriptorBindings(graph_view_.builder, pass);"));
        VR_CHECK(Contains(source, "create_info_cache.clear_swapchain"));
        VR_CHECK(Contains(source, "create_info_cache.clear_depth"));
        VR_CHECK(Contains(source, "create_info_cache.clear_depth_value"));
    }

    {
        const std::string geometry_2d_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/geometry/geometry_renderer_2d.cpp"});
        VR_CHECK(Contains(geometry_2d_source, "BuildDirectRuntimeGraph("));
        VR_CHECK(Contains(geometry_2d_source, "create_info_cache.clear_swapchain"));
    }

    struct SplitRenderer2DSourceContract final {
        std::string_view root_source;
        std::string_view prepare_source;
        std::string_view graph_source;
        std::string_view resource_source;
        std::string_view extra_source;
        std::string_view extra_token;
    };

    constexpr std::array<SplitRenderer2DSourceContract, 3U> split_renderer_sources_2d{{
        {
            "src/surface/surface_renderer_2d.cpp",
            "src/surface/surface_renderer_2d_prepare.cpp",
            "src/surface/surface_renderer_2d_graph_overlay.cpp",
            "src/surface/surface_renderer_2d_pipeline_resources.cpp",
            "",
            "EnsureLightingDescriptorObjects(",
        },
        {
            "src/text/text_renderer_2d.cpp",
            "src/text/text_renderer_2d_prepare.cpp",
            "src/text/text_renderer_2d_graph_overlay.cpp",
            "src/text/text_renderer_2d_resource.cpp",
            "",
            "ScheduleGraphInstanceUpload(",
        },
        {
            "src/particle/particle_renderer_2d.cpp",
            "src/particle/particle_renderer_2d_prepare.cpp",
            "src/particle/particle_renderer_2d_graph_overlay.cpp",
            "src/particle/particle_renderer_2d_resource.cpp",
            "src/particle/particle_renderer_2d_graph_overlay.cpp",
            "ScheduleGraphComputeBuild(",
        },
    }};
    for (const auto& contract : split_renderer_sources_2d) {
        const std::string root_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.root_source});
        const std::string prepare_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.prepare_source});
        const std::string graph_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.graph_source});
        const std::string resource_source =
            ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.resource_source});

        VR_CHECK(!Contains(root_source, "BuildDirectRuntimeGraph("));
        VR_CHECK(!Contains(root_source, "RecordGraphOverlay("));
        VR_CHECK(Contains(prepare_source, "PrepareFrame("));
        VR_CHECK(Contains(graph_source, "BuildDirectRuntimeGraph("));
        VR_CHECK(Contains(graph_source, "DescribeGraphDescriptorBindings(graph_view_.builder, pass);"));
        VR_CHECK(Contains(graph_source, "create_info_cache.clear_swapchain"));
        VR_CHECK(Contains(graph_source, "_direct"));
        VR_CHECK(Contains(resource_source, "EnsurePipelineObjects("));
        if (!contract.extra_source.empty()) {
            const std::string extra_source =
                ReadUtf8TextFile(SourceRoot() / std::filesystem::path{contract.extra_source});
            VR_CHECK(Contains(extra_source, contract.extra_token));
        } else {
            VR_CHECK(Contains(resource_source, contract.extra_token));
        }
    }
}

VR_TEST_CASE(Runtime_graph_only_graph_core_split_keeps_builder_and_native_pass_public_roots_thin,
             "unit;contract;runtime;render_graph") {
    const std::string builder_root =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/render_graph_builder.cpp"});
    const std::string builder_compile =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/render_graph_builder_compile.cpp"});
    const std::string builder_mutation =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/render_graph_builder_mutation.cpp"});
    const std::string builder_debug =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/compiled_render_graph_debug.cpp"});
    const std::string native_root =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/native_pass_plan.cpp"});
    const std::string native_attachments =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/native_pass_plan_attachments.cpp"});
    const std::string native_boundaries =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/native_pass_plan_boundaries.cpp"});
    const std::string native_debug =
        ReadUtf8TextFile(SourceRoot() / std::filesystem::path{"src/render_graph/native_pass_plan_debug.cpp"});

    VR_CHECK(!Contains(builder_root, "CompiledRenderGraph::BuildDebugString("));
    VR_CHECK(!Contains(builder_root, "CompiledRenderGraph::BuildDotGraph("));
    VR_CHECK(!Contains(builder_root, "CompiledRenderGraph::BuildJson("));
    VR_CHECK(!Contains(builder_root, "RenderGraphBuilder::Compile() const"));
    VR_CHECK(Contains(builder_compile, "RenderGraphBuilder::Compile() const"));
    VR_CHECK(Contains(builder_mutation, "RenderGraphBuilder::CreateTexture("));
    VR_CHECK(Contains(builder_mutation, "RenderGraphBuilder::SetExecuteCallback("));
    VR_CHECK(Contains(builder_debug, "CompiledRenderGraph::BuildDebugString() const"));
    VR_CHECK(Contains(builder_debug, "CompiledRenderGraph::BuildDotGraph() const"));
    VR_CHECK(Contains(builder_debug, "CompiledRenderGraph::BuildJson() const"));

    VR_CHECK(!Contains(native_root, "BuildNativePassPlan("));
    VR_CHECK(!Contains(native_root, "BuildNativePassPlanDebugString("));
    VR_CHECK(!Contains(native_root, "BuildNativePassPlanJson("));
    VR_CHECK(Contains(native_attachments, "PopulateAttachmentDecisions("));
    VR_CHECK(Contains(native_boundaries, "BuildNativePassPlan("));
    VR_CHECK(Contains(native_boundaries, "EvaluateBoundary("));
    VR_CHECK(Contains(native_debug, "BuildNativePassPlanDebugString("));
    VR_CHECK(Contains(native_debug, "BuildNativePassPlanJson("));
}
