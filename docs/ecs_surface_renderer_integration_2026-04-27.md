# ECS Surface Renderer 接入完成记录（2026-04-27）

## 范围

本次改动把 Surface 链路从“Runtime + Upload”推进到“Runtime + Upload + Draw”：

- `SurfaceRuntimeSystem<Dim>`
- `SurfaceUploadPlanSystem<Dim>`
- `SurfaceUploadHost`
- `SurfaceRenderer2D / SurfaceRenderer3D`

并保持 ECS 分层：

- 组件仍为纯 POD（`Surface<Dim>`、`Transform<Dim>`、`Camera<Dim>`）。
- 行为全部由 System/Host/Renderer 负责。

---

## 新增模块

### 1) Renderer 头文件

- `include/vr/surface/surface_renderer_2d.hpp`
- `include/vr/surface/surface_renderer_3d.hpp`

### 2) Renderer 实现

- `src/surface/surface_renderer_2d.cpp`
- `src/surface/surface_renderer_3d.cpp`

### 3) Surface Shader

- `shaders/surface_2d.vert`
- `shaders/surface_2d.frag`
- `shaders/surface_3d.vert`
- `shaders/surface_3d.frag`

---

## 接入策略

## 2D

- `PrepareFrame` 直接调用 `SurfaceUploadHost::PrepareRuntimeAndUpload2D(...)`。
- 支持 dirty hint 注入：`SetTransformDirtyHint(...)`。
- 支持按 batch 参数选择 blend pipeline（alpha/additive/multiply/screen）。
- 动态渲染 + 动态 viewport/scissor。

## 3D

- `PrepareFrame` 直接调用 `SurfaceUploadHost::PrepareRuntimeAndUpload3D(...)`。
- 支持 dirty hint 注入：`SetTransformDirtyHint(...)`。
- 支持按 batch 参数选择 pipeline 变体：
  - 深度：`no_depth / depth_read / depth_read_write`
  - 面剔除：`back / none`
- 内建 depth image 生命周期与 retire 回收，避免销毁时机错误。

---

## 质量与结构约束

- 新逻辑集中在 `vr/surface` 目录，不污染 `text/geometry` 目录。
- Renderer 仅消费 Runtime/Upload 输出，不反向侵入 ECS 组件定义。
- Vulkan 状态转换与资源生命周期保持局部封装（Renderer 私有函数）。

---

## 验证结果

### 构建

- `cmake --build build -j 8 --target vulkan_init vr_tests` ✅

### 测试

- `build\\tests\\vr_tests.exe --include-tag surface` ✅
  - 16/16 通过（含 Runtime + Upload 集成测试）

### 基准

- `build\\bench\\vr_bench_runner.exe --filter EcsSurfaceUploadPlan ...` ✅
- 基线文件：
  - `bench/baselines/ecs_surface_upload_plan_2026-04-27_after_renderer_integration.json`

---

## 说明

当前渲染链路已打通到实际 Draw；纹理采样资源路由（image/sprite/texture 的真正图像绑定）可在下一阶段接入 `SurfaceImageHost/SurfaceMaterialHost` 时进一步完善。

