# Bench 脚本说明

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

默认对比 metric 固定为 `mean_ns_per_iteration`。

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
- 非 `0`：门禁失败（可直接给 CI 拦截）

