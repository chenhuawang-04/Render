#include "support/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef VR_TEST_SOURCE_DIR
#error "VR_TEST_SOURCE_DIR must be defined for FrameComposer graph-only contract tests"
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

VR_TEST_CASE(FrameComposer_graph_only_mainline_source_avoids_scene_level_transition_and_transient_acquire_calls,
             "unit;contract;frame_composer;render_graph") {
    const std::string host_source =
        ReadUtf8TextFile(SourceRoot() / "src" / "render" / "frame_composer_host.cpp");
    const std::string composite_source =
        ReadUtf8TextFile(SourceRoot() / "src" / "render" / "render_target_composite_renderer.cpp");

    VR_CHECK(Contains(host_source, "PrepareGraphManagedSceneTargets("));
    VR_CHECK(!Contains(host_source, "PrepareFrameAndConfigure("));
    VR_CHECK(!Contains(host_source, "OnSwapchainRecreatedAndConfigure("));
    VR_CHECK(!Contains(host_source, "AcquireTransientTarget("));
    VR_CHECK(!Contains(host_source, "CreateTransientTarget("));
    VR_CHECK(!Contains(composite_source, "RecordTransition("));
}
