# ECS Surface Runtime + Upload 一体化接入（2026-04-27）

## 接入目标

把下面链路做成一个高内聚 API，供 `SurfaceRenderer2D/3D::PrepareFrame` 直接调用：

`SurfaceRuntimeSystem -> SurfaceUploadPlanSystem -> SurfaceUploadHost`

---

## 已落地接口

文件：`include/vr/surface/surface_upload_host.hpp`

新增：

- `PrepareRuntimeAndUpload2D(...)`
- `PrepareRuntimeAndUpload3D(...)`

返回：

- `Surface2DRuntimeUploadResult`
- `Surface3DRuntimeUploadResult`

结果中包含：

- runtime build stats
- upload plan stats
- upload range（full/partial）
- `used_partial_upload`
- `skipped_upload`

---

## 策略细节（高性能）

### 1) 上传决策统一化

`PrepareRuntimeAndUpload*` 内部统一处理：

1. `cache hit_reused` -> 跳过上传
2. `hit_partial_update` 且满足 dirty-hint 条件 -> 走 patch 规划 + patch 上传
3. 其余情况 -> 全量上传

### 2) partial 上传严格门控

通过以下项控制是否允许 partial：

- `enable_partial_upload`
- `require_dirty_hint_for_partial`
- `min_partial_dirty_component_count`

并提供静态判定函数：

- `ShouldAttemptPartialUpload(...)`（2D/3D）

### 3) 上传 revision 统一

新增：

- `ComposeUploadRevision(surface_signature, transform_signature)`

用于让 upload 缓冲复用逻辑与 runtime 签名保持一致。

---

## 测试补强

### 单测

文件：`tests/cases/surface_upload_host_policy_tests.cpp`

覆盖：

- partial 判定门控
- hint 严格/非严格模式
- upload revision 变化行为

### 集成测试

文件：`tests/cases/runtime_surface_upload_host_integration_tests.cpp`

覆盖：

- 首帧 full upload
- 同一 frame-slot 下 transform dirty partial upload
- 后续 cache reuse skip upload
- host stats（partial / patch copy）验证

---

## 备注

当前已完成 Runtime/Plan/Upload 一体化接入与验证。  
下一步只需在 `SurfaceRenderer2D/3D::PrepareFrame` 调这个 API，即可无缝接上增量上传路径。

