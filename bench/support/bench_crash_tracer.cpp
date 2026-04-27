#include "support/bench_crash_tracer.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>

#if defined(VR_BENCH_HAS_CRASH_TRACER)
#include <crash_tracer/crash_tracer.hpp>
#endif

namespace vr::bench {

namespace {

[[nodiscard]] std::string ReadEnvironmentVariable(std::string_view key_) {
    const std::string key(key_);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4996) // getenv is acceptable here; no ownership transfer.
#endif
    const char* value = std::getenv(key.c_str());
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (value == nullptr) {
        return {};
    }
    return std::string(value);
}

[[nodiscard]] std::filesystem::path ResolveCrashOutputDirectory() {
    const std::string output_dir_from_env = ReadEnvironmentVariable("VR_BENCH_CRASH_OUTPUT_DIR");
    if (!output_dir_from_env.empty()) {
        return std::filesystem::path(output_dir_from_env);
    }
    return std::filesystem::current_path() / "crash_reports";
}

} // namespace

void InstallBenchCrashTracer(int argc_, char** argv_) noexcept {
#if defined(VR_BENCH_HAS_CRASH_TRACER)
    try {
        Center::Debug::CrashTracerConfig config{};
        config.application_name = "vr_bench_runner";
        config.output_directory = ResolveCrashOutputDirectory();
        config.report_filename_prefix = "vr_bench";
        config.write_report_to_file = true;
        config.write_report_to_stderr = true;
        config.include_environment_variables = false;
        config.max_stack_frames = 192U;
        config.max_source_frames = 192U;

        std::error_code create_error{};
        std::filesystem::create_directories(config.output_directory, create_error);

        Center::Debug::CrashTracer::instance().set_extra_info_callback(
            [argc_, argv_](std::ostream& out_) {
                out_ << "[bench]\n";
                out_ << "argc=" << argc_ << "\n";
                out_ << "argv=";
                for (int i = 0; i < argc_; ++i) {
                    if (i > 0) {
                        out_ << ' ';
                    }
                    if (argv_ != nullptr && argv_[i] != nullptr) {
                        out_ << argv_[i];
                    }
                }
                out_ << "\n";
            });

        Center::Debug::CrashTracer::instance().install(config);
        std::cout << "[crash_tracer] installed output_directory="
                  << std::filesystem::absolute(config.output_directory).string()
                  << "\n";
    } catch (const std::exception& exception_) {
        std::cerr << "[crash_tracer] install failed: " << exception_.what() << "\n";
    } catch (...) {
        std::cerr << "[crash_tracer] install failed: unknown exception\n";
    }
#else
    (void)argc_;
    (void)argv_;
#endif
}

} // namespace vr::bench
