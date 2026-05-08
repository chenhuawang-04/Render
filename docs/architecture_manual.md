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
│  │ Shadow Atlas │ Animation    │ IBL Bake     │                    │ │
│  │ Coordinator  │ Coordinator  │ Coordinator  │                    │ │
│  └──────────────┴──────────────┴──────────────┴────────────────────┘ │
├──────────────────────────────────────────────────────────────────────┤
│                    Scene Submission Layer                              │
│  ┌──────────────────┬──────────────────┬──────────────────────────┐  │
│  │  RenderView<Dim>  │  ScenePacket<Dim>│  SceneRecorder2D/3D      │  │
│  ├──────────────────┼──────────────────┼──────────────────────────┤  │
│  │  SceneRenderStage │  FrameComposer   │  ViewSubmissionUtils     │  │
│  └──────────────────┴──────────────────┴──────────────────────────┘  │
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
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Animation │ Anim.    │ Anim.    │ Anim.    │ Anim.            │   │
│  │ Clock     │ Property  │ Material │ Camera   │ Skeletal/Morph   │   │
│  ├──────────┼──────────┼──────────┼──────────┼──────────────────┤   │
│  │ Anim.     │ Anim.    │ Anim.    │ Anim.    │                  │   │
│  │ Path      │ Vertex   │ FrameSeq │ Resource │                  │   │
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

**职责**: 帧循环、交换链管理、命令缓冲、描述符/管线管理、上传系统、帧协调器、场景提交与录制、IBL 环境光照、天空盒、帧合成。

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

#### 3.3.3 Render Target / 离屏渲染系统

**职责**: 管理离屏渲染目标 (Render Target) 的完整生命周期，包括创建、复用、后处理 (Bloom) 和最终合成至 Swapchain。

| 组件 | 说明 |
|------|------|
| `RenderTargetTypes` | 渲染目标基础类型定义。Image 句柄、View 描述符、Pool 槽位标识。 |
| `RenderTargetDesc` | 渲染目标描述符。格式 (颜色/深度/模板)、分辨率、MSAA 采样数、mip 层级、layer 数。 |
| `RenderTargetFormatUtils` | 格式工具类。颜色/深度格式查询、格式族兼容性判断。 |
| `RenderTargetView` | 渲染目标视图。封装 Vulkan ImageView 和附件引用，支持多子资源。 |
| `RenderTargetHost` | 渲染目标管理器。创建/缓存/销毁 RenderTarget、附件生命周期、格式协商、与 GpuMemoryHost 集成。 |
| `RenderTargetPool` | 渲染目标对象池。按尺寸预设分组，空闲/占用槽位管理，延迟回收 (与 FrameRetire 联动)。 |
| `RenderTargetPass` | 渲染通道 (VkRenderPass) 封装。Load/Store Op、附件 Clear Value、子通道依赖自动配置。 |
| `ColorBlendState` | 颜色混合状态。Attachment Blend 参数 (srcFactor/dstFactor/blendOp)、Logic Op、Write Mask。与 TransparencyRenderPolicy 协同工作。 |
| `RenderPassPreset` | 渲染通道预设库。预定义常用配置 (颜色清除/加载、深度模板读写、多附件布局)。 |
| `SceneRenderTargetSet` | 场景渲染目标集。将场景所需的颜色/深度/中间目标打包为一个完整配置，管理多附件 RenderPass。 |
| `SceneBloomPostStack` | Scene Bloom 后处理栈。Bloom 强度/阈值/半径参数、多级降采样金字塔、最终合成逻辑。 |
| `RenderTargetBloomRenderer` | Bloom 渲染器。三阶段管线：Prefilter (亮度提取) → Blur (多级降采样+升采样模糊) → Combine (与源图混合)。 |
| `RenderTargetCompositeRenderer` | 合成渲染器。将最终离屏渲染目标通过全屏四边形合成至 Swapchain Backbuffer。 |

#### 3.3.4 帧协调器 (Frame Coordinators)

帧协调器是位于 RenderRuntimeHost 和 ECS 系统之间的编排层，负责每帧的数据准备、排序、缓存和跨系统协调。

| 协调器 | 说明 |
|--------|------|
| `AppearanceFrameCoordinator` | Appearance 帧协调。管理 Appearance 组件的帧级生命周期：脏记录扫描、链接扫描、与 Geometry/Surface 渲染器协调。支持 2D 和 3D 双渲染器模式。 |
| `LightFrameCoordinator` | Light 帧协调。收集活跃光源组件、处理光源 GPU 数据准备、协调光源上传。 |
| `ShadowFrameCoordinator` | Shadow 帧协调。管理 Shadow Caster 组件收集、Shadow Map 渲染准备、与光源的关联。 |
| `LightShadowLinkCoordinator` | Light-Shadow 链接协调。建立光源与阴影投射器之间的关联关系。 |
| `ShadowAtlasBindingCoordinator` | Shadow Atlas 绑定协调。管理 Shadow Map Atlas 的纹理绑定和描述符更新。 |

#### 3.3.5 准备阶段 (Prepare Stages)

| 阶段 | 说明 |
|------|------|
| `AppearancePrepareBridge` | Appearance 准备桥接：连接 AppearanceFrameCoordinator 和 RenderRuntime 的帧准备流程。 |
| `AppearancePrepareStage` | Appearance 准备阶段：执行 Appearance 数据的帧级准备操作。 |
| `LightPrepareStage` | Light 准备阶段：执行光源数据的帧级准备。 |
| `LightShadowLinkStage` | Light-Shadow 链接阶段：执行光源-阴影关联的帧级操作。 |
| `ShadowPrepareStage` | Shadow 准备阶段：执行阴影数据的帧级准备。 |

#### 3.3.6 场景提交与录制 (Scene Submission & Recorders)

| 组件 | 说明 |
|------|------|
| `RenderView<Dim>` | 渲染视图。定义 active/scene/overlay/reflection/custom view kind，管理目标/视口/清除策略与 layer mask。 |
| `RenderScenePacket<Dim>` | 场景提交包。将 ECS 场景数据打包为统一提交结构，通过 `SceneSubmissionPolicy` 路由分发。 |
| `RenderViewSubmissionUtils` | View 提交工具。Multi-view packet 解析、Layer mask 分流、typed view kind→recorder 路由。 |
| `SceneRecorder2D` | 2D 场景录制器。编排 Scene→Consumer→Present 或 Offscreen 路径，支持 UI-only/Mixed packet。 |
| `SceneRecorder3D` | 3D 场景录制器。编排 Light/Shadow/Scene/PostProcess 管线，支持 Multi-view packet、Reflection/Custom view、Animation 集成。 |
| `SceneRenderStage` | 场景渲染阶段。Opaque→Transparent 阶段顺序、Bloom 编排、后处理策略路由。 |

#### 3.3.7 IBL / 环境光照

| 组件 | 说明 |
|------|------|
| `IBLHost` | IBL 宿主。管理 Irradiance Diffuse IBL、Specular Prefiltered Environment Map、BRDF Integration LUT。接收外部 HDR/Cubemap 数据。 |
| `IBLBakeHost` | IBL 烘焙宿主。将 HDR 环境贴图离线烘焙为 Irradiance Map + Prefiltered Mipmap + BRDF LUT。Compute Shader 驱动。 |
| `IBLBakeCoordinator` | IBL 烘焙协调器。管理烘焙 Pass 编排、描述符更新、与 RenderTarget/Pipeline 的集成。 |

#### 3.3.8 天空盒与帧合成

| 组件 | 说明 |
|------|------|
| `SkyboxRenderer` | 天空盒渲染器。立方体贴图/HDR 等距矩形输入、与 IBL 环境图联动。 |
| `FrameComposerHost` | 帧合成宿主。管理最终帧合成 Pass：Scene Target + 后处理栈 + UI Overlay → Swapchain。 |

#### 3.3.9 动画帧协调器

| 组件 | 说明 |
|------|------|
| `AnimationFrameCoordinator<Dim>` | 动画帧协调。统一协调 AnimationClock、Evaluation、Host 的每帧更新，将动画输出注入 SceneRecorder 和渲染管线。 |

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
| **Animation** | 2D animation clock/track | 3D animation clock/track | clock state, Property/Material/Camera/Path/Skeletal/Morph/VertexDeform/FrameSequence tracks, clip route, evaluation state |

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

##### 动画系统组 (Phase 1 完整实现)

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `AnimationClockSystem<Dim>` | 动画时钟 | 时间推进 (delta/time scale)、播放状态 (Play/Pause/Stop)、循环/单次/乒乓模式 |
| `AnimationCurveSystem<Dim>` | 动画曲线 | 关键帧曲线采样 (Linear/Step/CubicSpline)、时间→值映射 |
| `AnimationPropertyTrackSystem<Dim>` | Property 轨道 | 采样 Position/Rotation/Scale/Color/Float 轨道 |
| `AnimationPropertyEvaluationSystem<Dim>` | Property 求值 | 将 Property 轨道输出写入 Transform、Appearance 组件 |
| `AnimationMaterialTrackSystem` | Material 轨道 | 采样 Albedo/Metallic/Roughness/Emissive 材质动画轨道 |
| `AnimationMaterialEvaluationSystem` | Material 求值 | 将 Material 轨道输出写入 Appearance/Material 组件 |
| `AnimationCameraTrackSystem<Dim>` | Camera 轨道 | 采样 FOV/Near/Far/Position/LookAt 动画轨道 |
| `AnimationCameraEvaluationSystem<Dim>` | Camera 求值 | 同步 Camera 轨道输出到 Camera 组件 |
| `AnimationPathMotionSystem<Dim>` | 路径运动 | Position/Rotation/Scale 路径曲线采样、路径约束 (Follow/LookAt) |
| `AnimationPathEvaluationSystem<Dim>` | 路径求值 | 将 Path Motion 输出写入 Transform 组件 |
| `AnimationSkeletalEvaluationSystem<Dim>` | 骨骼求值 | 骨骼轨道采样、Joint Palette 更新、Root Motion 提取 |
| `AnimationMorphEvaluationSystem<Dim>` | 变形求值 | Morph Target 权重采样、变形缓冲区构建 |
| `AnimationVertexDeformEvaluationSystem<Dim>` | 顶点变形求值 | Vertex Deform 轨道采样、顶点负载输出 |
| `AnimationFrameSequenceEvaluationSystem<Dim>` | 帧序列求值 | Frame Sequence 轨道采样、A/B 帧混合、子网格帧索引路由 |
| `AnimationResourceTrackSystem` | 资源轨道 | 动画资源轨道生命周期管理和路由 |

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
| `ShadowRenderer3D` | 3D 阴影渲染器：3D Shadow Map 渲染、深度管线、Directional Shadow Fit + 骨骼动画阴影支持。 |

#### 3.5.5 动画 Host 层 (`vr/animation`)

| 组件 | 说明 |
|------|------|
| `AnimationClipHost<Dim>` | 动画片段管理器。Clip 生命周期 (Create/Destroy)、轨道收集、采样求值。支持循环/单次/乒乓模式。 |
| `AnimationSkeletalHost<Dim>` | 骨骼动画宿主。关节层级、Joint Palette 构建、GPU Skinning 矩阵上传、Root Motion 提取。 |
| `AnimationMorphHost<Dim>` | 变形目标 (Morph Target) 宿主。变形目标权重管理、GPU 缓冲区上传、Blend Shape 支持。 |
| `AnimationPathHost<Dim>` | 路径动画宿主。路径曲线管理、路径采样与运动合成。 |
| `AnimationVertexDeformHost<Dim>` | 顶点变形宿主。GPU 顶点变形缓冲区管理、Deform Payload 上传。 |
| `AnimationFrameSequenceHost<Dim>` | 帧序列动画宿主。子网格帧序列播放、A/B 帧混合、帧索引路由。 |

#### 3.5.6 资产纹理 (`vr/asset`)

| 组件 | 说明 |
|------|------|
| `TextureHost` | 资产纹理宿主。接收外部已解码像素数据、创建/接管 GPU Image、Mipmap 生成。不包含 PNG/KTX2 解码，仅消费已准备数据。 |

#### 3.5.7 GPU 骨骼蒙皮 (`vr/geometry`)

| 组件 | 说明 |
|------|------|
| `GeometrySkeletalPaletteBuilder` | GPU 骨骼调色板构建器。从 `AnimationSkeletalHost` 消费 Joint Palette，构建用于顶点着色器骨骼蒙皮的矩阵 UBO/SSBO。 |

---

## 4. 帧协调器管线流程

Appearance、Light、Shadow、Animation 四套协调器协同工作，形成完整的每帧渲染准备管线：

```
RenderRuntime::Tick()
  │
  ├── FrameSync::Acquire  → 获取下一帧槽位
  ├── FrameCommand::Begin  → 开始命令录制
  │
  ├── AnimationFrameCoordinator::Prepare
  │   ├── AnimationClockSystem::Advance    (推进时间)
  │   ├── AnimationEvaluationSystems      (Property/Material/Camera/Skeletal/Morph 求值)
  │   └── AnimationHost::Update           (更新 GPU 端骨骼/变形/帧序列数据)
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
  ├── SceneRecorder3D::Record       (Multi-view packet 路由 → 场景 Pass)
  │   ├── SceneRenderStage           (Opaque → Transparent 阶段)
  │   └── SkyboxRenderer::Render    (天空盒)
  │
  ├── IBLHost::Bind                  (绑定 Irradiance/Specular/BRDF LUT)
  ├── SceneRenderTargetSet::Acquire  (获取场景渲染目标)
  ├── Renderer::Render               (Text/Geometry/Surface/Shadow → 离屏目标)
  ├── SceneBloomPostStack::Process   (Bloom 后处理)
  ├── FrameComposerHost::Compose     (场景 + 后处理 + UI overlay → Swapchain)
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
| `render_target_composite.vert/.frag` | 离屏合成 (全屏四边形 + 渲染目标采样) |
| `render_target_bloom_prefilter.frag` | Bloom Prefilter (亮度阈值提取) |
| `render_target_bloom_blur.frag` | Bloom Blur (多级降采样/升采样高斯模糊) |
| `render_target_bloom_combine.frag` | Bloom Combine (模糊结果与源图混合) |
| `skybox.frag` | Skybox 片段着色器 (立方体贴图/HDR 等距矩形采样) |

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

基于自定义轻量框架 (`test_framework.hpp/cpp`)。约 55 个测试文件，覆盖：
- ECS 组件/系统单元测试 (Transform, Camera, Bounds, Culling, Geometry, Surface, Text, Light, Shadow, Appearance, Animation)
- 动画系统单元测试 (Clock/Curve/Property/Material/Camera/Path/Skeletal/Morph/VertexDeform/FrameSequence)
- 动画 Host 单元测试 (Clip/Skeletal/Morph/VertexDeform/FrameSequence)
- 运行时集成测试
- GPU 资源类型测试
- FreeType/Glyph 渲染测试
- FrameRetire 回收测试
- Light/Shadow 协调器测试
- Appearance 协调器测试
- IBL/IBLBake/FrameComposer/TextureHost 集成测试
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
11. **离屏渲染 / Render Target**: 场景渲染先输出到离屏渲染目标 (Scene Render Target Set)，然后通过 Bloom Post Stack 后处理，最终由 Composite Renderer 合成至 Swapchain。RenderTargetPool 按尺寸预设复用目标，避免每帧重新创建。
12. **Bloom 后处理栈**: 三阶段 Bloom 管线 — Prefilter (亮度阈值) → Blur (多级降采样/升采样) → Combine (混合)。通过 SceneBloomPostStack 封装参数配置和 Pass 编排。
13. **透明排序与混合**: `TransparencyRenderPolicy` 统一管理跨渲染器的透明排序策略。结合按帧材质路由 (`AppearanceRuntimeSystem`) 和 `ColorBlendState` 实现 64 位排序键中的 pass 位驱动分层透明/不透明渲染。
14. **C++20 模块**: `feature/cpp20-modules` 分支已迁移所有 10 个核心库为 `.cppm` 接口单元。使用 `vr_module_fwd.hpp` 统一全局片段以实现零重复。`vr.ecs` 模块通过模板声明模式避免了与 `vr.text` 的循环依赖。
15. **统一场景提交**: 通过 `RenderView<Dim>` + `RenderScenePacket<Dim>` + `SceneRecorder<Dim>` 三层结构统一场景提交路径。2D 和 3D 共享相同 packet/view 概念，但 Recorder 独立实现 (避免过早合并)。
16. **动画 ECS Phase 1**: 完整的 7 类动画轨道 (Property/Material/Camera/Path/Skeletal/Morph/VertexDeform/FrameSequence) + 时钟 + 曲线 + 求值系统。通过 `AnimationFrameCoordinator` 统一注入 SceneRecorder 和渲染管线。GPU Skinning 通过 `SkeletalPaletteBuilder` 上传 Joint Palette 至顶点着色器。
17. **IBL 环境光照**: IBL 分为在线宿主 (IBLHost) 和离线烘焙宿主 (IBLBakeHost)。Irradiance Map + Prefiltered Environment Map (多级 mip) + BRDF Integration LUT 三件套。通过 Compute Shader 驱动烘焙流程。
18. **Text 运行时契约**: `TextRuntimeContract` 定义 Text 系统与 Renderer 之间的资源契约和上传策略，消除隐式依赖，使字形页面调度可诊断。
19. **Directional Shadow Fit & Stabilization**: 3D Shadow Renderer 支持 Directional Light 的 CSM 风格阴影贴合 (Shadow Fit) 和坐标稳定化，减少阴影抖动。
