# 统一 Test + Bench 执行框架（Profile 驱动）

该目录提供一个**可维护、可配置、可扩展**的统一执行层，不替换现有 `vr_tests` / `vr_bench_runner`，而是在其上提供稳定编排能力。

---

## 1. 设计目标

- **可维护**：执行流程写在 `quality_profiles.json`，不是硬编码在脚本里。
- **可配置**：支持 `--build-dir`、`--tests-bin`、`--bench-bin`、`--report-dir`、`--set KEY=VALUE` 覆盖。
- **可扩展**：profile 通过 step 列表拼装任意 test/bench/gate 流程，支持独立退出码策略。

---

## 2. 核心文件

- `vr_quality_runner.py`：统一执行器。
- `quality_profiles.json`：测试/基准配置模板（可直接新增 profile）。

---

## 3. 典型用法

```powershell
# 列出可用 profile
python scripts/testing/vr_quality_runner.py --list-profiles

# 仅跑 unit tests
python scripts/testing/vr_quality_runner.py --profile test_unit

# 跑 integration tests（全 skip 时允许 125）
python scripts/testing/vr_quality_runner.py --profile test_integration

# 跑 bench smoke
python scripts/testing/vr_quality_runner.py --profile bench_smoke

# 跑完整质量门（unit + integration + bench）
python scripts/testing/vr_quality_runner.py --profile quality_full
```

> 说明：`bench_gate_geometry` 默认使用 `build_preset/qa_relwithdebinfo/bench/vr_bench_runner.exe`，
> 避免 Debug `-O0` 构建导致性能门禁误报。

---

## 4. 变量覆盖示例

```powershell
python scripts/testing/vr_quality_runner.py `
  --profile quality_full `
  --build-dir E:/Project/MelosyneTest/VulkanRender_New/build `
  --report-dir E:/Project/MelosyneTest/VulkanRender_New/build/reports `
  --set bench_baseline_geometry=E:/Project/MelosyneTest/VulkanRender_New/bench/baselines/ecs_geometry_runtime_gold.json
```

---

## 5. profile 配置结构（摘要）

```json
{
  "version": 1,
  "variables": { "build_dir": "...", "tests_bin": "...", "bench_bin": "..." },
  "profiles": {
    "quality_full": {
      "description": "...",
      "steps": [
        {
          "name": "vr_tests_unit",
          "command": ["${tests_bin}", "--exclude-tag", "integration"],
          "allow_exit_codes": [0],
          "continue_on_failure": false,
          "timeout_sec": 600
        }
      ]
    }
  }
}
```

---

## 6. 输出产物

- 每个 step：
  - `${report_dir}/<profile>_<step>.stdout.log`
  - `${report_dir}/<profile>_<step>.stderr.log`
- 整体 summary：
  - `${report_dir}/quality_<profile>_<timestamp>.json`

---

## 7. 扩展建议

1. 新增 profile（如 nightly、gpu-stress、release-gate）  
2. 在 CI 中固定调用 `quality_full` 或单独 gate profile  
3. 将基线门禁（`bench_gate_geometry`）拆分成多条热点链路（text/surface/light/shadow）
