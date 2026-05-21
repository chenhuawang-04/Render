# VulkanRender_New 项目架构手册

## 1. 项目概述

**项目名称**: VulkanRender_New (Melosyne 引擎 Vulkan 渲染模块)
**语言标准**: C++23 (预留 C++20 Module 迁移 — 见 `feature/cpp20-modules` 分支)
**构建系统**: CMake 3.21+
**API 目标**: Vulkan 1.3
**主要平台**: Windows (SDL3 后端)
**调试工具**: CrashTracer (Runtime 级崩溃回溯，通过 `InstallProcessCrashTracer` 应用程序入口安装，日志输出至 `crash_reports/`)

项目是一个基于 Vulkan 1.3 的实时 2D/3D 图形渲染框架，由以下核心层次组成。

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
│                    RenderGraph Layer (渲染图)                           │
│  ┌──────────────┬──────────────┬──────────────┬────────────────────┐ │
│  │  GraphBuilder │  Compile     │  BarrierPlan │  NativePassFusion  │ │
│  ├──────────────┼──────────────┼──────────────┼────────────────────┤ │
│  │  FrameGraph   │  FrameSnapshot│ AliasAlloc   │  QueueSchedule     │ │
│  ├──────────────┼──────────────┼──────────────┼────────────────────┤ │
│  │  GraphExec.   │  GraphCmdCtx │  VkResTable  │  GraphSvc          │ │
│  └──────────────┴──────────────┴──────────────┴────────────────────┘  │
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
│  ├──────────┼──────────┼──────────┤                                  │
│  │ Particle │ Particle │ Particle │                                  │
│  │ System   │ Runtime  │ Emitter  │                                  │
│  └──────────┴──────────┴──────────┴──────────────────────────────────┘   │
├──────────────────────────────────────────────────────────────────────┤
│                    Renderer Backends + Particle                      │
│  ┌──────────┬──────────┬──────────┬──────────┬─────────────────────┐ │
│  │ Text     │ Geometry │ Surface  │ Shadow   │ Particle            │ │
│  │Renderer  │ Renderer │ Renderer │ Renderer │ Renderer 2D/3D      │ │
│  │ 2D / 3D  │ 2D / 3D  │ 2D / 3D  │ 2D / 3D  │ 2D / 3D            │ │
│  └──────────┴──────────┴──────────┴──────────┴─────────────────────┘ │
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
| **CrashTracer** | 崩溃追踪与堆栈回溯 (Runtime 集成 + Bench) | `CrashTracer/` |
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
| `DescriptorHost` | 描述符池管理 + DescriptorSetLayout 缓存 (哈希去重)。支持 buffer/image/texel buffer writes、Bindless Table (VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_EXT + VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT)。每帧独立池，可选验证。 |
| `BindlessResourceSystem` | Bindless 资源系统。统一管理全帧描述符索引纹理/采样器访问：2 个全局 Descriptor Table (Set 0=SampledImage[8192], Set 1=Sampler[256])。Configure 方法为各子系统 (Texture/Surface/Geometry/Shadow/RenderTarget/Glyph) 分配 bindless slot。占位符 Image/Sampler 防止未绑定访问。 |
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
| `SceneRenderTargetSetCreateInfo` | 场景渲染目标创建信息。Phase12 移除 `SceneRenderTargetSet` 类 (场景目标由 RenderGraph 声明式管理：`BuildMinimalFrameGraph` 创建 `scene_color`/`scene_depth` Transient 资源并通过 RasterPassDesc 管理 Load/Store Op)，仅保留 `SceneRenderTargetSetCreateInfo` 结构和 `MakeSceneRendererBinding` 辅助函数。 |
| `SceneBloomPostStack` | (Phase12 已移除类) Bloom 管线由 RenderGraph 的 `NativePassPlan` 管理 (Bloom Prefilter/Blur/Combine 作为独立 Graph Pass，融合为单次 Dynamic Rendering scope)。仅保留基础结构定义。 |
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
| `SceneRecorder2D` | 2D 场景录制器。编排 PreScene/Scene/Overlay 三层录制。Phase12 完成后为 **纯 RenderGraph 路径**：移除了 legacy 命令式 `Record(VkCommandBuffer,...)` 入口，全部录制通过 `BuildRenderGraph()`→SetGraphBuildCallback→RecordGraphPass 完成。 |
| `SceneRecorder3D` | 3D 场景录制器。编排 Light/Shadow/Scene/PostProcess 管线。Phase12 完成后为 **纯 RenderGraph 路径**：移除了 legacy 命令式 `Record(VkCommandBuffer,...)` 入口，全部录制通过 `BuildRenderGraph()`→SetGraphBuildCallback→RecordGraphPass + Scene3DDescriptorContract 完成。 |
| `SceneRenderStage` | 场景渲染阶段。Opaque→Transparent 阶段顺序、Bloom 编排、后处理策略路由。 |

#### 3.3.7 IBL / 环境光照 (惰性管线)

| 组件 | 说明 |
|------|------|
| `IBLHost` | IBL 宿主。管理 Irradiance Diffuse IBL、Specular Prefiltered Environment Map、BRDF Integration LUT。支持惰性 IBL 管线：环境注册 → 请求烘焙 → 自动应用。`SkyEnvironmentGpuHost` 通过 `EnsureIblEnvironment` 触发按需 IBL 注册。 |
| `IBLBakeHost` | IBL 烘焙宿主。将 HDR 环境贴图离线烘焙为 Irradiance Map + Prefiltered Mipmap + BRDF LUT。Compute Shader 驱动。与 `SkyEnvironmentGpuHost` 通过 `TryBakePendingEnvironment` 协调惰性烘焙流程。 |
| `IBLBakeCoordinator` | IBL 烘焙协调器。管理烘焙 Pass 编排、描述符更新、与 RenderTarget/Pipeline 的集成。 |

#### 3.3.8 环境与背景渲染

| 组件 | 说明 |
|------|------|
| `SkyEnvironmentGpuHost` | 天空环境 GPU 宿主。管理天空环境 Register/Update 缓存 (基于 `EquivalentState` 去重)、GPU 参数构建 (tint/exposure/rotation/IBL 强度/SH9 球谐系数)、IBL 数据追踪 (irradiance/prefiltered/brdf_lut texture IDs)、惰性烘焙描述符 (`SkyEnvironmentBakeDesc`)。持有每环境 `VkDescriptorSet`。 |
| `SkyEnvironmentPass` | 天空环境渲染 Pass。6 种渲染模式 (none/solid_color/gradient/cubemap/equirectangular_hdr/procedural_atmosphere)、摄像机对齐全屏四边形、Push Constant 分发 (渐变/等距矩形/大气散射参数)。Prepare→Record 两阶段设计。 |
| `BackgroundPass2D` | 2D 背景 Pass。5 种模式 (none/solid_color/gradient/sprite/surface_entity)、全屏四边形渲染、Push Constant 驱动。与 `SurfaceRenderer2D` 集成处理 sprite/surface_entity 模式。 |
| `FrameComposerHost` | 帧合成宿主。通过 RenderGraph `BuildRenderGraph()` 管理 Tonemap Composite Pass (场景 Color → Swapchain Present Target)。Phase12 移除 `FrameComposerTargets`、`SceneRenderTargetSet` 成员、`ConfigureSceneRenderer()`/`ResetSceneRenderer()`/`RecordTonemapPass()`：所有场景目标创建、生命周期和录制均由 RenderGraph 声明式管理。 |
| `SceneBackgroundTraits` | 编译期背景 trait。2D→surface path, 3D→environment GPU path。通过模板特化实现零开销分支选择。 |

#### 3.3.9 动画帧协调器

| 组件 | 说明 |
|------|------|
| `AnimationFrameCoordinator<Dim>` | 动画帧协调。统一协调 AnimationClock、Evaluation、Host 的每帧更新，将动画输出注入 SceneRecorder 和渲染管线。 |

#### 3.3.10 Appearance GPU 准备与 PBR 语义

| 组件 | 说明 |
|------|------|
| `AppearanceGpuPrepare3D` | Appearance GPU 准备器。聚合所有 Appearance 记录 (Geometry + Surface)、解析 `AppearanceSampledSurfaceBinding3D` (5 纹理通道) 到 bindless slot index、生成 GPU-bound material parameter buffer (base_color/metallic/roughness/occlusion/emissive/normal_scale/unlit)。`AppearanceSampledSurfacePresenceFlags3D` 位掩码指示通道绑定状态。 |
| `AppearanceSampledSurfaceBinding` | 采样表面绑定。定义每个 Appearance 关联的 5 个纹理通道及其 bindless slot。`AppearanceSampledSurfaceDomain` 枚举区分 geometry_image/surface_image 纹理源。 |
| `GeometryAppearanceHost` | 几何 Appearance 宿主。替代 `GeometryMaterialHost`，PBR 材质通过 Appearance 组件而非独立的 Material 系统管理。为每个 Appearance 构建 SampledSurfaceBinding 并管理 UV 变换/Alpha Test 标志。 |
| `GeometryAppearanceResolver` | 从 Appearance 组件 + GeometryAppearanceHost 合并解析最终 PBR 渲染参数。处理纹理/常量混合 (例如 base_color_texture * base_color_factor)。 |

#### 3.3.11 PBR 着色基础 (`shaders/include/vr/render/pbr.glsl`)

PBR 着色模型遵循 GGX-Smith + Fresnel Schlick 的 metallic-roughness 工作流。

| 组件 | 说明 |
|------|------|
| `PbrParams` | PBR 参数结构体：base_color, metallic, roughness, normal_ts, occlusion, emissive, unlit_flag |
| `DecodePbrParams` | 从纹理采样结果解码 PBR 参数 (linear→sRGB base_color, occlusion×roughness) |
| `EvaluateDirectionalLight` | 直接光 PBR：NdotL/NdotV/NdotH、GGX Distribution (Trowbridge-Reitz)、Smith Geometry、Fresnel Schlick |
| `EvaluateAmbientIBL` | 环境光 PBR：Diffuse IBL (irradiance map × base_color × occlusion) + Specular IBL (prefiltered map × split-sum BRDF LUT) |
| `appearance_decode_3d.glsl` | Appearance 纹理解码：`DecodeBaseColor/DecodeNormal/DecodeMetallicRoughness/DecodeOcclusion/DecodeEmissive` 从 bindless 纹理表读取并组合为 `PbrParams` |
| `geometry_tangent_space.hpp` | GPU 切线空间预备。从 position/normal/uv 生成 tangent/bitangent (MikkTSpace 风格)、fallback basis、退化 UV 检测。法线贴图必需。 |

#### 3.3.12 Bindless 统一渲染合约

**职责**: 将所有渲染子系统的纹理/采样器访问统一为 Bindless Descriptor Indexing。替代传统的 per-pass descriptor set 绑定，实现渲染器间零开销的纹理访问。

| 组件 | 说明 |
|------|------|
| `BindlessResourceSystem` | Bindless 资源系统。管理 2 个全局 Descriptor Table：Set 0 = SampledImage[8192] (texture2D/texture2DArray/textureCube)、Set 1 = Sampler[256]。在 Init 时创建占位符 Image/Sampler 填充所有 slot (防止未绑定访问崩溃)。提供 6 个 `Configure*` 方法为各子系统注入 bindless slot 分配逻辑。 |
| `BindlessSlot` | Bindless 槽位句柄。`{index, generation}` 双重校验，防止 use-after-free。`IsValid()` 检查 generation != 0。 |
| `BindlessTableDesc` | Bindless 表描述符。descriptor_type、capacity、stage_flags、partially_bound、variable_descriptor_count、update_after_bind 策略。 |
| `BindlessUpdateAfterBindPolicy` | Update-After-Bind 策略：auto_if_supported (检测设备支持并自动启用) / disabled (禁止) / required (必须)。 |

**GLSL 端合约** (`shaders/include/vr/render/bindless.glsl`):
- Set 0: `texture2D g_Textures2D[]`, `texture2DArray g_Textures2DArray[]`, `textureCube g_TexturesCube[]`
- Set 1: `sampler g_Samplers[]`
- 统一采样函数: `SampleTexture2D(idx, sampler_idx, uv)`, `SampleTextureCube(idx, sampler_idx, dir)`, `SampleTextureCubeLod(idx, sampler_idx, dir, lod)`, `SampleTexture2DArray(idx, sampler_idx, uvw)`
- 所有渲染着色器通过 `#include` 引用，使用 `nonuniformEXT` 修饰符实现 divergent indexing

**子系统 Bindless 集成**:
```
BindlessResourceSystem::ConfigureTextureHost      → TextureHost 的每个 Texture 分配 SampledImage slot + Sampler slot
BindlessResourceSystem::ConfigureSurfaceImageHost  → SurfaceImageHost 分配 SampledImage slot
BindlessResourceSystem::ConfigureGeometryImageHost → GeometryImageHost 分配 SampledImage slot
BindlessResourceSystem::ConfigureShadowAtlasHost   → ShadowAtlasHost 分配 SampledImage slot
BindlessResourceSystem::ConfigureRenderTargetHost  → RenderTargetHost 分配 SampledImage slot
BindlessResourceSystem::ConfigureGlyphUploadHost   → GlyphUploadHost 分配 SampledImageArray slot
```

**设计理念**:
- 渲染器不再持有独立的 VkDescriptorSet，改为从 BindlessResourceSystem 查询 `BindlessSlot`
- Push Constant 传递 slot index 至着色器，着色器通过 `nonuniformEXT` 动态索引纹理数组
- DescriptorHost 负责底层的 VkDescriptorPool/VkDescriptorSetLayout/VkDescriptorSet 管理
- 合约验证: `bindless_shader_contract_tests.cpp` 交叉验证 C++ BindlessTableDesc 与 SPIR-V 反射输出

#### 3.3.13 RenderGraph 渲染图系统 (`include/vr/render_graph/`)

**职责**: RenderGraph 是渲染帧内执行的**唯一调度中心**。所有 Pass 通过声明式 API 描述资源读写、访问语义、队列偏好，由编译器推导资源状态转换、瞬态别名分析、Pipeline Barrier 生成和跨队列同步。高层渲染模块不得直接操作 VkImageLayout/VkPipelineStageFlags/VkBarrier。

**核心理念**:
- **声明式**: Pass 声明 Read/Write 的资源和访问语义，不手写 barrier
- **SSA 版本化**: 每个 Write 产生新资源版本，依赖链自动形成 DAG
- **图编译**: Builder→Compile 生成 Pass 顺序、Liveness、Barrier Plan、Native Pass Plan
- **Transient Aliasing**: 图级 Liveness 分析驱动瞬态资源复用，替代旧 RenderTargetPool
- **Native Pass Fusion**: 连续兼容的 Raster Pass 自动融合为单次 Dynamic Rendering Scope
- **Multi-Queue**: Graphics/Compute/Transfer 三队列自动调度，Timeline Semaphore 协调

##### 3.3.13.1 构建阶段 (Build)

| 组件 | 说明 |
|------|------|
| `RenderGraphBuilder` | 图构建器。CreateTexture/CreateBuffer (逻辑资源声明)、AddPass (声明 Pass)、Read/Write (版本化资源引用)、SetRasterPassDesc (附件描述)、SetExecuteCallback (录制回调)、AddBindlessTableBinding/AddExternalBufferBinding (描述符绑定)。Compile() 生成 CompiledRenderGraph。 |
| `FrameSnapshot<Dim>` | 帧快照。从 `RenderScenePacket` 提取冻结的 POD 数据 (Extent/Views/Camera/Background/Environment)，64-bit 签名用于缓存去重。模板化 Dim2/Dim3。 |
| `BuildMinimalFrameGraph<Dim>` | 最小帧图构建。Scene Pass (Color+Depth) → Overlay Pass → Present Copy/Transition Pass。通过扩展回调注入自定义场景 Pass。 |

##### 3.3.13.2 编译阶段 (Compile)

| 组件 | 说明 |
|------|------|
| `CompiledRenderGraph` | 编译后的渲染图。Pass 拓扑排序执行顺序、CompiledResource 资源描述、CompiledResourceVersionLiveness 活跃区间 (first_pass_order→last_pass_order)、DescriptorBindingPlan、NativePassPlan、BarrierPlan、TransientAllocationPlan。支持 DebugString/DotGraph/Json 序列化。 |
| `TransientAllocationPlan` | 瞬态分配计划。对每个 transient 资源：计算 ResourceFootprint (size/alignment/memory_type_bits)、TransientCompatibilityKey (兼容性分类)、活跃区间。兼容资源共享内存页 (Page)，输出 AliasBarrierDecision (别名屏障需求)。TransientMemoryTimeline 记录逻辑/物理峰值内存。 |
| `NativePassPlan` | Native Pass 融合计划。连续 Raster Pass 融合为 NativePassGroup (单次 Dynamic Rendering Scope)。21 种融合阻止原因 (queue_mismatch/side_effect/color_attachment_target_mismatch/depth_attachment_read_only_mismatch/sampled_group_resource_read 等)。自动推理 Load/Store Op (elide_transient_last_use→DONT_CARE、infer_clear_from_first_use→CLEAR、preserve_for_future_use→STORE)。支持 VK_KHR_dynamic_rendering_local_read。 |
| `BarrierPlan` | 屏障计划。从 Pass DAG 推导 LogicalBarrier (AccessKind before→after)、QueueDependencyPlan (跨队列同步)、QueueSubmitBatch (队列提交批次)。自动处理 aliasing/uav_ordering/host_boundary/queue_transfer 特殊情况。 |
| `VulkanBarrierPlan` | Vulkan 屏障降级。将 LogicalBarrier 降级为 VkPipelineStageFlags2 + VkAccessFlags2 + VkImageLayout。VulkanCommandReadyPlan 生成可直接调用的 VkDependencyInfo (vkCmdPipelineBarrier2)。支持 Queue Family Ownership Transfer (跨队列资源转移)。 |
| `QueueExecutionPolicy` | 队列执行策略。检测设备能力 (InspectQueueExecutionCapabilities)、分析图队列需求 (GraphRequestsQueue)、决策多队列启用/回退。支持 Graphics Fallback（设备不支持所需队列时自动退回单 Graphics Queue）。 |

##### 3.3.13.3 执行阶段 (Execute)

| 组件 | 说明 |
|------|------|
| `GraphCommandContext` | 图命令上下文。持有 VulkanContext + VkCommandBuffer + CompiledRenderGraph + VulkanResourceTable。提供逻辑→物理资源解析 (ResolveTextureTarget/BuildRenderingInfo)、BeginRendering/EndRendering、CurrentPassDescriptorSet 管理。 |
| `RenderGraphExecutor` | 图执行器。Record() 遍历执行顺序：应用 Barrier Batches → SetCurrentPass + DescriptorSets → 调用 PassExecutionThunk → ClearCurrentPass。RecordQueueBatch() 处理多队列提交批次 (含 Cross-Queue Dependency Semaphore)。统计 RenderGraphRecordStats。 |
| `RenderGraphRuntimeService` | 渲染图运行时服务 (~1695 行)。集成 FrameSnapshot→BuildMinimalFrameGraph→Compile→BarrierLower→VulkanResourceResolve→QueuePolicy→Record/Submit 全管线。支持 3 种 Graph Build 路径: 2D FrameSnapshot、3D FrameSnapshot、DirectGraphBuildCallback。Multi-Queue Submit 通过 vkQueueSubmit2 + Timeline Semaphore 管理 graphics/compute/transfer 三队列间依赖。Lazy Memory 按需分配。 |

##### 3.3.13.4 描述符合约

| 组件 | 说明 |
|------|------|
| `PassShaderContractDesc` | Pass 着色器合约。声明 Pass 需要的 Descriptor Set 布局 (set/binding/kind/stage_flags/descriptor_count)。`MakeSharedBindlessFragmentShaderContract` 提供标准 3D 合约: Set 0=SampledImage[3], Set 1=Sampler[1]。 |
| `PassDescriptorBindingDesc` | Pass 描述符绑定。Bindless Table Binding + External Buffer Binding。ExternalBufferBindingResolver 支持回调函数式的缓冲绑定解析 (用于 Text Glyph Descriptor Set 等)。 |
| `Scene3DDescriptorContract` | 3D 场景共享合约。Set 0=SampledImage[3], Set 1=Sampler[1], Set 2=SharedBuffer[9] (light_records/cluster_headers/cluster_indices/shadow_views/lighting_uniform/skeletal_components/skeletal_matrices/geometry_appearance/surface_appearance), Set 3=IBL Params。 |
| `VulkanResourceTable` | Vulkan 物理资源表。将逻辑 ResourceHandle 映射到 VkImage/VkBuffer/VkDeviceMemory。管理 Transient Memory Page (BufferPage/ImagePage)、Imported Resource 注册、Lazy Memory 决议、跨帧 RetireBus 延迟回收。 |

##### 3.3.13.5 运行时集成

RenderGraph 在 `Runtime::Tick()` 中的执行位置 (通过 `ExecutionPhaseDriver`):

```
RenderRuntime::Tick()
  ├── ExecutionPhase::ServiceBeginFrame  → RenderGraphService::BeginFrame
  ├── ExecutionPhase::Prepare            → (scene recorders prepare frame data)
  ├── ExecutionPhase::FlushUploads       → (GPU upload flush)
  ├── ExecutionPhase::PreRecord          → RenderGraphService::PreRecord
  │     ├── builder.Reset()
  │     ├── BuildMinimalFrameGraph (Scene+Overlay+Present)
  │     ├── builder.Compile() → CompiledRenderGraph
  │     ├── BuildTransientAllocationPlan
  │     ├── BuildNativePassPlan
  │     ├── BuildBarrierPlan
  │     ├── LowerToVulkanBarrierPlan
  │     ├── ResolveQueueExecutionPolicy
  │     └── ResolvePhysicalResources (VulkanResourceTable)
  ├── ExecutionPhase::Record             → RenderGraphService::Record
  │     ├── (multi-queue: RecordPreparedMultiQueueGraph)
  │     └── (single-queue: RenderGraphExecutor::Record)
  ├── ExecutionPhase::Submit             → SubmitPreparedMultiQueueWork (多队列 vkQueueSubmit2)
  ├── ExecutionPhase::PostRecord         → MarkGraphicsSubmissionEnqueued
  ├── ExecutionPhase::Present
  ├── ExecutionPhase::EndFrame
  └── ExecutionPhase::Retire
```

### 3.4 Runtime 类型化服务层 (`vr/runtime`)

**职责**: 新一代运行时核心。将每个 Host/Renderer 包装为带类型标签的 Service，通过编译期依赖图 (DAG) 进行生命周期编排和执行调度。支持 Profile 驱动的二进制裁剪。

#### 3.4.1 核心架构

| 组件 | 说明 |
|------|------|
| `Runtime<Profile>` | 顶层 Runtime 模板。聚合 RuntimeKernel + 所有 Services，Init/Shutdown/Tick 统一编排。基于 `RuntimeProfile` 编译期裁剪。 |
| `RuntimeKernel` | Runtime 内核。管理 Service 注册、依赖解析 (DAG)、启动/停止/帧循环时序。静态依赖图无运行时开销。 |
| `RuntimeContext` | Runtime 全局上下文。持有 VulkanContext、GpuMemoryHost、帧索引等全局共享资源。 |
| `RuntimeExecution` | Runtime 执行追踪。通过 `ExecutionPhaseDriver` 管理精细帧内阶段：ServiceBeginFrame→Prepare→FlushUploads→PreRecord→Record→Submit→PostRecord→Present→EndFrame→Retire。Multi-Queue 提交协调。 |
| `FrameScheduler` | 帧调度器。帧内任务排序、屏障插入、Queue 提交批处理。 |
| `RuntimeDiagnostics` | 运行时诊断 V2 (`RuntimeFrameDiagnosticsV2`)。5 级诊断深度 (Off/CountersOnly/Detailed/GpuTiming/Capture)。`RenderGraphRuntimeDiagnostics` 提供编译/执行/瞬态内存/队列时间线全量诊断。`RenderGraphQueueTimeline` 可视化多队列提交批次和跨队列依赖。`BuildRenderGraphQueueTimelineJson()` 生成标准 JSON 报告。 |
| `RuntimeStatus` | 运行时状态监控。Service 健康度 (Healthy/Degraded/Failed)、帧率、资源使用指标。 |
| `RuntimeViews` | 运行时视图集合。编译期生成零开销 Service 引用访问接口。 |
| `ServiceDependencyGraph` | 服务依赖图。编译期 DAG 构建、拓扑排序、循环依赖检测。 |

#### 3.4.2 Runtime Profile 系统

Profile 系统通过编译期模板参数决定哪些 Service 被包含和激活，实现二进制裁剪。

| Profile | 说明 |
|---------|------|
| `MinimalProfile` | 最小 Profile：仅 Core 服务 (VulkanContext + Swapchain + FrameSync + Command)。适合最小化启动和调试。 |
| `Runtime2DProfile` | 2D Profile：Text + Geometry2D + Surface2D + Light/Shadow2D + Particle2D。 |
| `Runtime3DProfile` | 3D Profile：2D Profile 全部 + 3D 服务 + IBL + Animation + Skybox + FrameComposer。 |

#### 3.4.3 服务包装器 (`vr/runtime/services/`)

每个现有 Host/Renderer 被包装为 `RuntimeService<S>` 接口，共约 25 个服务：

| 服务 | 包装对象 | 说明 |
|------|---------|------|
| `BoundHostService` | 任意 Host | 泛型 Host 服务绑定器，自动生成 Init/Shutdown/Tick 回调。 |
| `CommandService` | FrameCommandHost | CommandBuffer 池化管理服务。 |
| `DescriptorService` | DescriptorHost | 描述符池与 Layout 服务。 |
| `GpuMemoryService` | GpuMemoryHost | GPU Buddy Allocator 服务。 |
| `SamplerService` | SamplerHost | 采样器缓存服务。 |
| `TextureService` | TextureHost | 资产纹理服务。 |
| `UploadService` | UploadHost | Staging Buffer 上传服务。 |
| `PipelineService` | PipelineHost | 管线缓存服务。 |
| `RenderTargetService` | RenderTargetHost | 渲染目标管理服务。 |
| `FrameComposerService` | FrameComposerHost | 帧合成服务。Phase12 RenderGraph-only (移除 `RenderTargetPool*` 依赖)。 |
| `FreeTypeService` | FreeTypeHost | FreeType 字体引擎服务。 |
| `GlyphAtlasService` | GlyphAtlasHost | 字形图集服务。 |
| `GlyphUploadService` | GlyphUploadHost | 字形上传服务。 |
| `IBLService` | IBLHost | IBL 环境光照服务。 |
| `IBLBakeService` | IBLBakeHost | IBL 烘焙服务。 |
| `ParticleSimulationService` | ParticleSimulationHost<Dim> | 粒子 GPU 模拟服务。 |
| `ParticleUploadService` | ParticleUploadHost<Dim> | 粒子上传服务。 |
| `ParticleRenderService` | ParticleRenderer<Dim> | 粒子渲染服务。 |
| `RenderGraphRuntimeService` | RenderGraph | RenderGraph 运行时服务。管理帧图构建/编译/执行全管线 + Multi-Queue 提交。依赖 GpuMemoryService/RenderTargetService/DescriptorService。 |

**依赖模型**: 每个 Service 通过 `ServiceDependency` 声明所需的其他 Service。编译期 DAG 验证确保无循环依赖和无缺失依赖。运行时仅调用已满足依赖的 Service。

### 3.5 ECS 层 (`vr/ecs`)

**职责**: 纯数据组件 + 静态方法系统，零虚函数、零继承，全 POD 组件设计。

#### 3.5.1 概念基础

| 文件 | 说明 |
|------|------|
| `concept/dimension.hpp` | `Dim2` / `Dim3` 维度标签，`DimensionTag` concept。 |
| `component/spatial_types.hpp` | 数学类型别名：`Float2/3/4`, `Quaternion`, `Affine2x3`, `Matrix4x4` (D3D 列主序)。 |

#### 3.5.2 ECS 组件 (全 POD)

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
| **Appearance** | 2D visibility+link group | 3D visibility+link group + PBR params + sampled surface bindings | appearance record, dirty tracking, link handle, `AppearanceSampledSurfaceBinding` (5 texture channels: base_color/normal/metallic-roughness/occlusion/emissive), `AppearanceGpuRecord`, `AppearanceHandle`, `AppearancePipelineBucket` |
| **Animation** | 2D animation clock/track | 3D animation clock/track | clock state, Property/Material/Camera/Path/Skeletal/Morph/VertexDeform/FrameSequence tracks, clip route, evaluation state |
| **Particle** | 2D particle (position+velocity+color+size+rotation) | 3D particle (position+velocity+color+size+rotation) | age/lifetime, emitter route, sort key, texture index, GPU compute update |
| **ParticleEmitter** | 2D emission shape (Point/Circle) | 3D emission shape (Point/Sphere/Cone/Box) | emission rate, burst, initial property ranges, lifetime distribution |

#### 3.5.3 ECS 系统

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
| `AppearanceSystem<Dim>` | Appearance 组件管理 | 可见性记录、脏标记、link 句柄、PBR 材质参数查询 (base_color/metallic/roughness/normal_scale/occlusion/emissive/unlit)、`AppearanceSampledSurfaceBinding` 纹理通道管理 |
| `AppearanceLinkSystem` | Appearance 链接 | 链接 Geometry/Surface 到 Appearance 记录、跨渲染器协调、增量 Link/Delink、batch 刷新 |
| `AppearanceRuntimeSystem<Dim>` | Appearance 运行时 | 运行时 appearance 数据生成、`AppearanceGpuRecord` 缓存、与渲染器批量对接。`AppearanceSampledSurfaceBinding3D` 对外提供统一纹理绑定接口 |
| `VisualRuntimeRouteCommon` | Visual 运行时路由公共宏 | 跨子系统共享宏 `VR_ECS_VISUAL_RUNTIME_ROUTE_SORT_KEY_FIELD/TRAILING_FIELDS` 用于 Geometry/Surface/Particle 的统一 runtime route 字段定义 |

##### 动画系统组 (Phase 1 完整实现)

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `AnimationClockSystem<Dim>` | 动画时钟 | 时间推进 (delta/time scale)、播放状态 (Play/Pause/Stop)、循环/单次/乒乓模式 |
| `AnimationCurveSystem<Dim>` | 动画曲线 | 关键帧曲线采样 (Linear/Step/CubicSpline)、时间→值映射 |
| `AnimationPropertyTrackSystem<Dim>` | Property 轨道 | 采样 Position/Rotation/Scale/Color/Float 轨道 |
| `AnimationPropertyEvaluationSystem<Dim>` | Property 求值 | 将 Property 轨道输出写入 Transform、Appearance 组件 |
| `AnimationVisualTrackSystem` | Visual 轨道 (原 Material Track) | 采样 base_color/metallic/roughness/emissive 外观动画轨道 |
| `AnimationVisualEvaluationSystem` | Visual 求值 (原 Material Evaluation) | 将 Visual Track 输出写入 Appearance 组件 |
| `AnimationCameraTrackSystem<Dim>` | Camera 轨道 | 采样 FOV/Near/Far/Position/LookAt 动画轨道 |
| `AnimationCameraEvaluationSystem<Dim>` | Camera 求值 | 同步 Camera 轨道输出到 Camera 组件 |
| `AnimationPathMotionSystem<Dim>` | 路径运动 | Position/Rotation/Scale 路径曲线采样、路径约束 (Follow/LookAt) |
| `AnimationPathEvaluationSystem<Dim>` | 路径求值 | 将 Path Motion 输出写入 Transform 组件 |
| `AnimationSkeletalEvaluationSystem<Dim>` | 骨骼求值 | 骨骼轨道采样、Joint Palette 更新、Root Motion 提取 |
| `AnimationMorphEvaluationSystem<Dim>` | 变形求值 | Morph Target 权重采样、变形缓冲区构建 |
| `AnimationVertexDeformEvaluationSystem<Dim>` | 顶点变形求值 | Vertex Deform 轨道采样、顶点负载输出 |
| `AnimationFrameSequenceEvaluationSystem<Dim>` | 帧序列求值 | Frame Sequence 轨道采样、A/B 帧混合、子网格帧索引路由 |
| `AnimationResourceTrackSystem` | 资源轨道 | 动画资源轨道生命周期管理和路由 |

##### 粒子系统组

| 系统 | 职责 | 关键特性 |
|------|------|---------|
| `ParticleSystem<Dim>` | 粒子路由与排序 | 64 位排序键 (pass/material/depth/batch)、生命周期管理、可见性路由 |
| `ParticleRuntimeSystem<Dim>` | 粒子运行时 | GPU 粒子实例生成 (position/velocity/color/size/rotation)、批次合并、深度/layer 排序 |
| `ParticleEmitterSystem<Dim>` | 粒子发射器 | 发射形状采样 (Point/Circle/Sphere/Cone/Box)、爆发/持续发射、初始属性随机化、发射计数管理 |
| `TransparencyRenderPolicy` | 透明度策略 | 统一管理跨渲染器 (Particle/Text/Geometry/Surface) 的透明排序与混合模式路由 |

### 3.6 渲染器模块

#### 3.6.1 Text 渲染器 (`vr/text`)

| 组件 | 说明 |
|------|------|
| `FreeTypeHost` | FreeType 库初始化、字体 face 加载 (内存/文件)、字形指标/位图/SDF 渲染。 |
| `GlyphAtlasHost` | 字形图集：运行时页面分配、字形打包、脏页追踪、双层缓存 (粗→细)。 |
| `GlyphUploadHost` | 字形上传：将脏图集页面通过 UploadHost 上传至 GPU Image。 |
| `TextRenderer2D` | 2D 文本渲染：绑定图集纹理、字形顶点/索引缓冲、Push Constant。支持 SDF/Outline。 |
| `TextRenderer3D` | 3D 文本渲染：世界空间文本渲染，支持 Billboard、深度测试。 |

#### 3.6.2 Geometry 渲染器 (`vr/geometry`)

| 组件 | 说明 |
|------|------|
| `GeometryResourceHost` | 几何资源注册：mesh/image 资源 ID 管理。 |
| `GeometryImageHost` | 几何图像：纹理 Image/View/Sampler 绑定、Bindless slot 注册。 |
| `GeometryUploadHost` | 几何上传：顶点/索引缓冲区上传、路径数据上传、切线空间数据上传。 |
| `GeometryAppearanceHost` | 几何 Appearance 宿主 (替代旧 `GeometryMaterialHost`)。管理每个 Appearance 的 SampledSurface 绑定 (base_color/normal/metallic-roughness/occlusion/emissive)、UV 变换、Alpha Test 标志。PBR 材质从此通过 Appearance 而非独立 Material 系统处理。 |
| `GeometryAppearanceResolver` | Geometry Appearance 解析器。从 Appearance 组件 + `GeometryAppearanceHost` 解析最终 PBR 渲染参数 (base_color/metallic/roughness/normal_scale/occlusion/unlit)。 |
| `GeometryTangentSpace` | 几何切线空间构建。从 position/normal/uv 生成 tangent/bitangent 用于法线贴图。支持 fallback basis、退化 UV 检测。PBR 法线贴图必需的预备阶段。 |
| `GeometryRenderer2D` | 2D 几何渲染：路径线段→顶点缓冲、描边/填充、抗锯齿、Push Constant (含 bindless slot index)。集成 Appearance 渲染支持。 |
| `GeometryRenderer3D` | 3D 几何渲染：GPU 实例化绘制、PBR 材质 + Bindless + Appearance SampledSurface 绑定 (通过 `appearance_decode_3d.glsl` + `pbr.glsl`)、Shadow mapping 支持。 |

#### 3.6.3 Surface 渲染器 (`vr/surface`)

| 组件 | 说明 |
|------|------|
| `SurfaceImageHost` | Surface 图像/精灵管理：图集页面、采样器绑定、Descriptor 写入。 |
| `SurfaceUploadHost` | Surface 上传：图像数据上传至 GPU、脏区域追踪。 |
| `SurfaceRenderer2D` | 2D Surface 渲染：精灵绘制、混合模式 (bindless texture slot)、UV 变换、tint color。 |
| `SurfaceRenderer3D` | 3D Surface 渲染：世界空间 Surface、纹理过滤 (bindless sampler slot)、双面渲染。 |

#### 3.6.4 Light/Shadow 渲染器 (`vr/light`, `vr/shadow`)

| 组件 | 说明 |
|------|------|
| `LightShadowUploadHost` | Light-Shadow 数据上传：光源数据 (UBO)、阴影矩阵、Atlas 引用的 GPU 上传。 |
| `ShadowAtlasHost` | Shadow Map Atlas 管理：Atlas 页面分配、阴影贴图打包、脏页追踪。 |
| `ShadowRenderer2D` | 2D 阴影渲染器：2D 遮挡物深度图渲染、光源关联。 |
| `ShadowRenderer3D` | 3D 阴影渲染器：3D Shadow Map 渲染、深度管线、Directional Shadow Fit + 骨骼动画阴影支持。 |

#### 3.6.5 动画 Host 层 (`vr/animation`)

| 组件 | 说明 |
|------|------|
| `AnimationClipHost<Dim>` | 动画片段管理器。Clip 生命周期 (Create/Destroy)、轨道收集、采样求值。支持循环/单次/乒乓模式。 |
| `AnimationSkeletalHost<Dim>` | 骨骼动画宿主。关节层级、Joint Palette 构建、GPU Skinning 矩阵上传、Root Motion 提取。 |
| `AnimationMorphHost<Dim>` | 变形目标 (Morph Target) 宿主。变形目标权重管理、GPU 缓冲区上传、Blend Shape 支持。 |
| `AnimationPathHost<Dim>` | 路径动画宿主。路径曲线管理、路径采样与运动合成。 |
| `AnimationVertexDeformHost<Dim>` | 顶点变形宿主。GPU 顶点变形缓冲区管理、Deform Payload 上传。 |
| `AnimationFrameSequenceHost<Dim>` | 帧序列动画宿主。子网格帧序列播放、A/B 帧混合、帧索引路由。 |

#### 3.6.6 资产纹理 (`vr/asset`)

| 组件 | 说明 |
|------|------|
| `TextureHost` | 资产纹理宿主。接收外部已解码像素数据、创建/接管 GPU Image、Mipmap 生成。不包含 PNG/KTX2 解码，仅消费已准备数据。 |

#### 3.6.7 GPU 骨骼蒙皮 (`vr/geometry`)

| 组件 | 说明 |
|------|------|
| `GeometrySkeletalPaletteBuilder` | GPU 骨骼调色板构建器。从 `AnimationSkeletalHost` 消费 Joint Palette，构建用于顶点着色器骨骼蒙皮的矩阵 UBO/SSBO。 |

#### 3.6.8 粒子渲染器 (`vr/particle`)

| 组件 | 说明 |
|------|------|
| `ParticleTypes` | 粒子公共类型。POD 粒子数据 (position/velocity/color/size/rotation/age/lifetime)、发射器描述符、模拟参数。 |
| `ParticleSimulationHost<Dim>` | 粒子模拟宿主。GPU Compute Shader 驱动的粒子物理：速度/重力/阻尼/生命周期更新。支持 AABB 裁剪和排序键。 |
| `ParticleUploadHost<Dim>` | 粒子上传宿主。粒子/发射器缓冲区通过 UploadHost 传输至 GPU。支持增量上传减少 PCI-e 带宽。 |
| `ParticleRenderer2D` | 2D 粒子渲染器。四边形绘制、GPU 实例化、纹理动画、混合模式、透明度排序。 |
| `ParticleRenderer3D` | 3D 粒子渲染器。世界空间粒子、Billboard 摄像机对齐、Z 深度排序、纹理动画、GPU 实例化绘制。 |

---

### 3.7 Scene 场景抽象层 (`vr/scene`)

Scene 层提供统一的场景抽象，将背景/环境管理、Scene Root 组件和提交构建从 ECS 和 Recorder 中分离为独立层。

| 组件 | 说明 |
|------|------|
| `Scene<Dim, Background>` | 场景模板类。聚合 `SceneRoot<Dim>` + `Background` (2D: `SpriteBackground`, 3D: `SkyEnvironment`) + 场景修订号。`scene_revision` 用于追踪场景级脏状态。 |
| `SceneTraits<Dim, Background>` | 场景 trait。编译期类型映射：`[Dimension, Background]` → `RenderState` → `View` → `Packet` → `BackgroundTraits`。零开销编译期派发。 |
| `SceneRoot<Dim>` | 场景根组件 (POD)。指向 active_camera_entity、background_entity、environment_entity。携带场景级 flags 和 revision。 |
| `ScenePrepare<Dim, Background>` | 场景准备器。静态方法 `Resolve(Scene)` 从 Scene 解析背景/环境渲染状态。2D→`Background2DRenderState`, 3D→`SkyEnvironmentRenderState`。 |
| `SceneSubmissionBuilder<Dim, Background>` | 场景提交构建器。`Build()` 从 Scene + RenderViews 构建 `RenderScenePacket`，注入背景/环境数据到 `packet.extra.background` / `packet.extra.environment`。支持 `BackgroundOverrideMode` (inherit/override_state/disabled) 允许 View 覆写场景背景。 |

**背景类型**:

| 类型 | 维度 | 渲染路径 |
|------|------|---------|
| `SpriteBackground` | 2D | Surface 路径 → `BackgroundPass2D` |
| `SkyEnvironment` | 3D | Environment GPU 路径 → `SkyEnvironmentGpuHost` + `SkyEnvironmentPass` |

**天空环境模式**: none / solid_color / gradient / cubemap / equirectangular_hdr / procedural_atmosphere
**2D 背景模式**: none / solid_color / gradient / sprite / surface_entity

## 4. 运行时帧循环管线

### 4.1 Legacy 命令式路径 (Phase12 已移除双轨)

以下 Legacy 命令式管线中，以 `SceneRenderTargetSet`/`SceneBloomPostStack`/`RecordTonemapPass` 为代表的**命令式场景目标+后处理路径**在 Phase12 已完全移除。Appearance、Light、Shadow、Animation 四套协调器的 ECS 数据准备仍然保留，但场景目标的创建、生命周期和录制全部由 RenderGraph 声明式接管。仅保留此图作为数据准备阶段的参考：

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
  ├── ParticleSimulationHost::Simulate   (GPU Compute 粒子物理 + 排序)
  │   ├── ParticleEmitterSystem::Emit   (新粒子生成)
  │   └── ParticleUploadHost::Upload    (粒子数据→GPU)
  │
  ├── ScenePrepare::ResolveEnvironment    (解析 SkyEnvironment 渲染状态)
  ├── SkyEnvironmentGpuHost::PrepareFrame  (Register/Update 环境 → GPU 参数 + IBL 数据)
  │   ├── TryBakePendingEnvironment        (惰性 IBL: 检查待烘焙 → 触发 IblBakeHost)
  │   └── EnsureIblEnvironment             (按需注册 IBL 环境)
  │
  ├── SceneSubmissionBuilder::Build       (构建 RenderScenePacket + 背景/环境注入)
  │
  ├── BindlessResourceSystem::PrepareFrame   (刷新 SampledImage/Sampler 表写入)
  │
  ├── SceneRecorder3D::Record       (Multi-view packet 路由 → 场景 Pass)
  │   ├── SkyEnvironmentPass::Record (天空环境渲染 → bindless texture slot)
  │   ├── SceneRenderStage           (Opaque → Transparent 阶段)
  │   ├── ParticleRenderer::Render  (粒子绘制 → bindless slot + Push Constant)
  │   └── BackgroundPass2D (2D only) (2D 背景渲染)
  │
  ├── IBLHost::Bind                  (绑定 Irradiance/Specular/BRDF LUT)
  │
  ├── [Phase12 移除] SceneRenderTargetSet::Acquire  → 现由 RenderGraph Transient Resource 管理
  ├── [Phase12 移除] Renderer::Record (legacy)      → 现通过 RecordGraphPass (GraphCommandContext)
  ├── [Phase12 移除] SceneBloomPostStack::Process   → 现由 NativePassPlan 管理
  ├── [Phase12 移除] FrameComposerHost::RecordTonemapPass → 现由 RenderGraph Present Pass 管理
  │
  ├── FrameSync::Submit  → 提交命令
  └── FrameRetire::Collect  → 延迟回收
```

### 4.2 RenderGraph 声明式路径 (ExecutionPhase 驱动, Phase12 后为唯一路径)

Phase12 关闭了旧双轨制 (Legacy 命令式 + Graph 声明式)，RenderGraph 现为**帧渲染的唯一调度中心**。所有 Pass 通过 `ExecutionPhaseDriver` 管理帧内阶段，完全声明式：

```
RenderRuntime::Tick()
  │
  ├── FrameSync::Acquire + FrameCommand::Begin
  │
  ├── [Phase: ServiceBeginFrame] → RenderGraphService::BeginFrame
  │
  ├── [Phase: Prepare] → ECS/Coordinators/Scene Prepare (同上)
  │
  ├── [Phase: FlushUploads] → UploadService::Flush (GPU upload)
  │
  ├── [Phase: PreRecord] → RenderGraphService::PreRecord
  │   ├── FrameSnapshot (从 ScenePacket 提取冻结数据)
  │   ├── RenderGraphBuilder::BuildMinimalFrameGraph
  │   │   ├── Scene Pass (Color+Depth attachments)
  │   │   ├── Overlay Pass (Color load+store)
  │   │   ├── Extensible Graph Callback (2D/3D scene content)
  │   │   ├── Present Copy/Blit Pass
  │   │   └── Present Transition Pass (side_effect)
  │   ├── builder.Compile() → CompiledRenderGraph
  │   │   ├── Topological Sort → ExecutionOrder
  │   │   ├── Resource Liveness Analysis
  │   │   └── DescriptorBindingPlan
  │   ├── BuildTransientAllocationPlan (瞬态内存别名分析)
  │   ├── BuildNativePassPlan (Pass 融合 + Load/Store 推理)
  │   ├── BuildBarrierPlan (逻辑屏障 + 队列提交批次)
  │   ├── LowerToVulkanBarrierPlan (Vulkan 屏障降级)
  │   ├── ResolveQueueExecutionPolicy (Multi-Queue 决策)
  │   └── VulkanResourceTable::Resolve (物理资源映射)
  │
  ├── [Phase: Record] → RenderGraphService::Record
  │   ├── (multi-queue) RecordPreparedMultiQueueGraph
  │   │   ├── 按队列批次录制 Command Buffers
  │   │   └── 插入 Cross-Queue Dependency Semaphores
  │   └── (single-queue) RenderGraphExecutor::Record
  │       ├── 按批次应用 Barrier (vkCmdPipelineBarrier2)
  │       ├── 遍历 Pass 执行顺序: SetCurrentPass → Execute → Clear
  │       └── 统计 RenderGraphRecordStats
  │
  ├── [Phase: Submit] → SubmitPreparedMultiQueueWork
  │   └── vkQueueSubmit2 (graphics/compute/transfer 并行)
  │
  ├── [Phase: Present] → Swapchain Present
  │
  ├── [Phase: EndFrame] → Pipeline/RenderTarget cleanup
  │
  └── [Phase: Retire] → FrameRetire::Collect (延迟回收)
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
| `render_target_composite_far.vert` | 远平面合成顶点着色器 (天空环境全屏四边形) |
| `background_2d.frag` | 2D 背景 (solid/gradient/sprite/surface_entity) |
| `sky_environment.frag` | 天空环境入口 (模式分发) |
| `sky_environment_image.frag` | 天空环境图像 (立方体贴图采样) |
| `sky_environment_equirect.frag` | 天空环境等距矩形 (HDR 映射) |
| `sky_environment_atmosphere.frag` | 天空环境大气散射 (太阳角/Mie/Rayleigh) |

### 5.2 粒子着色器

粒子系统通过 Compute Shader 进行 GPU 端模拟，通过顶点/片段着色器进行渲染。

| 着色器 | 用途 |
|--------|------|
| `particle_build_2d.comp` | 2D 粒子构建计算着色器 (发射器→粒子实例化) |
| `particle_build_3d.comp` | 3D 粒子构建计算着色器 (发射器→粒子实例化) |
| `particle_update_2d.comp` | 2D 粒子更新计算着色器 (生命周期/速度/位置/颜色插值) |
| `particle_update_3d.comp` | 3D 粒子更新计算着色器 (生命周期/物理/颜色插值) |
| `particle_sort_3d.comp` | 3D 粒子排序计算着色器 (Z 深度排序) |
| `particle_2d.vert` | 2D 粒子顶点着色器 (四边形 + 实例化) |
| `particle_2d.frag` | 2D 粒子片段着色器 (纹理 + 颜色/Alpha) |
| `particle_3d.vert` | 3D 粒子顶点着色器 (Billboard + 世界空间) |
| `particle_3d.frag` | 3D 粒子片段着色器 (纹理 + 深度) |

### 5.3 共享 GLSL 头文件 (`shaders/include/`)

通过 `#include` 在着色器间共享通用函数，避免重复代码。

| 文件 | 说明 |
|------|------|
| `shaders/include/vr/common/math.glsl` | 通用数学工具函数 (矩阵变换、坐标转换)。 |
| `shaders/include/vr/text/text_shading.glsl` | 文本着色函数 (SDF 边缘平滑、轮廓、颜色混合)。被 `text_2d.frag` 和 `text_3d.frag` 引用。 |
| `shaders/include/vr/render/bindless.glsl` | Bindless 纹理采样共享头文件。声明全局 bindless descriptor arrays (Set 0: `g_Textures2D[]/g_Textures2DArray[]/g_TexturesCube[]`, Set 1: `g_Samplers[]`)。提供 `SampleTexture2D/SampleTextureCube/SampleTexture2DArray/SampleTextureCubeLod` 统一采样函数。所有渲染着色器通过 `#include` 引用。 |
| `shaders/include/vr/render/pbr.glsl` | PBR 着色共享头文件 (91 行)。`PbrParams` 结构体、`DecodePbrParams`、`EvaluateDirectionalLight` (GGX-Smith/Fresnel Schlick BRDF)、`EvaluateAmbientIBL` (Diffuse+Specular)、ACES Tone Map。 |
| `shaders/include/vr/render/appearance_decode_3d.glsl` | Appearance 3D 解码共享头文件 (105 行)。从 bindless 纹理表解码 SampledSurface 贴图→`PbrParams`。`DecodeBaseColor/DecodeNormal/DecodeMetallicRoughness/DecodeOcclusion/DecodeEmissive`。 |

### 5.4 着色器工具链

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

基于自定义轻量框架 (`test_framework.hpp/cpp`)。约 73 个测试文件，覆盖：
- ECS 组件/系统单元测试 (Transform, Camera, Bounds, Culling, Geometry, Surface, Text, Light, Shadow, Appearance, Animation, Particle)
- 动画系统单元测试 (Clock/Curve/Property/Material/Camera/Path/Skeletal/Morph/VertexDeform/FrameSequence)
- 动画 Host 单元测试 (Clip/Skeletal/Morph/VertexDeform/FrameSequence)
- 粒子系统单元测试 (Component/RuntimeSystem/SimulationHost)
- Scene 背景核心测试 (SpriteBackground + SkyEnvironment)
- 运行时集成测试 (含 Particle 2D/3D 渲染器集成, TextRenderer 集成, SkyEnvironment 集成)
- GPU 资源类型测试 (已扩展至 1130 行)
- FreeType/Glyph 渲染测试
- FrameRetire 回收测试
- Light/Shadow 协调器测试
- Appearance 协调器测试
- IBL/IBLBake/FrameComposer/TextureHost 集成测试
- Shadow Renderer 生命周期测试
- 着色器合约测试 (Bindless/PBR/Appearance)

### 8.2 基准测试 (`bench/`)

基于自定义基准框架 (`bench_framework.hpp/cpp`)，支持：
- 崩溃追踪器集成
- JSON 基线存储/对比
- 门控检查 (`run_bench_gate.ps1`)
- 多轮快照 (`_snapshot.md`)

约 22 个基准文件，添加：
- `runtime_diagnostics_bench.cpp` (442 行)：RenderGraph Queue Timeline 序列化性能、RuntimeDiagnostics 数据收集性能
- `runtime_steady_state_allocation_bench.cpp` (354 行)：Text-heavy 长时运行的内存分配稳态行为
- `render_graph_queue_timeline_gold.json` (296 行)：Queue Timeline 黄金基线

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
11. **离屏渲染 / Render Target (Phase12 重构)**: 场景渲染目标管理已从命令式 `SceneRenderTargetSet`/`SceneBloomPostStack`/`FrameComposerTargets` 迁移至 RenderGraph 声明式管理。`BuildMinimalFrameGraph` 创建 `scene_color`/`scene_depth` 为 Transient 资源并通过 RasterPassDesc 管理 Load/Store Op。Bloom 后处理由 RenderGraph Native Pass Plan 以独立 Graph Pass 管理。`RenderTargetPool` 从一等 Service 降级为内部实现细节 (仅通过 `RenderTargetPoolStats()` 暴露统计)，瞬态目标内存由 `TransientAllocationPlan` (Transient Aliasing) 管理。
12. **Bloom 后处理栈**: 三阶段 Bloom 管线 — Prefilter (亮度阈值) → Blur (多级降采样/升采样) → Combine (混合)。通过 SceneBloomPostStack 封装参数配置和 Pass 编排。
13. **透明排序与混合**: `TransparencyRenderPolicy` 统一管理跨渲染器的透明排序策略。结合按帧材质路由 (`AppearanceRuntimeSystem`) 和 `ColorBlendState` 实现 64 位排序键中的 pass 位驱动分层透明/不透明渲染。
14. **C++20 模块**: `feature/cpp20-modules` 分支已迁移所有 10 个核心库为 `.cppm` 接口单元。使用 `vr_module_fwd.hpp` 统一全局片段以实现零重复。`vr.ecs` 模块通过模板声明模式避免了与 `vr.text` 的循环依赖。
15. **统一场景提交**: 通过 `RenderView<Dim>` + `RenderScenePacket<Dim>` + `SceneRecorder<Dim>` 三层结构统一场景提交路径。2D 和 3D 共享相同 packet/view 概念，但 Recorder 独立实现 (避免过早合并)。
16. **动画 ECS Phase 1**: 完整的 7 类动画轨道 (Property/Material/Camera/Path/Skeletal/Morph/VertexDeform/FrameSequence) + 时钟 + 曲线 + 求值系统。通过 `AnimationFrameCoordinator` 统一注入 SceneRecorder 和渲染管线。GPU Skinning 通过 `SkeletalPaletteBuilder` 上传 Joint Palette 至顶点着色器。
17. **IBL 环境光照**: IBL 分为在线宿主 (IBLHost) 和离线烘焙宿主 (IBLBakeHost)。Irradiance Map + Prefiltered Environment Map (多级 mip) + BRDF Integration LUT 三件套。通过 Compute Shader 驱动烘焙流程。
18. **Text 运行时契约**: `TextRuntimeContract` 定义 Text 系统与 Renderer 之间的资源契约和上传策略，消除隐式依赖，使字形页面调度可诊断。
19. **Directional Shadow Fit & Stabilization**: 3D Shadow Renderer 支持 Directional Light 的 CSM 风格阴影贴合 (Shadow Fit) 和坐标稳定化，减少阴影抖动。
20. **GPU Compute 粒子系统**: 粒子模拟完全在 GPU 端通过 Compute Shader 执行 (Build→Update→Sort 三阶段)，CPU 仅管理发射逻辑。减少 CPU-GPU 带宽需求，支持百万级粒子。
21. **类型化服务与 Profile 驱动**: 新一代 `Runtime<Profile>` 将每个 Host 包装为编译期类型安全的 Service，通过静态依赖 DAG 管理生命周期。Profile (Minimal/2D/3D) 编译期裁剪未使用的 Service，减小二进制体积。
22. **质量分级测试框架**: `scripts/testing/` 引入 Quality Profiles (Critical/High/Medium/Low)，通过 `vr_quality_runner.py` 自动化测试编排。`quality_profiles.json` 定义可配置的门禁规则，支持 CI 集成。
23. **Scene 场景抽象层**: 将场景背景/环境管理从 ECS 组件和 Recorder 中分离为独立 `vr/scene/` 层。`Scene<Dim, Background>` 模板通过编译期 trait 派发实现零开销的 2D/3D 分支选择。
24. **天空环境重构与惰性 IBL**: `SkyboxRenderer` 被三件套替换：`SkyEnvironment` (POD 数据) + `SkyEnvironmentGpuHost` (GPU 资源管理 + 惰性烘焙协调) + `SkyEnvironmentPass` (6 种渲染模式)。IBL 管线从主动烘焙改为惰性模式：`SkyEnvironmentGpuHost` 追踪待烘焙环境，通过 `TryBakePendingEnvironment` 按需触发 `IblBakeHost`，仅在环境首次可见或参数变更时才烘焙。
25. **Bindless 统一渲染合约**: 所有渲染子系统通过 `BindlessResourceSystem` 统一纹理/采样器访问。2 个全局 Descriptor Table (Set 0=SampledImage[8192], Set 1=Sampler[256]) + 占位符填充 + `BindlessSlot {index, generation}` 双重校验。渲染器不再持有独立 VkDescriptorSet，改为 Push Constant 传递 bindless slot index，着色器通过 `nonuniformEXT` 动态索引。统一后各渲染器代码大幅精简 (Surface -500+, Particle -700+, Text -500+)。
26. **CrashTracer 运行时集成**: CrashTracer 从 bench-only 提升为 Runtime 级基础设施。`InstallProcessCrashTracer` 在应用程序入口点安装进程级 SEH 崩溃处理器，替代旧的 `bench_crash_tracer_support`。`vulkan_context.cpp` 集成 CrashTracer 初始化逻辑。
27. **fast_math 迁移**: 移除 math shim 层，将 `spatial_math.hpp` 和所有 ECS 系统直接迁移至新 `fast_math` API。降低头文件依赖、提升 SIMD 优化覆盖面、消除中间层的编译开销。
28. **Appearance 语义统一**: `Appearance` 组件现在是所有视觉渲染的**唯一语义中心**。PBR 材质参数 (base_color/metallic/roughness/normal_scale/occlusion/emissive) 和 SampledSurface 纹理绑定 (5 通道) 统一存储在 Appearance 中。`GeometryMaterialHost` 被 `GeometryAppearanceHost` + `GeometryAppearanceResolver` 替代，消除了 Material→Appearance 的隐式耦合和重复数据。
29. **PBR 着色基础**: 新增 `pbr.glsl` (GGX-Smith/Fresnel Schlick BRDF) 和 `appearance_decode_3d.glsl` (bindless 纹理解码)。`GeometryTangentSpace` 提供法线贴图必需的切线空间构建。PBR 渲染路径：Appearance 组件 → `GeometryAppearanceHost` → `AppearanceGpuPrepare` → bindless slot → `appearance_decode_3d.glsl` → `pbr.glsl` → `geometry_3d.frag`。
30. **Visual Runtime Route 统一**: 新增 `VisualRuntimeRouteCommon` 跨子系统共享宏，Geometry/Surface/Particle 的 runtime route 使用统一的字段定义和操作函数。`HasAppearanceHandleChanged()` / `ClearAppearanceRuntimeRoute()` 跨子系统共通。
31. **RenderGraph 声明式渲染调度**: RenderGraph 是渲染帧内执行的唯一调度中心。所有 Pass 通过声明式 API 描述资源读写和访问语义，由编译器推导资源状态转换 (Barrier Plan)、瞬态别名分析 (Transient Allocation) 和跨队列同步 (Queue Scheduling)。高层渲染模块不再手写 VkImageLayout/VkPipelineStageFlags/VkBarrier。
32. **SSA 资源版本化**: RenderGraph 的资源引用采用 Static Single Assignment 风格。每次 Write 产生新的 `ResourceVersionHandle`，Read 引用特定版本。依赖链自动形成 DAG，编译器由此推导执行顺序和 Barrier。
33. **Native Pass Fusion**: 连续兼容的 Raster Pass 自动融合为单次 Dynamic Rendering Scope。编译器分析 Attachment Load/Store Op (elide last-use DONT_CARE、infer first-use CLEAR、preserve STORE)、21 种融合阻止条件 (queue mismatch/side_effect/attachment mismatch/sampled resource aliasing 等)。减少 VkRenderPass 切换开销。
34. **瞬态别名分配 (Transient Aliasing)**: 替代旧 `RenderTargetPool`。图级 Liveness 分析确定每个 transient 资源的活跃区间，兼容性分类 (TransientCompatibilityKey) 后，别名配对共享物理内存页。`TransientMemoryTimeline` 记录逻辑/物理峰值内存。Reduced memory footprint ~30-60%。
35. **Multi-Queue 运行时调度**: 编译后的图按队列 (Graphics/Compute/Transfer) 拆分为独立提交批次。`QueueExecutionPolicy` 检测设备能力并决策启用/回退。跨队列依赖通过 Timeline Semaphore 协调。`RenderGraphRuntimeService` 管理多队列 vkQueueSubmit2 提交。
36. **Pipeline Barrier 自动降级**: 平台无关的 `LogicalBarrier` (AccessKind before→after) 编译后由 `VulkanBarrierPlan::LowerToVulkan` 降级为 VkPipelineStageFlags2 + VkAccessFlags2 + VkImageLayout。`VulkanCommandReadyPlan` 生成可直调 `vkCmdPipelineBarrier2` 的 VkDependencyInfo。支持 Queue Family Ownership Transfer (跨队列资源转移)。
37. **FrameSnapshot 冻结帧数据**: 从 `RenderScenePacket` 提取并冻结 POD 快照，供 RenderGraph 构建使用。64-bit FNV-1a 签名用于跨帧缓存去重。分离帧数据采集 (ECS/Scene 层) 与图构建 (RenderGraph 层)，解除 ECS 遍历顺序与渲染调度的耦合。
38. **Phase12 双轨关闭 — RenderGraph 成为唯一渲染路径**: 彻底移除旧命令式场景目标管线 (`SceneRenderTargetSet`/`SceneBloomPostStack`/`FrameComposerTargets`/`RecordTonemapPass`)。`SceneRecorder2D/3D` 移除 legacy `Record()` 入口，仅保留 `BuildRenderGraph()`→`RecordGraphPass` 图录制路径。`RenderTargetPool` 从一等 Service 降级为内部实现细节 (仅通过 `RenderTargetPoolStats()` 暴露统计)。`RuntimeTickRecorder` concept 扩展为接受任意含 `PrepareFrame()`/`BuildRenderGraph()` 的 Recorder。`color_final_state`/`depth_final_state` 从 `RenderView` 和 `FrameViewSnapshot` 签名中移除 (目标最终状态由 RenderGraph Barrier Plan 推导)。`RenderLoopHost::AcquireFrame()` 移除 `FrameRecorder`/`FrameContextRecorder` 约束。结果：~2272 行旧代码删除，RenderGraph 成为渲染帧内唯一的调度和执行中心。
