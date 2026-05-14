#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace vr::bench {

enum class BenchmarkOutcome : std::uint8_t {
    completed = 0U,
    skipped = 1U,
    failed = 2U,
};

struct SampleMeasurement {
    double duration_ms = 0.0;
    std::uint64_t iterations = 0U;
    std::uint64_t items_processed = 0U;
    std::uint64_t bytes_processed = 0U;
};

struct BenchmarkMetrics {
    double min_ms = 0.0;
    double max_ms = 0.0;
    double mean_ms = 0.0;
    double median_ms = 0.0;
    double p95_ms = 0.0;
    double stddev_ms = 0.0;
    double min_ns_per_iteration = 0.0;
    double max_ns_per_iteration = 0.0;
    double mean_ns_per_iteration = 0.0;
    double median_ns_per_iteration = 0.0;
    double p95_ns_per_iteration = 0.0;
    double stddev_ns_per_iteration = 0.0;
    double items_per_second = 0.0;
    double bytes_per_second = 0.0;
    std::uint64_t iterations = 0U;
};

class BenchmarkContext final {
public:
    explicit BenchmarkContext(std::uint64_t iterations_);

    [[nodiscard]] std::uint64_t Iterations() const noexcept;

    void AddItems(std::uint64_t items_) noexcept;
    void AddBytes(std::uint64_t bytes_) noexcept;

    [[nodiscard]] std::uint64_t ItemsProcessed() const noexcept;
    [[nodiscard]] std::uint64_t BytesProcessed() const noexcept;

    template<typename FnT>
    void ForEachIteration(FnT&& function_) {
        const std::uint64_t count = iterations;
        for (std::uint64_t i = 0U; i < count; ++i) {
            function_(i);
        }
    }

    template<typename T>
    static void DoNotOptimize(const T& value_) noexcept {
        volatile const T* volatile sink = &value_;
        (void)sink;
    }

    static void ClobberMemory() noexcept;

private:
    std::uint64_t iterations = 0U;
    std::uint64_t items_processed = 0U;
    std::uint64_t bytes_processed = 0U;
};

class SkipBenchmark final : public std::exception {
public:
    explicit SkipBenchmark(std::string reason_);
    [[nodiscard]] const char* what() const noexcept override;

private:
    std::string reason{};
};

using BenchmarkFunction = void(*)(BenchmarkContext& bench_context_);

struct BenchmarkCaseDefinition {
    std::string name{};
    std::string tags{};
    BenchmarkFunction function = nullptr;
};

enum class BaselineMetricKind : std::uint8_t {
    mean_ns_per_iteration = 0U,
    mean_ms = 1U,
    items_per_second = 2U,
    bytes_per_second = 3U,
};

struct BenchmarkRunnerOptions {
    bool list_only = false;
    bool verbose = false;
    bool shuffle = false;
    bool stop_on_failure = false;
    std::string filter{};
    std::vector<std::string> include_tags{};
    std::vector<std::string> exclude_tags{};
    std::uint64_t iterations = 0U; // 0 = auto calibrate
    std::uint64_t min_calibrated_iterations = 8U;
    std::uint32_t warmup_runs = 2U;
    std::uint32_t measured_runs = 9U;
    std::uint32_t min_duration_ms = 25U;
    std::string baseline_json_path{};
    BaselineMetricKind baseline_metric = BaselineMetricKind::mean_ns_per_iteration;
    double fail_on_regression_ratio = -1.0; // < 0 disables regression gate
    bool require_baseline_match = false;
    bool fail_on_empty_selection = false;
    std::string report_json_path{};
    std::uint32_t suite_repetitions = 1U;
    std::uint64_t shuffle_seed = 0U;
};

enum class BaselineComparisonStatus : std::uint8_t {
    not_checked = 0U,
    missing_baseline = 1U,
    improved = 2U,
    within_threshold = 3U,
    regressed = 4U,
};

struct BaselineComparison {
    BaselineComparisonStatus status = BaselineComparisonStatus::not_checked;
    BaselineMetricKind metric_kind = BaselineMetricKind::mean_ns_per_iteration;
    bool has_baseline = false;
    double baseline_mean_ms = 0.0;
    double current_mean_ms = 0.0;
    double baseline_metric_value = 0.0;
    double current_metric_value = 0.0;
    double delta_ratio = 0.0; // (current - baseline) / baseline
};

struct BenchmarkCaseResult {
    std::string name{};
    std::vector<std::string> tags{};
    BenchmarkOutcome outcome = BenchmarkOutcome::completed;
    std::string message{};
    BenchmarkMetrics metrics{};
    BaselineComparison baseline{};
    std::vector<SampleMeasurement> samples{};
};

struct BenchmarkRunSummary {
    std::uint32_t selected_count = 0U;
    std::uint32_t executed_count = 0U;
    std::uint32_t completed_count = 0U;
    std::uint32_t failed_count = 0U;
    std::uint32_t skipped_count = 0U;
    std::uint32_t regression_fail_count = 0U;
    std::uint32_t baseline_missing_count = 0U;
    double total_duration_ms = 0.0;
    std::vector<BenchmarkCaseResult> results{};
};

class BenchmarkRegistry final {
public:
    static BenchmarkRegistry& Instance();

    bool Register(std::string_view name_,
                  std::string_view tags_,
                  BenchmarkFunction function_);

    [[nodiscard]] const std::vector<BenchmarkCaseDefinition>& Cases() const noexcept;

private:
    std::vector<BenchmarkCaseDefinition> cases{};
};

[[nodiscard]] bool RegisterBenchmarkCase(std::string_view name_,
                                         std::string_view tags_,
                                         BenchmarkFunction function_);

int RunAllBenchmarksMain(int argc_, char** argv_);

} // namespace vr::bench

#define VR_BENCH_STRINGIFY_IMPL(value_) #value_
#define VR_BENCH_STRINGIFY(value_) VR_BENCH_STRINGIFY_IMPL(value_)

#define VR_BENCH_CONCAT_IMPL(lhs_, rhs_) lhs_ ## rhs_
#define VR_BENCH_CONCAT(lhs_, rhs_) VR_BENCH_CONCAT_IMPL(lhs_, rhs_)

#define VR_BENCHMARK_CASE(bench_name_, tags_literal_)                                      \
    static void bench_name_(::vr::bench::BenchmarkContext& bench_context_);                \
    namespace {                                                                             \
    const bool VR_BENCH_CONCAT(bench_name_, _registered_) =                                \
        ::vr::bench::RegisterBenchmarkCase(VR_BENCH_STRINGIFY(bench_name_), tags_literal_, &bench_name_); \
    }                                                                                       \
    static void bench_name_(::vr::bench::BenchmarkContext& bench_context_)

#define VR_BENCH_SKIP(reason_)                                                              \
    do {                                                                                    \
        throw ::vr::bench::SkipBenchmark(reason_);                                          \
    } while (false)

