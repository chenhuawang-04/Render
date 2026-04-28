# Appearance Renderer 分组化重构与分层优化计划（2026-04-28）

## 0. 背景与目标

当前 `GeometryRenderer2D/3D` 与 `SurfaceRenderer2D/3D` 已完成 Appearance runtime + link 调度链接入：

1. `AppearanceRuntimeSystem<Dim>::Build`
2. `AppearanceLinkSystem<Dim>::ApplyTo*Aligned`
3. 目标 renderer 自身 runtime build/upload

该实现保证功能正确，但仍存在性能与结构优化空间：
- 多 renderer 重复执行 appearance build/link（潜在 O(R * N)）
- 调度逻辑分散在四个 renderer，后续 Text/更多 renderer 接入成本高
- link 默认全量扫描，增量链路尚未最大化

## 1. 不可破坏约束（Hard Constraints）

- 组件保持 POD，不引入虚函数/动态多态。
- 系统边界清晰：`ECS Data -> Runtime Build -> Upload/Record`。
- Vulkan 热路径不引入哈希表查询作为必经步骤。
- 所有参数命名遵循 `snake_case_`，普通成员变量不加下划线。
- 不引入与旧接口兼容层（按项目要求可直接重构）。

## 2. 分层目标架构

### 2.1 Layer A：ECS 纯数据与系统层
- `AppearanceSystem / AppearanceRuntimeSystem / AppearanceLinkSystem`
- `GeometryRuntimeSystem / SurfaceRuntimeSystem / TextRuntimeSystem`

### 2.2 Layer B：Runtime Stage 层（新增聚合层）
- `AppearancePrepareStage<Dim>`
  - 构建 appearance runtime
  - 维护每帧 `AppearanceFrameContext<Dim>`
  - 输出 link 可消费视图

### 2.3 Layer C：Renderer Consumption 层
- Geometry/Surface/Text renderer 仅消费 `AppearanceFrameContext<Dim>`
- renderer 不再持有重复的 appearance scratch/cache

### 2.4 Layer D：Upload/Driver 层
- 上传主机（upload host）与描述符管理
- 不感知 ECS 概念，仅处理 buffer/image 生命周期与拷贝计划

## 3. 迭代实施阶段

## Phase 0（已开始）— 基线冻结与观测完善

### 输出
- 固定基线命令与指标口径
- 记录当前关键 bench 结果
- 统一 renderer stats 中 appearance 指标语义

### 验收
- bench 与 test 全绿
- 基线数据可复现

---

## Phase 1 — 抽离 Appearance 调度阶段（结构优化）

### 目标
将四个 renderer 中重复的 appearance build/link 逻辑抽离到统一 stage helper，减少重复代码与维护分叉。

### 任务
1. 新增 `include/vr/render/appearance_prepare_stage.hpp`
2. 提供统一 API：
   - `BuildAppearanceRuntime(...)`
   - `LinkToGeometry(...)`
   - `LinkToSurface(...)`
3. 四个 renderer 改为调用 stage API，不直接拼装 hint/build/link 细节

### 性能预期
- CPU 绝对时间变化不大（结构性重构阶段）
- 维护复杂度显著降低，便于后续共享缓存

---

## Phase 2 — Renderer 分组共享（核心性能阶段）

### 目标
同维度同帧内 appearance runtime 只构建一次。

### 任务
1. 引入 `AppearanceFrameContext<Dim>`：
   - 指向 appearance 组件数组
   - runtime scratch/cache
   - 本帧 build stats
2. 在 grouped recorder / runtime orchestrator 内先构建 context
3. Geometry/Surface/Text renderer 仅引用 context，不再各自 build

### 性能预期
- 避免重复 build，降低 O(R * N) 到 O(N + dispatch)
- 大场景下 CPU prepare 明显下降

---

## Phase 3 — 增量 link 优化（热点优化）

### 目标
避免默认全量 link 扫描。

### 任务
1. 新增 `LinkHint` 双通道：
   - appearance dirty indices
   - consumer dirty indices
2. 构建 `appearance_handle -> consumer_indices` 的稠密反向索引（按维度/renderer）
3. fallback 路径保留全量扫描（调试或索引失效时）

### 性能预期
- 静态帧 / 小脏区帧，link CPU 时间显著下降

---

## Phase 4 — 上传与描述符切换协同优化

### 目标
将 appearance 变化直接驱动 upload range 合并与 descriptor/pipeline 切换最小化。

### 任务
1. 将 appearance upload range 与 geometry/surface upload plan 做跨系统合并策略
2. 统一 pipeline/resource key 使用路径，避免重复 decode
3. 对 batch 排序加入可配置 tie-break 约束，减少抖动

---

## Phase 5 — 回归护栏与持续性能审计

### 任务
1. 补齐以下测试：
   - grouped context 生命周期
   - 共享 context 多 renderer 消费一致性
   - dirty hint 与 fallback 一致性
2. bench 护栏：
   - 1k / 10k / 100k 组件规模
   - 静态帧、少量脏帧、全量重建帧

### 验收
- 功能一致性全绿
- 性能指标达到阶段门槛（不低于 Phase 0 基线）

## 4. 基线命令（当前）

- `cmake --build build -j 8`
- `build/tests/vr_tests.exe --exclude-tag integration`
- `build/bench/vr_bench_runner.exe --filter EcsAppearance --warmup 1 --runs 3 --min-duration-ms 10`

## 5. 风险与规避

1. **风险：** Group context 生命周期管理错误导致悬挂引用
   - **规避：** frame-index 绑定 + generation 校验 + 单元测试
2. **风险：** 过早引入复杂抽象导致热路径分支上升
   - **规避：** stage helper 仅做组织，不做运行时多态
3. **风险：** 增量索引维护成本超过收益
   - **规避：** 保留 fallback 全量路径并以 bench 数据驱动切换策略

## 6. 下一步执行顺序

1. Phase 1：先抽离 `AppearancePrepareStage`（不改语义）
2. 通过 test+bench 对齐基线
3. 再进入 Phase 2 的 group-level shared context
