# Vulkan Runtime 强化清单（Geometry / Text3D）

更新时间：2026-04-27

> 目标：在保持 ECS 纯 POD 组件 + `System<Dim>` 职责边界不破坏的前提下，持续提高性能、可维护性与渲染质量。

---

## 1) Geometry 强化清单

### 1.1 已完成（本轮已落地）

- [x] Runtime 构建支持外部签名提示（避免每帧全量哈希）  
  - `Geometry2DRuntimeBuildHint`
  - `Geometry3DRuntimeBuildHint`
- [x] 3D Transform-only 路径支持脏索引增量更新（`O(k)`）  
  - 通过 `transform_dirty_component_indices + count` 仅更新受影响实例 world matrix。
- [x] 缓存结构补强  
  - 新增 `component_to_instance_index` 映射缓存，避免 transform-only 回扫全实例。
- [x] 统计可观测性补强  
  - 新增 hint 命中与 dirty-hint 使用统计字段，便于 bench / 线上日志验证。
- [x] 单测覆盖补强  
  - 2D 外部签名 hint 复用路径测试
  - 3D dirty-hint 单实例更新路径测试
- [x] Bench 覆盖补强  
  - 新增 `EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint`

### 1.2 待强化（按优先级）

#### P0（建议优先）

- [ ] 将 `BuildHint` 自动接入 ECS Runtime 调度层  
  - 由调度器统一维护 scene revision / dirty list，业务层不手写 hint。
- [ ] Geometry Runtime 多线程构建（可配置 worker 数）  
  - 目标：重建路径在大规模实体下线性扩展。
- [ ] 引入可见性裁剪前置（frustum culling）  
  - 在 batch 前先减少参与排序与实例生成的数量。

#### P1（高收益）

- [ ] DrawIndirect / MultiDrawIndirect 路径  
  - 目标：降低 CPU 提交成本与状态切换开销。
- [ ] GPU 侧 instance 数据布局压缩（按 shader 实际读取字段裁剪）  
  - 目标：减少上传带宽与缓存压力。
- [ ] 材质/几何资源热路径无锁化（或低锁化）  
  - 目标：并行 Prepare 下避免锁竞争。

#### P2（质量增强）

- [ ] 遮挡裁剪（Hi-Z / software occlusion）  
- [ ] LOD 策略与距离分桶  
- [ ] 资源碎片整理与后台紧缩策略（不影响前台帧稳定）

---

## 2) Text3D 完整性评估

### 2.1 当前已具备能力（可用于生产级基础链路）

- [x] FreeType + Atlas + Upload + Vulkan Renderer 3D 端到端链路
- [x] 3D 文本实例化渲染（字形四边形在 3D 空间中布局）
- [x] `world_size` / `max_screen_size_px` 尺寸控制
- [x] 颜色、描边、SDF/Bitmap 路径切换
- [x] billboard 与非 billboard（跟随物体变换）
- [x] 深度测试/深度写入分批
- [x] reverse-Z 管线分支
- [x] kerning（基础字偶距）
- [x] 单元测试 + 3D 集成测试覆盖

### 2.2 当前“尚未完整”的点（如果目标是高级文本系统）

- [ ] 真正体积字（Extruded / Bevel / 厚度）  
  - 目前是“3D 空间中的文本面片”，不是体积网格字体。
- [ ] 复杂排版 shaping（HarfBuzz 级）  
  - 当前不含 ligature、复杂脚本 shaping、完整 bidi。
- [ ] 字体回退链（fallback chain）与 emoji/彩色字体体系
- [ ] MSDF/MTSDF 高质量矢量距离场流程（超大缩放下边缘质量更稳）
- [ ] Text3D 专项 benchmark 与性能门禁基线

---

## 3) 进入“3DText 完整版”建议里程碑

### M1（强可用）

- [ ] Text3D benchmark（CPU build + GPU draw 分离指标）落地
- [ ] fallback chain + 字体缺字策略
- [ ] atlas 页面抖动与重建策略压测（长时间运行稳定性）

### M2（高质量）

- [ ] MSDF/MTSDF 管线
- [ ] 复杂排版引擎接入（shaping / bidi）

### M3（高级表现）

- [ ] Extruded 体积字 Geometry 管线（与 `Geometry<Dim3>` 统一）
- [ ] 光照/阴影一致性（与场景材质系统对齐）

