# VulkanRender_New Benchmark 框架说明

## 目标

`bench/` 提供一个**轻量、可扩展、可脚本化**的 benchmark runner，用于评估 CPU 热路径与渲染框架关键逻辑。

设计重点：

- 低耦合：注册机制 + 独立 runner；
- 可扩展：标签、过滤、自动校准、JSON 报告；
- 可维护：统计和执行流程清晰分层。

---

## 目录结构

```text
bench/
  support/
    bench_framework.hpp
    bench_framework.cpp
  cases/
    *.cpp
  bench_main.cpp
  CMakeLists.txt
```

---

## 核心能力

- `VR_BENCHMARK_CASE(name, "tag1;tag2")`：静态注册
- `BenchmarkContext`：
  - `Iterations()`
  - `AddItems(n)`
  - `AddBytes(n)`
  - `DoNotOptimize(value)`
  - `ClobberMemory()`
- `VR_BENCH_SKIP(reason)`：在环境不满足时跳过
- CLI：
  - `--filter <pattern>`
  - `--include-tag <tag>`
  - `--exclude-tag <tag>`
  - `--iterations <n>`（0 表示自动校准）
  - `--warmup <n>`
  - `--runs <n>`
  - `--min-duration-ms <n>`
  - `--baseline-json <path>`
  - `--fail-on-regression-percent <n>`
  - `--require-baseline-match`
  - `--fail-on-empty-selection`
  - `--report-json <path>`
- 统计项：
  - `min / max / mean / median / p95 / stddev`
  - `items/s`
  - `bytes/s`

---

## 新增 benchmark

1. 在 `bench/cases/` 新建 `xxx_bench.cpp`
2. 包含头文件：

```cpp
#include "support/bench_framework.hpp"
```

3. 添加用例：

```cpp
VR_BENCHMARK_CASE(MyHotPath, "core;cpu") {
    std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < bench_context_.Iterations(); ++i) {
        sum += i;
    }
    bench_context_.AddItems(bench_context_.Iterations());
    vr::bench::BenchmarkContext::DoNotOptimize(sum);
}
```

4. 在 `bench/CMakeLists.txt` 将该文件加入 `vr_bench_runner` 源列表

---

## 常用命令

```powershell
# 列出 benchmark
.\build\bench\vr_bench_runner.exe --list

# 跑某一类 benchmark
.\build\bench\vr_bench_runner.exe --filter FrameSync --runs 5 --warmup 1 --min-duration-ms 10

# 固定迭代次数
.\build\bench\vr_bench_runner.exe --iterations 1000000 --runs 7

# 输出 JSON 报告
.\build\bench\vr_bench_runner.exe --report-json .\build\reports\bench.json

# 以历史结果作为 baseline，超过 8% 性能回退则失败
.\build\bench\vr_bench_runner.exe `
  --baseline-json .\build\reports\bench_prev.json `
  --fail-on-regression-percent 8 `
  --report-json .\build\reports\bench_now.json

# 通过 CTest 运行 benchmark smoke
ctest --test-dir build -L bench --output-on-failure
```

---

## 回归门禁建议

推荐 CI 两阶段：

1. 固定环境生成 baseline（例如每个主分支 release tag）
2. PR/变更分支运行：
   - `--baseline-json`
   - `--fail-on-regression-percent 5~10`
   - `--require-baseline-match`（保证关键 case 不被漏跑）

这样可以把 benchmark 从“观察工具”升级成“可自动拦截性能退化的门禁”。  
