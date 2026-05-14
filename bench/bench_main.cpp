#include "support/bench_framework.hpp"
#include "vr/runtime/crash_tracer_support.hpp"

int main(int argc_, char** argv_) {
    vr::runtime::InstallProcessCrashTracer(argc_, argv_);
    return vr::bench::RunAllBenchmarksMain(argc_, argv_);
}

