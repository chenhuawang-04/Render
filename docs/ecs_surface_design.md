# ECS Surface 统一组件设计（Dim2/Dim3）

更新时间：2026-04-27

## 目标

- 统一概念：`Surface<Dim>`
  - `Surface<Dim2>`：Image / Sprite
  - `Surface<Dim3>`：Texture
- 组件保持纯 POD（只存数据，不放行为）
- 全部操作由 `System<Dim>` 承担
- 对齐已有 `Text/Geometry` 的排序、批处理、运行时缓存范式

---

## 组件层（POD）

文件：`include/vr/ecs/component/surface_component.hpp`

- `Surface<Dim2>`
  - style：uv/tint/opacity/layer/blend/flip
  - runtime.route：sort_key/surface_id/material_id/batch_tag/depth_bin/visible/pass_hint/dirty_flags
  - runtime.source：image_id/sprite_id/atlas_page_id/source_kind/source_revision
- `Surface<Dim3>`
  - style：uv transform/tint/opacity/depth flags/filter/address
  - runtime.route：同上
  - runtime.texture：texture_id/sampler_id/uv_set/flags/texture_revision

---

## 系统层（规则）

文件：`include/vr/ecs/system/surface_system.hpp`

- 初始化默认值
- dirty flags 管理
- source/texture route 管理
- 64-bit sort key 规则
  - `[pass:2][material:16][surface:16][minor:16][batch:14]`
- binding key 规则
  - `binding_shift = sort_key_surface_shift`
  - 即按 `pass/material/surface` 分组，忽略 `minor/batch`

### 性能细节

- 热路径 setter 全部加入 no-op guard（值不变直接返回）
- 避免重复 dirty 标记与不必要 sort key 重建

---

## 批处理层（Batch）

文件：`include/vr/ecs/system/surface_batch_system.hpp`

- 纯 ECS 数据到排序可见列表
- Radix sort（64-bit sort_key）
- 提供：
  - `BuildVisibleItems`
  - `BuildAndSort`
  - `ForEachSortKeyGroup`
  - `ForEachBindingGroup`

---

## 运行时层（Runtime）

文件：`include/vr/ecs/system/surface_runtime_system.hpp`

- `SurfaceRuntimeSystem<Dim2/Dim3>`：
  - 生成 GPU 可消费实例数组 + draw batch
  - 内建缓存（component pointer + transform pointer + count + signature）
  - 支持 transform-only partial update
  - 支持 dirty-index hint（`O(k)` 更新）
  - 支持外部 signature hint（绕开全量签名计算）

---

## 测试覆盖

- `tests/cases/ecs_surface_component_tests.cpp`
- `tests/cases/ecs_surface_batch_runtime_system_tests.cpp`

覆盖点：
- POD 约束
- sort/bucket/binding 分组
- Dim2/Dim3 runtime 构建
- cache 命中与 partial update
- dirty-hint 路径

---

## 性能基准（当前）

文件：`bench/cases/ecs_surface_runtime_system_bench.cpp`

- `EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild`
- `EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint`

用于验证 full rebuild 与 dirty-hint transform-only 的性能差异。

---

## 下一阶段建议

1. `SurfaceRenderer2D` 接入（sprite/image draw）
2. `SurfaceRenderer3D` 接入（texture draw）
3. Surface 资源主机（image/sampler/material route）
4. runtime ↔ renderer 的 revision 粒度对齐（最小上传）

