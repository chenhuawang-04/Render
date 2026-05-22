#include "support/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef VR_TEST_SOURCE_DIR
#error "VR_TEST_SOURCE_DIR must be defined for SceneRecorder2D graph-only contract tests"
#endif

namespace {

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

[[nodiscard]] bool Contains(std::string_view haystack_,
                            std::string_view needle_) noexcept {
    return haystack_.find(needle_) != std::string_view::npos;
}

} // namespace

VR_TEST_CASE(SceneRecorder2D_graph_only_mainline_source_avoids_scene_level_transition_and_transient_acquire_calls,
             "unit;contract;scene2d;render_graph") {
    const std::string header =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "render" /
                         "scene_recorder_2d.hpp");
    const std::string source =
        ReadUtf8TextFile(SourceRoot() / "src" / "render" / "scene_recorder_2d.cpp");

    VR_CHECK(Contains(source, "UsesGraphManagedSceneOutput("));
    VR_CHECK(!Contains(header, "SceneRenderTargetSet& SceneTargets("));
    VR_CHECK(!Contains(header, "const SceneRenderTargetSet& SceneTargets("));
    VR_CHECK(!Contains(header, "SceneRenderTargetSet scene_targets"));
    VR_CHECK(!Contains(source, "SceneRecorder2D::Record("));
    VR_CHECK(!Contains(source, "color_final_state"));
    VR_CHECK(!Contains(source, "depth_final_state"));
    VR_CHECK(!Contains(source, "PreferGraphOnlyRuntimePath("));
    VR_CHECK(!Contains(source, "RecordTransition("));
    VR_CHECK(!Contains(source, "AcquireTransientTarget("));
    VR_CHECK(!Contains(source, "CreateTransientTarget("));
}

VR_TEST_CASE(SceneRecorder2D_graph_only_source_split_keeps_orchestration_facade_thin,
             "unit;contract;scene2d;render_graph") {
    const auto header_path =
        SourceRoot() / "include" / "vr" / "render" / "scene_recorder_2d.hpp";
    const auto detail_header_path =
        SourceRoot() / "include" / "vr" / "render" / "detail" /
        "scene_recorder_2d_registration_detail.hpp";
    const auto root_source_path =
        SourceRoot() / "src" / "render" / "scene_recorder_2d.cpp";
    const auto registration_source_path =
        SourceRoot() / "src" / "render" / "scene_recorder_2d_registration.cpp";
    const auto prepare_source_path =
        SourceRoot() / "src" / "render" / "scene_recorder_2d_prepare.cpp";
    const auto graph_source_path =
        SourceRoot() / "src" / "render" / "scene_recorder_2d_graph.cpp";

    const std::string header = ReadUtf8TextFile(header_path);
    const std::string detail_header = ReadUtf8TextFile(detail_header_path);
    const std::string root_source = ReadUtf8TextFile(root_source_path);
    const std::string registration_source =
        ReadUtf8TextFile(registration_source_path);
    const std::string prepare_source = ReadUtf8TextFile(prepare_source_path);
    const std::string graph_source = ReadUtf8TextFile(graph_source_path);

    VR_CHECK(std::filesystem::exists(detail_header_path));
    VR_CHECK(std::filesystem::exists(registration_source_path));
    VR_CHECK(std::filesystem::exists(prepare_source_path));
    VR_CHECK(std::filesystem::exists(graph_source_path));
    VR_CHECK(
        Contains(header,
                 "#include \"vr/render/detail/scene_recorder_2d_registration_detail.hpp\""));
    VR_CHECK(!Contains(header, "renderer_ref.PrepareFrame("));
    VR_CHECK(!Contains(header, "renderer_ref.SetLightFrameCoordinator("));
    VR_CHECK(Contains(detail_header, "SceneRecorder2D::RegisterSceneRenderer"));
    VR_CHECK(Contains(detail_header, "SceneRecorder2D::PrepareRenderer"));
    VR_CHECK(Contains(detail_header, "renderer_ref.PrepareFrame("));
    VR_CHECK(Contains(detail_header, "renderer_ref.SetLightFrameCoordinator("));
    VR_CHECK(!Contains(root_source, "SceneRecorder2D::PrepareFrame("));
    VR_CHECK(!Contains(root_source, "SceneRecorder2D::BuildRenderGraph("));
    VR_CHECK(Contains(root_source, "UsesGraphManagedSceneOutput("));
    VR_CHECK(Contains(registration_source, "SceneRecorder2D::RegisterShadowRenderer("));
    VR_CHECK(Contains(prepare_source, "SceneRecorder2D::PrepareFrame("));
    VR_CHECK(Contains(graph_source, "SceneRecorder2D::BuildRenderGraph("));
}
