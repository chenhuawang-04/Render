# Appearance 统一语义重构规划书

> 本文为当前重构任务的最终统一规划。
>
> 核心约束：**Appearance 必须在整个项目中保持统一命名，不拆解为独立的 Paint / Material 公共命名体系。**
>
> 说明：`Appearance<Dim2>` 在语义上对应 2D paint-like visual semantics；`Appearance<Dim3>` 在语义上对应 3D material-like visual semantics。但这只是**语义解释**，**不是命名拆分**。

---

## 1. 目标与硬约束

### 1.1 最终目标

把当前项目收敛为三大稳定 ECS 概念：

- `Geometry`：空间几何 / 形状表达
- `Surface`：可采样表面内容表达
- `Appearance`：统一视觉外观表达

并满足以下结果：

1. **语义边界清晰**：Geometry / Surface / Appearance 不再串味
2. **命名全局统一**：整个项目统一使用 `Appearance`，不引入并行的 `Paint` / `Material` 公共命名体系
3. **冗余结构清除**：重复 GPU record、重复 route、重复 fallback 全部收口
4. **3D 渲染链统一**：Geometry3D 与 Surface3D 最终消费同一套 `Appearance<Dim3>` 外观语义
5. **2D/3D 抽象一致**：维度不同，语义 specialization 不同，但概念层与命名层保持统一

### 1.2 不可违反的硬约束

1. **Appearance 是第三个统一 ECS 概念，不能拆。**
2. `Appearance<Dim2>` 与 `Appearance<Dim3>` 允许语义 specialization，但不允许出现“项目一半叫 Appearance，一半叫 Paint / Material”的命名分裂。
3. `Paint` / `Material` 只允许作为**语义说明词**出现，不作为新的主公共类型命名替换 `Appearance`。
4. 所有渲染、运行时、GPU ABI、资源桥接层，都应优先向 `Appearance` 统一命名收敛。
5. 不接受补丁式并存架构；最终目标是**单一语义体系**。

---

## 2. 最终语义定义

## 2.1 Geometry

### 定义

`Geometry` 负责描述对象的**空间形状、拓扑与几何边界**。

### 统一内容

- `Geometry<Dim2>`：Path、Line、Bezier、Stroke/Fill shape
- `Geometry<Dim3>`：Mesh、Triangle、Line、Point、Submesh、LOD

### 应负责

- path / mesh 数据路由
- 拓扑结构
- 几何边界
- tessellation / mesh revision / bounds
- 形状本身的维度相关属性

### 不应负责

- 采样表面内容
- sampler / texture / image source
- 颜色、金属度、粗糙度、阴影、混合模式等外观语义

### 一句话定义

> Geometry = 形状是什么。

---

## 2.2 Surface

### 定义

`Surface` 负责描述对象上的**可采样表面内容及其采样方式**。

### 统一内容

- `Surface<Dim2>`：Image、Sprite、Atlas region
- `Surface<Dim3>`：Texture、Sampler、UV set、可采样表面资源

### 应负责

- sampled source route
- image / texture / atlas / sprite / page
- sampler route
- uv set / uv transform
- filter / address
- surface source revision

### 不应负责

- mesh / path / 拓扑
- blend / alpha / metallic / roughness / shadow / shading model
- 任何外观或材质语义

### 一句话定义

> Surface = 表面上采样什么，以及如何采样。

---

## 2.3 Appearance

### 定义

`Appearance` 是统一视觉外观抽象，负责描述对象**最终如何被渲染成视觉结果**。

### 维度特化语义

- `Appearance<Dim2>`：语义上等价于 2D paint-like 外观系统
- `Appearance<Dim3>`：语义上等价于 3D material-like 外观系统

这里的重点是：

- **语义允许不同**
- **命名保持统一**

即：

- 2D 外观看起来像“paint”
- 3D 外观看起来像“material”
- 但在项目结构中它们都统一属于 `Appearance`

### 应负责

- color / tint / opacity
- gradient / pattern / mask / lut（2D specialization）
- base / emissive / metallic / roughness / normal / occlusion（3D specialization）
- alpha / blend / shading model
- shadow / double sided / depth state 等视觉渲染相关语义
- 外观层对 sampled surfaces 的绑定关系

### 一句话定义

> Appearance = 最终看起来怎样。

---

## 2.4 三者关系

### Geometry 与 Surface

二者是**正交并列**关系：

- Geometry 负责“形状”
- Surface 负责“采样内容”

二者都不是对方的降级版本，也不存在“Surface = Lite”的定义。

### Appearance 与 Geometry / Surface

Appearance 是作用于 Geometry 或 Surface 的**统一视觉外观层**。

因此成立四种合法组合：

- `Geometry<Dim2> + Appearance<Dim2>`
- `Surface<Dim2> + Appearance<Dim2>`
- `Geometry<Dim3> + Appearance<Dim3>`
- `Surface<Dim3> + Appearance<Dim3>`

---

## 3. 命名统一规则

本次重构必须遵守以下命名规则：

### 3.1 统一保留 Appearance 公共命名

保留并强化以下统一命名：

- `Appearance<Dim2>` / `Appearance<Dim3>`
- `AppearanceStyle2D` / `AppearanceStyle3D`
- `AppearanceBinding2D` / `AppearanceBinding3D`
- `AppearanceSystem<Dim2>` / `AppearanceSystem<Dim3>`
- `AppearanceRuntimeSystem<Dim2>` / `AppearanceRuntimeSystem<Dim3>`
- `AppearanceGpuRecord<Dim2>` / `AppearanceGpuRecord<Dim3>`

### 3.2 不新增分裂式公共命名

不引入以下作为新的项目主命名：

- `PaintSystem`
- `MaterialSystem`
- `PaintGpuRecord`
- `MaterialComponent`
- `MaterialSystem`
- `PaintBinding`
- `MaterialBinding`

这些词可以出现在：

- 文档语义解释
- 注释
- specialization 描述

但**不应成为 Appearance 的并行公共类型体系**。

### 3.3 模块命名收敛方向

视觉外观相关逻辑应继续向 `appearance_*` 命名统一，而不是拆分为互不相干的 paint/material 模块族。

允许的方向：

- `appearance_component.hpp`
- `appearance_system.hpp`
- `appearance_runtime_system.hpp`
- `appearance_gpu.hpp`
- `appearance_resolver.hpp`

不推荐继续保留“geometry/material 混名”式文件，除非其职责确实属于 geometry domain。

---

## 4. 当前代码的主要偏差

以下是当前代码与目标语义之间的主要偏差。

## 4.1 Geometry3D 混入 Appearance 语义

文件：`include/vr/ecs/component/geometry_component.hpp`

当前 `GeometryStyle3D` 仍包含：

- `albedo_color`
- `metallic`
- `roughness`
- `normal_scale`
- `shading_model`
- `cast_shadow`
- `receive_shadow`
- `depth_test`
- `depth_write`
- `double_sided`

这些字段本质上不属于 Geometry，而属于 `Appearance<Dim3>`。

### 结论

`GeometryStyle3D` 需要瘦身，只保留几何语义字段。

---

## 4.2 Surface2D / Surface3D 混入 Appearance 语义

文件：`include/vr/ecs/component/surface_component.hpp`

当前 `SurfaceStyle2D` / `SurfaceStyle3D` 仍混入：

- tint / opacity
- blend / premultiplied alpha
- depth_test / depth_write / double_sided

这些字段本质上不属于 Surface，而属于 `Appearance`。

### 结论

Surface 组件必须只保留 sampled-content 语义，视觉外观字段迁移到 Appearance。

---

## 4.3 3D GPU ABI 存在重复结构

文件：

- `include/vr/ecs/system/appearance_runtime_system.hpp`
- `include/vr/geometry/geometry_material_gpu.hpp`

当前存在两套几乎同构结构：

- `AppearanceGpuRecord<Dim3>`
- `geometry::MaterialGpuRecord`

这属于明显冗余。

### 结论

3D 外观 GPU ABI 必须统一收敛到 **`AppearanceGpuRecord<Dim3>`**，并清除并行重复的 `MaterialGpuRecord` 公共结构。

---

## 4.4 3D Surface 渲染仍处于简化实现

文件：`shaders/surface_3d.frag`

当前表面路径仍使用硬编码式外观 fallback，例如：

- `metallic = 0.0`
- `roughness = 0.55`
- `occlusion = 1.0`
- `emissive = vec3(0.0)`

这说明 Surface3D 当前只是实现未 fully appearance-driven，不代表 Surface 语义是 Lite。

### 结论

`SurfaceRenderer3D` 必须最终完整消费 `Appearance<Dim3>` 语义，而不是维持单独简化路径。

---

## 4.5 Route / Runtime 公共字段重复

文件：

- `geometry_component.hpp`
- `surface_component.hpp`

当前 `GeometryRuntimeRoute` 与 `SurfaceRuntimeRoute` 大量重复：

- sort key
- visible
- batch tag
- appearance link
- pipeline bucket
- resource bucket
- dirty flags

### 结论

需要抽出共享 route common，避免双份维护。

---

## 4.6 命名存在“Appearance / Material 混用”问题

文件示例：

- `include/vr/geometry/geometry_material_gpu.hpp`
- `include/vr/geometry/geometry_material_resolver.hpp`

在 Appearance 已经是统一视觉外观抽象的前提下，继续长期保留 geometry/material 混名，会造成职责误导。

### 结论

与统一视觉外观相关的后端结构，命名应逐步向 `appearance_*` 收敛。

---

## 5. 最终目标架构

## 5.1 ECS 组件层

### Geometry

- 仅承载几何形状信息
- 不再存储 appearance-like 字段

### Surface

- 仅承载 sampled-content 路由与采样参数
- 不再存储 appearance-like 字段

### Appearance

- 保持统一公共命名
- 通过 `Dim2` / `Dim3` specialization 表达 2D 与 3D 外观差异
- 承担所有视觉外观语义

---

## 5.2 Runtime / GPU ABI 层

### 统一原则

- 2D 和 3D 的外观运行时都归入 `AppearanceRuntimeSystem`
- 2D 与 3D 的 GPU record 都归入 `AppearanceGpuRecord`
- 不再长期维持 `AppearanceGpuRecord<Dim3>` 与 `MaterialGpuRecord` 双结构

### 目标结果

- `AppearanceGpuRecord<Dim2>`：承载 2D appearance specialization 数据
- `AppearanceGpuRecord<Dim3>`：承载 3D appearance specialization 数据

### 约束

**只有 Appearance 体系对外存在，Material 只作为 3D specialization 语义解释，不再作为并行 ABI 名字。**

---

## 5.3 Renderer 层

### GeometryRenderer2D / SurfaceRenderer2D

- 都消费 `Appearance<Dim2>`
- 2D appearance specialization 负责 color / gradient / pattern / alpha / blend 等

### GeometryRenderer3D / SurfaceRenderer3D

- 都消费 `Appearance<Dim3>`
- 3D appearance specialization 负责 base / emissive / metallic / roughness / normal / occlusion / shadow / alpha 等

### 目标

不允许出现：

- Geometry3D 走完整 appearance 路线
- Surface3D 走简化 hard-coded 路线

最终必须两者统一到同一外观抽象：`Appearance<Dim3>`。

---

## 5.4 Resource / Binding 层

### 统一目标

Surface 负责“sampleable content”，Appearance 负责“如何把这些 sampled surfaces 用于视觉外观”。

因此：

- `Surface` 提供 sampled resource identity / sampling route
- `AppearanceBinding2D` / `AppearanceBinding3D` 提供视觉用途上的绑定关系

### 方向

逐步减少高层 API 中 `image_id` / `texture_id` 的概念割裂，向统一 sampled-surface 语义收敛。

---

## 6. 字段归属裁决

以下裁决是本次重构的硬边界。

## 6.1 Geometry 应保留的字段

### `Geometry<Dim2>`

- path inline data
- fill rule
- line join
- line cap
- miter limit
- stroke width（仅作为几何 stroke shape 属性）
- topology
- tessellation / bounds / revision

### `Geometry<Dim3>`

- mesh route
- submesh / lod / mesh flags
- topology
- line width / point size（如确属 primitive 形状参数）
- bounds / mesh revision

### Geometry 必须移出的字段

- albedo / base color
- opacity
- metallic / roughness / normal scale
- shading model
- alpha / blend
- shadow flags
- depth state
- double sided

---

## 6.2 Surface 应保留的字段

### `Surface<Dim2>`

- image / sprite / atlas route
- uv rect
- size / pivot / flip
- sampler / source revision

### `Surface<Dim3>`

- texture / sampled source route
- sampler route
- uv set
- uv transform
- filter / address
- texture/source revision

### Surface 必须移出的字段

- tint / opacity
- blend / premultiplied alpha
- metallic / roughness / emissive
- alpha mode / shading model
- depth state / shadow / double sided

---

## 6.3 Appearance 应保留的字段

### `Appearance<Dim2>` specialization

- fill / stroke colors
- gradient data
- pattern / mask / lut bindings
- opacity
- blend / alpha
- antialiasing
- premultiplied alpha
- layer

### `Appearance<Dim3>` specialization

- base color / emissive
- metallic / roughness / normal scale / occlusion
- emissive intensity
- alpha cutoff / alpha mode / blend mode
- shading model
- double sided
- cast / receive shadow
- depth test / depth write
- 3D visual sampled-surface bindings

---

## 7. 重构阶段规划

以下阶段按“先定边界，再收结构，再统一渲染，再清冗余”的顺序执行。

## Phase 0：术语冻结与命名对齐

### 目标

先冻结统一术语，防止后续实现继续漂移。

### 工作

1. 冻结三大概念：`Geometry` / `Surface` / `Appearance`
2. 明确 `Appearance<Dim2>` 与 `Appearance<Dim3>` 是统一命名下的语义 specialization
3. 停止任何试图将 Appearance 公共命名拆为 Paint / Material 的后续演化
4. 统一文档与注释口径：
   - 2D appearance specialization = paint-like
   - 3D appearance specialization = material-like
   - 但公共名始终是 `Appearance`

### 完成标准

代码设计讨论、注释、文档中不再出现“拆 Appearance 公共命名”的方案。

---

## Phase 1：清洗组件字段归属

### 目标

把当前混入 Geometry / Surface 的 Appearance 字段迁出。

### 工作

1. 精简 `GeometryStyle3D`
   - 移除外观字段
   - 保留纯几何字段
2. 精简 `SurfaceStyle2D`
   - 移除 tint / opacity / blend 等 appearance 字段
3. 精简 `SurfaceStyle3D`
   - 移除 tint / opacity / depth / double-sided 等 appearance 字段
4. 将迁出字段归入：
   - `AppearanceStyle2D`
   - `AppearanceStyle3D`
5. 调整对应 system 默认值与 dirty flag 逻辑

### 涉及文件

- `include/vr/ecs/component/geometry_component.hpp`
- `include/vr/ecs/component/surface_component.hpp`
- `include/vr/ecs/component/appearance_component.hpp`
- `include/vr/ecs/system/geometry_system.hpp`
- `include/vr/ecs/system/surface_system.hpp`
- `include/vr/ecs/system/appearance_system.hpp`

### 完成标准

- Geometry / Surface 中不再残留 appearance-like 字段
- 默认值与 dirty 逻辑全部基于新边界工作

---

## Phase 2：统一 Appearance Runtime 与 GPU ABI

### 目标

消除 3D appearance GPU record 重复定义。

### 工作

1. 确定 `AppearanceGpuRecord<Dim3>` 为唯一 canonical 3D 外观 GPU ABI
2. 删除或内部吸收 `geometry::MaterialGpuRecord`
3. 把 `geometry_material_gpu.hpp` / `geometry_material_resolver.hpp` 中的视觉外观职责迁移或重命名到 `appearance` 域
4. 统一 renderer / shader / upload path 对 `AppearanceGpuRecord<Dim3>` 的依赖
5. 清理重复 encode / compare / upload 逻辑

### 涉及文件

- `include/vr/ecs/system/appearance_runtime_system.hpp`
- `include/vr/geometry/geometry_material_gpu.hpp`
- `include/vr/geometry/geometry_material_resolver.hpp`
- `src/geometry/geometry_renderer_3d.cpp`
- `shaders/geometry_3d.frag`
- 相关 contract / integration tests

### 完成标准

- 3D 外观只有一套 GPU record 结构
- 全项目视觉外观 ABI 名称统一向 `Appearance` 收口

---

## Phase 3：统一 Geometry / Surface 的 3D Appearance 消费路径

### 目标

让 `SurfaceRenderer3D` 与 `GeometryRenderer3D` 都成为完整 `Appearance<Dim3>` 消费者。

### 工作

1. 重构 `SurfaceRenderer3D` 的外观绑定与上传逻辑
2. 改写 `surface_3d.frag`，从 `AppearanceGpuRecord<Dim3>` 解码外观数据
3. 去除 `surface_3d.frag` 中硬编码式 PBR fallback
4. 统一 Geometry3D 与 Surface3D 的 IBL / alpha / shading / emissive / texture presence 语义
5. 对齐两条渲染路径的 bindless 资源访问方式

### 涉及文件

- `include/vr/surface/surface_renderer_3d.hpp`
- `src/surface/surface_renderer_3d.cpp`
- `shaders/surface_3d.frag`
- `render/appearance_prepare_bridge.hpp`
- 相关 surface runtime / upload systems

### 完成标准

- Surface3D 不再是独立简化外观路径
- Geometry3D / Surface3D 统一消费 `Appearance<Dim3>`

---

## Phase 4：统一 sampled-content 语义

### 目标

把 image / texture 的高层分裂命名逐步收敛到 Surface 语义。

### 工作

1. 梳理 2D image 与 3D texture 的资源身份模型
2. 在高层 API 中逐步引入统一 sampled-surface 语义
3. 调整 `AppearanceBinding2D/3D` 的绑定解释：
   - 绑定的是视觉用途
   - 资源本体统一属于 sampled surface domain
4. 清理只因历史原因存在的 image/texture 公共 API 差异

### 完成标准

- Surface 语义成为“sampleable content”的稳定统一抽象
- Appearance 绑定只表达“外观如何使用 surface 资源”

---

## Phase 5：抽取共享 Route / Runtime Common

### 目标

消除 Geometry 与 Surface 在 runtime route 层的大量重复。

### 工作

1. 抽取共享 route common，例如：
   - sort key
   - visible
   - batch tag
   - user data
   - appearance handle
   - pipeline / resource bucket
   - dirty flags
2. 重新审视 `material_id` 命名
   - 若其语义本质是视觉外观基路由，应改为 `appearance_id` 或更准确的统一名
3. 让 Geometry / Surface 各自只保留 domain-specific route 数据
4. 调整 batch / runtime / upload 系统的读写点

### 完成标准

- GeometryRuntimeRoute / SurfaceRuntimeRoute 仅保留必要差异
- 公共调度逻辑只维护一份

---

## Phase 6：清除历史 fallback、兼容层与冗余代码

### 目标

结束过渡状态，形成单一稳定实现。

### 工作

1. 删除 Geometry3D 从自身 style 偷读外观字段的 fallback 逻辑
2. 删除 Surface3D 简化外观硬编码路径
3. 清理历史适配 alias、兼容 helper、重复 encode utilities
4. 清理由于旧命名遗留的误导性文件名 / 注释 / 测试描述
5. 统一渲染统计与 debug 输出中的视觉外观术语

### 完成标准

- 项目中不存在双轨视觉外观体系
- 不存在“临时转接结构长期留存”的状态

---

## Phase 7：测试、验证与验收收尾

### 目标

保证重构后稳定性、性能与语义一致性。

### 工作

1. 更新 ECS component/system tests
2. 更新 runtime / link / coordinator tests
3. 更新 shader contract tests
4. 更新 renderer integration tests
5. 对 Geometry3D / Surface3D / 2D visual paths 做回归验证
6. 核验性能：
   - record 上传量
   - descriptor/bindless 访问路径
   - sort / batch 复用效率
   - draw call 与 patch upload 变化

### 完成标准

- 所有核心测试通过
- 不再出现由语义混乱引起的 shader / runtime 分歧
- 性能无明显回退，或结构优化后更稳定

---

## 8. 文件级重构优先顺序

推荐按以下优先顺序推进，以减少返工：

1. `include/vr/ecs/component/appearance_component.hpp`
2. `include/vr/ecs/component/geometry_component.hpp`
3. `include/vr/ecs/component/surface_component.hpp`
4. `include/vr/ecs/system/appearance_system.hpp`
5. `include/vr/ecs/system/geometry_system.hpp`
6. `include/vr/ecs/system/surface_system.hpp`
7. `include/vr/ecs/system/appearance_runtime_system.hpp`
8. `include/vr/geometry/geometry_material_gpu.hpp`（收口或删除）
9. `include/vr/geometry/geometry_material_resolver.hpp`（迁移或重命名职责）
10. `src/geometry/geometry_renderer_3d.cpp`
11. `src/surface/surface_renderer_3d.cpp`
12. `shaders/geometry_3d.frag`
13. `shaders/surface_3d.frag`
14. `tests/cases/*appearance*`
15. `tests/cases/*geometry*`
16. `tests/cases/*surface*`
17. 相关 integration / contract tests

---

## 9. 最终验收标准

当以下条件全部成立时，视为本次重构完成：

### 9.1 语义验收

- `Geometry` 只管形状
- `Surface` 只管可采样表面内容
- `Appearance` 统一管视觉外观
- 不再存在职责交叉字段

### 9.2 命名验收

- 视觉外观公共命名统一为 `Appearance`
- 不存在并行的 Paint / Material 公共命名体系
- 2D/3D specialization 仅体现在维度和字段语义，不体现在公共概念拆裂上

### 9.3 架构验收

- 3D 外观 GPU ABI 只保留一套 canonical `AppearanceGpuRecord<Dim3>`
- Surface3D 与 Geometry3D 都完整接入 `Appearance<Dim3>`
- Runtime route 公共字段去重完成

### 9.4 清洁度验收

- 删除历史 fallback、补丁层、重复 record、重复 helper
- 文件命名与职责不再互相误导
- 不残留“过渡层长期挂账”的代码

### 9.5 性能验收

- 无多余 descriptor/record 编码路径
- 无重复 upload/patch 路径
- 绑定与批处理路径更一致
- 重构后不引入额外结构性性能损失

---

## 10. 一句话总纲

> 本次重构的最终目标，不是把 Appearance 拆成 Paint / Material 两套并行体系，而是在 **Appearance 统一命名不动** 的前提下，彻底厘清 `Geometry / Surface / Appearance` 的职责边界，清除历史冗余，让 `Appearance<Dim2>` 与 `Appearance<Dim3>` 成为同一抽象在不同维度下的稳定 specialization。

---

## 11. 当前推荐执行顺序

如果从现在开始进入实现，建议按以下顺序落地：

1. **先完成 Phase 1**：字段归属清洗
2. **再做 Phase 2**：统一 3D appearance GPU ABI
3. **然后做 Phase 3**：打通 Surface3D 的完整 appearance 消费路径
4. **最后做 Phase 5 + Phase 6**：清路由重复、删历史兼容与 fallback

原因：

- 不先清字段边界，后面的 runtime / shader 会继续串味
- 不先统一 3D 外观 ABI，Surface3D 很难真正接入完整 appearance 语义
- 不先统一 appearance 消费链，最终还是会残留“双轨渲染逻辑”

---

## 12. 结论

最终架构应当严格收敛为：

- `Geometry`：形状
- `Surface`：可采样表面内容
- `Appearance`：统一视觉外观

并且：

- `Appearance<Dim2>` = 2D appearance specialization
- `Appearance<Dim3>` = 3D appearance specialization
- **语义可特化，命名不拆裂**

这是本项目后续所有重构、清理、性能优化与渲染统一化工作的基准线。
