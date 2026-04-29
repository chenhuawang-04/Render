# ECS Surface Upload Plan 优化快照（2026-04-27）

## 运行命令

```powershell
build\bench\vr_bench_runner.exe --filter EcsSurfaceUploadPlan --warmup 1 --runs 7 --min-duration-ms 20 --report-json bench\baselines\ecs_surface_upload_plan_2026-04-27_pre_opt.json
build\bench\vr_bench_runner.exe --filter EcsSurfaceUploadPlan --warmup 1 --runs 7 --min-duration-ms 20 --report-json bench\baselines\ecs_surface_upload_plan_2026-04-27_post_opt.json
```

## 结果对比（mean_ns_per_iteration）

| Benchmark | Pre-opt | Post-opt | Delta |
|---|---:|---:|---:|
| EcsSurfaceUploadPlan_dim3_dense_4k | 368,819.643 ns | 392,508.929 ns | +6.42% |
| EcsSurfaceUploadPlan_dim3_sparse_4k | 7,495.474 ns | 7,649.046 ns | +2.05% |

> 说明：当前为 DebugLike 构建，bench 波动较大。  
> 本次优化重点是**减少后续 GPU patch copy 次数与上传体积**，规划阶段 CPU 时间略有增加属于预期折中。

## 新增基准（优化能力验证）

| Benchmark | mean_ns_per_iteration | 说明 |
|---|---:|---|
| EcsSurfaceUploadPlan_dim3_sparse_4k_gap_merge_1 | 38,612.349 ns | 启用 `merge_gap_instances=1`，用于验证“少量空洞换更少 copy ranges”的策略 |

## 关联产物

- `bench/baselines/ecs_surface_upload_plan_2026-04-27_pre_opt.json`
- `bench/baselines/ecs_surface_upload_plan_2026-04-27_post_opt.json`

