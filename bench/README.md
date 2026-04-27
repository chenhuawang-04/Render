# VulkanRender_New Bench 框架说明

## 目标

`bench/` 提供一套轻量、可扩展、可脚本化的 Benchmark Runner，用于：

- 追踪 CPU 热路径性能
- 做基线对比（baseline compare）
- 在 CI 中启用性能回退门禁

---

## 目录结构

```text
bench/
  support/
    bench_framework.hpp
    bench_framework.cpp
    bench_crash_tracer.hpp
    bench_crash_tracer.cpp
  cases/
    *.cpp
  bench_main.cpp
  CMakeLists.txt
```

---

## 核心能力

### 1) 用例注册

```cpp
VR_BENCHMARK_CASE(MyHotPath, "core;cpu") {
    std::uint64_t sum = 0U;
    for (std::uint64_t i = 0U; i < bench_context_.Iterations(); ++i) {
        sum += i;
    }
    bench_context_.AddItems(bench_context_.Iterations());
    vr::bench::BenchmarkContext::DoNotOptimize(sum);
}
```

### 2) 统计指标

- 时延：`min / max / mean / median / p95 / stddev`（ms）
- 归一化时延：`min/max/mean/median/p95/stddev_ns_per_iteration`
- 吞吐：`items_per_second`、`bytes_per_second`

> 推荐对比时优先看 `mean_ns_per_iteration`，可避免 auto-calibrate 导致的“迭代数不同”误判。

### 3) 基线回归比较

支持按以下 metric 做回归判定：

- `mean_ns_per_iteration`（默认，推荐）
- `mean_ms`
- `items_per_second`
- `bytes_per_second`

其中：

- 时延类（`mean_ns_per_iteration` / `mean_ms`）为 **越小越好**
- 吞吐类（`items_per_second` / `bytes_per_second`）为 **越大越好**

---

## CLI

```text
--help, -h
--list
--filter <pattern>
--include-tag <tag>
--exclude-tag <tag>
--iterations <n>                 # 0 = auto calibrate
--min-iterations <n>             # auto-calibrate 下迭代次数下限，默认 8
--warmup <n>
--runs <n>
--min-duration-ms <n>
--baseline-json <path>
--baseline-metric <name>         # mean_ns_per_iteration (default) / mean_ms / items_per_second / bytes_per_second
--fail-on-regression-percent <n>
--require-baseline-match
--fail-on-empty-selection
--report-json <path>
--verbose
```

---

## 常用命令

```powershell
# 列出所有 benchmark
.\build\bench\vr_bench_runner.exe --list

# 跑指定用例
.\build\bench\vr_bench_runner.exe --filter EcsGeometryRuntimeSystem --runs 9 --warmup 2 --min-duration-ms 40

# Auto calibrate 但至少 64 iterations（降低小迭代噪声）
.\build\bench\vr_bench_runner.exe --filter EcsGeometryRuntimeSystem --min-iterations 64 --runs 9 --warmup 2

# 固定迭代（做严格 A/B 对比）
.\build\bench\vr_bench_runner.exe --filter EcsGeometryRuntimeSystem --iterations 512 --runs 9 --warmup 2

# 输出报告
.\build\bench\vr_bench_runner.exe --filter EcsGeometryRuntimeSystem --report-json .\bench\baselines\report.json

# 与 baseline 做门禁（推荐按 ns/iter）
.\build\bench\vr_bench_runner.exe `
  --filter EcsGeometryRuntimeSystem `
  --baseline-json .\bench\baselines\baseline.json `
  --baseline-metric mean_ns_per_iteration `
  --fail-on-regression-percent 8 `
  --report-json .\bench\baselines\current.json
```

---

## CI 建议

建议分两步：

1. 固定机器/驱动/电源策略，生成并提交 baseline
2. PR 上做对比并设门禁

推荐参数：

- `--baseline-metric mean_ns_per_iteration`
- `--fail-on-regression-percent 5~10`
- `--require-baseline-match`

---

## 一键脚本（推荐）

仓库已提供脚本：

- `scripts/bench/new_golden_baseline.ps1`：生成黄金基线
- `scripts/bench/run_bench_gate.ps1`：执行性能门禁

详见：`scripts/bench/README.md`
