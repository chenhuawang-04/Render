# ECS Surface 上传链路优化记录（2026-04-27）

## 本次完成的三点优化

### 1) UploadPlan 支持可配置策略（含“间隙合并”）
- 文件：`include/vr/ecs/system/surface_upload_plan_system.hpp`
- 新增 `SurfaceUploadPlanBuildOptions`
  - `merge_gap_instances`
  - `dense_path_min_dirty_count`
  - `dense_path_min_coverage_percent`
- 新增可选重载：
  - `BuildRangesFromDirtyComponents(..., build_options, scratch)`

效果：
- 允许用很小的“clean gap”换更少的 patch range，降低后续 GPU copy 调用数。
- dense/sparse 路径触发阈值不再写死，便于按场景调参。

---

### 2) UploadHost 增加 patch 归一化与自动合并
- 文件：
  - `include/vr/surface/surface_upload_host.hpp`
  - `src/surface/surface_upload_host.cpp`
- 在 patch 上传前执行：
  1. 无效 patch 过滤 + clamp
  2. 排序
  3. 按 `patch_merge_gap_bytes` 合并成更少 byte ranges

效果：
- 减少 `StageAndRecordCopyBuffer` 次数；
- 减少小碎片 patch 带来的 CPU 提交开销；
- 为“局部频繁修改”场景提供更稳定的上传行为。

---

### 3) UploadHost 增加自适应回退与可观测统计
- 新增配置：
  - `patch_fallback_coverage_percent`
  - `patch_fallback_copy_count`
- 当 patch 覆盖率过高或 copy 次数过多时，自动回退为全量上传。
- 新增统计字段：
  - `patch_input_count`
  - `patch_merged_count`
  - `patch_dropped_count`
  - `patch_fallback_full_upload_count`

效果：
- 避免“补丁过多反而更慢”的反模式；
- 为后续 renderer 层策略调优提供数据基础。

---

## 测试与基准补强

### 测试
- 新增：`tests/cases/ecs_surface_upload_plan_system_tests.cpp`
  - 新增 `merge_gap_instances` 覆盖用例
- 现状：`--include-tag surface-upload-plan` 4/4 通过

### 基准
- 新增：`bench/cases/ecs_surface_upload_plan_system_bench.cpp`
  - `EcsSurfaceUploadPlan_dim3_sparse_4k_gap_merge_1`
- 快照：
  - `bench/baselines/ecs_surface_upload_plan_2026-04-27_pre_opt.json`
  - `bench/baselines/ecs_surface_upload_plan_2026-04-27_post_opt.json`
  - `bench/baselines/ecs_surface_upload_plan_2026-04-27_opt_snapshot.md`

