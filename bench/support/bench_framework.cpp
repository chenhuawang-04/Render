#include "support/bench_framework.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace vr::bench {

namespace {

struct BaselineSnapshot final {
    bool has_iterations = false;
    bool has_mean_ms = false;
    bool has_mean_ns_per_iteration = false;
    bool has_items_per_second = false;
    bool has_bytes_per_second = false;
    std::uint64_t iterations = 0U;
    double mean_ms = 0.0;
    double mean_ns_per_iteration = 0.0;
    double items_per_second = 0.0;
    double bytes_per_second = 0.0;
};

using BaselineSnapshotMap = std::unordered_map<std::string, BaselineSnapshot>;

struct ParsedArguments {
    bool ok = true;
    bool show_help = false;
    std::string error{};
    BenchmarkRunnerOptions options{};
};

[[nodiscard]] std::string ToLower(std::string_view value_) {
    std::string lowered{};
    lowered.reserve(value_.size());
    for (const unsigned char ch : value_) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

[[nodiscard]] std::string TrimCopy(std::string_view value_) {
    std::size_t begin = 0U;
    while (begin < value_.size() &&
           std::isspace(static_cast<unsigned char>(value_[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value_.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value_[end - 1U])) != 0) {
        --end;
    }

    return std::string(value_.substr(begin, end - begin));
}

[[nodiscard]] std::vector<std::string> SplitTags(std::string_view tags_) {
    std::vector<std::string> tokens{};
    std::string current{};
    current.reserve(tags_.size());

    const auto flush_current = [&]() {
        std::string token = TrimCopy(current);
        if (!token.empty()) {
            tokens.push_back(ToLower(token));
        }
        current.clear();
    };

    for (const char ch : tags_) {
        const bool separator = (ch == ',') || (ch == ';') ||
            (std::isspace(static_cast<unsigned char>(ch)) != 0);
        if (separator) {
            flush_current();
            continue;
        }
        current.push_back(ch);
    }
    flush_current();

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

[[nodiscard]] bool ContainsTag(const std::vector<std::string>& tags_,
                               std::string_view expected_) {
    const std::string lowered = ToLower(expected_);
    return std::find(tags_.begin(), tags_.end(), lowered) != tags_.end();
}

[[nodiscard]] bool GlobMatchCaseInsensitive(std::string_view pattern_,
                                            std::string_view text_) {
    const std::string pattern = ToLower(pattern_);
    const std::string text = ToLower(text_);

    std::size_t pattern_index = 0U;
    std::size_t text_index = 0U;
    std::size_t star_pattern_index = std::string::npos;
    std::size_t star_text_index = std::string::npos;

    while (text_index < text.size()) {
        const bool direct_match =
            (pattern_index < pattern.size()) &&
            ((pattern[pattern_index] == '?') ||
             (pattern[pattern_index] == text[text_index]));

        if (direct_match) {
            ++pattern_index;
            ++text_index;
            continue;
        }

        if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_pattern_index = pattern_index;
            ++pattern_index;
            star_text_index = text_index;
            continue;
        }

        if (star_pattern_index != std::string::npos) {
            pattern_index = star_pattern_index + 1U;
            ++star_text_index;
            text_index = star_text_index;
            continue;
        }

        return false;
    }

    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

[[nodiscard]] bool MatchFilter(std::string_view name_, std::string_view filter_) {
    if (filter_.empty()) {
        return true;
    }

    if (filter_.find('*') != std::string_view::npos ||
        filter_.find('?') != std::string_view::npos) {
        return GlobMatchCaseInsensitive(filter_, name_);
    }

    const std::string lowered_name = ToLower(name_);
    const std::string lowered_filter = ToLower(filter_);
    return lowered_name.find(lowered_filter) != std::string::npos;
}

[[nodiscard]] bool ParseUnsigned64(std::string_view value_, std::uint64_t& out_value_) {
    if (value_.empty()) {
        return false;
    }

    std::uint64_t accumulator = 0U;
    for (const char ch : value_) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (accumulator > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return false;
        }
        accumulator = accumulator * 10U + digit;
    }
    out_value_ = accumulator;
    return true;
}

[[nodiscard]] bool ParseUnsigned32(std::string_view value_, std::uint32_t& out_value_) {
    std::uint64_t parsed = 0U;
    if (!ParseUnsigned64(value_, parsed) ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    out_value_ = static_cast<std::uint32_t>(parsed);
    return true;
}

[[nodiscard]] bool ParseDouble(std::string_view value_, double& out_value_) {
    if (value_.empty()) {
        return false;
    }

    std::string temp(value_);
    char* end_ptr = nullptr;
    const double parsed = std::strtod(temp.c_str(), &end_ptr);
    if (end_ptr == nullptr || *end_ptr != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    out_value_ = parsed;
    return true;
}

[[nodiscard]] std::string BaselineMetricKindString(BaselineMetricKind metric_kind_) {
    switch (metric_kind_) {
        case BaselineMetricKind::mean_ns_per_iteration:
            return "mean_ns_per_iteration";
        case BaselineMetricKind::mean_ms:
            return "mean_ms";
        case BaselineMetricKind::items_per_second:
            return "items_per_second";
        case BaselineMetricKind::bytes_per_second:
            return "bytes_per_second";
    }
    return "unknown";
}

[[nodiscard]] std::optional<BaselineMetricKind> ParseBaselineMetricKind(std::string_view value_) {
    const std::string lowered = ToLower(TrimCopy(value_));
    if (lowered == "mean_ns_per_iteration" || lowered == "ns_per_iter" || lowered == "ns_per_iteration") {
        return BaselineMetricKind::mean_ns_per_iteration;
    }
    if (lowered == "mean_ms" || lowered == "ms") {
        return BaselineMetricKind::mean_ms;
    }
    if (lowered == "items_per_second" || lowered == "items/s" || lowered == "itemsps") {
        return BaselineMetricKind::items_per_second;
    }
    if (lowered == "bytes_per_second" || lowered == "bytes/s" || lowered == "bytesps") {
        return BaselineMetricKind::bytes_per_second;
    }
    return std::nullopt;
}

[[nodiscard]] bool IsLowerBetterMetric(BaselineMetricKind metric_kind_) noexcept {
    switch (metric_kind_) {
        case BaselineMetricKind::mean_ns_per_iteration:
        case BaselineMetricKind::mean_ms:
            return true;
        case BaselineMetricKind::items_per_second:
        case BaselineMetricKind::bytes_per_second:
            return false;
    }
    return true;
}

[[nodiscard]] ParsedArguments ParseArguments(int argc_, char** argv_) {
    ParsedArguments parsed{};

    for (int i = 1; i < argc_; ++i) {
        const std::string_view arg = argv_[i];
        if (arg == "-h" || arg == "--help") {
            parsed.show_help = true;
            continue;
        }
        if (arg == "--list") {
            parsed.options.list_only = true;
            continue;
        }
        if (arg == "--verbose") {
            parsed.options.verbose = true;
            continue;
        }
        if (arg == "--require-baseline-match") {
            parsed.options.require_baseline_match = true;
            continue;
        }
        if (arg == "--fail-on-empty-selection") {
            parsed.options.fail_on_empty_selection = true;
            continue;
        }

        const auto require_value = [&](std::string_view option_name_) -> std::optional<std::string_view> {
            if (i + 1 >= argc_) {
                parsed.ok = false;
                parsed.error = std::string("Missing value for option: ") + std::string(option_name_);
                return std::nullopt;
            }
            ++i;
            return std::string_view(argv_[i]);
        };

        if (arg == "--filter") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            parsed.options.filter = std::string(value.value());
            continue;
        }
        if (arg == "--include-tag") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            parsed.options.include_tags.push_back(ToLower(TrimCopy(value.value())));
            continue;
        }
        if (arg == "--exclude-tag") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            parsed.options.exclude_tags.push_back(ToLower(TrimCopy(value.value())));
            continue;
        }
        if (arg == "--iterations") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            std::uint64_t parsed_value = 0U;
            if (!ParseUnsigned64(value.value(), parsed_value)) {
                parsed.ok = false;
                parsed.error = "Invalid integer for --iterations";
                return parsed;
            }
            parsed.options.iterations = parsed_value;
            continue;
        }
        if (arg == "--min-iterations") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            std::uint64_t parsed_value = 0U;
            if (!ParseUnsigned64(value.value(), parsed_value) || parsed_value == 0U) {
                parsed.ok = false;
                parsed.error = "--min-iterations must be >= 1";
                return parsed;
            }
            parsed.options.min_calibrated_iterations = parsed_value;
            continue;
        }
        if (arg == "--warmup") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            std::uint32_t parsed_value = 0U;
            if (!ParseUnsigned32(value.value(), parsed_value)) {
                parsed.ok = false;
                parsed.error = "Invalid integer for --warmup";
                return parsed;
            }
            parsed.options.warmup_runs = parsed_value;
            continue;
        }
        if (arg == "--runs") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            std::uint32_t parsed_value = 0U;
            if (!ParseUnsigned32(value.value(), parsed_value) || parsed_value == 0U) {
                parsed.ok = false;
                parsed.error = "--runs must be >= 1";
                return parsed;
            }
            parsed.options.measured_runs = parsed_value;
            continue;
        }
        if (arg == "--min-duration-ms") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            std::uint32_t parsed_value = 0U;
            if (!ParseUnsigned32(value.value(), parsed_value) || parsed_value == 0U) {
                parsed.ok = false;
                parsed.error = "--min-duration-ms must be >= 1";
                return parsed;
            }
            parsed.options.min_duration_ms = parsed_value;
            continue;
        }
        if (arg == "--baseline-json") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            parsed.options.baseline_json_path = std::string(value.value());
            continue;
        }
        if (arg == "--baseline-metric") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            const auto parsed_metric = ParseBaselineMetricKind(value.value());
            if (!parsed_metric.has_value()) {
                parsed.ok = false;
                parsed.error =
                    "--baseline-metric must be one of: mean_ns_per_iteration, mean_ms, items_per_second, bytes_per_second";
                return parsed;
            }
            parsed.options.baseline_metric = parsed_metric.value();
            continue;
        }
        if (arg == "--fail-on-regression-percent") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            double parsed_percent = 0.0;
            if (!ParseDouble(value.value(), parsed_percent) || parsed_percent < 0.0) {
                parsed.ok = false;
                parsed.error = "--fail-on-regression-percent must be a non-negative number";
                return parsed;
            }
            parsed.options.fail_on_regression_ratio = parsed_percent / 100.0;
            continue;
        }
        if (arg == "--report-json") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            parsed.options.report_json_path = std::string(value.value());
            continue;
        }

        parsed.ok = false;
        parsed.error = std::string("Unknown option: ") + std::string(arg);
        return parsed;
    }

    if (parsed.options.require_baseline_match &&
        parsed.options.baseline_json_path.empty()) {
        parsed.ok = false;
        parsed.error = "--require-baseline-match requires --baseline-json";
        return parsed;
    }

    return parsed;
}

void PrintHelp() {
    std::cout
        << "vr_bench command line options:\n"
        << "  --help, -h                        Show this help message\n"
        << "  --list                            List selected benchmark cases\n"
        << "  --filter <pattern>                Name filter (substring or glob with * ?)\n"
        << "  --include-tag <tag>               Include only benchmarks with this tag (repeatable)\n"
        << "  --exclude-tag <tag>               Exclude benchmarks with this tag (repeatable)\n"
        << "  --iterations <n>                  Fixed iterations per run (0 = auto calibrate)\n"
        << "  --min-iterations <n>              Lower bound for auto-calibrated iterations (default: 8)\n"
        << "  --warmup <n>                      Warmup run count (default: 2)\n"
        << "  --runs <n>                        Measured run count (default: 9)\n"
        << "  --min-duration-ms <n>             Target minimum runtime for auto calibration\n"
        << "  --baseline-json <path>            Baseline JSON report generated by vr_bench\n"
        << "  --baseline-metric <name>          Regression metric (default: mean_ns_per_iteration)\n"
        << "                                   Supported: mean_ns_per_iteration, mean_ms, items_per_second, bytes_per_second\n"
        << "  --fail-on-regression-percent <n>  Mark benchmark as FAIL when selected baseline metric regresses above n%\n"
        << "  --require-baseline-match          Treat missing baseline entries as FAIL\n"
        << "  --fail-on-empty-selection         Return non-zero when selection is empty\n"
        << "  --report-json <path>              Write machine-readable JSON report\n"
        << "  --verbose                         Emit per-sample details\n";
}

[[nodiscard]] bool IsCaseSelected(const BenchmarkCaseDefinition& definition_,
                                  const BenchmarkRunnerOptions& options_,
                                  std::vector<std::string>& out_tags_) {
    out_tags_ = SplitTags(definition_.tags);
    if (!MatchFilter(definition_.name, options_.filter)) {
        return false;
    }

    for (const auto& include_tag : options_.include_tags) {
        if (!ContainsTag(out_tags_, include_tag)) {
            return false;
        }
    }
    for (const auto& exclude_tag : options_.exclude_tags) {
        if (ContainsTag(out_tags_, exclude_tag)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] SampleMeasurement ExecuteSample(const BenchmarkCaseDefinition& definition_,
                                              std::uint64_t iterations_) {
    BenchmarkContext bench_context(iterations_);
    const auto start = std::chrono::steady_clock::now();
    definition_.function(bench_context);
    const auto end = std::chrono::steady_clock::now();

    SampleMeasurement sample{};
    sample.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
    sample.iterations = iterations_;
    sample.items_processed = bench_context.ItemsProcessed();
    sample.bytes_processed = bench_context.BytesProcessed();
    return sample;
}

[[nodiscard]] std::uint64_t CalibrateIterations(const BenchmarkCaseDefinition& definition_,
                                                const BenchmarkRunnerOptions& options_) {
    if (options_.iterations > 0U) {
        return options_.iterations;
    }

    constexpr std::uint64_t kMaxIterations = 1ULL << 34U;
    const std::uint64_t min_auto_iterations = std::max<std::uint64_t>(1U, options_.min_calibrated_iterations);
    std::uint64_t iterations = 1U;
    const double target_duration_ms = static_cast<double>(options_.min_duration_ms);

    for (std::uint32_t attempt = 0U; attempt < 12U; ++attempt) {
        const SampleMeasurement sample = ExecuteSample(definition_, iterations);
        const double measured_ms = std::max(sample.duration_ms, 0.001);
        if (measured_ms >= target_duration_ms) {
            return std::max(iterations, min_auto_iterations);
        }

        const double scale = std::max(target_duration_ms / measured_ms, 1.6);
        std::uint64_t next_iterations = static_cast<std::uint64_t>(std::ceil(iterations * scale));
        if (next_iterations <= iterations) {
            next_iterations = iterations + 1U;
        }
        if (next_iterations > kMaxIterations) {
            return kMaxIterations;
        }
        iterations = next_iterations;
    }

    return std::max(iterations, min_auto_iterations);
}

[[nodiscard]] double ComputeMean(const std::vector<double>& values_) {
    if (values_.empty()) {
        return 0.0;
    }
    const double sum = std::accumulate(values_.begin(), values_.end(), 0.0);
    return sum / static_cast<double>(values_.size());
}

[[nodiscard]] double ComputeMedian(std::vector<double> values_) {
    if (values_.empty()) {
        return 0.0;
    }
    std::sort(values_.begin(), values_.end());
    const std::size_t count = values_.size();
    if ((count % 2U) == 1U) {
        return values_[count / 2U];
    }
    return (values_[count / 2U - 1U] + values_[count / 2U]) * 0.5;
}

[[nodiscard]] double ComputePercentile95(std::vector<double> values_) {
    if (values_.empty()) {
        return 0.0;
    }
    std::sort(values_.begin(), values_.end());
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(values_.size()))) - 1U;
    return values_[std::min(index, values_.size() - 1U)];
}

[[nodiscard]] double ComputeStddev(const std::vector<double>& values_,
                                   double mean_) {
    if (values_.empty()) {
        return 0.0;
    }
    double variance = 0.0;
    for (const double value : values_) {
        const double delta = value - mean_;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values_.size());
    return std::sqrt(variance);
}

[[nodiscard]] BenchmarkMetrics ComputeMetrics(const std::vector<SampleMeasurement>& samples_,
                                              std::uint64_t iterations_) {
    BenchmarkMetrics metrics{};
    metrics.iterations = iterations_;
    if (samples_.empty()) {
        return metrics;
    }

    std::vector<double> durations{};
    durations.reserve(samples_.size());
    double total_duration_ms = 0.0;
    std::uint64_t total_items = 0U;
    std::uint64_t total_bytes = 0U;
    for (const auto& sample : samples_) {
        durations.push_back(sample.duration_ms);
        total_duration_ms += sample.duration_ms;
        total_items += sample.items_processed;
        total_bytes += sample.bytes_processed;
    }

    auto min_max = std::minmax_element(durations.begin(), durations.end());
    metrics.min_ms = *min_max.first;
    metrics.max_ms = *min_max.second;
    metrics.mean_ms = ComputeMean(durations);
    metrics.median_ms = ComputeMedian(durations);
    metrics.p95_ms = ComputePercentile95(durations);
    metrics.stddev_ms = ComputeStddev(durations, metrics.mean_ms);

    if (iterations_ > 0U) {
        constexpr double kNsPerMs = 1'000'000.0;
        const double iteration_count = static_cast<double>(iterations_);
        metrics.min_ns_per_iteration = (metrics.min_ms * kNsPerMs) / iteration_count;
        metrics.max_ns_per_iteration = (metrics.max_ms * kNsPerMs) / iteration_count;
        metrics.mean_ns_per_iteration = (metrics.mean_ms * kNsPerMs) / iteration_count;
        metrics.median_ns_per_iteration = (metrics.median_ms * kNsPerMs) / iteration_count;
        metrics.p95_ns_per_iteration = (metrics.p95_ms * kNsPerMs) / iteration_count;
        metrics.stddev_ns_per_iteration = (metrics.stddev_ms * kNsPerMs) / iteration_count;
    }

    const double total_seconds = total_duration_ms / 1000.0;
    if (total_seconds > 0.0) {
        metrics.items_per_second = static_cast<double>(total_items) / total_seconds;
        metrics.bytes_per_second = static_cast<double>(total_bytes) / total_seconds;
    }
    return metrics;
}

[[nodiscard]] std::string EscapeJson(std::string_view value_) {
    std::string escaped{};
    escaped.reserve(value_.size() + 8U);
    for (const char ch : value_) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: {
                const unsigned char code = static_cast<unsigned char>(ch);
                if (code < 0x20U) {
                    std::ostringstream unicode_escape;
                    unicode_escape << "\\u"
                                   << std::hex
                                   << std::setw(4)
                                   << std::setfill('0')
                                   << static_cast<int>(code);
                    escaped += unicode_escape.str();
                } else {
                    escaped.push_back(ch);
                }
                break;
            }
        }
    }
    return escaped;
}

[[nodiscard]] std::optional<double> ParseJsonNumberNear(std::string_view content_,
                                                        std::size_t offset_) {
    const std::size_t begin = content_.find_first_of("+-0123456789.", offset_);
    if (begin == std::string_view::npos) {
        return std::nullopt;
    }

    std::size_t end = begin;
    while (end < content_.size()) {
        const char ch = content_[end];
        const bool valid = (ch >= '0' && ch <= '9') ||
            ch == '.' || ch == '+' || ch == '-' || ch == 'e' || ch == 'E';
        if (!valid) {
            break;
        }
        ++end;
    }

    std::string token(content_.substr(begin, end - begin));
    char* end_ptr = nullptr;
    const double parsed = std::strtod(token.c_str(), &end_ptr);
    if (end_ptr == nullptr || *end_ptr != '\0' || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::optional<std::string> ParseJsonStringNear(std::string_view content_,
                                                             std::size_t offset_) {
    const std::size_t first_quote = content_.find('"', offset_);
    if (first_quote == std::string_view::npos) {
        return std::nullopt;
    }

    std::string parsed{};
    parsed.reserve(32U);

    bool escaped = false;
    for (std::size_t i = first_quote + 1U; i < content_.size(); ++i) {
        const char ch = content_[i];
        if (escaped) {
            switch (ch) {
                case '"': parsed.push_back('"'); break;
                case '\\': parsed.push_back('\\'); break;
                case '/': parsed.push_back('/'); break;
                case 'b': parsed.push_back('\b'); break;
                case 'f': parsed.push_back('\f'); break;
                case 'n': parsed.push_back('\n'); break;
                case 'r': parsed.push_back('\r'); break;
                case 't': parsed.push_back('\t'); break;
                default:
                    // Baseline JSON is produced by this runner; keep unknown escapes as-is.
                    parsed.push_back(ch);
                    break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return parsed;
        }
        parsed.push_back(ch);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> FindMatchingDelimiter(std::string_view content_,
                                                               std::size_t open_index_,
                                                               char open_delimiter_,
                                                               char close_delimiter_) {
    if (open_index_ >= content_.size() || content_[open_index_] != open_delimiter_) {
        return std::nullopt;
    }

    std::uint32_t depth = 0U;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = open_index_; i < content_.size(); ++i) {
        const char ch = content_[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == open_delimiter_) {
            ++depth;
            continue;
        }
        if (ch == close_delimiter_) {
            if (depth == 0U) {
                return std::nullopt;
            }
            --depth;
            if (depth == 0U) {
                return i;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> ParseJsonNumberField(std::string_view object_text_,
                                                         std::string_view field_name_) {
    const std::string key = "\"" + std::string(field_name_) + "\"";
    const std::size_t key_offset = object_text_.find(key);
    if (key_offset == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon_offset = object_text_.find(':', key_offset + key.size());
    if (colon_offset == std::string::npos) {
        return std::nullopt;
    }
    return ParseJsonNumberNear(object_text_, colon_offset + 1U);
}

[[nodiscard]] bool TryParseJsonUnsigned64Field(std::string_view object_text_,
                                               std::string_view field_name_,
                                               std::uint64_t& out_value_) {
    const std::optional<double> parsed_value =
        ParseJsonNumberField(object_text_, field_name_);
    if (!parsed_value.has_value()) {
        return false;
    }
    if (!std::isfinite(parsed_value.value()) ||
        parsed_value.value() < 0.0 ||
        parsed_value.value() > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    const double rounded = std::floor(parsed_value.value() + 0.5);
    if (std::abs(parsed_value.value() - rounded) > 1e-6) {
        return false;
    }
    out_value_ = static_cast<std::uint64_t>(rounded);
    return true;
}

[[nodiscard]] std::optional<std::string_view> ExtractJsonObjectField(std::string_view object_text_,
                                                                     std::string_view field_name_) {
    const std::string key = "\"" + std::string(field_name_) + "\"";
    const std::size_t key_offset = object_text_.find(key);
    if (key_offset == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon_offset = object_text_.find(':', key_offset + key.size());
    if (colon_offset == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t object_begin = object_text_.find('{', colon_offset + 1U);
    if (object_begin == std::string::npos) {
        return std::nullopt;
    }
    const std::optional<std::size_t> object_end =
        FindMatchingDelimiter(object_text_, object_begin, '{', '}');
    if (!object_end.has_value()) {
        return std::nullopt;
    }
    return object_text_.substr(object_begin, object_end.value() - object_begin + 1U);
}

[[nodiscard]] BaselineSnapshot ParseBaselineSnapshot(std::string_view metrics_object_text_) {
    BaselineSnapshot snapshot{};
    if (TryParseJsonUnsigned64Field(metrics_object_text_, "iterations", snapshot.iterations)) {
        snapshot.has_iterations = true;
    }

    const std::optional<double> mean_ms =
        ParseJsonNumberField(metrics_object_text_, "mean_ms");
    if (mean_ms.has_value() && std::isfinite(mean_ms.value())) {
        snapshot.has_mean_ms = true;
        snapshot.mean_ms = mean_ms.value();
    }

    const std::optional<double> mean_ns_per_iteration =
        ParseJsonNumberField(metrics_object_text_, "mean_ns_per_iteration");
    if (mean_ns_per_iteration.has_value() &&
        std::isfinite(mean_ns_per_iteration.value())) {
        snapshot.has_mean_ns_per_iteration = true;
        snapshot.mean_ns_per_iteration = mean_ns_per_iteration.value();
    } else if (snapshot.has_mean_ms && snapshot.has_iterations && snapshot.iterations > 0U) {
        snapshot.has_mean_ns_per_iteration = true;
        snapshot.mean_ns_per_iteration =
            (snapshot.mean_ms * 1'000'000.0) /
            static_cast<double>(snapshot.iterations);
    }

    const std::optional<double> items_per_second =
        ParseJsonNumberField(metrics_object_text_, "items_per_second");
    if (items_per_second.has_value() && std::isfinite(items_per_second.value())) {
        snapshot.has_items_per_second = true;
        snapshot.items_per_second = items_per_second.value();
    }

    const std::optional<double> bytes_per_second =
        ParseJsonNumberField(metrics_object_text_, "bytes_per_second");
    if (bytes_per_second.has_value() && std::isfinite(bytes_per_second.value())) {
        snapshot.has_bytes_per_second = true;
        snapshot.bytes_per_second = bytes_per_second.value();
    }
    return snapshot;
}

[[nodiscard]] BaselineSnapshotMap LoadBaselineSnapshotMap(const std::string& path_) {
    std::ifstream in(path_, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open baseline file: " + path_);
    }

    std::string content{};
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size > 0) {
        content.resize(static_cast<std::size_t>(size));
        in.seekg(0, std::ios::beg);
        in.read(content.data(), static_cast<std::streamsize>(content.size()));
    }
    in.close();

    BaselineSnapshotMap baseline_snapshots{};
    const std::size_t results_key = content.find("\"results\"");
    if (results_key == std::string::npos) {
        return baseline_snapshots;
    }

    const std::size_t results_colon = content.find(':', results_key);
    if (results_colon == std::string::npos) {
        return baseline_snapshots;
    }

    const std::size_t results_array_begin = content.find('[', results_colon + 1U);
    if (results_array_begin == std::string::npos) {
        return baseline_snapshots;
    }
    const std::optional<std::size_t> results_array_end =
        FindMatchingDelimiter(content, results_array_begin, '[', ']');
    if (!results_array_end.has_value()) {
        return baseline_snapshots;
    }

    std::size_t cursor = results_array_begin + 1U;
    while (cursor < results_array_end.value()) {
        const std::size_t result_object_begin = content.find('{', cursor);
        if (result_object_begin == std::string::npos ||
            result_object_begin >= results_array_end.value()) {
            break;
        }
        const std::optional<std::size_t> result_object_end =
            FindMatchingDelimiter(content, result_object_begin, '{', '}');
        if (!result_object_end.has_value() ||
            result_object_end.value() > results_array_end.value()) {
            break;
        }

        const std::string_view result_object =
            std::string_view(content).substr(result_object_begin,
                                             result_object_end.value() - result_object_begin + 1U);

        const std::string name_key = "\"name\"";
        const std::size_t name_offset = result_object.find(name_key);
        if (name_offset != std::string::npos) {
            const std::size_t name_colon = result_object.find(':', name_offset + name_key.size());
            if (name_colon != std::string::npos) {
                const std::optional<std::string> name =
                    ParseJsonStringNear(result_object, name_colon + 1U);
                if (name.has_value()) {
                    const std::optional<std::string_view> metrics_object =
                        ExtractJsonObjectField(result_object, "metrics");
                    if (metrics_object.has_value()) {
                        baseline_snapshots[name.value()] =
                            ParseBaselineSnapshot(metrics_object.value());
                    }
                }
            }
        }

        cursor = result_object_end.value() + 1U;
    }

    return baseline_snapshots;
}

void AppendMessage(std::string& message_, std::string_view appended_) {
    if (!message_.empty()) {
        message_ += "; ";
    }
    message_ += appended_;
}

void CompareCaseWithBaseline(const BenchmarkRunnerOptions& options_,
                             const BaselineSnapshotMap& baseline_snapshots_,
                             BenchmarkCaseResult& case_result_,
                             BenchmarkRunSummary& summary_) {
    if (case_result_.outcome != BenchmarkOutcome::completed) {
        return;
    }

    case_result_.baseline.metric_kind = options_.baseline_metric;

    auto iter = baseline_snapshots_.find(case_result_.name);
    if (iter == baseline_snapshots_.end()) {
        case_result_.baseline.status = BaselineComparisonStatus::missing_baseline;
        case_result_.baseline.has_baseline = false;
        ++summary_.baseline_missing_count;
        if (options_.require_baseline_match) {
            case_result_.outcome = BenchmarkOutcome::failed;
            AppendMessage(case_result_.message, "baseline entry is missing");
        }
        return;
    }

    const BaselineSnapshot& snapshot = iter->second;
    const double baseline_mean_ms = snapshot.has_mean_ms ? snapshot.mean_ms : 0.0;
    case_result_.baseline.has_baseline = true;
    case_result_.baseline.baseline_mean_ms = baseline_mean_ms;
    case_result_.baseline.current_mean_ms = case_result_.metrics.mean_ms;
    case_result_.baseline.current_metric_value = 0.0;
    case_result_.baseline.baseline_metric_value = 0.0;

    const auto get_baseline_metric_value = [&](BaselineMetricKind metric_kind_) -> std::optional<double> {
        switch (metric_kind_) {
            case BaselineMetricKind::mean_ns_per_iteration:
                if (snapshot.has_mean_ns_per_iteration) {
                    return snapshot.mean_ns_per_iteration;
                }
                return std::nullopt;
            case BaselineMetricKind::mean_ms:
                if (snapshot.has_mean_ms) {
                    return snapshot.mean_ms;
                }
                return std::nullopt;
            case BaselineMetricKind::items_per_second:
                if (snapshot.has_items_per_second) {
                    return snapshot.items_per_second;
                }
                return std::nullopt;
            case BaselineMetricKind::bytes_per_second:
                if (snapshot.has_bytes_per_second) {
                    return snapshot.bytes_per_second;
                }
                return std::nullopt;
        }
        return std::nullopt;
    };

    const auto get_current_metric_value = [&](BaselineMetricKind metric_kind_) -> std::optional<double> {
        switch (metric_kind_) {
            case BaselineMetricKind::mean_ns_per_iteration:
                if (case_result_.metrics.mean_ns_per_iteration > 0.0) {
                    return case_result_.metrics.mean_ns_per_iteration;
                }
                return std::nullopt;
            case BaselineMetricKind::mean_ms:
                if (case_result_.metrics.mean_ms > 0.0) {
                    return case_result_.metrics.mean_ms;
                }
                return std::nullopt;
            case BaselineMetricKind::items_per_second:
                if (case_result_.metrics.items_per_second > 0.0) {
                    return case_result_.metrics.items_per_second;
                }
                return std::nullopt;
            case BaselineMetricKind::bytes_per_second:
                if (case_result_.metrics.bytes_per_second > 0.0) {
                    return case_result_.metrics.bytes_per_second;
                }
                return std::nullopt;
        }
        return std::nullopt;
    };

    const std::optional<double> baseline_metric_value =
        get_baseline_metric_value(options_.baseline_metric);
    const std::optional<double> current_metric_value =
        get_current_metric_value(options_.baseline_metric);
    if (!baseline_metric_value.has_value()) {
        case_result_.baseline.status = BaselineComparisonStatus::missing_baseline;
        ++summary_.baseline_missing_count;
        if (options_.require_baseline_match) {
            case_result_.outcome = BenchmarkOutcome::failed;
            AppendMessage(case_result_.message,
                          "baseline metric is missing: " + BaselineMetricKindString(options_.baseline_metric));
        }
        return;
    }

    if (!std::isfinite(baseline_metric_value.value()) ||
        !(baseline_metric_value.value() > 0.0)) {
        case_result_.baseline.status = BaselineComparisonStatus::missing_baseline;
        ++summary_.baseline_missing_count;
        if (options_.require_baseline_match) {
            case_result_.outcome = BenchmarkOutcome::failed;
            AppendMessage(case_result_.message,
                          "baseline metric is invalid: " + BaselineMetricKindString(options_.baseline_metric));
        }
        return;
    }

    if (!current_metric_value.has_value() ||
        !std::isfinite(current_metric_value.value()) ||
        !(current_metric_value.value() > 0.0)) {
        case_result_.baseline.status = BaselineComparisonStatus::regressed;
        ++summary_.regression_fail_count;
        case_result_.outcome = BenchmarkOutcome::failed;
        AppendMessage(case_result_.message,
                      "current metric is invalid: " + BaselineMetricKindString(options_.baseline_metric));
        return;
    }

    case_result_.baseline.baseline_metric_value = baseline_metric_value.value();
    case_result_.baseline.current_metric_value = current_metric_value.value();
    const double delta_ratio = IsLowerBetterMetric(options_.baseline_metric)
                                   ? ((current_metric_value.value() - baseline_metric_value.value()) /
                                      baseline_metric_value.value())
                                   : ((baseline_metric_value.value() - current_metric_value.value()) /
                                      baseline_metric_value.value());
    case_result_.baseline.delta_ratio = delta_ratio;

    if (delta_ratio < 0.0) {
        case_result_.baseline.status = BaselineComparisonStatus::improved;
        return;
    }

    const bool regression_gate_enabled = options_.fail_on_regression_ratio >= 0.0;
    if (regression_gate_enabled && delta_ratio > options_.fail_on_regression_ratio) {
        case_result_.baseline.status = BaselineComparisonStatus::regressed;
        ++summary_.regression_fail_count;

        std::ostringstream oss;
        oss << BaselineMetricKindString(options_.baseline_metric)
            << " regressed by " << std::fixed << std::setprecision(2)
            << (delta_ratio * 100.0) << "%"
            << " (baseline=" << case_result_.baseline.baseline_metric_value
            << ", current=" << case_result_.baseline.current_metric_value << ")";
        AppendMessage(case_result_.message, oss.str());
        case_result_.outcome = BenchmarkOutcome::failed;
        return;
    }

    case_result_.baseline.status = BaselineComparisonStatus::within_threshold;
}

[[nodiscard]] const char* BaselineStatusString(BaselineComparisonStatus status_) {
    switch (status_) {
        case BaselineComparisonStatus::not_checked: return "not_checked";
        case BaselineComparisonStatus::missing_baseline: return "missing_baseline";
        case BaselineComparisonStatus::improved: return "improved";
        case BaselineComparisonStatus::within_threshold: return "within_threshold";
        case BaselineComparisonStatus::regressed: return "regressed";
    }
    return "unknown";
}

void WriteJsonReport(const std::string& path_,
                     const BenchmarkRunnerOptions& options_,
                     const BenchmarkRunSummary& summary_) {
    std::filesystem::path report_path(path_);
    if (report_path.has_parent_path()) {
        std::filesystem::create_directories(report_path.parent_path());
    }

    std::ofstream out(path_, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open JSON report path: " + path_);
    }

    out << "{\n";
    out << "  \"options\": {\n";
    out << "    \"filter\": \"" << EscapeJson(options_.filter) << "\",\n";
    out << "    \"iterations\": " << options_.iterations << ",\n";
    out << "    \"min_calibrated_iterations\": " << options_.min_calibrated_iterations << ",\n";
    out << "    \"warmup_runs\": " << options_.warmup_runs << ",\n";
    out << "    \"measured_runs\": " << options_.measured_runs << ",\n";
    out << "    \"min_duration_ms\": " << options_.min_duration_ms << ",\n";
    out << "    \"baseline_json\": \"" << EscapeJson(options_.baseline_json_path) << "\",\n";
    out << "    \"baseline_metric\": \"" << BaselineMetricKindString(options_.baseline_metric) << "\",\n";
    out << "    \"fail_on_regression_ratio\": " << std::fixed << std::setprecision(6)
        << options_.fail_on_regression_ratio << ",\n";
    out << "    \"require_baseline_match\": " << (options_.require_baseline_match ? "true" : "false") << ",\n";
    out << "    \"fail_on_empty_selection\": " << (options_.fail_on_empty_selection ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"summary\": {\n";
    out << "    \"selected_count\": " << summary_.selected_count << ",\n";
    out << "    \"executed_count\": " << summary_.executed_count << ",\n";
    out << "    \"completed_count\": " << summary_.completed_count << ",\n";
    out << "    \"failed_count\": " << summary_.failed_count << ",\n";
    out << "    \"skipped_count\": " << summary_.skipped_count << ",\n";
    out << "    \"regression_fail_count\": " << summary_.regression_fail_count << ",\n";
    out << "    \"baseline_missing_count\": " << summary_.baseline_missing_count << ",\n";
    out << "    \"total_duration_ms\": " << std::fixed << std::setprecision(3) << summary_.total_duration_ms << "\n";
    out << "  },\n";
    out << "  \"results\": [\n";

    for (std::size_t i = 0U; i < summary_.results.size(); ++i) {
        const BenchmarkCaseResult& result = summary_.results[i];
        out << "    {\n";
        out << "      \"name\": \"" << EscapeJson(result.name) << "\",\n";
        out << "      \"outcome\": \""
            << (result.outcome == BenchmarkOutcome::completed
                    ? "completed"
                    : (result.outcome == BenchmarkOutcome::skipped ? "skipped" : "failed"))
            << "\",\n";
        out << "      \"message\": \"" << EscapeJson(result.message) << "\",\n";
        out << "      \"metrics\": {\n";
        out << "        \"iterations\": " << result.metrics.iterations << ",\n";
        out << "        \"min_ms\": " << std::fixed << std::setprecision(6) << result.metrics.min_ms << ",\n";
        out << "        \"max_ms\": " << std::fixed << std::setprecision(6) << result.metrics.max_ms << ",\n";
        out << "        \"mean_ms\": " << std::fixed << std::setprecision(6) << result.metrics.mean_ms << ",\n";
        out << "        \"median_ms\": " << std::fixed << std::setprecision(6) << result.metrics.median_ms << ",\n";
        out << "        \"p95_ms\": " << std::fixed << std::setprecision(6) << result.metrics.p95_ms << ",\n";
        out << "        \"stddev_ms\": " << std::fixed << std::setprecision(6) << result.metrics.stddev_ms << ",\n";
        out << "        \"min_ns_per_iteration\": " << std::fixed << std::setprecision(6) << result.metrics.min_ns_per_iteration << ",\n";
        out << "        \"max_ns_per_iteration\": " << std::fixed << std::setprecision(6) << result.metrics.max_ns_per_iteration << ",\n";
        out << "        \"mean_ns_per_iteration\": " << std::fixed << std::setprecision(6) << result.metrics.mean_ns_per_iteration << ",\n";
        out << "        \"median_ns_per_iteration\": " << std::fixed << std::setprecision(6) << result.metrics.median_ns_per_iteration << ",\n";
        out << "        \"p95_ns_per_iteration\": " << std::fixed << std::setprecision(6) << result.metrics.p95_ns_per_iteration << ",\n";
        out << "        \"stddev_ns_per_iteration\": " << std::fixed << std::setprecision(6) << result.metrics.stddev_ns_per_iteration << ",\n";
        out << "        \"items_per_second\": " << std::fixed << std::setprecision(3) << result.metrics.items_per_second << ",\n";
        out << "        \"bytes_per_second\": " << std::fixed << std::setprecision(3) << result.metrics.bytes_per_second << "\n";
        out << "      },\n";
        out << "      \"baseline\": {\n";
        out << "        \"status\": \"" << BaselineStatusString(result.baseline.status) << "\",\n";
        out << "        \"metric_kind\": \"" << BaselineMetricKindString(result.baseline.metric_kind) << "\",\n";
        out << "        \"has_baseline\": " << (result.baseline.has_baseline ? "true" : "false") << ",\n";
        out << "        \"baseline_mean_ms\": " << std::fixed << std::setprecision(6) << result.baseline.baseline_mean_ms << ",\n";
        out << "        \"current_mean_ms\": " << std::fixed << std::setprecision(6) << result.baseline.current_mean_ms << ",\n";
        out << "        \"baseline_metric_value\": " << std::fixed << std::setprecision(6) << result.baseline.baseline_metric_value << ",\n";
        out << "        \"current_metric_value\": " << std::fixed << std::setprecision(6) << result.baseline.current_metric_value << ",\n";
        out << "        \"delta_ratio\": " << std::fixed << std::setprecision(6) << result.baseline.delta_ratio << "\n";
        out << "      },\n";

        out << "      \"tags\": [";
        for (std::size_t tag_index = 0U; tag_index < result.tags.size(); ++tag_index) {
            out << "\"" << EscapeJson(result.tags[tag_index]) << "\"";
            if (tag_index + 1U < result.tags.size()) {
                out << ", ";
            }
        }
        out << "],\n";

        out << "      \"samples\": [\n";
        for (std::size_t sample_index = 0U; sample_index < result.samples.size(); ++sample_index) {
            const SampleMeasurement& sample = result.samples[sample_index];
            out << "        {\n";
            out << "          \"duration_ms\": " << std::fixed << std::setprecision(6) << sample.duration_ms << ",\n";
            out << "          \"iterations\": " << sample.iterations << ",\n";
            out << "          \"items_processed\": " << sample.items_processed << ",\n";
            out << "          \"bytes_processed\": " << sample.bytes_processed << "\n";
            out << "        }";
            if (sample_index + 1U < result.samples.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (i + 1U < summary_.results.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
}

} // namespace

BenchmarkContext::BenchmarkContext(std::uint64_t iterations_) : iterations(iterations_) {}

std::uint64_t BenchmarkContext::Iterations() const noexcept {
    return iterations;
}

void BenchmarkContext::AddItems(std::uint64_t items_) noexcept {
    items_processed += items_;
}

void BenchmarkContext::AddBytes(std::uint64_t bytes_) noexcept {
    bytes_processed += bytes_;
}

std::uint64_t BenchmarkContext::ItemsProcessed() const noexcept {
    return items_processed;
}

std::uint64_t BenchmarkContext::BytesProcessed() const noexcept {
    return bytes_processed;
}

void BenchmarkContext::ClobberMemory() noexcept {
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

SkipBenchmark::SkipBenchmark(std::string reason_) : reason(std::move(reason_)) {}

const char* SkipBenchmark::what() const noexcept {
    return reason.c_str();
}

BenchmarkRegistry& BenchmarkRegistry::Instance() {
    static BenchmarkRegistry registry{};
    return registry;
}

bool BenchmarkRegistry::Register(std::string_view name_,
                                 std::string_view tags_,
                                 BenchmarkFunction function_) {
    if (function_ == nullptr || name_.empty()) {
        return false;
    }

    for (const auto& existing : cases) {
        if (existing.name == name_) {
            std::ostringstream oss;
            oss << "Duplicate benchmark registration: " << name_;
            throw std::runtime_error(oss.str());
        }
    }

    BenchmarkCaseDefinition definition{};
    definition.name = std::string(name_);
    definition.tags = std::string(tags_);
    definition.function = function_;
    cases.push_back(std::move(definition));
    return true;
}

const std::vector<BenchmarkCaseDefinition>& BenchmarkRegistry::Cases() const noexcept {
    return cases;
}

bool RegisterBenchmarkCase(std::string_view name_,
                           std::string_view tags_,
                           BenchmarkFunction function_) {
    return BenchmarkRegistry::Instance().Register(name_, tags_, function_);
}

int RunAllBenchmarksMain(int argc_, char** argv_) {
    const ParsedArguments parsed = ParseArguments(argc_, argv_);
    if (!parsed.ok) {
        std::cerr << "[vr_bench] " << parsed.error << '\n';
        PrintHelp();
        return 2;
    }
    if (parsed.show_help) {
        PrintHelp();
        return 0;
    }

    BaselineSnapshotMap baseline_snapshots{};
    bool baseline_loaded = false;
    if (!parsed.options.baseline_json_path.empty()) {
        try {
            baseline_snapshots = LoadBaselineSnapshotMap(parsed.options.baseline_json_path);
            baseline_loaded = true;
            std::cout << "[vr_bench] baseline loaded: " << parsed.options.baseline_json_path
                      << " entries=" << baseline_snapshots.size()
                      << " metric=" << BaselineMetricKindString(parsed.options.baseline_metric)
                      << '\n';
        } catch (const std::exception& exception_) {
            std::cerr << "[vr_bench] failed to read baseline JSON: " << exception_.what() << '\n';
            return 3;
        }
    }

    const auto& cases = BenchmarkRegistry::Instance().Cases();
    BenchmarkRunSummary summary{};
    summary.results.reserve(cases.size());

    std::vector<std::pair<const BenchmarkCaseDefinition*, std::vector<std::string>>> selected{};
    selected.reserve(cases.size());
    for (const auto& definition : cases) {
        std::vector<std::string> parsed_tags{};
        if (!IsCaseSelected(definition, parsed.options, parsed_tags)) {
            continue;
        }
        selected.push_back({&definition, std::move(parsed_tags)});
    }
    std::sort(selected.begin(),
              selected.end(),
              [](const auto& lhs_, const auto& rhs_) {
                  return lhs_.first->name < rhs_.first->name;
              });
    summary.selected_count = static_cast<std::uint32_t>(selected.size());

    if (parsed.options.list_only) {
        std::cout << "[vr_bench] listed " << summary.selected_count << " benchmark(s)\n";
        for (const auto& selected_bench : selected) {
            std::cout << " - " << selected_bench.first->name;
            if (!selected_bench.second.empty()) {
                std::cout << " [";
                for (std::size_t i = 0U; i < selected_bench.second.size(); ++i) {
                    std::cout << selected_bench.second[i];
                    if (i + 1U < selected_bench.second.size()) {
                        std::cout << ",";
                    }
                }
                std::cout << "]";
            }
            std::cout << '\n';
        }
        return 0;
    }

    if (summary.selected_count == 0U) {
        std::cout << "[SUMMARY] selected=0 executed=0 completed=0 failed=0 skipped=0\n";
        return parsed.options.fail_on_empty_selection ? 4 : 0;
    }

    const auto run_start = std::chrono::steady_clock::now();

    for (const auto& selected_case : selected) {
        const BenchmarkCaseDefinition& definition = *selected_case.first;
        BenchmarkCaseResult case_result{};
        case_result.name = definition.name;
        case_result.tags = selected_case.second;
        case_result.outcome = BenchmarkOutcome::completed;

        try {
            std::cout << "[RUN  ] " << definition.name << " calibrating..." << std::endl;
            const std::uint64_t iterations = CalibrateIterations(definition, parsed.options);
            std::cout << "[RUN  ] " << definition.name
                      << " iterations=" << iterations
                      << " warmup=" << parsed.options.warmup_runs
                      << " measured=" << parsed.options.measured_runs
                      << std::endl;
            for (std::uint32_t warmup_index = 0U;
                 warmup_index < parsed.options.warmup_runs;
                 ++warmup_index) {
                if (parsed.options.verbose) {
                    std::cout << "    warmup " << (warmup_index + 1U)
                              << "/" << parsed.options.warmup_runs
                              << std::endl;
                }
                (void)ExecuteSample(definition, iterations);
            }

            case_result.samples.reserve(parsed.options.measured_runs);
            for (std::uint32_t run_index = 0U;
                 run_index < parsed.options.measured_runs;
                 ++run_index) {
                if (parsed.options.verbose) {
                    std::cout << "    sample " << (run_index + 1U)
                              << "/" << parsed.options.measured_runs
                              << std::endl;
                }
                case_result.samples.push_back(ExecuteSample(definition, iterations));
            }

            case_result.metrics = ComputeMetrics(case_result.samples, iterations);

            if (baseline_loaded) {
                CompareCaseWithBaseline(parsed.options,
                                        baseline_snapshots,
                                        case_result,
                                        summary);
            }
        } catch (const SkipBenchmark& skip_benchmark) {
            case_result.outcome = BenchmarkOutcome::skipped;
            case_result.message = skip_benchmark.what();
        } catch (const std::exception& exception_) {
            case_result.outcome = BenchmarkOutcome::failed;
            case_result.message = exception_.what();
        } catch (...) {
            case_result.outcome = BenchmarkOutcome::failed;
            case_result.message = "unknown non-standard exception";
        }

        summary.results.push_back(std::move(case_result));
    }

    const auto run_end = std::chrono::steady_clock::now();
    summary.total_duration_ms = std::chrono::duration<double, std::milli>(run_end - run_start).count();
    summary.executed_count = static_cast<std::uint32_t>(summary.results.size());

    for (const auto& result : summary.results) {
        switch (result.outcome) {
            case BenchmarkOutcome::completed: {
                ++summary.completed_count;
                std::cout << "[BENCH] " << result.name
                          << " iters=" << result.metrics.iterations
                          << " runs=" << result.samples.size()
                          << " min=" << std::fixed << std::setprecision(3) << result.metrics.min_ms << "ms"
                          << " mean=" << result.metrics.mean_ms << "ms"
                          << " median=" << result.metrics.median_ms << "ms"
                          << " p95=" << result.metrics.p95_ms << "ms"
                          << " stddev=" << result.metrics.stddev_ms << "ms";
                if (result.metrics.mean_ns_per_iteration > 0.0) {
                    std::cout << " ns/iter=" << std::fixed << std::setprecision(3)
                              << result.metrics.mean_ns_per_iteration;
                }
                if (result.metrics.items_per_second > 0.0) {
                    std::cout << " items/s=" << std::fixed << std::setprecision(2) << result.metrics.items_per_second;
                }
                if (result.metrics.bytes_per_second > 0.0) {
                    std::cout << " bytes/s=" << std::fixed << std::setprecision(2) << result.metrics.bytes_per_second;
                }

                if (result.baseline.status != BaselineComparisonStatus::not_checked) {
                    if (result.baseline.status == BaselineComparisonStatus::missing_baseline) {
                        std::cout << " baseline=missing";
                    } else {
                        const double percent = result.baseline.delta_ratio * 100.0;
                        std::cout << " baseline_metric="
                                  << BaselineMetricKindString(result.baseline.metric_kind)
                                  << " baseline_value=" << std::fixed << std::setprecision(3)
                                  << result.baseline.baseline_metric_value
                                  << " current_value=" << result.baseline.current_metric_value
                                  << " delta=" << std::showpos << std::fixed << std::setprecision(2)
                                  << percent << "%" << std::noshowpos;
                        std::cout << " baseline_status=" << BaselineStatusString(result.baseline.status);
                    }
                }
                std::cout << '\n';

                if (parsed.options.verbose) {
                    for (std::size_t i = 0U; i < result.samples.size(); ++i) {
                        const SampleMeasurement& sample = result.samples[i];
                        std::cout << "    sample#" << i
                                  << " duration_ms=" << std::fixed << std::setprecision(4) << sample.duration_ms
                                  << " items=" << sample.items_processed
                                  << " bytes=" << sample.bytes_processed
                                  << '\n';
                    }
                }
                break;
            }
            case BenchmarkOutcome::skipped:
                ++summary.skipped_count;
                std::cout << "[SKIP ] " << result.name;
                if (!result.message.empty()) {
                    std::cout << " - " << result.message;
                }
                std::cout << '\n';
                break;
            case BenchmarkOutcome::failed:
                ++summary.failed_count;
                std::cout << "[FAIL ] " << result.name;
                if (!result.message.empty()) {
                    std::cout << " - " << result.message;
                }
                if (result.baseline.status != BaselineComparisonStatus::not_checked &&
                    result.baseline.status != BaselineComparisonStatus::missing_baseline) {
                    std::cout << " | baseline_status=" << BaselineStatusString(result.baseline.status);
                    std::cout << " metric=" << BaselineMetricKindString(result.baseline.metric_kind);
                    std::cout << " delta=" << std::showpos << std::fixed << std::setprecision(2)
                              << (result.baseline.delta_ratio * 100.0) << "%" << std::noshowpos;
                }
                std::cout << '\n';
                break;
        }
    }

    std::cout << "[SUMMARY] selected=" << summary.selected_count
              << " executed=" << summary.executed_count
              << " completed=" << summary.completed_count
              << " failed=" << summary.failed_count
              << " skipped=" << summary.skipped_count
              << " regression_fail=" << summary.regression_fail_count
              << " baseline_missing=" << summary.baseline_missing_count
              << " total_ms=" << std::fixed << std::setprecision(3) << summary.total_duration_ms
              << '\n';

    if (!parsed.options.report_json_path.empty()) {
        try {
            WriteJsonReport(parsed.options.report_json_path, parsed.options, summary);
        } catch (const std::exception& exception_) {
            std::cerr << "[vr_bench] failed to write JSON report: " << exception_.what() << '\n';
            return 3;
        }
    }

    if (summary.failed_count > 0U) {
        return 1;
    }
    return 0;
}

} // namespace vr::bench
