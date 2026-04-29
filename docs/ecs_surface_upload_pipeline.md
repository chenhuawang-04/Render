# ECS Surface 上传管线设计（高性能增量上传）
更新时间：2026-04-27

## 目标

在 `SurfaceRuntimeSystem<Dim>` 已能做 **transform-only 增量重写** 的前提下，补齐“CPU Runtime -> GPU Buffer”这一段的高性能链路，避免每帧全量上传。

---

## 新增模块

### 1) `SurfaceUploadPlanSystem<Dim>`
文件：`include/vr/ecs/system/surface_upload_plan_system.hpp`

职责：
- 输入：runtime 缓存里的 `component_to_instance_index` + dirty component list。
- 输出：可直接用于上传的 `SurfaceUploadPatchRange[]`（`instance_begin + instance_count`）。

关键点：
- **稀疏路径（sparse）**：排序 + 去重 + 连续区间合并。
- **稠密路径（dense）**：`mark array` 扫描合并，减少大 dirty 集下排序成本。
- 自动过滤无效 component（越界、未映射实例）。

性能收益：
- 上传规划从“全量 N”变成“脏集 k”，并通过区间合并减少 copy 次数。

---

### 2) `SurfaceUploadHost`
文件：
- `include/vr/surface/surface_upload_host.hpp`
- `src/surface/surface_upload_host.cpp`

职责：
- 管理 2D/3D 实例流式 GPU buffer（按 frame in flight 分片）。
- 提供全量上传和增量 patch 上传接口：
  - `Upload2DInstances / Upload3DInstances`
  - `Upload2DInstancePatches / Upload3DInstancePatches`

关键点：
- 缓冲复用（revision + size 命中直接复用）
- 自动扩容（power-of-two growth）
- patch 上传时按区间偏移复制，仅上传变更段
- 同步2屏障只发一次覆盖范围，减少 barrier 开销

---

## 接口分层（ECS 原则）

- 组件：`Surface<Dim>` 仍保持纯 POD。
- 行为：
  - `SurfaceRuntimeSystem<Dim>` 负责可消费实例数据构建；
  - `SurfaceUploadPlanSystem<Dim>` 负责 dirty -> upload ranges；
  - `SurfaceUploadHost` 负责 GPU buffer 生命周期与上传。

即：**组件无行为，系统分责明确，可替换、可测试、可 bench。**

---

## 测试与基准

### 单元测试
新增：`tests/cases/ecs_surface_upload_plan_system_tests.cpp`
- 稀疏 dirty：验证排序、去重、合并区间
- 稠密 dirty：验证 dense 路径选择与合并结果
- 无效 dirty：验证丢弃统计与边界安全

### 性能基准
新增：`bench/cases/ecs_surface_upload_plan_system_bench.cpp`
- `EcsSurfaceUploadPlan_dim3_sparse_4k`
- `EcsSurfaceUploadPlan_dim3_dense_4k`

---

## 下一步建议（可直接推进）

1. 在 `SurfaceRenderer2D/3D` 的 `PrepareFrame` 中接入：
   - cache hit: 不上传
   - transform-only + dirty hint: 走 patch 上传
   - 其余 miss: 全量上传
2. 对 patch 数量设置阈值（例如 patch_count 过大时自动降级全量上传）。
3. 将上传统计并入 renderer stats（观察 full/partial 命中率）。

