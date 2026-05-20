#include "support/test_framework.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef VR_TEST_SOURCE_DIR
#error "VR_TEST_SOURCE_DIR must be defined for Phase 12 graph-only closeout contract tests"
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

[[nodiscard]] std::string NormalizeRelativePath(const std::filesystem::path& path_) {
    return path_.lexically_normal().generic_string();
}

[[nodiscard]] bool HasTrackedSourceExtension(const std::filesystem::path& path_) {
    const auto extension = path_.extension().generic_string();
    return extension == ".cpp" || extension == ".hpp";
}

[[nodiscard]] std::vector<std::string> CollectTrackedSourceFiles(
    const std::initializer_list<std::filesystem::path> roots_) {
    std::vector<std::string> files{};
    for (const auto& root : roots_) {
        const auto absolute_root = SourceRoot() / root;
        if (!std::filesystem::exists(absolute_root)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(absolute_root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto relative = std::filesystem::relative(entry.path(), SourceRoot());
            if (!HasTrackedSourceExtension(relative)) {
                continue;
            }
            files.push_back(NormalizeRelativePath(relative));
        }
    }
    return files;
}

struct LoadedSourceFile final {
    std::string relative_path{};
    std::string source{};
};

[[nodiscard]] std::vector<LoadedSourceFile> LoadTrackedSourceFiles(
    const std::initializer_list<std::filesystem::path> roots_) {
    std::vector<LoadedSourceFile> loaded_files{};
    for (const auto& relative_path : CollectTrackedSourceFiles(roots_)) {
        loaded_files.push_back(LoadedSourceFile{
            .relative_path = relative_path,
            .source = ReadUtf8TextFile(SourceRoot() / relative_path),
        });
    }
    return loaded_files;
}

struct ForbiddenTokenCheck final {
    std::string_view path;
    std::string_view token;
};

struct TokenAllowlist final {
    std::string_view token;
    std::initializer_list<std::string_view> allowed_paths;
};

} // namespace

VR_TEST_CASE(Phase12_graph_only_closeout_source_contract_blocks_legacy_orchestration_tokens,
             "unit;contract;phase12;render_graph") {
    constexpr std::array<ForbiddenTokenCheck, 21U> forbidden_tokens{{
        {"include/vr/runtime/services/render_graph_runtime_service.hpp", "EnableRecordExecution("},
        {"include/vr/runtime/services/render_graph_runtime_service.hpp", "EnableGraphOnlyRecordPath("},
        {"include/vr/runtime/services/render_graph_runtime_service.hpp", "GraphOnlyRecordPathEnabled("},
        {"include/vr/runtime/services/render_graph_runtime_service.hpp", "RequireStrictGraphOnlyRecord("},
        {"include/vr/runtime/services/render_graph_runtime_service.hpp", "StrictGraphOnlyRecordRequired("},
        {"include/vr/runtime/services/render_graph_runtime_service.hpp", "SupportsGraphOnlyRecord("},
        {"include/vr/render/render_runtime_host.hpp", "RequireStrictGraphOnlyRecord("},
        {"include/vr/render/render_runtime_host.hpp", "StrictGraphOnlyRecordRequired("},
        {"include/vr/render/render_runtime_host.hpp", "SupportsGraphOnlyRecord("},
        {"include/vr/render/runtime_prepare_views.hpp", "prefer_graph_only_runtime_path"},
        {"src/render/scene_recorder_2d.cpp", "RecordTransition("},
        {"src/render/scene_recorder_2d.cpp", "AcquireTransientTarget("},
        {"src/render/scene_recorder_2d.cpp", "CreateTransientTarget("},
        {"src/render/scene_recorder_2d.cpp", "PreferGraphOnlyRuntimePath("},
        {"src/render/scene_recorder_3d.cpp", "RecordTransition("},
        {"src/render/scene_recorder_3d.cpp", "AcquireTransientTarget("},
        {"src/render/scene_recorder_3d.cpp", "PreferGraphOnlyRuntimePath("},
        {"src/render/frame_composer_host.cpp", "AcquireTransientTarget("},
        {"src/render/frame_composer_host.cpp", "CreateTransientTarget("},
        {"src/render/render_target_composite_renderer.cpp", "RecordTransition("},
        {"bench/cases/runtime_steady_state_allocation_bench.cpp", "GraphOnlyRecordPathEnabled("},
    }};

    for (const auto& check : forbidden_tokens) {
        const std::string source = ReadUtf8TextFile(SourceRoot() / check.path);
        VR_CHECK(!Contains(source, check.token));
    }
}

VR_TEST_CASE(Phase12_graph_only_closeout_source_contract_keeps_bench_on_factual_graph_diagnostics,
             "unit;contract;phase12;bench;render_graph") {
    const std::string bench_source =
        ReadUtf8TextFile(SourceRoot() / "bench" / "cases" /
                         "runtime_steady_state_allocation_bench.cpp");

    VR_CHECK(Contains(bench_source, "LastDiagnostics().graph_only_active"));
    VR_CHECK(!Contains(bench_source, "GraphOnlyRecordPathEnabled("));
}

VR_TEST_CASE(Phase12_graph_only_closeout_source_contract_removes_dual_track_gate_tokens_repo_wide,
             "unit;contract;phase12;runtime;render_graph") {
    constexpr std::array<std::string_view, 7U> forbidden_tokens{
        "EnableRecordExecution(",
        "EnableGraphOnlyRecordPath(",
        "GraphOnlyRecordPathEnabled(",
        "RequireStrictGraphOnlyRecord(",
        "StrictGraphOnlyRecordRequired(",
        "SupportsGraphOnlyRecord(",
        "prefer_graph_only_runtime_path",
    };
    const auto tracked_files = LoadTrackedSourceFiles({
        "include/vr/render",
        "include/vr/runtime",
        "src/render",
        "bench/cases",
    });

    for (const auto& file : tracked_files) {
        for (const auto token : forbidden_tokens) {
            VR_CHECK(!Contains(file.source, token));
        }
    }
}

VR_TEST_CASE(Phase12_graph_only_closeout_source_contract_isolates_transition_and_transient_tokens_to_low_level_helpers,
             "unit;contract;phase12;render_graph") {
    const std::array<TokenAllowlist, 3U> allowlists{{
        TokenAllowlist{
            .token = "RecordTransition(",
            .allowed_paths = {
                "include/vr/render/render_target_host.hpp",
                "src/render/render_target_bloom_renderer.cpp",
                "src/render/render_target_host.cpp",
                "src/render/render_target_pass.cpp",
            },
        },
        TokenAllowlist{
            .token = "AcquireTransientTarget(",
            .allowed_paths = {
                "include/vr/render/render_target_pool.hpp",
                "src/render/render_target_bloom_renderer.cpp",
                "src/render/render_target_pool.cpp",
                "src/render/scene_render_target_set.cpp",
            },
        },
        TokenAllowlist{
            .token = "CreateTransientTarget(",
            .allowed_paths = {
                "include/vr/render/render_target_host.hpp",
                "src/render/render_target_host.cpp",
                "src/render/render_target_pool.cpp",
            },
        },
    }};
    const auto tracked_files = LoadTrackedSourceFiles({
        "include/vr/render",
        "include/vr/runtime",
        "src/render",
        "bench/cases",
    });

    for (const auto& allowlist : allowlists) {
        const std::set<std::string> allowed_paths(allowlist.allowed_paths.begin(),
                                                  allowlist.allowed_paths.end());
        for (const auto& file : tracked_files) {
            if (!Contains(file.source, allowlist.token)) {
                continue;
            }
            VR_CHECK(allowed_paths.find(file.relative_path) != allowed_paths.end());
        }
    }
}
