#pragma once

#include <string>

namespace vr::runtime {

struct CrashTracerInstallOptions final {
    std::string application_name{};
    std::string report_filename_prefix{};
    bool print_install_banner = false;
};

void InstallProcessCrashTracer(int argc_,
                               char** argv_,
                               const CrashTracerInstallOptions& options_ = {}) noexcept;

} // namespace vr::runtime
