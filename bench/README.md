# VulkanRender_New Benchmark Framework

`bench/` 提供项目原生性能基准框架，用于热点回归监控与性能门禁。

---

## 1. 结构

```text
bench/
  support/
    bench_framework.hpp
    bench_framework.cpp
    bench_crash_tracer.hpp
    bench_crash_tracer.cpp
  cases/
    *_bench.cpp
  bench_main.cpp
  CMakeLists.txt
```

---

## 2. 核心能力

- 静态注册：`VR_BENCHMARK_CASE(Name, "tag1;tag2")`
- 自动校准迭代 / 固定迭代
- 统计指标：
  - `min/max/mean/median/p95/stddev`（ms）
  - `*_ns_per_iteration`
  - `items_per_second` / `bytes_per_second`
- 基线比较：
  - `--baseline-json`
  - `--baseline-metric`（`mean_ns_per_iteration` 等）
  - `--fail-on-regression-percent`
  - `--require-baseline-match`
- JSON 报告：
  - `--report-json <path>`

---

## 3. 常用命令

```powershell
# 列出 benchmark
.\build\vr_bench_runner.exe --list

# 指定 case 运行
.\build\vr_bench_runner.exe --filter EcsGeometryRuntimeSystem --runs 9 --warmup 2 --min-duration-ms 40

# 固定迭代做 A/B 对比
.\build\vr_bench_runner.exe --filter EcsGeometryRuntimeSystem --iterations 512 --runs 9 --warmup 2

# 基线门禁
.\build\vr_bench_runner.exe `
  --filter EcsGeometryRuntimeSystem `
  --iterations 512 `
  --warmup 2 `
  --runs 9 `
  --baseline-json .\bench\baselines\ecs_geometry_runtime_gold.json `
  --baseline-metric mean_ns_per_iteration `
  --fail-on-regression-percent 8 `
  --report-json .\build\reports\bench_geometry_gate.json
```

---

## 4. 推荐执行入口

建议通过统一编排器执行：

```powershell
python scripts/testing/vr_quality_runner.py --profile bench_smoke
python scripts/testing/vr_quality_runner.py --profile bench_gate_geometry
```

---

## 5. 脚本

- `scripts/bench/new_golden_baseline.ps1`：生成黄金基线
- `scripts/bench/run_bench_gate.ps1`：执行单项门禁
- `scripts/testing/vr_quality_runner.py`：统一执行 test + bench profile
