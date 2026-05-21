#include "support/test_framework.hpp"

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

VR_TEST_CASE(Runtime_graph_only_mainline_source_removes_long_lived_dual_track_gates,
             "unit;contract;runtime;render_graph") {
    const std::string runtime_service =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" / "services" /
                         "render_graph_runtime_service.hpp");
    const std::string runtime_host =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "render" /
                         "render_runtime_host.hpp");
    const std::string runtime_public =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "runtime" /
                         "runtime.hpp");
    const std::string prepare_views =
        ReadUtf8TextFile(SourceRoot() / "include" / "vr" / "render" /
                         "runtime_prepare_views.hpp");
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
    VR_CHECK(!Contains(runtime_host, " TargetPool("));
    VR_CHECK(!Contains(runtime_public, " TargetPool("));
    VR_CHECK(!Contains(runtime_2d_profile, "RenderTargetPoolService"));
    VR_CHECK(!Contains(runtime_3d_profile, "RenderTargetPoolService"));
    VR_CHECK(!Contains(frame_composer_service, "RenderTargetPoolService"));
    VR_CHECK(!Contains(runtime_host, "SceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(runtime_host, "SceneBloomPostStackPrepareView"));

    VR_CHECK(Contains(prepare_views, "prefer_render_graph_upload_path"));
    VR_CHECK(Contains(prepare_views, "prefer_render_graph_compute_path"));
    VR_CHECK(!Contains(prepare_views, "prefer_graph_only_runtime_path"));
    VR_CHECK(!Contains(prepare_views, "SceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(prepare_views, "SceneBloomPostStackPrepareView"));
    VR_CHECK(!Contains(runtime_views, "SceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(runtime_views, "SceneBloomPostStackPrepareView"));
    VR_CHECK(!Contains(runtime_views, "MakeSceneRenderTargetSetPrepareView"));
    VR_CHECK(!Contains(runtime_views, "MakeSceneBloomPostStackPrepareView"));
}
