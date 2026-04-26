#include "support/test_framework.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vr::test {

namespace {

class RequirementFailure final : public std::exception {};

class SkipSignal final : public std::exception {
public:
    explicit SkipSignal(std::string reason_) : reason(std::move(reason_)) {}

    [[nodiscard]] const char* what() const noexcept override {
        return reason.c_str();
    }

private:
    std::string reason{};
};

struct ParsedArguments {
    bool ok = true;
    bool show_help = false;
    std::string error{};
    TestRunnerOptions options{};
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
        const bool wildcard_match =
            (pattern_index < pattern.size()) &&
            ((pattern[pattern_index] == '?') ||
             (pattern[pattern_index] == text[text_index]));

        if (wildcard_match) {
            ++pattern_index;
            ++text_index;
            continue;
        }

        const bool is_star =
            (pattern_index < pattern.size()) &&
            (pattern[pattern_index] == '*');
        if (is_star) {
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

[[nodiscard]] bool ParseIntegerArg(std::string_view value_, int& out_value_) {
    if (value_.empty()) {
        return false;
    }

    int sign = 1;
    std::size_t index = 0U;
    if (value_[0] == '-') {
        sign = -1;
        index = 1U;
    } else if (value_[0] == '+') {
        index = 1U;
    }
    if (index >= value_.size()) {
        return false;
    }

    std::int64_t accumulator = 0;
    for (; index < value_.size(); ++index) {
        const char ch = value_[index];
        if (ch < '0' || ch > '9') {
            return false;
        }
        const int digit = ch - '0';
        accumulator = accumulator * 10 + digit;
        if (accumulator > static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1LL) {
            return false;
        }
    }

    accumulator *= sign;
    if (accumulator < std::numeric_limits<int>::min() ||
        accumulator > std::numeric_limits<int>::max()) {
        return false;
    }
    out_value_ = static_cast<int>(accumulator);
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
        if (arg == "--fail-on-empty-selection") {
            parsed.options.fail_on_empty_selection = true;
            continue;
        }

        const auto require_value = [&](std::string_view option_name_) -> std::optional<std::string_view> {
            if ((i + 1) >= argc_) {
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
        if (arg == "--report-json") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            parsed.options.report_json_path = std::string(value.value());
            continue;
        }
        if (arg == "--return-on-all-skipped") {
            const auto value = require_value(arg);
            if (!value.has_value()) {
                return parsed;
            }
            int parsed_code = -1;
            if (!ParseIntegerArg(value.value(), parsed_code) || parsed_code < 0) {
                parsed.ok = false;
                parsed.error = "Invalid non-negative integer for --return-on-all-skipped";
                return parsed;
            }
            parsed.options.return_on_all_skipped = parsed_code;
            continue;
        }

        parsed.ok = false;
        parsed.error = std::string("Unknown option: ") + std::string(arg);
        return parsed;
    }

    return parsed;
}

void PrintHelp() {
    std::cout
        << "vr_tests command line options:\n"
        << "  --help, -h                    Show this help message\n"
        << "  --list                        List selected test cases without running\n"
        << "  --filter <pattern>            Name filter (substring or glob with * ?)\n"
        << "  --include-tag <tag>           Include only tests having this tag (repeatable)\n"
        << "  --exclude-tag <tag>           Exclude tests having this tag (repeatable)\n"
        << "  --report-json <path>          Write machine-readable JSON report\n"
        << "  --return-on-all-skipped <n>   Exit code when all selected tests were skipped\n"
        << "  --fail-on-empty-selection     Return non-zero when selection is empty\n"
        << "  --verbose                     Emit extra pass details\n";
}

[[nodiscard]] bool IsCaseSelected(const TestCaseDefinition& definition_,
                                  const TestRunnerOptions& options_,
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

void WriteJsonReport(const std::string& path_,
                     const TestRunnerOptions& options_,
                     const TestRunSummary& summary_) {
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
    out << "    \"list_only\": " << (options_.list_only ? "true" : "false") << ",\n";
    out << "    \"fail_on_empty_selection\": " << (options_.fail_on_empty_selection ? "true" : "false") << ",\n";
    out << "    \"return_on_all_skipped\": " << options_.return_on_all_skipped << "\n";
    out << "  },\n";
    out << "  \"summary\": {\n";
    out << "    \"selected_count\": " << summary_.selected_count << ",\n";
    out << "    \"executed_count\": " << summary_.executed_count << ",\n";
    out << "    \"passed_count\": " << summary_.passed_count << ",\n";
    out << "    \"failed_count\": " << summary_.failed_count << ",\n";
    out << "    \"skipped_count\": " << summary_.skipped_count << ",\n";
    out << "    \"total_duration_ms\": " << std::fixed << std::setprecision(3) << summary_.total_duration_ms << "\n";
    out << "  },\n";
    out << "  \"results\": [\n";

    for (std::size_t i = 0U; i < summary_.results.size(); ++i) {
        const TestCaseResult& result = summary_.results[i];
        out << "    {\n";
        out << "      \"name\": \"" << EscapeJson(result.name) << "\",\n";
        out << "      \"outcome\": \""
            << (result.outcome == TestOutcome::passed
                    ? "passed"
                    : (result.outcome == TestOutcome::failed ? "failed" : "skipped"))
            << "\",\n";
        out << "      \"duration_ms\": " << std::fixed << std::setprecision(3) << result.duration_ms << ",\n";
        out << "      \"check_count\": " << result.check_count << ",\n";
        out << "      \"failure_count\": " << result.failure_count << ",\n";
        out << "      \"message\": \"" << EscapeJson(result.message) << "\",\n";
        out << "      \"tags\": [";
        for (std::size_t tag_index = 0U; tag_index < result.tags.size(); ++tag_index) {
            out << "\"" << EscapeJson(result.tags[tag_index]) << "\"";
            if (tag_index + 1U < result.tags.size()) {
                out << ", ";
            }
        }
        out << "],\n";
        out << "      \"failed_assertions\": [\n";

        for (std::size_t assertion_index = 0U;
             assertion_index < result.failed_assertions.size();
             ++assertion_index) {
            const AssertionRecord& assertion = result.failed_assertions[assertion_index];
            out << "        {\n";
            out << "          \"expression\": \"" << EscapeJson(assertion.expression) << "\",\n";
            out << "          \"message\": \"" << EscapeJson(assertion.message) << "\",\n";
            out << "          \"fatal\": " << (assertion.fatal ? "true" : "false") << ",\n";
            out << "          \"file\": \"" << EscapeJson(assertion.location.file) << "\",\n";
            out << "          \"line\": " << assertion.location.line << "\n";
            out << "        }";
            if (assertion_index + 1U < result.failed_assertions.size()) {
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

TestContext::TestContext(std::string_view test_name_) : test_name(test_name_) {}

void TestContext::Check(bool condition_,
                        std::string_view expression_,
                        SourceLocation location_,
                        std::string_view message_) {
    ++check_count;
    if (condition_) {
        return;
    }

    ++failure_count;
    AssertionRecord record{};
    record.passed = false;
    record.fatal = false;
    record.location = location_;
    record.expression = std::string(expression_);
    record.message = std::string(message_);
    assertions.push_back(std::move(record));
}

void TestContext::Require(bool condition_,
                          std::string_view expression_,
                          SourceLocation location_,
                          std::string_view message_) {
    ++check_count;
    if (condition_) {
        return;
    }

    ++failure_count;
    AssertionRecord record{};
    record.passed = false;
    record.fatal = true;
    record.location = location_;
    record.expression = std::string(expression_);
    record.message = std::string(message_);
    assertions.push_back(std::move(record));
    throw RequirementFailure{};
}

[[noreturn]] void TestContext::Skip(std::string_view reason_) {
    skip_reason = std::string(reason_);
    throw SkipSignal(skip_reason);
}

std::string_view TestContext::TestName() const noexcept {
    return test_name;
}

std::uint32_t TestContext::CheckCount() const noexcept {
    return check_count;
}

std::uint32_t TestContext::FailureCount() const noexcept {
    return failure_count;
}

const std::vector<AssertionRecord>& TestContext::Assertions() const noexcept {
    return assertions;
}

std::string_view TestContext::SkipReason() const noexcept {
    return skip_reason;
}

TestRegistry& TestRegistry::Instance() {
    static TestRegistry registry{};
    return registry;
}

bool TestRegistry::Register(std::string_view name_,
                            std::string_view tags_,
                            TestFunction function_) {
    if (function_ == nullptr || name_.empty()) {
        return false;
    }

    for (const auto& existing : cases) {
        if (existing.name == name_) {
            std::ostringstream oss;
            oss << "Duplicate test case registration: " << name_;
            throw std::runtime_error(oss.str());
        }
    }

    TestCaseDefinition definition{};
    definition.name = std::string(name_);
    definition.tags = std::string(tags_);
    definition.function = function_;
    cases.push_back(std::move(definition));
    return true;
}

const std::vector<TestCaseDefinition>& TestRegistry::Cases() const noexcept {
    return cases;
}

bool RegisterTestCase(std::string_view name_,
                      std::string_view tags_,
                      TestFunction function_) {
    return TestRegistry::Instance().Register(name_, tags_, function_);
}

int RunAllTestsMain(int argc_, char** argv_) {
    const ParsedArguments parsed = ParseArguments(argc_, argv_);
    if (!parsed.ok) {
        std::cerr << "[vr_tests] " << parsed.error << '\n';
        PrintHelp();
        return 2;
    }

    if (parsed.show_help) {
        PrintHelp();
        return 0;
    }

    const auto& cases = TestRegistry::Instance().Cases();
    TestRunSummary summary{};
    summary.results.reserve(cases.size());

    std::vector<std::pair<const TestCaseDefinition*, std::vector<std::string>>> selected{};
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
        std::cout << "[vr_tests] listed " << summary.selected_count << " case(s)\n";
        for (const auto& selected_case : selected) {
            std::cout << " - " << selected_case.first->name;
            if (!selected_case.second.empty()) {
                std::cout << " [";
                for (std::size_t i = 0U; i < selected_case.second.size(); ++i) {
                    std::cout << selected_case.second[i];
                    if (i + 1U < selected_case.second.size()) {
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
        std::cout << "[SUMMARY] selected=0 executed=0 passed=0 failed=0 skipped=0 total_ms=0.000\n";
        return parsed.options.fail_on_empty_selection ? 4 : 0;
    }

    const auto run_start = std::chrono::steady_clock::now();

    for (const auto& selected_case : selected) {
        const TestCaseDefinition& definition = *selected_case.first;
        const auto case_start = std::chrono::steady_clock::now();

        TestContext test_context(definition.name);
        TestCaseResult case_result{};
        case_result.name = definition.name;
        case_result.tags = selected_case.second;
        case_result.outcome = TestOutcome::passed;

        try {
            definition.function(test_context);
        } catch (const SkipSignal& skip_signal) {
            case_result.outcome = TestOutcome::skipped;
            case_result.message = skip_signal.what();
        } catch (const RequirementFailure&) {
            case_result.outcome = TestOutcome::failed;
            case_result.message = "fatal requirement failed";
        } catch (const std::exception& exception_) {
            case_result.outcome = TestOutcome::failed;
            case_result.message = exception_.what();
        } catch (...) {
            case_result.outcome = TestOutcome::failed;
            case_result.message = "unknown non-standard exception";
        }

        case_result.check_count = test_context.CheckCount();
        case_result.failure_count = test_context.FailureCount();
        case_result.failed_assertions = test_context.Assertions();
        if (case_result.outcome == TestOutcome::passed && case_result.failure_count > 0U) {
            case_result.outcome = TestOutcome::failed;
            if (case_result.message.empty()) {
                case_result.message = "one or more non-fatal checks failed";
            }
        }

        const auto case_end = std::chrono::steady_clock::now();
        case_result.duration_ms = std::chrono::duration<double, std::milli>(case_end - case_start).count();
        summary.results.push_back(std::move(case_result));
    }

    const auto run_end = std::chrono::steady_clock::now();
    summary.total_duration_ms = std::chrono::duration<double, std::milli>(run_end - run_start).count();
    summary.executed_count = static_cast<std::uint32_t>(summary.results.size());

    for (const auto& result : summary.results) {
        switch (result.outcome) {
            case TestOutcome::passed: ++summary.passed_count; break;
            case TestOutcome::failed: ++summary.failed_count; break;
            case TestOutcome::skipped: ++summary.skipped_count; break;
        }

        const char* label = result.outcome == TestOutcome::passed
            ? "PASS"
            : (result.outcome == TestOutcome::failed ? "FAIL" : "SKIP");
        std::cout << "[" << label << "] " << result.name
                  << " (" << std::fixed << std::setprecision(3) << result.duration_ms << " ms)";

        if (!result.message.empty()) {
            std::cout << " - " << result.message;
        }
        std::cout << '\n';

        if (result.outcome == TestOutcome::failed) {
            for (const auto& assertion : result.failed_assertions) {
                std::cout << "    at " << assertion.location.file << ":" << assertion.location.line
                          << " | " << assertion.expression;
                if (!assertion.message.empty()) {
                    std::cout << " | " << assertion.message;
                }
                if (assertion.fatal) {
                    std::cout << " | fatal";
                }
                std::cout << '\n';
            }
        } else if (parsed.options.verbose) {
            std::cout << "    checks=" << result.check_count << '\n';
        }
    }

    std::cout << "[SUMMARY] selected=" << summary.selected_count
              << " executed=" << summary.executed_count
              << " passed=" << summary.passed_count
              << " failed=" << summary.failed_count
              << " skipped=" << summary.skipped_count
              << " total_ms=" << std::fixed << std::setprecision(3) << summary.total_duration_ms
              << '\n';

    if (!parsed.options.report_json_path.empty()) {
        try {
            WriteJsonReport(parsed.options.report_json_path, parsed.options, summary);
        } catch (const std::exception& exception_) {
            std::cerr << "[vr_tests] failed to write JSON report: " << exception_.what() << '\n';
            return 3;
        }
    }

    if (summary.failed_count > 0U) {
        return 1;
    }

    const bool all_skipped =
        (summary.selected_count > 0U) &&
        (summary.skipped_count == summary.selected_count);
    if (all_skipped && parsed.options.return_on_all_skipped >= 0) {
        return parsed.options.return_on_all_skipped;
    }
    return 0;
}

} // namespace vr::test
