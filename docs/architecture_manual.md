# VulkanRender_New 项目架构手册

## 1. 项目概述

**项目名称**: VulkanRender_New (Melosyne 引擎 Vulkan 渲染模块)
**语言标准**: C++23 (预留 C++20 Module 迁移 — 见 `feature/cpp20-modules` 分支)
**构建系统**: CMake 3.21+
**API 目标**: Vulkan 1.3
**主要平台**: Windows (SDL3 后端)
**调试工具**: CrashTracer (Benchmark 崩溃回溯，日志输出至 `crash_reports/`)

项目是一个基于 Vulkan 1.3 的实时 2D/3D 图形渲染框架，由以下核心层次组成：

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Examples (Demo)                                │
├──────────────────────────────────────────────────────────────────────┤
│                    Render Runtime Host                                │
│  ┌──────────┬──────────┬──────────┬──────────┬────────────────────┐  │
│  │ Loop     │ Swapchain│  Sync    │ Command  │ Retire / RetireBus │  │
│  │ Host     │  Host    │  Host    │  Host    │                    │  │
│  ├──────────┼──────────┼──────────┼──────────┼────────────────────┤  │
│  │ Upload   │Descriptor│ Pipeline │ Frame    │ Render Runtime     │  │
│  │ Host     │  Host    │  Host    │ Prepare  │ Prepare Stages     │  │
│  └──────────┴──────────┴──────────┴──────────┴────────────────────┘  │
├──────────────────────────────────────────────────────────────────────┤
│                    Frame Coordinators                                 │
│  ┌──────────────┬──────────────┬──────────────┬────────────────────┐ │
│  │ Appearance   │ Light        │ Shadow       │ Light-Shadow Link  │ │
│  │ Coordinator  │ Coordinator  │ Coordinator  │ Coordinator        │ │
│  ├──────────────┼──────────────┼──────────────┼────────────────────┤ │
│  │ Shadow Atlas │                                                                     │
│  │ Coordinator  │                                                                     │
│  └──────────────┴──────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────────────┤
│                    ECS (Entity Component System)                      │
│  ┌──────────┬──────────┬──────────┬──────────┬──────────────────┐   │
│  │ Transform │ Camera   │ Bounds   │ Culling  │ Spatial Math     │   │
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Geometry  │ Surface  │ Text     │ Light    │ Shadow           │   │
│  │ System    │ System   │ System   │ System   │ System           │   │
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Geometry  │ Surface  │ Text     │ Light    │ Shadow           │   │
│  │ Batch     │ Batch    │ Batch    │ Culling  │ Caster           │   │
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Geometry  │ Surface  │ Text     │ Light    │ Shadow           │   │
│  │ Runtime   │ Runtime  │ Runtime  │ Runtime  │ Runtime          │   │
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Appear.   │ Appear.  │ Appear.  │ Surface  │                  │   │
│  │ System    │ Link     │ Runtime  │ Upload   │                  │   │
│  └──────────┴──────────┴──────────┴──────────┴──────────────────┘   │
├──────────────────────────────────────────────────────────────────────┤
│                    Renderer Backends                                  │
│  ┌──────────┬──────────┬──────────┬────────────────────────────────┐ │
│  │ Text     │ Geometry │ Surface  │ Shadow                         │ │
│  │Renderer  │ Renderer │ Renderer │ Renderer 2D/3D                 │ │
│  │ 2D / 3D  │ 2D / 3D  │ 2D / 3D  │                                │ │
│  └──────────┴──────────┴──────────┴────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────────────┤
│                    Resource & Domain Hosts                            │
│  ┌──────────┬──────────┬──────────┬──────────┬──────────────────┐   │
│  │  Buffer  │  Image   │  GPU     │ Sampler  │ Geometry         │   │
│  │  Host    │  Host    │  Memory  │  Host    │ Resource/Material│   │
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Geometry │ Surface  │ Glyph    │ FreeType │ Glyph Atlas      │   │
│  │ Image    │ Image    │ Upload   │  Host    │ Host             │   │
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Geometry │ Surface  │ Light    │ Shadow   │                  │   │
│  │ Upload   │ Upload   │ Upload   │ Atlas    │                  │   │
│  └──────────┴──────────┴──────────┴──────────┴──────────────────┘   │
├──────────────────────────────────────────────────────────────────────┤
│                    Platform Layer                                     │
│  ┌───────────────────┬────────────────────────────────────────────┐  │
│  │  VulkanContext     │  WindowSurface (SDL3)                      │  │
│  │  RenderHost        │                                            │  │
│  └───────────────────┴────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────────┤
│              External Dependencies                                    │
│  Vulkan SDK 1.3 | SDL 3.4.0 | FreeType | MemoryCenter (mimalloc)    │
│  fast_math | McVector | CrashTracer                                  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 2. 外部依赖

| 依赖库 | 用途 | 路径 |
|--------|------|------|
| **Vulkan SDK 1.3** | 图形 API | 系统安装 (VULKAN_SDK) |
| **SDL 3.4.0** | 窗口创建、事件处理、Vulkan Surface | `vender/SDL-release-3.4.0` |
| **FreeType** | TrueType/OpenType 字体光栅化 | `freetype-master/` (项目内) |
| **MemoryCenter** | GPU 内存分配 (Buddy Allocator) + mimalloc | `MemoryCenterNew/` |
| **McVector** | 自定义 STL 兼容容器 (基于 mimalloc) | `Vector_New/` |
| **fast_math** | SIMD 优化的矩阵/向量数学库 | `Math/fast_math/` |
| **CrashTracer** | 崩溃追踪与堆栈回溯 (Bench 可选) | `CrashTracer/` |
| **glslangValidator** | GLSL 编译 → SPIR-V | Vulkan SDK Bin 目录 |

---

## 3. 层次架构详解

### 3.1 Platform 层 (`vr/platform` / `vr/vulkan_context`)

**职责**: 窗口创建、Vulkan 实例/设备初始化、平台抽象。

| 组件 | 说明 |
|------|------|
| `VulkanContext` | Vulkan 实例、设备、队列族的生命周期管理。支持 Validation Layers、Debug Messenger、单次命令提交。 |
| `WindowSurface<BackendTag>` | 窗口创建 (SDL3)、VkSurfaceKHR 生命周期、事件轮询。支持 resize、high-dpi。 |
| `RenderHost<BackendTag>` | 组合 `VulkanContext` + `WindowSurface`，统一初始化/关闭流程。 |

**命名空间**: `vr` / `vr::platform`
**BackendTag 机制**: 使用 `BackendTag<BackendKind>` 模板参数实现编译期多态，当前仅实现 SDL3。

### 3.2 Resource 层 (`vr/resource`)

**职责**: GPU 资源创建、内存分配、采样器管理。

| 组件 | 说明 |
|------|------|
| `GpuMemoryHost` | Buddy Allocator 内存分配器 (64MB 块, 256 子分区)。支持 buffer/image 绑定、dedicated allocation、flush/invalidate、trim。 |
| `BufferHost` | GPU Buffer 创建/销毁/映射/Flush/Invalidate。纯静态方法类。 |
| `ImageHost` | GPU Image 创建/销毁、ImageView 创建。支持 mip、array layers、默认 view。 |
| `SamplerHost` | VkSampler 缓存 (基于哈希去重)。支持 anisotropy、border color、compare 等全部 Vulkan 采样器特性。 |

### 3.3 Render 层 (`vr/render`)

**职责**: 帧循环、交换链管理、命令缓冲、描述符/管线管理、上传系统、帧协调器。

#### 3.3.1 帧循环核心

| 组件 | 说明 |
|------|------|
| `SwapchainHost<WindowSurfaceT>` | 交换链生命周期：创建、重建、Acquire/Present。支持 VSync、Mailbox/FIFO、延迟销毁 (deferred retire)、Framebuffer 自动创建。 |
| `FrameSyncHost<framesInFlight>` | 帧同步：信号量/Fence 管理、帧索引轮转、提交值追踪。支持 Vulkan 1.3 Synchronization2。 |
| `FrameCommandHost` | 每帧 CommandPool + CommandBuffer 池化管理。支持按需增长。 |
| `RenderLoopHost<...>` | 主帧循环：组合 Swapchain + Sync + Command + Retire。Tick() 驱动单帧录制与呈现。支持 Recorder 模式 (模板回调)。 |
| `FrameRetireHost` | 延迟资源回收：基于提交值的退役队列，避免 use-after-submit。 |
| `RetireBus` | 退休总线：管道化延迟销毁操作，支持跨模块发布延迟回收事件。 |

#### 3.3.2 渲染资源管理

| 组件 | 说明 |
|------|------|
| `DescriptorHost` | 描述符池管理 + DescriptorSetLayout 缓存 (哈希去重)。支持 buffer/image/texel buffer writes。每帧独立池，可选验证。 |
| `PipelineHost` | ShaderModule + PipelineLayout + Graphics/Compute Pipeline 缓存 + 延迟编译队列。支持 VkPipelineCache 持久化。可配置 Pipeline 预热。 |
| `UploadHost` | Staging Buffer 上传：Allocate/Write/RecordCopy。支持 Transfer Queue 异步上传 (带 Semaphore 跨队列同步)。支持 Synchronization2 barrier。 |
| `RuntimePrepareContext` | 帧准备上下文结构体，聚合所有运行时子系统指针，传递至 `PrepareFrame` 回调。包含 VulkanContext、帧索引、提交值、GpuMemoryHost、UploadHost、DescriptorHost、PipelineHost、SamplerHost、FreeTypeHost、GlyphAtlasHost、GlyphUploadHost。 |

#### 3.3.3 帧协调器 (Frame Coordinators)

帧协调器是位于 RenderRuntimeHost 和 ECS 系统之间的编排层，负责每帧的数据准备、排序、缓存和跨系统协调。

| 协调器 | 说明 |
|--------|------|
| `AppearanceFrameCoordinator` | Appearance 帧协调。管理 Appearance 组件的帧级生命周期：脏记录扫描、链接扫描、与 Geometry/Surface 渲染器协调。支持 2D 和 3D 双渲染器模式。 |
| `LightFrameCoordinator` | Light 帧协调。收集活跃光源组件、处理光源 GPU 数据准备、协调光源上传。 |
| `ShadowFrameCoordinator` | Shadow 帧协调。管理 Shadow Caster 组件收集、Shadow Map 渲染准备、与光源的关联。 |
| `LightShadowLinkCoordinator` | Light-Shadow 链接协调。建立光源与阴影投射器之间的关联关系。 |
| `ShadowAtlasBindingCoordinator` | Shadow Atlas 绑定协调。管理 Shadow Map Atlas 的纹理绑定和描述符更新。 |

#### 3.3.4 准备阶段 (Prepare Stages)

| 阶段 | 说明 |
|------|------|
| `AppearancePrepareBridge` | Appearance 准备桥接：连接 AppearanceFrameCoordinator 和 RenderRuntime 的帧准备流程。 |
| `AppearancePrepareStage` | Appearance 准备阶段：执行 Appearance 数据的帧级准备操作。 |
| `LightPrepareStage` | Light 准备阶段：执行光源数据的帧级准备。 |
| `LightShadowLinkStage` | Light-Shadow 链接阶段：执行光源-阴影关联的帧级操作。 |
| `ShadowPrepareStage` | Shadow 准备阶段：执行阴影数据的帧级准备。 |

### 3.4 ECS 层 (`vr/ecs`)

**职责**: 纯数据组件 + 静态方法系统，零虚函数、零继承，全 POD 组件设计。

#### 3.4.1 概念基础

| 文件 | 说明 |
|------|------|
| `concept/dimension.hpp` | `Dim2` / `Dim3` 维度标签，`DimensionTag` concept。 |
| `component/spatial_types.hpp` | 数学类型别名：`Float2/3/4`, `Quaternion`, `Affine2x3`, `Matrix4x4` (D3D 列主序)。 |

#### 3.4.2 ECS 组件 (全 POD)

| 组件 | Dim2 特有 | Dim3 特有 | 共享 |
|------|----------|----------|------|
| **Transform** | position+rotation+scale (2D) | position+quaternion+scale (3D) | hierarchy link, dirty flags, local/world matrix, revision |
| **Camera** | orthographic height, zoom, y_down | projection mode, fov, reverse_z | view/projection/VP matrices, culling mask, viewport |
| **Bounds** | 2D AABB (local/world) | 3D AABB (local/world) | center, extents, radius, visibility mask, revision tracking |
| **Geometry** | path inline data, 2D style (fill/stroke/aa) | mesh route, 3D style (PBR) | sort key, render route, dirty flags, bounds |
| **Surface** | image/sprite source, 2D blend mode | texture route, filter/address mode | sort key, render route, tint, opacity, size/pivot |
| **Text** | pixel size, 2D style | world size, billboard, 3D style | UTF8 buffer, font/material/batch data, color, align, SDF |
| **Light** | 2D point light (position+radius+color) | 3D point/spot/directional | intensity, color, range, shadow flags, culling mask |
| **Shadow** | 2D shadow caster (occluder shape) | 3D shadow caster (mesh route) | shadow map slot, bias, filter mode, depth format |
| **Appearance** | 2D visibility+link group | 3D visibility+link group | appearance record, dirty tracking, link handle |

#### 3.4.3 ECS 系统

##### 核心数学与空间系统

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `spatial_math` | 数学工具函数 | 矩阵组合/求逆/相乘, 视锥体/正交投影构建, 四元数工具 |
| `TransformSystem<Dim>` | 层级变换 | 父子链接、Detach/Attach、脏标记传播、层级更新 (防循环)、平坦层级快速路径 |
| `CameraSystem<Dim>` | 相机矩阵 | 投影重建 (正交/透视)、View 矩阵 (从 Transform 逆)、ViewProjection 合并 |
| `BoundsSystem<Dim>` | AABB 世界空间变换 | 中心+范围方法 (避免 8 角点全变换)、多次更新变体 (全部/按索引)、Transform 分离快速路径 |
| `CullingSystem<Dim>` | 视锥体裁剪 | 视锥体平面提取 (从 VP 矩阵)、球体拒绝 (快速路径) + AABB 细化、可见性掩码过滤、候选列表接口、可见集签名 (哈希)、epoch 机制 |

##### 几何系统组

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `GeometrySystem<Dim>` | 几何排序与路由 | 64 位排序键: [pass:2][material:16][geometry:16][layer/depth:16][batch:14] |
| `GeometryBatchSystem<Dim>` | 批次收集与排序 | BuildAndSort: 收集可见几何、排序、去重 |
| `GeometryPathSystem<Dim2>` | 路径命令解析 | MoveTo/LineTo/QuadTo/CubicTo/Close 命令视图、数据哈希 |
| `GeometryMeshSystem<Dim3>` | 3D 网格路由 | MeshRoute 结构与操作 |
| `GeometryRuntimeSystem<Dim>` | GPU 数据生成 | 2D: 路径→线段图元 + 合并批次。3D: 组件→GPU 实例 (世界矩阵+材质+包围盒) + 批次合并 + 缓存 (签名追踪)、增量 Transform 更新 |

##### Surface 系统组

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `SurfaceSystem<Dim>` | Surface 路由 | 64 位排序键: [pass:2][material:16][surface:16][minor:16][batch:14]、可见性、source route |
| `SurfaceBatchSystem<Dim>` | Surface 批次 | 收集与排序 |
| `SurfaceRuntimeSystem<Dim>` | Surface 运行时 | GPU 实例生成 + 批次合并 + 缓存 |
| `SurfaceUploadPlanSystem` | Surface 上传计划 | 上传脏数据页面调度 |

##### 文本系统组

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `TextSystem<Dim>` | 文本排序与路由 | 64 位排序键: [pass:2][material:16][font:12][atlas:10][minor:16][batch:8]、UTF8 revision 追踪 |
| `TextBatchSystem<Dim>` | 文本批次 | 收集与排序 |
| `TextRuntimeSystem<Dim>` | 文本运行时 | 字形实例生成 + 批次合并 + 缓存 |
| `TextRender3DSystem` | 3D 文本特殊处理 | Billboard、深度测试等 3D 特定逻辑 |

##### 光照系统组

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `LightSystem<Dim>` | 光照路由与排序 | 光源组件管理、阴影关联、排序键 |
| `LightCullingSystem` | 光源视锥体裁剪 | 光源可见性判断、Tile-based 裁剪预备 |
| `LightRuntimeSystem<Dim>` | 光照运行时 | GPU 光源数据生成 (UBO/SSBO)，光照参数打包 |
| `LightGpuLayout` | 光照 GPU Layout | 光源 UBO/SSBO 的内存布局定义 |

##### 阴影系统组

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `ShadowSystem<Dim>` | 阴影路由与排序 | Shadow Caster 管理、Shadow Map 槽分配 |
| `ShadowCasterSystem<Dim>` | 阴影投射器收集 | 可见投射器筛选、去重 |
| `ShadowRuntimeSystem<Dim>` | 阴影运行时 | Shadow Map 渲染调度、深度变换矩阵生成 |
| `ShadowGpuLayout` | 阴影 GPU Layout | Shadow Map 相关 GPU 内存布局定义 |

##### Appearance 系统组

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `AppearanceSystem<Dim>` | Appearance 组件管理 | 可见性记录、脏标记、link 句柄 |
| `AppearanceLinkSystem` | Appearance 链接 | 链接 Geometry/Surface 到 Appearance 记录、跨渲染器协调 |
| `AppearanceRuntimeSystem<Dim>` | Appearance 运行时 | 运行时 appearance 数据生成、缓存、与渲染器批量对接 |

### 3.5 渲染器模块

#### 3.5.1 Text 渲染器 (`vr/text`)

| 组件 | 说明 |
|------|------|
| `FreeTypeHost` | FreeType 库初始化、字体 face 加载 (内存/文件)、字形指标/位图/SDF 渲染。 |
| `GlyphAtlasHost` | 字形图集：运行时页面分配、字形打包、脏页追踪、双层缓存 (粗→细)。 |
| `GlyphUploadHost` | 字形上传：将脏图集页面通过 UploadHost 上传至 GPU Image。 |
| `TextRenderer2D` | 2D 文本渲染：绑定图集纹理、字形顶点/索引缓冲、Push Constant。支持 SDF/Outline。 |
| `TextRenderer3D` | 3D 文本渲染：世界空间文本渲染，支持 Billboard、深度测试。 |

#### 3.5.2 Geometry 渲染器 (`vr/geometry`)

| 组件 | 说明 |
|------|------|
| `GeometryResourceHost` | 几何资源注册：mesh/image/material 资源 ID 管理。 |
| `GeometryMaterialHost` | 材质系统：PBR 材质 (albedo/metallic/roughness)、材质参数缓冲区上传。 |
| `GeometryImageHost` | 几何图像：纹理 Image/View/Sampler 绑定、Descriptor 写入。 |
| `GeometryUploadHost` | 几何上传：顶点/索引缓冲区上传、路径数据上传。 |
| `GeometryRenderer2D` | 2D 几何渲染：路径线段→顶点缓冲、描边/填充、抗锯齿、Push Constant。集成 Appearance 渲染支持。 |
| `GeometryRenderer3D` | 3D 几何渲染：GPU 实例化绘制、PBR 材质绑定、Shadow mapping 支持。集成 Appearance 渲染支持。 |

#### 3.5.3 Surface 渲染器 (`vr/surface`)

| 组件 | 说明 |
|------|------|
| `SurfaceImageHost` | Surface 图像/精灵管理：图集页面、采样器绑定、Descriptor 写入。 |
| `SurfaceUploadHost` | Surface 上传：图像数据上传至 GPU、脏区域追踪。 |
| `SurfaceRenderer2D` | 2D Surface 渲染：精灵绘制、混合模式、UV 变换、tint color。 |
| `SurfaceRenderer3D` | 3D Surface 渲染：世界空间 Surface、纹理过滤、双面渲染。 |

#### 3.5.4 Light/Shadow 渲染器 (`vr/light`, `vr/shadow`)

| 组件 | 说明 |
|------|------|
| `LightShadowUploadHost` | Light-Shadow 数据上传：光源数据 (UBO)、阴影矩阵、Atlas 引用的 GPU 上传。 |
| `ShadowAtlasHost` | Shadow Map Atlas 管理：Atlas 页面分配、阴影贴图打包、脏页追踪。 |
| `ShadowRenderer2D` | 2D 阴影渲染器：2D 遮挡物深度图渲染、光源关联。 |
| `ShadowRenderer3D` | 3D 阴影渲染器：3D Shadow Map 渲染、深度管线。 |

---

## 4. 帧协调器管线流程

Appearance、Light、Shadow 三套协调器协同工作，形成完整的每帧渲染准备管线：

```
RenderRuntime::Tick()
  │
  ├── FrameSync::Acquire  → 获取下一帧槽位
  ├── FrameCommand::Begin  → 开始命令录制
  │
  ├── AppearanceFrameCoordinator::Prepare
  │   ├── AppearancePrepareStage    (扫描脏记录)
  │   └── AppearancePrepareBridge   (桥接至渲染器)
  │
  ├── LightShadowLinkCoordinator::Prepare
  │   ├── LightPrepareStage         (收集光源)
  │   ├── ShadowPrepareStage        (收集投射器)
  │   └── LightShadowLinkStage      (建立关联)
  │
  ├── ShadowFrameCoordinator::Prepare
  │   └── ShadowAtlasBindingCoordinator  (Atlas 绑定)
  │
  ├── Renderer::Render  (Text/Geometry/Surface/Shadow)
  │
  ├── FrameSync::Submit  → 提交命令
  └── FrameRetire::Collect  → 延迟回收
```

---

## 5. 着色器管线

所有着色器位于 `shaders/` 目录，通过 CMake 自定义命令编译为 SPIR-V 后嵌入 C++ 头文件。

### 5.1 着色器源文件

| 着色器 | 用途 |
|--------|------|
| `text_2d.vert/.frag` | 2D 文本渲染 (SDF + 图集采样) |
| `text_3d.vert/.frag` | 3D 文本渲染 (Billboard + 深度) |
| `geometry_2d.vert/.frag` | 2D 几何 (线段 + 填充 + 抗锯齿) |
| `geometry_3d.vert/.frag` | 3D 几何 (PBR + 实例化) |
| `surface_2d.vert/.frag` | 2D Surface (精灵 + 混合模式) |
| `surface_3d.vert/.frag` | 3D Surface (纹理 + 过滤) |
| `shadow_depth_2d.vert` | 2D 阴影深度 (遮挡物深度图) |
| `shadow_depth_3d.vert` | 3D 阴影深度 (深度管线) |

### 5.2 共享 GLSL 头文件 (`shaders/include/`)

通过 `#include` 在着色器间共享通用函数，避免重复代码。

| 文件 | 说明 |
|------|------|
| `shaders/include/vr/common/math.glsl` | 通用数学工具函数 (矩阵变换、坐标转换)。 |
| `shaders/include/vr/text/text_shading.glsl` | 文本着色函数 (SDF 边缘平滑、轮廓、颜色混合)。被 `text_2d.frag` 和 `text_3d.frag` 引用。 |

### 5.3 着色器工具链

| 工具 | 说明 |
|------|------|
| `tools/spv_to_header.py` | SPIR-V 二进制 → C++ 头文件嵌入。 |
| `tools/spv_reflect_to_json.py` | SPIR-V 反射 → JSON。提取 Descriptor Set Layout、Push Constants、Entry Points。 |
| `tools/shader_contract_check.py` | 着色器合约检查。对比 C++ 端管线定义与 SPIR-V 反射输出，检测绑定冲突。 |
| `tools/shader_contract_summary.py` | 着色器合约摘要报告生成。 |

---

## 6. 构建系统架构

### 6.1 CMake 预设

`CMakePresets.json` 定义跨平台的 configure/build/test presets，统一开发环境配置。支持 VS/Clang/GCC 等生成器、Debug/Release 构建类型、以及测试/基准构建选项的预配置。

### 6.2 目标依赖图

```
vulkan_init (STATIC lib)
├── Vulkan::Vulkan
├── Center.Memory.Headers (MemoryCenter)
├── freetype
└── vr_generated_shaders (shader SPIR-V headers)

vulkan_platform_sdl (INTERFACE lib)
└── vulkan_init + SDL3::SDL3

Demo executables → vulkan_platform_sdl
Tests executable   → vulkan_platform_sdl
Bench executable   → vulkan_platform_sdl + CrashTracer
```

### 6.3 关键 CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `VR_BUILD_TESTS` | ON (顶层) | 构建测试套件 |
| `VR_BUILD_BENCHMARKS` | OFF | 构建基准测试套件 |
| `VR_BENCH_ENABLE_CRASH_TRACER` | ON | Benchmark 崩溃追踪 |

---

## 7. C++20 模块迁移 (`feature/cpp20-modules` 分支)

仓库的 `feature/cpp20-modules` 分支包含所有 10 个核心库的 C++20 `.cppm` 模块接口单元：

```
include/vr/modules/
├── vr.types/vr.types.cppm        (基础类型, McVector, 空间类型)
├── vr.context/vr.context.cppm    (VulkanContext 基础设施)
├── vr.platform/vr.platform.cppm  (WindowSurface, RenderHost)
├── vr.resource/vr.resource.cppm  (GpuMemoryHost, Buffer/Image/Sampler)
├── vr.render/vr.render.cppm      (Swapchain, FrameSync, Upload, Descriptor, Pipeline, RenderLoop)
├── vr.ecs/vr.ecs.cppm            (所有 ECS 组件 + 系统, 含 Appearance/Geometry/Surface)
├── vr.text/vr.text.cppm          (FreeType/Glyph + Text ECS 系统)
├── vr.geometry/vr.geometry.cppm  (Geometry 渲染器)
├── vr.surface/vr.surface.cppm    (Surface 渲染器)
├── vr.runtime/vr.runtime.cppm    (RenderRuntimeHost)
└── vr.detail/vr_module_fwd.hpp   (全局模块片段共享头文件)
```

模块依赖图 (无循环):
```
vr.types
  └─► vr.context ──► vr.resource ──► vr.render ──► vr.runtime
        │                                │
        ├─► vr.ecs ◄─────────────────────┤
        │                                │
        ├─► vr.platform ─────────────────┤
        │                                │
        ├─► vr.text ◄────────────────────┤
        │    (含 Text ECS 系统)           │
        │                                │
        ├─► vr.geometry ◄────────────────┤
        │                                │
        └─► vr.surface ◄─────────────────┘
```

---

## 8. 测试与基准测试

### 8.1 测试 (`tests/`)

基于自定义轻量框架 (`test_framework.hpp/cpp`)。约 41 个测试文件，覆盖：
- ECS 组件/系统单元测试 (Transform, Camera, Bounds, Culling, Geometry, Surface, Text, Light, Shadow, Appearance)
- 运行时集成测试
- GPU 资源类型测试
- FreeType/Glyph 渲染测试
- FrameRetire 回收测试
- Light/Shadow 协调器测试
- Appearance 协调器测试
- Shadow Renderer 生命周期测试

### 8.2 基准测试 (`bench/`)

基于自定义基准框架 (`bench_framework.hpp/cpp`)，支持：
- 崩溃追踪器集成
- JSON 基线存储/对比
- 门控检查 (`run_bench_gate.ps1`)
- 多轮快照 (`_snapshot.md`)

---

## 9. 关键设计决策

1. **纯静态 ECS**: 系统全为 `static` 方法类，零虚函数开销，组件全 POD 且 cache-line 友好。
2. **模板化维度**: `Dim2`/`Dim3` 通过模板特化而非运行时多态，编译期类型安全。
3. **延迟销毁**: `FrameRetireHost` 按提交值延迟销毁 GPU 资源，避免 use-after-submit。`RetireBus` 管道化延迟回收事件。
4. **哈希去重**: `PipelineHost`/`DescriptorHost`/`SamplerHost` 均使用基于哈希的缓存去重。
5. **排序键**: 64 位复合排序键统一 2D (layer) 和 3D (depth bin) 的绘制顺序控制。
6. **增量更新**: 3D Geometry Runtime 支持仅更新变换矩阵的快速路径，通过 `world_revision` 追踪。
7. **跨队列上传与同步重构**: `UploadHost` 支持 Transfer Queue 异步上传，通过 Semaphore 与 Graphics Queue 同步。各子系统的 upload host 均通过统一的 `UploadHost` 接口完成 staging→GPU 传输，实现上传同步的集中管理。
8. **着色器合约校验**: 通过 `spv_reflect_to_json.py` 反射 SPIR-V 接口信息，`shader_contract_check.py` 与 C++ 端管线定义交叉验证，确保 Descriptor Set Layout 和 Push Constant 的一致性。
9. **BackendTag 编译期多态**: 平台后端通过标签分发，易于扩展新窗口系统。
10. **帧协调器模式**: Appearance/Light/Shadow 使用独立的帧协调器 (Frame Coordinator) 进行每帧数据编排，每套协调器有自己的 Prepare Stage 和特定的数据流程。
11. **C++20 模块**: `feature/cpp20-modules` 分支已迁移所有 10 个核心库为 `.cppm` 接口单元。使用 `vr_module_fwd.hpp` 统一全局片段以实现零重复。`vr.ecs` 模块通过模板声明模式避免了与 `vr.text` 的循环依赖。
