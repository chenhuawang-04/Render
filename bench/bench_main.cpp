#include "support/bench_crash_tracer.hpp"
#include "support/bench_framework.hpp"

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace {

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

int main(int argc_, char** argv_) {
    ConfigureNonInteractiveCrashBehavior();
    vr::bench::InstallBenchCrashTracer(argc_, argv_);
    return vr::bench::RunAllBenchmarksMain(argc_, argv_);
}
