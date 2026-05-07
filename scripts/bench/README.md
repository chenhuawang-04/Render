# Bench 脚本说明

---

## 1) 生成黄金基线

```powershell
.\scripts\bench\new_golden_baseline.ps1 `
  -filter EcsGeometryRuntimeSystem `
  -output_json bench/baselines/ecs_geometry_runtime_gold.json `
  -iterations 512 `
  -warmup_runs 2 `
  -measured_runs 9 `
  -force_overwrite
```

默认对比 metric 为 `mean_ns_per_iteration`。

---

## 2) 运行性能门禁

```powershell
.\scripts\bench\run_bench_gate.ps1 `
  -filter EcsGeometryRuntimeSystem `
  -baseline_json bench/baselines/ecs_geometry_runtime_gold.json `
  -report_json bench/baselines/ecs_geometry_runtime_gate_latest.json `
  -iterations 512 `
  -warmup_runs 2 `
  -measured_runs 9 `
  -fail_on_regression_percent 8.0 `
  -require_baseline_match
```

返回码：

- `0`：门禁通过
- 非 `0`：门禁失败（可直接用于 CI 拦截）

---

## 3) 推荐

后续建议优先使用统一编排器：

```powershell
python scripts/testing/vr_quality_runner.py --profile bench_gate_geometry
```
