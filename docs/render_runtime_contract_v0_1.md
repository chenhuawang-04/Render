# Render Runtime Contract v0.1

本文件记录 runtime consumption layer 当前的 **ingress contract** 基线。
范围只覆盖 runtime 如何消费已经编译/准备好的资源与场景数据；**不**覆盖 importer、decoder、外部 asset compile pipeline。

## 1. 边界与责任

runtime 直接消费的主入口对象：

- `vr::asset::TextureHost`
- `vr::geometry::GeometryResourceHost`
- `vr::geometry::GeometryImageHost`
- `vr::surface::SurfaceImageHost`
- `vr::geometry::GeometryAppearanceHost`
- `vr::render::RenderScenePacket2D/3D`

R1 的目标不是扩张入口数量，而是把这些入口的 **ownership / handle / invalid-reference / submission handoff** 语义统一。

## 2. Canonical ingress ids

统一入口定义在 `include/vr/runtime/runtime_ingress_ids.hpp`：

- `vr::asset::TextureId`
- `vr::geometry::GeometryResourceId`
- `vr::geometry::GeometryImageId`
- `vr::surface::SurfaceImageId`
- `vr::geometry::GeometryAppearanceId`
- `vr::render::IblEnvironmentId`
- `vr::render::SceneSubmissionId`

约束：

- `0` 永远表示 invalid
- 非零才表示可提交给 runtime host 的 candidate handle
- wrapper 必须保持 trivial / standard-layout / 与底层整数同宽
- 不引入长期双轨兼容；runtime-facing host API 直接以这些 canonical ids 为准

## 3. Ownership 与生命周期

### 3.1 caller-owned authoring data

调用方向 runtime 提交的 upload/create 描述符只描述一次 ingress：

- `TextureUploadInfo`
- `GeometryMeshUploadInfo`
- `GeometryImageUploadInfo`
- `SurfaceImageUploadInfo`
- `GeometryAppearanceDesc`

这些描述符本身不拥有 runtime GPU 资源；runtime host 复制/上传后，自行管理 GPU 生命周期。

### 3.2 runtime-owned records

runtime host 内部 record 是 canonical runtime state：

- `TextureHost::TextureRecord`
- `GeometryResourceHost::MeshRecord`
- `GeometryImageHost::ImageRecord`
- `SurfaceImageHost::ImageRecord`
- `GeometryAppearanceHost::AppearanceRecord`

删除/替换语义：

- `Upload*` / `Upsert*` 针对同 id 视为更新
- `Remove*` 针对 invalid id 必须安全返回 `false`
- 已移除或未找到的 id 不得被解释为隐式创建

## 4. Appearance / material ingress

`AppearanceSampledSurfaceHandle` 是一个 **domain-tagged runtime ingress handle**：

- `asset_texture`
- `surface_image`
- `geometry_image`

canonical helper：

- `MakeAppearanceTextureHandle(TextureId)`
- `MakeAppearanceSurfaceImageHandle(SurfaceImageId)`
- `MakeAppearanceGeometryImageHandle(GeometryImageId)`

以及等价重载：

- `MakeAppearanceSampledSurfaceHandle(TextureId)`
- `MakeAppearanceSampledSurfaceHandle(SurfaceImageId)`
- `MakeAppearanceSampledSurfaceHandle(GeometryImageId)`

语义：

- domain 决定 runtime 应向哪个 host 解引用
- 若未绑定、host 不可用或 id 不存在，resolver 必须保持安全 fallback，而不是制造悬空资源访问

## 5. Scene handoff / submission

`RenderScenePacket2D/3D::submission_id` 使用 `SceneSubmissionId`。

这表示：

- scene handoff 的身份属于 runtime submission contract，而不是 ECS 内部状态
- scene packet / frame snapshot 使用同一 canonical submission identity
- 调用方可以把 submission id 当作 frame-level handoff token，而不是任意裸整数字段

scene/package ????? runtime-facing resource identity ???? typed contract?

- `SpriteBackground::image_id` / `Background2DRenderState::image_id` ?? `SurfaceImageId`
- `SkyEnvironment::sky_texture_id` / `SkyEnvironmentRenderState::*texture_id` ?? `TextureId`
- `SkyEnvironment::sky_appearance_id` ?? `GeometryAppearanceId`
- `RenderScenePacket3D::extra.ibl_environment_id` ?? `IblEnvironmentId`
- `SceneRecorder3DPrepareView` ??? renderer / environment pass ??? prepare-view ???? `IblEnvironmentId` / `TextureId`

?????? runtime ingress contract???? runtime ???????? `uint32_t` ???scene handoff?packet snapshot ? prepare-view ?????????????


## 6. Canonical 入口示例

推荐参考：

- `tests/cases/runtime_scene_3d_unified_integration_tests.cpp`
- `examples/sdl_scene_3d_unified_demo.cpp`

这两个入口展示了：

- typed ingress ids
- appearance typed helpers
- scene packet / submission handoff

## 7. 非目标

以下内容不属于 v0.1 ingress contract：

- glTF / FBX / Assimp / PNG / KTX2 importer
- 外部 compile package schema
- editor / graph viewer / timeline viewer
- TAA / SSAO / SSR / DOF
- Hi-Z / occlusion / LOD / streaming / indirect draw
- 多后端扩张
