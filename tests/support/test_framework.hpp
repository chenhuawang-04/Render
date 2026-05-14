#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vr::test {

struct SourceLocation {
    const char* file = "";
    std::uint32_t line = 0U;
};

struct AssertionRecord {
    bool passed = false;
    bool fatal = false;
    SourceLocation location{};
    std::string expression{};
    std::string message{};
};

enum class TestOutcome : std::uint8_t {
    passed = 0U,
    failed = 1U,
    skipped = 2U,
};

class TestContext final {
public:
    explicit TestContext(std::string_view test_name_);

    void Check(bool condition_,
               std::string_view expression_,
               SourceLocation location_,
               std::string_view message_ = {});

    void Require(bool condition_,
                 std::string_view expression_,
                 SourceLocation location_,
                 std::string_view message_ = {});

    [[noreturn]] void Skip(std::string_view reason_);

    [[nodiscard]] std::string_view TestName() const noexcept;
    [[nodiscard]] std::uint32_t CheckCount() const noexcept;
    [[nodiscard]] std::uint32_t FailureCount() const noexcept;
    [[nodiscard]] const std::vector<AssertionRecord>& Assertions() const noexcept;
    [[nodiscard]] std::string_view SkipReason() const noexcept;

private:
    std::string test_name{};
    std::vector<AssertionRecord> assertions{};
    std::string skip_reason{};
    std::uint32_t check_count = 0U;
    std::uint32_t failure_count = 0U;
};

using TestFunction = void(*)(TestContext& test_context_);

struct TestCaseDefinition {
    std::string name{};
    std::string tags{};
    TestFunction function = nullptr;
};

struct TestRunnerOptions {
    bool list_only = false;
    bool verbose = false;
    bool fail_on_empty_selection = false;
    bool shuffle = false;
    bool stop_on_failure = false;
    std::string filter{};
    std::vector<std::string> include_tags{};
    std::vector<std::string> exclude_tags{};
    std::string report_json_path{};
    int return_on_all_skipped = -1;
    std::uint32_t repeat_count = 1U;
    std::uint64_t shuffle_seed = 0U;
};

struct TestCaseResult {
    std::string name{};
    std::vector<std::string> tags{};
    TestOutcome outcome = TestOutcome::passed;
    std::string message{};
    std::vector<AssertionRecord> failed_assertions{};
    std::uint32_t check_count = 0U;
    std::uint32_t failure_count = 0U;
    double duration_ms = 0.0;
};

struct TestRunSummary {
    std::uint32_t selected_count = 0U;
    std::uint32_t executed_count = 0U;
    std::uint32_t passed_count = 0U;
    std::uint32_t failed_count = 0U;
    std::uint32_t skipped_count = 0U;
    double total_duration_ms = 0.0;
    std::vector<TestCaseResult> results{};
};

class TestRegistry final {
public:
    static TestRegistry& Instance();

    bool Register(std::string_view name_,
                  std::string_view tags_,
                  TestFunction function_);

    [[nodiscard]] const std::vector<TestCaseDefinition>& Cases() const noexcept;

private:
    std::vector<TestCaseDefinition> cases{};
};

[[nodiscard]] bool RegisterTestCase(std::string_view name_,
                                    std::string_view tags_,
                                    TestFunction function_);

int RunAllTestsMain(int argc_, char** argv_);

} // namespace vr::test

#define VR_TEST_STRINGIFY_IMPL(value_) #value_
#define VR_TEST_STRINGIFY(value_) VR_TEST_STRINGIFY_IMPL(value_)

#define VR_TEST_CONCAT_IMPL(lhs_, rhs_) lhs_ ## rhs_
#define VR_TEST_CONCAT(lhs_, rhs_) VR_TEST_CONCAT_IMPL(lhs_, rhs_)

#define VR_TEST_CASE(test_name_, tags_literal_)                                            \
    static void test_name_(::vr::test::TestContext& test_context_);                        \
    namespace {                                                                             \
    const bool VR_TEST_CONCAT(test_name_, _registered_) =                                  \
        ::vr::test::RegisterTestCase(VR_TEST_STRINGIFY(test_name_), tags_literal_, &test_name_); \
    }                                                                                       \
    static void test_name_(::vr::test::TestContext& test_context_)

#define VR_CHECK(expression_)                                                               \
    do {                                                                                    \
        test_context_.Check(static_cast<bool>(expression_),                                 \
                            #expression_,                                                   \
                            ::vr::test::SourceLocation{__FILE__, static_cast<std::uint32_t>(__LINE__)}); \
    } while (false)

#define VR_CHECK_MSG(expression_, message_)                                                 \
    do {                                                                                    \
        test_context_.Check(static_cast<bool>(expression_),                                 \
                            #expression_,                                                   \
                            ::vr::test::SourceLocation{__FILE__, static_cast<std::uint32_t>(__LINE__)}, \
                            (message_));                                                    \
    } while (false)

#define VR_REQUIRE(expression_)                                                             \
    do {                                                                                    \
        test_context_.Require(static_cast<bool>(expression_),                               \
                              #expression_,                                                 \
                              ::vr::test::SourceLocation{__FILE__, static_cast<std::uint32_t>(__LINE__)}); \
    } while (false)

#define VR_REQUIRE_MSG(expression_, message_)                                               \
    do {                                                                                    \
        test_context_.Require(static_cast<bool>(expression_),                               \
                              #expression_,                                                 \
                              ::vr::test::SourceLocation{__FILE__, static_cast<std::uint32_t>(__LINE__)}, \
                              (message_));                                                  \
    } while (false)

#define VR_SKIP(reason_)                                                                    \
    do {                                                                                    \
        test_context_.Skip(reason_);                                                        \
    } while (false)

