#include "vr/runtime/crash_tracer_support.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#if defined(VR_HAS_CRASH_TRACER)
#include <crash_tracer/crash_tracer.hpp>
#endif

namespace vr::runtime {

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

[[nodiscard]] bool IsTruthy(std::string_view value_) {
    return value_ == "1" ||
           value_ == "true" ||
           value_ == "TRUE" ||
           value_ == "on" ||
           value_ == "ON" ||
           value_ == "yes" ||
           value_ == "YES";
}

[[nodiscard]] std::string SanitizeFileComponent(std::string_view value_) {
    std::string sanitized;
    sanitized.reserve(value_.size());
    for (const char ch : value_) {
        const bool alnum = (ch >= '0' && ch <= '9') ||
                           (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z');
        if (alnum || ch == '_' || ch == '-') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized;
}

[[nodiscard]] std::string ResolveProcessName(const CrashTracerInstallOptions& options_,
                                             int argc_,
                                             char** argv_) {
    if (!options_.application_name.empty()) {
        return options_.application_name;
    }

    if (argc_ > 0 && argv_ != nullptr && argv_[0] != nullptr) {
        const std::filesystem::path executable_path(argv_[0]);
        const std::filesystem::path stem = executable_path.stem();
        if (!stem.empty()) {
            return stem.string();
        }
        const std::filesystem::path filename = executable_path.filename();
        if (!filename.empty()) {
            return filename.string();
        }
    }

    return "vulkan_render";
}

[[nodiscard]] std::filesystem::path ResolveCrashOutputDirectory() {
    const std::string output_dir_from_env = ReadEnvironmentVariable("VR_CRASH_OUTPUT_DIR");
    if (!output_dir_from_env.empty()) {
        return std::filesystem::path(output_dir_from_env);
    }
    return std::filesystem::current_path() / "crash_reports";
}

[[nodiscard]] std::vector<std::string> SnapshotArguments(int argc_, char** argv_) {
    std::vector<std::string> arguments{};
    if (argc_ <= 0 || argv_ == nullptr) {
        return arguments;
    }
    arguments.reserve(static_cast<std::size_t>(argc_));
    for (int index = 0; index < argc_; ++index) {
        arguments.emplace_back(argv_[index] != nullptr ? argv_[index] : "");
    }
    return arguments;
}

void ConfigureNonInteractiveCrashBehavior() noexcept {
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

} // namespace

void InstallProcessCrashTracer(int argc_,
                               char** argv_,
                               const CrashTracerInstallOptions& options_) noexcept {
#if defined(VR_HAS_CRASH_TRACER)
    static std::once_flag install_once{};
    std::call_once(install_once, [argc_, argv_, options_]() {
        if (IsTruthy(ReadEnvironmentVariable("VR_DISABLE_CRASH_TRACER"))) {
            return;
        }

        ConfigureNonInteractiveCrashBehavior();

        try {
            std::string process_name = ResolveProcessName(options_, argc_, argv_);
            std::string report_prefix = options_.report_filename_prefix.empty()
                ? process_name
                : options_.report_filename_prefix;
            report_prefix = SanitizeFileComponent(report_prefix);
            if (report_prefix.empty()) {
                report_prefix = "vulkan_render";
            }

            Center::Debug::CrashTracerConfig config{};
            config.application_name = std::move(process_name);
            config.output_directory = ResolveCrashOutputDirectory();
            config.report_filename_prefix = std::move(report_prefix);
            config.write_report_to_file = true;
            config.write_report_to_stderr = true;
            config.include_environment_variables = false;
            config.max_stack_frames = 192U;
            config.max_source_frames = 192U;

            std::error_code create_error{};
            std::filesystem::create_directories(config.output_directory, create_error);

            std::vector<std::string> argv_snapshot = SnapshotArguments(argc_, argv_);
            Center::Debug::CrashTracer::instance().set_extra_info_callback(
                [argc_, argv_snapshot = std::move(argv_snapshot)](std::ostream& out_) {
                    out_ << "[process_args]\n";
                    out_ << "argc=" << argc_ << "\n";
                    out_ << "argv=";
                    for (std::size_t index = 0U; index < argv_snapshot.size(); ++index) {
                        if (index > 0U) {
                            out_ << ' ';
                        }
                        out_ << argv_snapshot[index];
                    }
                    out_ << "\n";
                });

            if (!Center::Debug::CrashTracer::instance().installed()) {
                Center::Debug::CrashTracer::instance().install(config);
            }

            if (options_.print_install_banner) {
                std::error_code absolute_error{};
                const std::filesystem::path absolute_output =
                    std::filesystem::absolute(config.output_directory, absolute_error);
                const std::filesystem::path& output_path =
                    absolute_error ? config.output_directory : absolute_output;
                std::cerr << "[crash_tracer] installed app=" << config.application_name
                          << " output_directory=" << output_path.string() << "\n";
            }
        } catch (const std::exception& exception_) {
            std::cerr << "[crash_tracer] install failed: " << exception_.what() << "\n";
        } catch (...) {
            std::cerr << "[crash_tracer] install failed: unknown exception\n";
        }
    });
#else
    (void)argc_;
    (void)argv_;
    (void)options_;
#endif
}

} // namespace vr::runtime
