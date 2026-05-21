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
