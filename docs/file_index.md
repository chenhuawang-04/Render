# VulkanRender_New 代码文件索引

> 统计 (当前分支 `develop`, commit `b0a9a2d`): 约 210 个头文件, 62 个源文件, 33 个着色器 + 5 个 GLSL 头文件, 14 个示例, 73 个测试文件, 22 个基准文件, 16 个文档, 8 个脚本, 4 个工具, 1 个 CMake Presets

---

## 1. 构建配置

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 顶层 CMake 构建脚本。定义项目、外部依赖 (Vulkan/SDL3/FreeType/MemoryCenter)、`vulkan_init` 静态库、`vulkan_platform_sdl` 接口库、所有 Demo 可执行文件、着色器编译管道 (含 SPIR-V 反射和合约检查)、测试/基准子目录。 |
| `CMakePresets.json` | CMake 预设配置。定义跨平台的 configure/build/test presets，支持 VS/Clang/GCC 等生成器，统一开发环境配置。 |
| `.gitignore` | Git 忽略规则：`build/`, CMake 产物, IDE 文件, bench 临时/快照文件, Python `__pycache__/`。 |

---

## 2. include/ — 公开头文件 (`vr/` 命名空间)

### 2.1 核心 Vulkan 上下文

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `include/vr/vulkan_context.hpp` | `VulkanContext`, `VulkanInstanceCreateInfo`, `VulkanDeviceCreateInfo`, `QueueFamilyIndices` | Vulkan 实例/设备/队列族全生命周期管理。支持 Validation Layers、Synchronization2、Dynamic Rendering Local Read、单次命令提交辅助。新增 Multi-Queue (graphics/compute/transfer) 和 CrashTracer 集成。 |

### 2.2 平台层 (`include/vr/platform/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `window_surface.hpp` | `WindowSurface<Sdl3BackendTag>`, `WindowCreateInfo`, `BackendTag`, `BackendKind`, `Sdl3BackendTag`, `ActiveBackendTag` | SDL3 窗口创建、VkSurfaceKHR 管理、事件处理 (close/quit/poll)、窗口属性查询 (framebuffer size)。 |
| `render_host.hpp` | `RenderHost<Sdl3BackendTag>`, `RenderHostCreateInfo` | 组合 `VulkanContext` + `WindowSurface` 的统一初始化/关闭模板。SDL3 特化完整实现。 |

### 2.3 资源层 (`include/vr/resource/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `gpu_memory_host.hpp` | `GpuMemoryHost`, `GpuMemoryHostCreateInfo` | GPU 内存 Buddy Allocator (64MB 块/256 分区)。AllocateAndBindBuffer/Image, Deallocate, Flush/Invalidate/Trim。支持 Dedicated Allocation 查询、Memory Type 查询。 |
| `buffer_host.hpp` | `BufferHost`, `BufferCreateInfo`, `BufferResource` | GPU Buffer 创建/销毁/映射/Flush/Invalidate。全静态方法。BufferResource 为 RAII 包装。 |
| `image_host.hpp` | `ImageHost`, `ImageCreateInfo`, `ImageResource` | GPU Image 创建/销毁/ImageView 创建。支持 mip/array/default view。ImageResource 为 RAII 包装。 |
| `sampler_host.hpp` | `SamplerHost`, `SamplerDesc`, `SamplerId`, `SamplerHostCreateInfo`, `SamplerHostStats` | VkSampler 缓存 (哈希去重)。所有 Vulkan 采样器参数。 |

### 2.4 渲染框架层 (`include/vr/render/`)

#### 2.4.1 帧循环核心

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `swapchain_host.hpp` | `SwapchainHost<WindowSurfaceT>`, `SwapchainCreateInfo`, `SwapchainImage`, `AcquireResult`, `PresentResult` | 交换链生命周期。创建/重建、AcquireNextImage/Present、表面格式/呈现模式选择、延迟销毁、自动 Framebuffer。 |
| `frame_sync_host.hpp` | `FrameSyncHost<framesInFlight>`, `FrameToken`, `FrameBeginResult`, `FrameSlot`, `FrameSubmitWait` | 帧同步。信号量/Fence 管理、Acquire/Submit/Present 流水线。支持 Vulkan 1.3 Synchronization2、额外等待信号量、提交值追踪。 |
| `frame_command_host.hpp` | `FrameCommandHost`, `FrameCommandCreateInfo`, `FrameCommandSlot` | 每帧 CommandPool + CommandBuffer 池。AcquirePrimary/BeginPrimary/EndCommandBuffer，按需增长。 |
| `frame_retire_host.hpp` | `FrameRetireHost`, `FrameRetireStats` | 延迟 GPU 资源回收。ImageView/Framebuffer/Swapchain/CommandPool 延迟销毁。Collect (按提交值) / Flush (强制清除)。 |
| `render_loop_host.hpp` | `RenderLoopHost<WindowSurfaceT, SwapchainT, framesInFlight>`, `RenderLoopCreateInfo`, `FrameRecordContext`, `TickResult`, `TickCode`, `FrameRecorder` concept | 主帧循环。组合 Sync + Swapchain + Command + Retire，Tick() 驱动。支持 `FrameRecorder` 和 `FrameContextRecorder` 两种录制器 concept。Swapchain 变更自动通知。 |
| `swapchain_target_set.hpp` | `SwapchainTargetSet` | 交换链目标集。管理每帧 Swapchain Image → Framebuffer 的映射，与 FrameComposer 和 SceneRenderTargetSet 协调最终输出。 |
| `retire_bus.hpp` | `RetireBus` | 延迟销毁总线。管道化延迟回收事件，支持跨模块发布异步回收操作。 |

#### 2.4.2 渲染资源管理

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `descriptor_host.hpp` | `DescriptorHost`, `DescriptorHostCreateInfo`, `DescriptorSetLayoutDesc`, `DescriptorSetLayoutId`, `DescriptorBufferWrite`, `DescriptorImageWrite`, `DescriptorTexelBufferWrite` | 描述符池管理 + Layout 缓存 (哈希去重)。AllocateSet/UpdateSet，每帧独立池，可选验证。支持 Bindless Table (variable descriptor count + update-after-bind)、BindlessTableId/BindlessSlot 管理。 |
| `pipeline_host.hpp` | `PipelineHost`, `PipelineHostCreateInfo`, `PipelineHostStats`, `ShaderModuleId`, `PipelineLayoutId`, `GraphicsPipelineId`, `ComputePipelineId`, `GraphicsPipelineDesc`, `ComputePipelineDesc` | 管线缓存。ShaderModule/PipelineLayout/GraphicsPipeline/ComputePipeline 注册与获取。延迟编译队列 (Enqueue + ProcessPendingCompiles)。VkPipelineCache 文件持久化。 |
| `upload_host.hpp` | `UploadHost`, `UploadHostCreateInfo`, `UploadAllocation`, `UploadSubmitInfo`, `UploadEndFrameResult`, `UploadFrameStats` | Staging Buffer 上传。Allocate/Write/RecordCopyBuffer/RecordCopyImage + Barrier。支持 Transfer Queue 异步上传、Synchronization2。 |
| `bindless_resource_system.hpp` | `BindlessResourceSystem`, `BindlessResourceSystemCreateInfo`, `BindlessResourceSystemStats` | Bindless 资源系统。统一管理全帧描述符索引纹理/采样器访问：2 个全局 Descriptor Table (Set 0=SampledImage[8192], Set 1=Sampler[256])。ConfigureTextureHost/SurfaceImageHost/GeometryImageHost/ShadowAtlasHost/RenderTargetHost/GlyphUploadHost 为各子系统分配 bindless slot。占位符 Image/Sampler 防止未绑定访问。 |
| `bindless_types.hpp` | `BindlessSlot`, `BindlessTableId`, `BindlessTableDesc`, `BindlessTableStats` | Bindless 基础类型。BindlessSlot (index+generation 双重校验)、BindlessTableDesc (descriptor type/capacity/update_after_bind/variable_count)、BindlessTableStats。 |
| `runtime_prepare_views.hpp` | `RuntimePrepareViews` | 帧准备视图集合。替代旧版 `RuntimePrepareContext`，聚合所有运行时子系统指针并传递给 Recorder 的 PrepareFrame 回调。包含细粒度的 ECS 系统、Host 层、渲染器视图结构体。 |

#### 2.4.3 Render Target / 离屏渲染

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `render_target_types.hpp` | `RenderTargetImage`, `RenderTargetViewDesc`, `RenderTargetPoolSlot` | 渲染目标基础类型。Image 包装、View 描述符、Pool 槽位。 |
| `render_target_desc.hpp` | `RenderTargetDesc` | 渲染目标描述符。格式、分辨率、MSAA、mip、分层渲染等配置。 |
| `render_target_format_utils.hpp` | `RenderTargetFormatUtils` | 格式工具。颜色/深度格式查询、格式兼容性检查。 |
| `render_target_view.hpp` | `RenderTargetView` | 渲染目标视图。封装 VkImageView 和附件引用。 |
| `render_target_host.hpp` | `RenderTargetHost` | 渲染目标管理器。创建/销毁/缓存 RenderTarget、管理附件资源、格式协商。 |
| `render_target_pool.hpp` | `RenderTargetPool` | 渲染目标池。多尺寸预设池、空闲/占用槽位管理、延迟回收。 |
| `render_target_pass.hpp` | `RenderTargetPass` | 渲染通道封装。Load/Store Op、Clear Value、附件配置。 |
| `render_target_composite_renderer.hpp` | `RenderTargetCompositeRenderer` | 合成渲染器。将离屏渲染目标合成至 Swapchain (全屏四边形)。 |
| `render_target_bloom_renderer.hpp` | `RenderTargetBloomRenderer` | Bloom 后处理渲染器。Prefilter → Blur (多级) → Combine 管线。 |
| `scene_render_target_set.hpp` | `SceneRenderTargetSet` | 场景渲染目标集。管理场景所需的颜色/深度/中间目标集合。 |
| `scene_bloom_post_stack.hpp` | `SceneBloomPostStack` | 场景 Bloom 后处理栈。Bloom 参数配置、多级金字塔、最终合成。 |
| `render_pass_preset.hpp` | `RenderPassPreset` | RenderPass 预设。预定义常用渲染通道配置 (颜色清除/加载、深度模板等)。 |
| `color_blend_state.hpp` | `ColorBlendState` | 颜色混合状态封装。Attachment Blend + Logic Op 等 VkPipelineColorBlendState 配置。 |

#### 2.4.4 Appearance GPU 准备与采样表面

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `appearance_gpu_prepare.hpp` | `AppearanceGpuPrepare3D`, `AppearanceSampledSurfacePresenceFlags3D`, `LinkedAppearanceRecord3D` | Appearance GPU 准备器。聚合 Geometry/Surface 的所有 Appearance 记录、解析 `AppearanceSampledSurfaceBinding` 到 bindless slot、生成 GPU-bound material parameter buffer (base_color/metallic/roughness/occlusion/emissive/normal_scale)。Presence Flags 位掩码指示哪些贴图通道已绑定。 |
| `appearance_sampled_surface.hpp` | `AppearanceSampledSurfaceBinding`, `AppearanceSampledSurfaceDescriptor`, `AppearanceSampledSurfaceDomain` | Appearance 采样表面绑定。定义每个 Appearance 关联的 5 个纹理通道 (base_color/normal/metallic-roughness/occlusion/emissive) 及其 bindless slot/index。Domain 枚举区分 geometry_image/surface_image 源。 |

#### 2.4.5 帧协调器 (Frame Coordinators)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `appearance_frame_coordinator.hpp` | `AppearanceFrameCoordinator` | Appearance 帧协调器。管理 Appearance 组件的帧级生命周期：脏记录扫描、链接扫描、与 Geometry/Surface 渲染器协调。支持 2D/3D 双渲染器模式。 |
| `appearance_prepare_stage.hpp` | `AppearancePrepareStage` | Appearance 准备阶段。执行 Appearance 数据的帧级准备操作。 |
| `appearance_prepare_bridge.hpp` | `AppearancePrepareBridge` | Appearance 准备桥接。连接 AppearanceFrameCoordinator 和 RenderRuntime 的帧准备流程。 |
| `light_frame_coordinator.hpp` | `LightFrameCoordinator` | Light 帧协调器。收集活跃光源组件、处理光源 GPU 数据准备、协调光源上传。 |
| `light_prepare_stage.hpp` | `LightPrepareStage` | Light 准备阶段。执行光源数据的帧级准备。 |
| `shadow_frame_coordinator.hpp` | `ShadowFrameCoordinator` | Shadow 帧协调器。管理 Shadow Caster 组件收集、Shadow Map 渲染准备、与光源的关联。 |
| `shadow_prepare_stage.hpp` | `ShadowPrepareStage` | Shadow 准备阶段。执行阴影数据的帧级准备。 |
| `light_shadow_link_coordinator.hpp` | `LightShadowLinkCoordinator` | Light-Shadow 链接协调器。建立光源与阴影投射器之间的关联关系。 |
| `light_shadow_link_stage.hpp` | `LightShadowLinkStage` | Light-Shadow 链接阶段。执行光源-阴影关联的帧级操作。 |
| `shadow_atlas_binding_coordinator.hpp` | `ShadowAtlasBindingCoordinator` | Shadow Atlas 绑定协调器。管理 Shadow Map Atlas 的纹理绑定和描述符更新。 |
| `render_runtime_host.hpp` | `RenderRuntimeHost<BackendTag, framesInFlight>`, `RuntimeModulesCreateInfo`, `RuntimeTickResult`, `RuntimeFramePreparer` concept | 顶层运行时。组合所有子系统，统一 Init/Shutdown 顺序。Tick() 协调 Upload、Pipeline 编译、RenderLoop、prepare 回调。可选启用各模块。 |

#### 2.4.6 场景提交 (Scene Submission)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `render_view.hpp` | `RenderView<Dim>`, `RenderViewKind`, `RenderViewDesc`, `RenderViewSet` | 渲染视图。定义场景绘制的目标/视口/清除策略，支持 active/scene/overlay/reflection/custom 等 view kind。 |
| `scene_submission.hpp` | `RenderScenePacket<Dim>`, `SceneSubmissionPolicy`, `SceneSubmissionRoute` | 场景提交包。将 ECS 场景数据打包为统一的提交结构，通过 policy 路由分发至 Recorder。 |
| `render_view_submission_utils.hpp` | `RenderViewSubmissionUtils` | 渲染视图提交工具。View kind 解析、Packet 类型推导、Layer mask 分流、Multi-view 协调。 |
| `scene_render_stage.hpp` | `SceneRenderStage`, `SceneRenderStagePolicy` | 场景渲染阶段。管理 opaque → transparent 阶段顺序、bloom 编排、后处理策略路由。 |

#### 2.4.7 场景录制器 (Scene Recorders)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `scene_recorder_2d.hpp` | `SceneRecorder2D` | 2D 场景录制器。统一编排 PreScene/Scene/Overlay 三层录制。支持 RenderGraph 集成：BindFramePacket→SetGraphBuildCallback→RecordGraphPass 路径，与 RenderGraphRuntimeService 协作。保留 legacy 命令式录制路径。 |
| `scene_recorder_3d.hpp` | `SceneRecorder3D` | 3D 场景录制器。统一编排 Light/Shadow/Scene/PostProcess 管线。支持 RenderGraph 集成：BindFramePacket→SetGraphBuildCallback→RecordGraphPass 路径，通过 Scene3DDescriptorContract 传递共享描述符。保留 legacy 命令式录制路径。 |

#### 2.4.8 IBL / 环境光照

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `ibl_host.hpp` | `IBLHost`, `IBLHostCreateInfo`, `IBLProbeRoute`, `IBLEnvironmentMap` | IBL 宿主。Irradiance Diffuse IBL、Specular Prefilter + BRDF LUT 管理。支持惰性 IBL 管线：环境注册 → 延迟烘焙 → 自动应用。接收外部 HDR/cubemap 数据或已预卷积数据。 |
| `ibl_bake_host.hpp` | `IBLBakeHost`, `IBLBakeHostCreateInfo`, `IBLBakeConfig` | IBL 烘焙宿主。将 HDR 环境贴图烘焙为 Irradiance Map + Prefiltered Environment Map + BRDF Integration LUT。Compute Shader 驱动。与 `SkyEnvironmentGpuHost` 协调惰性烘焙流程。 |
| `ibl_bake_coordinator.hpp` | `IBLBakeCoordinator` | IBL 烘焙协调器。管理烘焙 Pass 编排、描述符更新、与 RenderTarget 和 Pipeline 的集成。 |

#### 2.4.9 环境与背景渲染 (`include/vr/render/environment/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `sky_environment_gpu_host.hpp` | `SkyEnvironmentGpuHost`, `SkyEnvironmentGpuHostCreateInfo`, `SkyEnvironmentGpuParams`, `SkyEnvironmentBakeDesc`, `SkyEnvironmentBakeResult`, `SkyEnvironmentIblData`, `SkyEnvironmentGpuHostStats` | 天空环境 GPU 宿主。管理天空环境 Register/Update、GPU 参数 (tint/exposure/rotation/IBL intensity/SH9)、IBL 数据缓存、延迟烘焙描述符、与 `IblBakeHost` 的惰性 IBL 管线集成。 |
| `sky_environment_pass.hpp` | `SkyEnvironmentPass`, `SkyEnvironmentPassCreateInfo`, `SkyEnvironmentPassStats` | 天空环境渲染 Pass。支持 6 种模式 (none/solid_color/gradient/cubemap/equirectangular_hdr/procedural_atmosphere)、摄像机对齐全屏四边形、Push Constant (渐变/等距矩形/大气散射参数)。支持 BindFramePacket (ScenePacket→Graph) 和 RecordGraphPass (GraphCommandContext)。 |
| `background_pass_2d.hpp` | `BackgroundPass2D`, `BackgroundPass2DCreateInfo`, `BackgroundPass2DStats` | 2D 背景渲染 Pass。支持 5 种模式 (none/solid_color/gradient/sprite/surface_entity)、Push Constant (颜色/透明度/模式)。支持 BindFramePacket (ScenePacket→Graph) 和 RecordGraphPass (GraphCommandContext)。 |
| `frame_composer_host.hpp` | `FrameComposerHost`, `FrameComposerCreateInfo` | 帧合成宿主。管理最终帧的合成 Pass：场景渲染目标 + 后处理栈 + UI overlay → Swapchain。 |

#### 2.4.10 动画帧协调器

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `animation_frame_coordinator.hpp` | `AnimationFrameCoordinator<Dim>` | 动画帧协调器。统一协调 AnimationClock、Evaluation、Host 的每帧更新，将动画输出注入 SceneRecorder。 |

### 2.5 Scene 场景层 (`include/vr/scene/`)

Scene 层提供统一的场景抽象，将背景/环境管理、Scene Root 组件和提交构建从 ECS 和 Recorder 中分离。

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `scene.hpp` | `Scene<Dim, Background>`, `Scene2D`, `Scene3D` | 场景模板类。聚合 `SceneRoot<Dim>` + `Background<Dim>` + 场景修订号。`Scene2D = Scene<Dim2, SpriteBackground>`, `Scene3D = Scene<Dim3, SkyEnvironment>`。 |
| `scene_traits.hpp` | `SceneTraits<Dim, Background>`, `SceneBackgroundTraits<Dim, Background>` | 场景 trait。编译期类型映射：Dimension→Background→RenderState→View→Packet。`BackgroundTraits` 标记渲染路径 (2D surface path / 3D environment GPU)。 |
| `scene_root_component.hpp` | `SceneRoot<Dim>` | 场景根组件 (POD)。指向 active_camera_entity、background_entity、environment_entity，携带场景标志和修订号。 |
| `scene_prepare.hpp` | `ScenePrepare<Dim, Background>` | 场景准备器。从 `Scene<Dim, Background>` 解析背景/环境渲染状态 (Background2DRenderState / SkyEnvironmentRenderState)。 |
| `scene_submission_builder.hpp` | `SceneSubmissionBuilder<Dim, Background>` | 场景提交构建器。从 Scene 和 RenderView 构建 `RenderScenePacket`，注入背景/环境数据到 packet.extra。支持 BackgroundOverrideMode (inherit/override/disabled)。 |

#### 2.5.1 背景与环境类型 (`include/vr/scene/background/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `background_traits.hpp` | `SceneBackgroundTraits<Dim, Background>` | 背景 trait 特化。2D/SpriteBackground→surface path, 3D/SkyEnvironment→environment GPU path。 |
| `sprite_background.hpp` | `SpriteBackground`, `Background2DMode`, `BackgroundScaleMode`, `Background2DRenderState` | 2D Sprite 背景 (POD)。模式 (none/solid/gradient/sprite/surface_entity)、color0/color1、opacity、layer、parallax 缩放模式。 |
| `sky_environment.hpp` | `SkyEnvironment`, `SkyEnvironmentMode`, `SkyEnvironmentDrawOrder`, `SkyEnvironmentGpuHandle`, `SkyEnvironmentRenderState` | 天空环境 (POD)。6 种模式、天空/辐照度/预过滤/BRDF LUT 纹理引用、天顶/地平线/地面颜色、大气散射参数 (太阳角/密度/Mie/Rayleigh)、IBL 强度、旋转、曝光。 |

#### 2.4.11 粒子渲染框架

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `include/vr/particle/particle_types.hpp` | `Particle2D`, `Particle3D`, `ParticleEmitterDesc`, `ParticleSimulationConfig`, `ParticleRenderBatch` | 粒子公共类型。POD 粒子数据、发射器描述符、2D/3D 模拟配置、渲染批次。 |
| `include/vr/particle/particle_simulation_host.hpp` | `ParticleSimulationHost<Dim>`, `ParticleSimulationCreateInfo` | 粒子模拟宿主。GPU Compute-driven 粒子物理 (速度/重力/阻尼/生命周期)，支持 AABB 裁剪、排序键。 |
| `include/vr/particle/particle_upload_host.hpp` | `ParticleUploadHost<Dim>`, `ParticleUploadCreateInfo` | 粒子上传宿主。粒子顶点/实例数据通过 UploadHost 上传至 GPU Buffer。 |
| `include/vr/particle/particle_renderer_2d.hpp` | `ParticleRenderer2D`, `ParticleRenderer2DCreateInfo`, `ParticleRenderer2DStats` | 2D 粒子渲染器。粒子四边形绘制、混合模式、纹理动画、透明度排序。 |
| `include/vr/particle/particle_renderer_3d.hpp` | `ParticleRenderer3D`, `ParticleRenderer3DCreateInfo`, `ParticleRenderer3DStats` | 3D 粒子渲染器。世界空间粒子、Billboard 摄像机对齐、GPU 实例化绘制、Z 排序。支持 RecordGraphPass (GraphCommandContext) 录制。 |

#### 2.4.12 RenderGraph 渲染图子系统 (`include/vr/render_graph/`)

RenderGraph 是渲染帧内执行的唯一调度中心。所有 Pass 通过声明式 API 描述资源读写/访问语义/队列偏好，由编译器推导资源状态转换、瞬态别名分析和跨队列同步。

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `render_graph_types.hpp` | `ResourceHandle`, `PassHandle`, `ResourceVersionHandle`, `ResourceKind`, `ResourceLifetime`, `AccessKind`, `QueueClass`, `TextureDesc`, `BufferDesc`, `RasterPassDesc`, `PassDescriptorBindingDesc`, `PassShaderContractDesc`, `DescriptorBindingPlan`, `ExternalBufferBindingResolver` | RenderGraph 基础类型。逻辑资源句柄 (Handle+Generation 双重校验)、Pass 句柄、资源版本 (SSA 风格)、19 种访问语义 (color_attachment/depth_stencil/shader_sample/storage/uniform/transfer/present)、3 种队列类、瞬态/持久/导入三种生命周期、纹理/缓冲描述符、Raster Pass 附件描述、Descriptor Binding Plan。所有类型为 standard layout POD。 |
| `render_graph_builder.hpp` | `RenderGraphBuilder` | RenderGraph 构建器。CreateTexture/CreateBuffer (声明式资源创建)、AddPass (声明 Pass)、Read/Write (版本化资源引用，SSA 风格)、SetRasterPassDesc/SetExecuteCallback、AddPassDescriptorBinding/AddBindlessTableBinding/AddExternalBufferBinding、Compile() 编译为 CompiledRenderGraph。 |
| `compiled_render_graph.hpp` | `CompiledRenderGraph`, `CompiledPass`, `CompiledResource`, `CompiledResourceVersionLiveness`, `PassExecutionThunk` | 编译后的渲染图。Pass 执行顺序 (拓扑排序)、资源活跃区间 (Liveness Analysis)、BarrierPlan、TransientAllocationPlan、DescriptorBindingPlan、NativePassPlan。支持 DebugString/DotGraph/Json 序列化。 |
| `render_graph_executor.hpp` | `RenderGraphExecutor`, `RenderGraphRecordStats` | 渲染图执行器。Record() 单队列录制、RecordQueueBatch() 多队列录制。统计 pass_count/rendering_scope_count/barrier_count。 |
| `graph_command_context.hpp` | `GraphCommandContext` | 图命令上下文。持有 VulkanContext + VkCommandBuffer + CompiledRenderGraph + VulkanResourceTable。提供 ResolveTextureTarget/ResolveTextureView/BuildRenderingInfo (逻辑资源→物理目标)、BeginRendering/EndRendering、CurrentPassDescriptorSet 管理。 |
| `frame_graph_build.hpp` | `BuildMinimalFrameGraph<DimT>`, `MinimalFrameGraphBuildResult<DimT>` | 最小帧图构建器。构建 Scene Pass + Overlay Pass + Present Copy/Transition Pass。支持 extensible graph callback (2D/3D custom passes)。 |
| `frame_snapshot.hpp` | `FrameSnapshot<DimT>`, `FrameViewSnapshot<DimT>`, `ResolvedFrameViewSelection`, `MakeFrameSnapshot()`, `MakeFrameViewSnapshot()`, `ComposeFrameSnapshotSignature()` | 帧快照。将从 RenderScenePacket 提取的帧数据冻结为 POD 快照，用于 RenderGraph 构建。包含 View 选择 (active/scene/overlay)、摄像机数据、背景/环境覆盖、64-bit 签名 (用于缓存去重)。 |
| `alias_allocator.hpp` | `TransientAllocationPlan`, `TransientAllocationRecord`, `TransientMemoryPage`, `TransientCompatibilityKey`, `AliasCandidate`, `AliasBarrierDecision`, `TransientMemoryTimeline`, `BuildTransientAllocationPlan()` | 瞬态别名分配器。基于图级 Liveness 分析计算资源活跃区间、兼容性分类、别名配对 (同页复用)。输出物理内存页分配计划 + 别名屏障。 |
| `barrier_plan.hpp` | `BarrierPlan`, `LogicalBarrier`, `QueueDependencyPlan`, `QueueSubmitBatch`, `BuildBarrierPlan()` | 屏障计划。从图 Pass DAG 推导逻辑屏障 (AccessKind before→after)、队列依赖计划 (跨队列同步)、队列提交批次 (queue submit batches)。 |
| `vulkan_barrier_plan.hpp` | `VulkanBarrierPlan`, `LoweredVulkanBarrier`, `VulkanDependencyInfoData`, `VulkanCommandReadyPlan`, `LowerToVulkanBarrierPlan()`, `BuildCommandReadyVulkanBarrierPlan()` | Vulkan 屏障降级。将平台无关的 LogicalBarrier 降级为 Vulkan 特定的 VkPipelineStageFlags2 + VkAccessFlags2 + VkImageLayout。支持 Queue Family Ownership Transfer。 |
| `native_pass_plan.hpp` | `NativePassPlan`, `NativePassGroup`, `NativePassBoundaryDecision`, `NativePassFusionBlockReason`, `NativePassPlanSummary`, `BuildNativePassPlan()` | Native Pass 融合规划。合并连续兼容的 raster pass 为单次 Dynamic Rendering scope。21 种融合阻止原因 (queue mismatch/side_effect/attachment target mismatch 等)。Load/Store Op 推理 (elide transient last use、infer clear from first use)。Dynamic Rendering Local Read 支持。 |
| `queue_execution_policy.hpp` | `QueueExecutionPolicy`, `QueueExecutionCapabilities`, `ResolveQueueExecutionPolicy()`, `GraphRequestsQueue()`, `InspectQueueExecutionCapabilities()` | 队列执行策略。检测设备 Queue 能力 (graphics/compute/transfer)、分析 RenderGraph 的队列需求、生成有效队列族分配。支持 Graphics Fallback。 |
| `vulkan_resource_table.hpp` | `VulkanResourceTable`, `PhysicalTextureRecord`, `PhysicalBufferRecord`, `TransientBufferPageRecord`, `TransientImagePageRecord`, `VulkanLazyMemoryResolution`, `VulkanResourceTableStats` | Vulkan 物理资源表。将逻辑 ResourceHandle 映射到 Vulkan VkImage/VkBuffer/内存。管理 Transient Memory Page 分配/退役、Imported Resource 注册、Lazy Memory 决议。支持跨帧 RetireBus 延迟回收。 |

#### 2.4.13 Scene 3D 描述符合约

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `include/vr/render/scene_3d_descriptor_contract.hpp` | `BuildSharedScene3DBufferLayoutDesc()`, `BuildSharedScene3DShaderContract()` | 3D 场景共享描述符合约。定义 4 个 Descriptor Set 的标准布局: Set 0=SampledImage[3], Set 1=Sampler[1], Set 2=SharedBuffer (light_records/cluster_headers/cluster_indices/shadow_views/lighting_uniform/skeletal_components/skeletal_matrices/geometry_appearance/surface_appearance), Set 3=IBL Params。为所有 3D 渲染器提供统一的 Shader Contract。 |

### 2.6 Runtime 层 (`include/vr/runtime/`)

Runtime 层是围绕类型化服务 (Typed Services) 和执行模型的新一代运行时核心。每个现有子系统 (Host/Renderer) 都被包装为可组合的 Service，通过 `RuntimeKernel` 进行生命周期编排。

#### 2.6.1 Runtime 核心

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `include/vr/runtime/runtime.hpp` | `Runtime<Profile>`, `RuntimeCreateInfo` | 顶层 Runtime 模板。聚合 RuntimeKernel + 所有 Services，Init/Shutdown/Tick 统一编排。基于 Profile 编译期裁剪。 |
| `include/vr/runtime/runtime_kernel.hpp` | `RuntimeKernel`, `RuntimeKernelCreateInfo` | Runtime 内核。管理 Service 注册、依赖解析、启动/停止/帧循环时序。静态依赖图 (DAG)。 |
| `include/vr/runtime/runtime_context.hpp` | `RuntimeContext` | Runtime 全局上下文。持有 VulkanContext、内存分配器、帧状态等全局资源。 |
| `include/vr/runtime/runtime_create_info.hpp` | `RuntimeCreateInfo` | Runtime 创建信息。聚合所有 Service 的 CreateInfo 和可选模块开关。 |
| `include/vr/runtime/runtime_diagnostics.hpp` | `RuntimeFrameDiagnosticsV2`, `RenderGraphRuntimeDiagnostics`, `RenderGraphQueueTimeline`, `RenderGraphQueueTimelineMode`, `DiagnosticsLevel` | 运行时诊断 V2。RenderGraph 编译/执行/瞬态内存/队列时间线诊断、Swapchain 诊断、每子系统计数器 (Upload/Pipeline/Bindless/IBL/Particle/RenderTarget/Descriptor/Allocation)、5 级诊断深度 (Off/CountersOnly/Detailed/GpuTiming/Capture)。 |
| `include/vr/runtime/runtime_execution.hpp` | `RuntimeExecution`, `RuntimeExecutionTrace`, `ExecutionPhaseDriver` | 运行时执行追踪。定义服务调用的帧内阶段 (ServiceBeginFrame→Prepare→FlushUploads→PreRecord→Record→Submit→PostRecord→Present→EndFrame→Retire)，支持多队列提交协调和诊断追踪。 |
| `include/vr/runtime/runtime_profile.hpp` | `RuntimeProfile` concept, `MinimalProfile`, `Runtime2DProfile`, `Runtime3DProfile` | 运行时 Profile。编译期定义哪些 Service 被包含和激活，通过模板参数裁剪二进制。 |
| `include/vr/runtime/runtime_service.hpp` | `RuntimeService<S>`, `ServiceTag`, `ServiceDependency` | 类型化服务包装器。将任意 Host/Renderer 包装为统一 Service 接口，声明依赖关系和生命周期。 |
| `include/vr/runtime/runtime_services.hpp` | `RuntimeServices` | 服务集合模板。通过变参模板聚合所有 Service，提供编译期依赖验证和统一访问接口。 |
| `include/vr/runtime/runtime_status.hpp` | `RuntimeStatus`, `ServiceStatus`, `ServiceHealth` | 运行时状态监控。Service 健康度、帧率/帧时间、资源使用指标。 |
| `include/vr/runtime/runtime_views.hpp` | `RuntimeViews` | 运行时视图集合。编译期生成每帧可访问的 Service 引用集合，零开销访问。 |
| `include/vr/runtime/service_dependency.hpp` | `ServiceDependencyGraph`, `ServiceDependencyEdge` | 服务依赖图。编译期 DAG 构建、依赖排序、循环依赖检测。 |
| `include/vr/runtime/frame_scheduler.hpp` | `FrameScheduler`, `ScheduledTask`, `TaskPriority` | 帧调度器。帧内任务排序、屏障插入、Queue 提交批处理。 |
| `include/vr/runtime/command_service.hpp` | `CommandService` | 命令服务。管理 Vulkan CommandBuffer 的帧级生命周期 (池分配、录制、提交)。 |
| `include/vr/runtime/frame_retire_service.hpp` | `FrameRetireService` | 帧退役服务。延迟 GPU 资源回收的 Service 化接口。 |
| `include/vr/runtime/crash_tracer_support.hpp` | `CrashTracerInstallOptions`, `InstallProcessCrashTracer` | CrashTracer 运行时集成。安装进程级崩溃处理器 (SEH 捕获)、自定义崩溃报告前缀、应用名标记。替代旧 `bench_crash_tracer` 的 bench-only 设计。 |
| `include/vr/runtime/queue_timeline.hpp` | `QueueTimeline`, `QueueSubmitPoint` | 队列时间线。跨队列提交点追踪、信号量/栅栏协调、同步保证。 |

#### 2.6.2 Runtime 子服务 (`include/vr/runtime/services/`)

每项服务将现有 Host/Renderer 包装为统一的 `RuntimeService` 接口，声明依赖和生命周期回调。

| 文件 | 包装对象 | 说明 |
|------|---------|------|
| `bound_host_service.hpp` | 泛型 Host 绑定 | 泛型 Host 服务绑定器。将任意 Host 类型包装为 Service 接口，提供 Init/Shutdown/Tick 回调。 |
| `command_service.hpp` | CommandService | CommandBuffer 池化与录制管理服务。 |
| `descriptor_service.hpp` | DescriptorHost | 描述符池与 Layout 管理服务。 |
| `frame_composer_service.hpp` | FrameComposerHost | 帧合成服务。场景 + 后处理 + UI → Swapchain。 |
| `freetype_service.hpp` | FreeTypeHost | FreeType 字体引擎服务。 |
| `glyph_atlas_service.hpp` | GlyphAtlasHost | 字形图集服务。页面分配与字形打包。 |
| `glyph_upload_service.hpp` | GlyphUploadHost | 字形上传服务。脏页→GPU 传输。 |
| `gpu_memory_service.hpp` | GpuMemoryHost | GPU 内存分配服务。Buddy Allocator。 |
| `ibl_bake_service.hpp` | IBLBakeHost | IBL 烘焙服务。环境图→Irradiance/Specular/BRDF LUT。 |
| `ibl_service.hpp` | IBLHost | IBL 环境光照服务。Irradiance/BRDF LUT 管理。 |
| `particle_render_service.hpp` | ParticleRenderer<Dim> | 粒子渲染服务。2D/3D 粒子绘制。 |
| `particle_simulation_service.hpp` | ParticleSimulationHost<Dim> | 粒子模拟服务。GPU Compute 物理。 |
| `particle_upload_service.hpp` | ParticleUploadHost<Dim> | 粒子上传服务。粒子数据→GPU Buffer。 |
| `pipeline_service.hpp` | PipelineHost | 图形/计算管线缓存服务。 |
| `render_graph_runtime_service.hpp` | RenderGraph | RenderGraph 运行时服务。管理帧图构建/编译/执行全管线：FrameSnapshot→BuildMinimalFrameGraph→Compile→BarrierLower→VulkanResourceResolve→QueueExecutionPolicy→Record/RecordMultiQueue→SubmitMultiQueue。约 1695 行，支持 2D/3D/Direct 三种 Graph Build 路径、Multi-Queue (graphics/compute/transfer) 提交、Lazy Memory、外部队列同步。 |
| `render_target_pool_service.hpp` | RenderTargetPool | 渲染目标池服务。 |
| `render_target_service.hpp` | RenderTargetHost | 渲染目标管理服务。 |
| `sampler_service.hpp` | SamplerHost | 采样器缓存服务。VkSampler 哈希去重。 |
| `sky_environment_service.hpp` | SkyEnvironmentGpuHost + SkyEnvironmentPass | 天空环境服务。天空环境渲染与 GPU 参数管理的 Service 化包装。 |
| `texture_service.hpp` | TextureHost | 资产纹理服务。外部像素数据→GPU Image。 |
| `upload_service.hpp` | UploadHost | Staging Buffer 上传服务。 |

### 2.7 ECS 概念层 (`include/vr/ecs/concept/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `dimension.hpp` | `Dim2`, `Dim3`, `SceneDimension`, `DimensionTag` concept | 维度标签类型。`Dim2` = 2D, `Dim3` = 3D。`DimensionTag` concept 约束模板参数。 |

### 2.8 ECS 组件层 (`include/vr/ecs/component/`)

| 文件 | 主要结构/类 | 说明 |
|------|------------|------|
| `spatial_types.hpp` | `Float2`, `Float3`, `Float4`, `Quaternion`, `Affine2x3`, `Matrix4x4`, `CoreMatrix4x4` | 数学类型别名。`MMath::Vec2/3/4`, `MMath::Quat`, `MMath::Mat3`, `MMath::D3D::Mat4` (列主序 D3D 风格)。 |
| `transform_component.hpp` | `Transform<Dim2/3>`, `TransformStyle2D/3D`, `TransformRuntime2D/3D`, `TransformHierarchyLink`, `TransformDirtyFlags` | 变换组件 (POD)。位置/旋转/缩放 + 4x4 矩阵 + 层级链接 + 修订追踪 + 脏标记。 |
| `camera_component.hpp` | `Camera<Dim2/3>`, `CameraStyle2D/3D`, `CameraRuntimeData`, `CameraViewport`, `CameraProjectionMode` | 相机组件 (POD)。投影模式/参数 + View/Proj/VP 矩阵 + 裁剪掩码 + 视口。 |
| `bounds_component.hpp` | `Bounds<Dim2/3>`, `BoundsStyle2D/3D`, `BoundsRuntime2D/3D`, `BoundsDirtyFlags` | 包围盒组件 (POD)。局部/世界 AABB + 中心/范围 + 半径 + 可见性掩码 + 修订追踪。 |
| `geometry_component.hpp` | `Geometry<Dim2/3>`, `GeometryStyle2D/3D`, `GeometryRuntime2D/3D`, `GeometryPathInlineData`, `GeometryMeshRoute`, `GeometryRuntimeRoute`, 各枚举 | 几何组件 (POD)。2D: 路径数据 (inline 1024B) + 填充/描边样式 + 排序键。3D: 子网格路由 + PBR 材质 + 包围盒。 |
| `surface_component.hpp` | `Surface<Dim2/3>`, `SurfaceStyle2D/3D`, `SurfaceRuntime2D/3D`, `SurfaceRuntimeRoute`, `Surface2DSourceRoute`, `Surface3DTextureRoute` | Surface 组件 (POD)。图像/精灵源 + 混合模式/纹理过滤 + 排序键 + 尺寸/轴心。 |
| `text_component.hpp` | `Text<Dim2/3>`, `TextStyle2D/3D`, `TextBufferInlineUtf8`, `TextRuntimeBatchData`, `Rgba8`, `TextHorizontalAlign`, `TextVerticalAlign` | 文本组件 (POD)。UTF8 内联缓冲 (240B) + SDF/Outline 参数 + 字形范围 + 排序键。 |
| `light_component.hpp` | `Light<Dim2/3>`, `LightStyle2D/3D`, `LightRuntimeData` | 光源组件 (POD)。2D: 点光源 (position+radius+color)。3D: 点/聚光/方向光。强度、颜色、范围、阴影标志、裁剪掩码。 |
| `shadow_component.hpp` | `Shadow<Dim2/3>`, `ShadowStyle2D/3D`, `ShadowRuntimeData` | 阴影组件 (POD)。2D: 遮挡器形状。3D: 网格路由。Shadow Map 槽、偏移、过滤模式、深度格式。 |
| `appearance_component.hpp` | `Appearance<Dim2/3>`, `AppearanceStyle`, `AppearanceRuntimeRecord`, `AppearanceSampledSurfaceBinding`, `AppearanceGpuRecord`, `AppearanceHandle`, `AppearancePipelineBucket` | Appearance 组件 (POD)。统一的视觉语义中心，桥接 ECS 组件到渲染器。可见性记录、link 组、脏追踪、PBR 材质参数 (base_color/metallic/roughness/normal_scale/occlusion/emissive)、采样表面绑定 (base_color/normal/metallic-roughness/occlusion/emissive)、Pipeline Bucket 路由。 |
| `animation_component.hpp` | `Animation<Dim2/3>`, `AnimationStyle`, `AnimationRuntime`, `AnimationTrack`, `AnimationPropertyValue`, `AnimationClockState`, `AnimationKind` (Property/Material/Path/Skeletal/Morph/VertexDeform/FrameSequence) | 动画组件 (POD)。动画时钟状态、轨道路由、Property 轨道 (position/rotation/scale/color/float)、Material 轨道、Skeletal 轨道 (骨骼引用 + Joint Weights)、Morph 轨道 (变形目标引用 + 权重)、VertexDeform 轨道、FrameSequence 轨道。支持 Clip 引用 + 播放回调。Phase 1 完整实现。 |
| `particle_component.hpp` | `Particle<Dim2/3>`, `ParticleStyle2D/3D`, `ParticleRuntime2D/3D`, `ParticleEmissionState` | 粒子组件 (POD)。粒子生命周期 (age/lifetime)、位置/速度/加速度、颜色/大小/旋转、纹理索引、排序键。GPU Compute 驱动更新。 |
| `particle_emitter_component.hpp` | `ParticleEmitter<Dim2/3>`, `EmitterShape`, `EmitterBurst`, `EmitterRate` | 粒子发射器组件 (POD)。发射形状 (Point/Circle/Sphere/Cone/Box)、发射速率/爆发、初始速度/颜色/大小范围、生命周期分布。 |

### 2.9 ECS 系统层 (`include/vr/ecs/system/`)

#### 2.9.1 核心空间系统

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `spatial_math.hpp` | (free functions) | 数学工具函数：单位矩阵、TRS 组成/分解、矩阵乘法、求逆 (仿射)、四元数工具、正交/透视投影构建 (D3D 右手系)。 |
| `transform_system.hpp` | `TransformSystem<Dim>`, `TransformHierarchyScratch<Dim>` | 变换层级系统。父子链接 (AttachChild/DetachFromParent)、局部/世界矩阵重建、脏标记传播、层级遍历 (防循环)、平坦层级快速路径。 |
| `camera_system.hpp` | `CameraSystem<Dim>` | 相机系统。投影重建 (正交/透视/Reverse-Z)、View 矩阵从 Transform 逆、ViewProjection 合并。支持 Aligned 批量更新。 |
| `bounds_system.hpp` | `BoundsSystem<Dim>` | 包围盒系统。世界空间 AABB 变换 (中心+范围法)、多种 UpdateAligned 变体、点包含/相交测试。修订追踪与脏标记传播、Transform 分离快速路径。 |
| `culling_system.hpp` | `CullingSystem<Dim>`, `CullingScratch<Dim>`, `CullingBuildOptions`, `CullingBuildStats`, `PreparedCamera`, `FrustumPlanes` | 裁剪系统。视锥体平面提取 (从 VP 矩阵)、球体拒绝 + AABB 细化、可见性掩码过滤、候选列表接口、可见集签名哈希、VisibilityStamp epoch 机制。模板展开的零分支扫描。 |

#### 2.9.2 几何系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `geometry_system.hpp` | `GeometrySystem<Dim>` | 几何基础系统。64 位排序键构建/解析、渲染路由设置 (geometry_id/material_id/batch_tag)、可见性筛选。 |
| `geometry_batch_system.hpp` | `GeometryBatchSystem<Dim>`, `GeometryBatchItem`, `GeometryBatchScratch<Dim>`, `GeometryBatchBuildStats` | 几何批次系统。收集可见几何组件、按排序键排序、去重。支持全量扫描和候选列表扫描。 |
| `geometry_mesh_system.hpp` | `GeometryMeshSystem` | 3D 网格路由操作 (submesh_index, lod, flags)。 |
| `geometry_path_system.hpp` | `GeometryPathSystem`, `GeometryPathCommandView`, `GeometryPathMoveToCommand`, `GeometryPathLineToCommand`, `GeometryPathQuadToCommand`, `GeometryPathCubicToCommand`, `GeometryPathCloseCommand`, `GeometryPathCommandType` | 2D 路径命令系统。内联路径数据解析、命令迭代 (ForEachCommandRaw)、路径数据哈希。 |
| `geometry_runtime_system.hpp` | `GeometryRuntimeSystem<Dim2/3>`, `Geometry2DPathPrimitive`, `Geometry3DGpuInstance`, `Geometry2DDrawBatch`, `Geometry3DDrawBatch`, 各种 RuntimeBuildConfig/BuildStats/Cache/Scratch | 几何运行时系统。2D: 路径→线段图元细分 (Quad/Cubic) + 样式打包 + 批次合并。3D: 组件→GPU 实例 (世界矩阵+PBR+包围盒) + 批次合并 + 增量变换更新。支持运行时缓存 (签名/epoch 追踪)。 |

#### 2.9.3 Surface 系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `surface_system.hpp` | `SurfaceSystem<Dim>` | Surface 基础系统。排序键布局 [pass:2][material:16][surface:16][minor:16][batch:14]。路由设置、可见性。 |
| `surface_batch_system.hpp` | `SurfaceBatchSystem<Dim>`, `SurfaceBatchItem`, `SurfaceBatchScratch<Dim>`, `SurfaceBatchBuildStats` | Surface 批次系统。收集与排序可见 Surface 组件。 |
| `surface_runtime_system.hpp` | `SurfaceRuntimeSystem<Dim2/3>`, `Surface2DGpuInstance`, `Surface3DGpuInstance`, `Surface2DDrawBatch`, `Surface3DDrawBatch` | Surface 运行时系统。GPU 实例生成 + 批次合并 + 缓存。 |
| `surface_upload_plan_system.hpp` | `SurfaceUploadPlanSystem`, `SurfaceUploadPatchRange`, `SurfaceUploadPlanStats`, `SurfaceUploadPlanBuildOptions`, `SurfaceUploadPlanScratch` | Surface 上传计划系统。识别需要上传的脏组件、页面调度。 |

#### 2.9.4 文本系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `text_system.hpp` | `TextSystem<Dim>` | 文本基础系统。排序键布局 [pass:2][material:16][font:12][atlas:10][minor:16][batch:8]。字体/材质路由、UTF8 修订追踪、内联字符串操作。 |
| `text_batch_system.hpp` | `TextBatchSystem<Dim>`, `TextBatchItem`, `TextBatchScratch<Dim>`, `TextBatchBuildStats` | 文本批次系统。收集可见文本组件、排序。 |
| `text_runtime_system.hpp` | `TextRuntimeSystem<Dim2/3>`, `TextGlyphQuad` | 文本运行时系统。字形四边形生成 (x0/y0/x1/y1 + uv 坐标)、字形缓存、批次合并。 |
| `text_render_3d_system.hpp` | `TextRender3DSystem`, `Text3DGpuInstance`, `Text3DDrawBatch`, `Text3DFrameData`, `TextRender3DScratch` | 3D 文本特殊系统。3D 文本 GPU 实例 (rect、UV、world position、billboard 参数、颜色、大小限制)。 |

#### 2.9.5 光照系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `light_system.hpp` | `LightSystem<Dim>` | 光照基础系统。光源排序键、阴影关联、路由。 |
| `light_culling_system.hpp` | `LightCullingSystem` | 光源裁剪系统。光源视锥体可见性判断、Tile-based 裁剪预备。 |
| `light_runtime_system.hpp` | `LightRuntimeSystem<Dim>` | 光照运行时系统。GPU 光源数据 (UBO/SSBO) 生成、光照参数打包。 |
| `light_gpu_layout.hpp` | `LightGpuLayout` | 光源 GPU Layout 定义。UBO/SSBO 内存布局结构。 |

#### 2.9.6 阴影系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `shadow_system.hpp` | `ShadowSystem<Dim>` | 阴影基础系统。Shadow Caster 管理、Shadow Map 槽分配。 |
| `shadow_caster_system.hpp` | `ShadowCasterSystem<Dim>` | 阴影投射器收集系统。可见投射器筛选、去重。 |
| `shadow_runtime_system.hpp` | `ShadowRuntimeSystem<Dim>` | 阴影运行时系统。Shadow Map 渲染调度、深度变换矩阵生成。 |
| `shadow_gpu_layout.hpp` | `ShadowGpuLayout` | 阴影 GPU Layout 定义。Shadow Map 相关 GPU 内存布局。 |

#### 2.9.7 透明度与混合系统

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `transparency_render_policy.hpp` | `TransparencyRenderPolicy` | 透明度渲染策略。统一管理跨渲染器 (Text/Geometry/Surface) 的透明排序、混合模式路由、Blend State 策略。结合 Appearance 材质路由实现分层透明渲染。 |

#### 2.9.8 Appearance 系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `appearance_system.hpp` | `AppearanceSystem<Dim>` | Appearance 基础系统。可见性记录、脏标记、link 句柄。PBR 材质查询 (base_color/metallic/roughness/normal_scale/occlusion/emissive/unlit)、SampledSurface 绑定。 |
| `appearance_link_system.hpp` | `AppearanceLinkSystem` | Appearance 链接系统。链接 Geometry/Surface 到 Appearance 记录、跨渲染器协调。支持增量 Link/Delink、batch 刷新。 |
| `appearance_runtime_system.hpp` | `AppearanceRuntimeSystem<Dim>` | Appearance 运行时系统。运行时 appearance 数据生成、`AppearanceGpuRecord` 缓存、与渲染器批量对接。`AppearanceSampledSurfaceBinding3D` 对外提供统一纹理绑定接口。 |
| `visual_runtime_route_common.hpp` | Visual Runtime Route 公共宏 | 共享宏 `VR_ECS_VISUAL_RUNTIME_ROUTE_SORT_KEY_FIELD()` 和 `VR_ECS_VISUAL_RUNTIME_ROUTE_TRAILING_FIELDS()` 用于所有视觉渲染路径 (Geometry/Surface/Particle) 的统一 runtime route 字段定义。`HasAppearanceHandleChanged()` / `ClearAppearanceRuntimeRoute()` 跨子系统共通操作。 |

#### 2.9.9 动画系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `animation_clock_system.hpp` | `AnimationClockSystem<Dim>` | 动画时钟系统。管理动画时间推进 (delta/time scale)、播放状态 (Play/Pause/Stop)、循环/单次/乒乓模式、时间范围钳制。 |
| `animation_evaluation_context.hpp` | `AnimationEvaluationContext<Dim>` | 动画求值上下文。聚合动画求值所需的所有子系统和资源句柄。 |
| `animation_curve_system.hpp` | `AnimationCurveSystem<Dim>` | 动画曲线系统。关键帧曲线采样 (Linear/Step/CubicSpline)、时间→值映射、曲线缓存。 |
| `animation_property_track_system.hpp` | `AnimationPropertyTrackSystem<Dim>` | Property 轨道系统。采样 Property Track (position/rotation/scale/color/float)、输出属性值到组件。 |
| `animation_property_evaluation_system.hpp` | `AnimationPropertyEvaluationSystem<Dim>` | Property 求值系统。将 Property Track 输出写入 Transform 和 Appearance 组件。 |
| `animation_visual_track_system.hpp` | `AnimationVisualTrackSystem` | Visual 轨道系统 (原 Material Track 重命名)。采样 Visual Track (base_color/metallic/roughness/emissive 等外观参数动画)。 |
| `animation_visual_evaluation_system.hpp` | `AnimationVisualEvaluationSystem` | Visual 求值系统 (原 Material Evaluation 重命名)。将 Visual Track 输出写入 Appearance 组件。 |
| `animation_camera_track_system.hpp` | `AnimationCameraTrackSystem<Dim>` | Camera 轨道系统。采样 Camera Track (fov/near/far/position/look-at 动画)。 |
| `animation_camera_evaluation_system.hpp` | `AnimationCameraEvaluationSystem<Dim>` | Camera 求值系统。将 Camera Track 输出写入 Camera 组件。 |
| `animation_path_motion_system.hpp` | `AnimationPathMotionSystem<Dim>` | 路径运动系统。Path 曲线 (Position/Rotation/Scale)×时间、路径约束 (Follow/LookAt)、路径采样与插值。 |
| `animation_path_evaluation_system.hpp` | `AnimationPathEvaluationSystem<Dim>` | 路径求值系统。将 Path Motion 输出写入 Transform 组件。 |
| `animation_skeletal_evaluation_system.hpp` | `AnimationSkeletalEvaluationSystem<Dim>` | 骨骼求值系统。骨骼轨道采样、Joint Palette 更新、Root Motion 提取。 |
| `animation_morph_evaluation_system.hpp` | `AnimationMorphEvaluationSystem<Dim>` | 变形求值系统。Morph Target 权重采样、变形缓冲区构建、与 GPU Skinning 协调。 |
| `animation_vertex_deform_evaluation_system.hpp` | `AnimationVertexDeformEvaluationSystem<Dim>` | 顶点变形求值系统。Vertex Deform 轨道采样、顶点负载输出、与 Geometry Runtime 协调。 |
| `animation_frame_sequence_evaluation_system.hpp` | `AnimationFrameSequenceEvaluationSystem<Dim>` | 帧序列求值系统。Frame Sequence 轨道采样、A/B 帧混合权重、子网格帧索引路由。 |
| `animation_resource_track_system.hpp` | `AnimationResourceTrackSystem` | 资源轨道系统。管理动画资源轨道的生命周期和路由。 |

#### 2.9.10 粒子系统组

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `particle_system.hpp` | `ParticleSystem<Dim>` | 粒子基础系统。64 位排序键 (pass/material/depth/batch)、生命周期管理、可见性路由。 |
| `particle_runtime_system.hpp` | `ParticleRuntimeSystem<Dim2/3>`, `Particle2DGpuInstance`, `Particle3DGpuInstance`, `Particle2DDrawBatch`, `Particle3DDrawBatch` | 粒子运行时系统。GPU 实例生成 (位置/颜色/大小/旋转/纹理索引)、批次合并、粒子排序 (按深度或 layer)。 |
| `particle_emitter_system.hpp` | `ParticleEmitterSystem<Dim>` | 粒子发射器系统。发射形状采样、爆发/持续发射、初始属性随机化、发射计数管理。 |
| `transparency_render_policy.hpp` | `TransparencyRenderPolicy` | 透明度渲染策略。(已移至 2.8.7) 统一管理跨渲染器的透明排序与混合模式路由。 |

### 2.10 文本渲染 (`include/vr/text/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `text_types.hpp` | `GlyphRectU16`, `GlyphUvRect`, `GlyphAtlasRegion`, `GlyphMetrics26_6`, `PodTextType`, `k_invalid_glyph_page_index` | 文本渲染公共类型定义。字形像素矩形 (U16)、UV 矩形 (float)、图集页面指针、字形度量。 |
| `freetype_host.hpp` | `FreeTypeHost`, `FreeTypeHostCreateInfo` | FreeType 库封装。字体 face 加载 (内存/文件)、字形度量查询、字形位图/SDF 渲染。 |
| `glyph_atlas_host.hpp` | `GlyphAtlasHost`, `GlyphAtlasCreateInfo` | 字形图集。动态页面分配、字形打包 (bin packing)、脏页追踪、双层缓存。 |
| `glyph_upload_host.hpp` | `GlyphUploadHost`, `GlyphUploadHostCreateInfo` | 字形上传。将 Atlas 脏页通过 UploadHost 传输至 GPU Image。 |
| `text_renderer_2d.hpp` | `TextRenderer2D` | 2D 文本渲染器。图集纹理绑定、字形顶点/索引缓冲生成、Push Constant (颜色/SDF/Outline 参数)。支持 RecordGraphPass (GraphCommandContext) + ImportedTextGlyphDescriptorSet。 |
| `text_renderer_3d.hpp` | `TextRenderer3D` | 3D 文本渲染器。世界空间文本、Billboard 矩阵、深度测试参数。支持 RecordGraphPass (GraphCommandContext) + ImportedTextGlyphDescriptorSet。 |
| `text_runtime_contract.hpp` | `TextRuntimeContract`, `TextContractValidation`, `TextUploadContract` | 文本运行时契约。定义 Text 系统与 Renderer 之间的资源契约、上传策略、字形页面调度协议、诊断统计。 |

### 2.11 几何渲染 (`include/vr/geometry/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `geometry_types.hpp` | `GeometryMeshVertex`, `GeometrySubmeshRange`, `GeometryMeshUploadInfo`, `GeometryUploadRange` | 几何渲染公共类型定义。3D 网格顶点格式 (position/normal/uv)、子网格索引范围。 |
| `geometry_resource_host.hpp` | `GeometryResourceHost` | 几何资源注册表。Mesh/Image ID 分配与管理。 |
| `geometry_image_host.hpp` | `GeometryImageHost` | 几何图像管理。纹理 Image/View/Sampler 绑定，Bindless slot 注册。 |
| `geometry_upload_host.hpp` | `GeometryUploadHost` | 几何数据上传。顶点/索引缓冲上传，路径数据上传。 |
| `geometry_appearance_host.hpp` | `GeometryAppearanceHost`, `GeometryAppearanceDesc` | 几何 Appearance 宿主 (替代旧 `GeometryMaterialHost`)。管理每个 Appearance 的 SampledSurface 绑定 (base_color/normal/metallic-roughness/occlusion/emissive)、UV 变换参数、Alpha Test 标志。 |
| `geometry_appearance_resolver.hpp` | `GeometryAppearanceResolvedState` | Geometry Appearance 解析器。从 Appearance 组件和 GeometryAppearanceHost 解析最终 PBR 渲染参数 (base_color/metallic/roughness/normal_scale/occlusion/unlit)。 |
| `geometry_tangent_space.hpp` | `GeometryTangentBuildResult`, `GeometryTangentBuildStatus` | 几何切线空间构建。从 position/normal/uv 生成 tangent/bitangent，支持 fallback basis、退化 UV 检测。PBR 法线贴图必需的预备阶段。 |
| `geometry_renderer_2d.hpp` | `GeometryRenderer2D`, `GeometryRenderer2DCreateInfo`, `GeometryRenderer2DStats` | 2D 几何渲染器。路径线段→顶点缓冲、描边/填充管线绑定、抗锯齿、Push Constant。集成 Appearance 链接和运行时支持。支持 RecordGraphPass (GraphCommandContext) + PrepareGraphAppearanceData。 |
| `geometry_renderer_3d.hpp` | `GeometryRenderer3D`, `GeometryRenderer3DCreateInfo`, `GeometryRenderer3DStats` | 3D 几何渲染器。GPU 实例化绘制 (instanced draw)、PBR 材质绑定 (bindless + Appearance SampledSurface)、管线状态。集成 Appearance 和 Shadow 支持。支持 RecordGraphPass (GraphCommandContext) + PrepareGraphAppearanceData。 |
| `geometry_skeletal_palette_builder.hpp` | `GeometrySkeletalPaletteBuilder`, `SkeletalPaletteUpload` | GPU 骨骼调色板构建器。从 AnimationSkeletalHost 消费 Joint Palette，构建用于 GPU Skinning 的骨骼矩阵 UBO/SSBO。 |

### 2.12 动画子系统 (`include/vr/animation/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `animation_clip_host.hpp` | `AnimationClipHost<Dim>`, `AnimationClipCreateInfo`, `AnimationClipRoute` | 动画片段管理器。Clip 生命周期 (Create/Destroy)、采样/求值、轨道收集 (Property/Material/Path/Skeletal/Morph/VertexDeform)。支持循环/单次/乒乓播放模式。 |
| `animation_skeletal_host.hpp` | `AnimationSkeletalHost<Dim>`, `SkeletalJointPalette`, `SkeletalJointIndex` | 骨骼动画宿主。关节层级 (Joint Hierarchy)、关节调色板 (Joint Palette)、GPU Skinning 矩阵上传、Root Motion 提取。 |
| `animation_morph_host.hpp` | `AnimationMorphHost<Dim>`, `MorphTargetBuffer`, `MorphTargetWeight` | 变形目标 (Morph Target) 宿主。变形目标权重管理、GPU 缓冲区上传、Blend Shape 支持。 |
| `animation_path_host.hpp` | `AnimationPathHost<Dim>`, `PathMotionCurve`, `PathMotionSample` | 路径动画宿主。路径曲线 (Position/Rotation/Scale)、路径采样、路径运动合成。 |
| `animation_vertex_deform_host.hpp` | `AnimationVertexDeformHost<Dim>`, `VertexDeformBuffer`, `VertexDeformPayload` | 顶点变形宿主。GPU 顶点变形缓冲区、变形负载上传、与 Geometry 渲染器集成。 |
| `animation_frame_sequence_host.hpp` | `AnimationFrameSequenceHost<Dim>`, `FrameSequenceState`, `FrameSequenceBlend` | 帧序列动画宿主。子网格帧序列播放、A/B 帧混合、帧索引路由。 |

### 2.13 资产层 (`include/vr/asset/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `texture_host.hpp` | `TextureHost`, `TextureHostCreateInfo`, `TextureResource`, `TextureUploadDesc` | 资产纹理宿主。接收外部已解码像素数据、创建/接管 GPU Image、Mipmap 生成、格式协商。不包含 PNG/KTX2 解码逻辑，仅消费已准备数据。 |

### 2.14 Surface 渲染 (`include/vr/surface/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `surface_types.hpp` | `SurfaceUploadPatch`, `SurfaceUploadRange` | Surface 渲染公共类型定义。上传补丁追踪、缓冲区范围描述。 |
| `surface_image_host.hpp` | `SurfaceImageHost` | Surface 图像管理。精灵/图集 Image/View 管理、采样器绑定。 |
| `surface_upload_host.hpp` | `SurfaceUploadHost` | Surface 数据上传。图像数据通过 UploadHost 上传至 GPU。 |
| `surface_renderer_2d.hpp` | `SurfaceRenderer2D`, `SurfaceRenderer2DCreateInfo`, `SurfaceRenderer2DStats` | 2D Surface 渲染器。精灵绘制、混合模式 (alpha/additive/multiply/screen)、UV 变换、tint color。集成 Appearance 和运行时上传支持。支持 RecordGraphPass (GraphCommandContext)。 |
| `surface_renderer_3d.hpp` | `SurfaceRenderer3D`, `SurfaceRenderer3DCreateInfo`, `SurfaceRenderer3DStats` | 3D Surface 渲染器。世界空间贴图、纹理过滤 (linear/nearest/anisotropic)、寻址模式、双面渲染。支持 RecordGraphPass (GraphCommandContext)。 |

### 2.15 Light/Shadow 渲染 (`include/vr/light/`, `include/vr/shadow/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `include/vr/light/light_shadow_upload_host.hpp` | `LightShadowUploadHost`, `LightShadowUploadHostCreateInfo` | Light-Shadow 数据上传。光源数据 (UBO)、阴影矩阵、Atlas 引用的 GPU 上传管理。 |
| `include/vr/shadow/shadow_atlas_host.hpp` | `ShadowAtlasHost`, `ShadowAtlasHostCreateInfo` | Shadow Map Atlas 管理。Atlas 页面分配、阴影贴图打包、脏页追踪。 |
| `include/vr/shadow/shadow_renderer_2d.hpp` | `ShadowRenderer2D`, `ShadowRenderer2DCreateInfo` | 2D 阴影渲染器。2D 遮挡物深度图渲染、光源关联。 |
| `include/vr/shadow/shadow_renderer_3d.hpp` | `ShadowRenderer3D`, `ShadowRenderer3DCreateInfo` | 3D 阴影渲染器。3D Shadow Map 渲染、深度管线。 |

### 2.16 粒子渲染 (`include/vr/particle/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `particle_types.hpp` | `Particle2D`, `Particle3D`, `ParticleEmitterDesc`, `ParticleSimulationConfig` | 粒子公共类型。POD 粒子数据 (位置、速度、颜色、大小、旋转、生命周期)、发射器描述符、模拟配置参数。 |
| `particle_simulation_host.hpp` | `ParticleSimulationHost<Dim>`, `ParticleSimulationCreateInfo` | 粒子模拟宿主。GPU Compute Shader 驱动的粒子物理模拟：速度/重力/阻尼/生命周期。支持 AABB 裁剪和粒子排序。 |
| `particle_upload_host.hpp` | `ParticleUploadHost<Dim>`, `ParticleUploadCreateInfo` | 粒子上传宿主。粒子顶点/实例数据通过 UploadHost 传输至 GPU Buffer。支持粒子和发射器缓冲区的增量上传。 |
| `particle_renderer_2d.hpp` | `ParticleRenderer2D`, `ParticleRenderer2DCreateInfo`, `ParticleRenderer2DStats` | 2D 粒子渲染器。粒子四边形绘制、纹理动画、混合模式、透明度排序、GPU 实例化。 |
| `particle_renderer_3d.hpp` | `ParticleRenderer3D`, `ParticleRenderer3DCreateInfo`, `ParticleRenderer3DStats` | 3D 粒子渲染器。世界空间粒子、Billboard 摄像机对齐、Z 深度排序、纹理动画、GPU 实例化。 |

---

## 3. src/ — 源文件实现

### 3.1 核心

| 文件 | 说明 |
|------|------|
| `src/vulkan_context.cpp` | `VulkanContext` 完整实现：实例创建/销毁、Validation 层、Debug Messenger、物理设备选取、逻辑设备创建、队列族、命令池。新增 CrashTracer 入口点集成。 |

### 3.2 渲染框架 (`src/render/`)

| 文件 | 说明 |
|------|------|
| `src/render/frame_command_host.cpp` | `FrameCommandHost` 实现：Pool/CommandBuffer 创建、BeginPrimary、ResetFrame。 |
| `src/render/upload_host.cpp` | `UploadHost` 实现：Staging buffer 管理、Allocate/Write/RecordCopy、Transfer Queue 提交、Synchronization2。 |
| `src/render/descriptor_host.cpp` | `DescriptorHost` 实现：DescriptorPool 管理、Layout 缓存、Set 分配与更新。 |
| `src/render/pipeline_host.cpp` | `PipelineHost` 实现：ShaderModule/PipelineLayout/GraphicsPipeline/ComputePipeline 缓存、延迟编译、VkPipelineCache。 |
| `src/render/render_target_host.cpp` | `RenderTargetHost` 实现：渲染目标创建/销毁、附件管理、格式协商。 |
| `src/render/render_target_pool.cpp` | `RenderTargetPool` 实现：多尺寸池管理、槽位分配/回收。 |
| `src/render/render_target_pass.cpp` | `RenderTargetPass` 实现：VkRenderPass 创建、Load/Store Op、Clear 配置。 |
| `src/render/render_target_composite_renderer.cpp` | `RenderTargetCompositeRenderer` 实现：离屏目标→Swapchain 合成。 |
| `src/render/render_target_bloom_renderer.cpp` | `RenderTargetBloomRenderer` 实现：Prefilter→Blur→Combine Bloom 管线。 |
| `src/render/scene_render_target_set.cpp` | `SceneRenderTargetSet` 实现：场景目标集管理。 |
| `src/render/scene_bloom_post_stack.cpp` | `SceneBloomPostStack` 实现：Bloom 后处理栈。 |
| `src/render/scene_recorder_2d.cpp` | `SceneRecorder2D` 实现：2D 场景统一编排、PreScene/Scene/Overlay 三层录制、RenderGraph 集成 (BindFramePacket→Graph Callback→RecordGraphPass)。 |
| `src/render/scene_recorder_3d.cpp` | `SceneRecorder3D` 实现：3D 场景统一编排、Multi-view/Light/Shadow/PostProcess 管线、RenderGraph 集成 (BindFramePacket→Graph Callback→RecordGraphPass + Scene3DDescriptorContract)。 |
| `src/render/ibl_host.cpp` | `IBLHost` 实现：Irradiance/Specular IBL 资源管理、BRDF LUT。 |
| `src/render/ibl_bake_host.cpp` | `IBLBakeHost` 实现：HDR 环境图烘焙→Irradiance Map + Prefiltered Map + BRDF LUT。支持惰性 IBL 管线与 `SkyEnvironmentGpuHost` 协调。 |
| `src/render/sky_environment_gpu_host.cpp` | `SkyEnvironmentGpuHost` 实现 (858 行)：天空环境 Register/Update 缓存、GPU 参数构建 (tint/exposure/IBL intensity/SH9)、IBL 数据追踪、惰性烘焙协调。 |
| `src/render/sky_environment_pass.cpp` | `SkyEnvironmentPass` 实现 (1100 行)：6 种天空环境模式渲染、摄像机对齐、Push Constant、VkPipeline 管理。 |
| `src/render/background_pass_2d.cpp` | `BackgroundPass2D` 实现 (217 行)：2D 背景渲染、solid/gradient/sprite/surface_entity 模式。 |
| `src/render/bindless_resource_system.cpp` | `BindlessResourceSystem` 实现 (606 行)：Bindless Table 创建/销毁、占位符 Image/Sampler 初始化、子系统 Configure 分发 (Texture/Surface/Geometry/Shadow/RenderTarget/Glyph)、BindlessSlot 解析。 |
| `src/render/frame_composer_host.cpp` | `FrameComposerHost` 实现：最终帧合成与 Swapchain 输出管理。 |

### 3.3 动画 (`src/animation/`)

| 文件 | 说明 |
|------|------|
| `src/animation/animation_clip_host.cpp` | `AnimationClipHost<Dim>` 实现 (688 行)：片段创建/销毁、轨道收集、采样求值、播放模式。 |
| `src/animation/animation_skeletal_host.cpp` | `AnimationSkeletalHost<Dim>` 实现 (498 行)：关节层级、调色板构建、GPU Skinning 矩阵上传、Root Motion。 |
| `src/animation/animation_morph_host.cpp` | `AnimationMorphHost<Dim>` 实现 (320 行)：变形目标管理、权重缓冲区上传、Blend Shape。 |
| `src/animation/animation_path_host.cpp` | `AnimationPathHost<Dim>` 实现 (384 行)：路径曲线管理、采样与合成。 |
| `src/animation/animation_vertex_deform_host.cpp` | `AnimationVertexDeformHost<Dim>` 实现 (322 行)：顶点变形缓冲区管理、payload 上传。 |
| `src/animation/animation_frame_sequence_host.cpp` | `AnimationFrameSequenceHost<Dim>` 实现 (263 行)：帧序列播放、A/B 混合、帧索引路由。 |

### 3.4 资产 (`src/asset/`)

| 文件 | 说明 |
|------|------|
| `src/asset/texture_host.cpp` | `TextureHost` 实现 (441 行)：外部已解码纹理数据接入、GPU Image 创建、Mipmap 生成。 |

### 3.5 资源 (`src/resource/`)

| 文件 | 说明 |
|------|------|
| `src/resource/buffer_host.cpp` | `BufferHost` 实现：vkCreateBuffer + 内存绑定、映射/Flush。 |
| `src/resource/image_host.cpp` | `ImageHost` 实现：vkCreateImage + 内存绑定、vkCreateImageView。 |
| `src/resource/gpu_memory_host.cpp` | `GpuMemoryHost` 实现：Buddy 分配器初始化、AllocateAndBind、Deallocate、Flush/Invalidate。 |
| `src/resource/sampler_host.cpp` | `SamplerHost` 实现：vkCreateSampler + 哈希缓存。 |

### 3.6 文本子系统 (`src/text/`)

| 文件 | 说明 |
|------|------|
| `src/text/freetype_host.cpp` | `FreeTypeHost` 实现。 |
| `src/text/glyph_atlas_host.cpp` | `GlyphAtlasHost` 实现：图集页面分配、字形打包。 |
| `src/text/glyph_upload_host.cpp` | `GlyphUploadHost` 实现：脏页上传。 |
| `src/text/text_renderer_2d.cpp` | `TextRenderer2D` 实现：顶点/索引缓冲、纹理绑定、绘制命令。 |
| `src/text/text_renderer_3d.cpp` | `TextRenderer3D` 实现：3D 世界空间文本。 |

### 3.7 几何子系统 (`src/geometry/`)

| 文件 | 说明 |
|------|------|
| `src/geometry/geometry_resource_host.cpp` | `GeometryResourceHost` 实现。 |
| `src/geometry/geometry_image_host.cpp` | `GeometryImageHost` 实现 (99 行)：Bindless 图像 slot 注册、VkDescriptorImageInfo 构建。 |
| `src/geometry/geometry_appearance_host.cpp` | `GeometryAppearanceHost` 实现 (146 行，替代旧 `GeometryMaterialHost`)：Appearance→SampledSurface 绑定、UV 变换管理。 |
| `src/geometry/geometry_upload_host.cpp` | `GeometryUploadHost` 实现。 |
| `src/geometry/geometry_renderer_2d.cpp` | `GeometryRenderer2D` 实现 (含 Appearance 集成)。 |
| `src/geometry/geometry_renderer_3d.cpp` | `GeometryRenderer3D` 实现 (含 Appearance + Shadow 集成)。 |

### 3.8 Surface 子系统 (`src/surface/`)

| 文件 | 说明 |
|------|------|
| `src/surface/surface_image_host.cpp` | `SurfaceImageHost` 实现 (新增 96 行)：Bindless surface 图像 slot 注册、VkDescriptorImageInfo 构建。 |
| `src/surface/surface_upload_host.cpp` | `SurfaceUploadHost` 实现。 |
| `src/surface/surface_renderer_2d.cpp` | `SurfaceRenderer2D` 实现 (含 Appearance + Runtime Upload 集成)。 |
| `src/surface/surface_renderer_3d.cpp` | `SurfaceRenderer3D` 实现。 |

### 3.9 Light/Shadow 子系统 (`src/light/`, `src/shadow/`)

| 文件 | 说明 |
|------|------|
| `src/light/light_shadow_upload_host.cpp` | `LightShadowUploadHost` 实现。 |
| `src/shadow/shadow_atlas_host.cpp` | `ShadowAtlasHost` 实现。 |
| `src/shadow/shadow_renderer_2d.cpp` | `ShadowRenderer2D` 实现。 |
| `src/shadow/shadow_renderer_3d.cpp` | `ShadowRenderer3D` 实现。 |

### 3.10 粒子子系统 (`src/particle/`)

| 文件 | 说明 |
|------|------|
| `src/particle/particle_simulation_host.cpp` | `ParticleSimulationHost<Dim>` 实现 (2452 行)：GPU Compute-driven 粒子物理模拟、发射逻辑、生命周期管理、排序。 |
| `src/particle/particle_upload_host.cpp` | `ParticleUploadHost<Dim>` 实现 (427 行)：粒子/发射器缓冲区上传、增量更新。 |
| `src/particle/particle_renderer_2d.cpp` | `ParticleRenderer2D` 实现 (1007 行)：2D 粒子渲染、混合模式、纹理动画、GPU 实例化。 |
| `src/particle/particle_renderer_3d.cpp` | `ParticleRenderer3D` 实现 (1791 行)：3D 粒子渲染、Billboard 对齐、Z 排序、GPU 实例化。 |

### 3.11 渲染运行时 (`src/render/` 新增)

| 文件 | 说明 |
|------|------|
| `src/render/swapchain_target_set.cpp` | `SwapchainTargetSet` 实现 (41 行)：交换链 Image→Framebuffer 映射管理。 |

### 3.12 运行时 (`src/runtime/`)

| 文件 | 说明 |
|------|------|
| `src/runtime/crash_tracer_support.cpp` | CrashTracer 运行时集成 (201 行)：`InstallProcessCrashTracer` 实现，进程级 SEH 崩溃处理器安装、自定义崩溃报告。替代旧 `bench_crash_tracer` 的 bench-only 设计。 |

### 3.13 渲染图 (`src/render_graph/`)

| 文件 | 说明 |
|------|------|
| `src/render_graph/alias_allocator.cpp` | Transient Alias Allocator 实现 (672 行)。图级资源活跃区间分析、兼容性分类（TransientCompatibilityKey）、别名候选配对、内存页分配、别名屏障生成、TransientMemoryTimeline 统计。 |
| `src/render_graph/barrier_plan.cpp` | Barrier Plan 实现 (891 行)。Pass DAG→LogicalBarrier 推导 (Read-After-Write/WAR/WAW)、队列依赖计划、队列提交批次 (QueueSubmitBatch) 构建。 |
| `src/render_graph/native_pass_plan.cpp` | Native Pass Plan 实现 (1658 行)。连续 Raster Pass 融合分析（21 种阻止原因）、Load/Store Op 推理（elide transient last use、infer clear）、Dynamic Rendering Local Read 支持决策、Attachment Plan 生成。 |

---

## 4. shaders/ — 着色器

| 文件 | 说明 |
|------|------|
| `shaders/text_2d.vert` | 2D 文本顶点着色器 (SDF 字形 + 图集 UV) |
| `shaders/text_2d.frag` | 2D 文本片段着色器 (SDF 边缘平滑、颜色、Outline) |
| `shaders/text_3d.vert` | 3D 文本顶点着色器 (世界空间 + Billboard) |
| `shaders/text_3d.frag` | 3D 文本片段着色器 |
| `shaders/geometry_2d.vert` | 2D 几何顶点着色器 (路径线段) |
| `shaders/geometry_2d.frag` | 2D 几何片段着色器 (填充/描边 + 抗锯齿) |
| `shaders/geometry_3d.vert` | 3D 几何顶点着色器 (实例化 + 世界矩阵) |
| `shaders/geometry_3d.frag` | 3D 几何片段着色器 (PBR 材质) |
| `shaders/surface_2d.vert` | 2D Surface 顶点着色器 (精灵四边形) |
| `shaders/surface_2d.frag` | 2D Surface 片段着色器 (纹理混合 + tint) |
| `shaders/surface_3d.vert` | 3D Surface 顶点着色器 |
| `shaders/surface_3d.frag` | 3D Surface 片段着色器 (纹理过滤) |
| `shaders/shadow_depth_2d.vert` | 2D 阴影深度顶点着色器 (遮挡物深度图) |
| `shaders/shadow_depth_3d.vert` | 3D 阴影深度顶点着色器 (深度管线) |
| `shaders/render_target_composite.vert` | 离屏合成顶点着色器 (全屏四边形) |
| `shaders/render_target_composite.frag` | 离屏合成片段着色器 (渲染目标→Swapchain) |
| `shaders/render_target_bloom_prefilter.frag` | Bloom Prefilter 片段着色器 (亮度阈值提取) |
| `shaders/render_target_bloom_blur.frag` | Bloom Blur 片段着色器 (多级高斯模糊) |
| `shaders/render_target_bloom_combine.frag` | Bloom Combine 片段着色器 (模糊结果与源图混合) |
| `shaders/background_2d.frag` | 2D 背景片段着色器 (solid/gradient/sprite/surface_entity 模式) |
| `shaders/render_target_composite_far.vert` | 远平面合成顶点着色器 (天空环境渲染用全屏四边形) |
| `shaders/sky_environment.frag` | 天空环境入口片段着色器 (模式分发→solid/gradient/cubemap/equirect HDR/大气散射) |
| `shaders/sky_environment_image.frag` | 天空环境图像片段着色器 (立方体贴图采样 → SkyEnvironment Pass) |
| `shaders/sky_environment_equirect.frag` | 天空环境等距矩形片段着色器 (HDR equirectangular 映射 → SkyEnvironment Pass) |
| `shaders/sky_environment_atmosphere.frag` | 天空环境大气散射片段着色器 (过程化大气散射、太阳角、Mie/Rayleigh 散射 → SkyEnvironment Pass) |

### 4.1 粒子着色器

| 文件 | 说明 |
|------|------|
| `shaders/particle_2d.vert` | 2D 粒子顶点着色器 (四边形 + 实例化属性) |
| `shaders/particle_2d.frag` | 2D 粒子片段着色器 (纹理采样 + 颜色/Alpha) |
| `shaders/particle_3d.vert` | 3D 粒子顶点着色器 (Billboard 对齐 + 世界空间) |
| `shaders/particle_3d.frag` | 3D 粒子片段着色器 (纹理 + 深度) |
| `shaders/particle_build_2d.comp` | 2D 粒子构建计算着色器 (发射器 → 粒子实例化) |
| `shaders/particle_build_3d.comp` | 3D 粒子构建计算着色器 (发射器 → 粒子实例化) |
| `shaders/particle_update_2d.comp` | 2D 粒子更新计算着色器 (生命周期/速度/位置/颜色插值) |
| `shaders/particle_update_3d.comp` | 3D 粒子更新计算着色器 (生命周期/物理/颜色插值) |
| `shaders/particle_sort_3d.comp` | 3D 粒子排序计算着色器 (Z 深度排序、内存重排) |

SPIR-V 编译产物嵌入到 `build/generated/vr/{text,geometry,surface,particle}/generated/` 下。

### 4.2 共享 GLSL 头文件 (`shaders/include/`)

着色器代码复用模块，通过 `#include` 在多个着色器间共享通用函数。

| 文件 | 说明 |
|------|------|
| `shaders/include/vr/common/math.glsl` | 通用数学函数。着色器间共享的数学工具函数 (矩阵变换、坐标转换)。 |
| `shaders/include/vr/text/text_shading.glsl` | 文本着色函数。SDF 边缘平滑、轮廓、颜色混合等文本渲染通用逻辑。文本着色器 (text_2d, text_3d) 通过 `#include` 引用。 |
| `shaders/include/vr/render/bindless.glsl` | Bindless 纹理采样共享头文件。声明全局 bindless descriptor arrays: `g_Textures2D[]` (set=0, b=0), `g_Textures2DArray[]`, `g_TexturesCube[]`, `g_Samplers[]` (set=1, b=0)。提供 `SampleTexture2D/SampleTextureCube/SampleTexture2DArray/SampleTextureCubeLod` 统一采样函数。所有渲染着色器通过 `#include` 引用，替代独立 descriptor set 绑定。 |
| `shaders/include/vr/render/pbr.glsl` | PBR 着色共享头文件 (91 行)。`PbrParams` 结构体 (base_color/metallic/roughness/normal/occlusion/emissive)、`DecodePbrParams` 解码函数、`EvaluateDirectionalLight` (GGX-Smith + Fresnel Schlick BRDF)、`EvaluateAmbientIBL` (Diffuse + Specular IBL 采样)、Tone Map (ACES)。`geometry_3d.frag` 和 `surface_3d.frag` 通过 `#include` 引用。 |
| `shaders/include/vr/render/appearance_decode_3d.glsl` | Appearance 解码共享头文件 (105 行)。从 bindless 纹理表解码 Appearance SampledSurface 贴图：`DecodeBaseColor`、`DecodeNormal`、`DecodeMetallicRoughness`、`DecodeOcclusion`、`DecodeEmissive`。组合 `PbrParams` 输出并通过 `nonuniformEXT` 动态索引纹理。 |

---

## 5. examples/ — 示例程序

| 文件 | 可执行目标 | 说明 |
|------|-----------|------|
| `examples/init_demo.cpp` | `vulkan_init_demo` | 最小 Vulkan 初始化示例 (无窗口，仅 Instance + Device + FreeType)。 |
| `examples/sdl_backend_demo.cpp` | `sdl_backend_demo` | SDL3 窗口 + Vulkan Surface 初始化。 |
| `examples/sdl_render_loop_demo.cpp` | `sdl_render_loop_demo` | 完整渲染循环 (Swapchain + FrameSync + Command + RenderLoop)。 |
| `examples/sdl_runtime_demo.cpp` | `sdl_runtime_demo` | `RenderRuntimeHost` 完整运行时演示。 |
| `examples/sdl_geometry_unified_demo.cpp` | `sdl_geometry_unified_demo` | 几何渲染统一演示 (2D 路径 + 3D 网格)。 |
| `examples/sdl_surface_unified_demo.cpp` | `sdl_surface_unified_demo` | Surface 渲染统一演示。 |
| `examples/sdl_surface_light_shadow_2d_demo.cpp` | `sdl_surface_light_shadow_2d_demo` | 2D Surface + Light + Shadow 完整演示。 |
| `examples/sdl_text_demo.cpp` | `sdl_text_demo` | 2D 文本渲染演示 (SDF + 图集)。 |
| `examples/sdl_text_3d_demo.cpp` | `sdl_text_3d_demo` | 3D 文本渲染演示 (Billboard + 深度 + RenderTarget)。 |
| `examples/sdl_offscreen_postprocess_demo.cpp` | `sdl_offscreen_postprocess_demo` | 离屏渲染与后处理演示。渲染目标创建、SceneRenderTargetSet、Bloom Post Stack、Composite→Swapchain 完整流程。 |
| `examples/sdl_scene_3d_unified_demo.cpp` | `sdl_scene_3d_unified_demo` | 3D 统一场景演示。完整 3D 渲染管线：Geometry + Surface + Text + Light + Shadow + RenderTarget + Bloom。 |
| `examples/sdl_particle_2d_demo.cpp` | `sdl_particle_2d_demo` | 2D 粒子系统演示 (274 行)。粒子发射、GPU 模拟、透明度排序、纹理动画。 |
| `examples/sdl_particle_3d_demo.cpp` | `sdl_particle_3d_demo` | 3D 粒子系统演示 (336 行)。世界空间粒子、Billboard 对齐、Z 排序、摄像机交互。 |
| `examples/sdl_pbr_material_grid_demo.cpp` | `sdl_pbr_material_grid_demo` | PBR 材质网格演示 (904 行)。30 个预配置材质球 (6×5 网格): 金属/非金属、粗糙度/光滑度渐变；3 个特性球: 自发光、透明涂层、各向异性。完整 PBR 着色管线 (Geometry3D + Appearance3D + PBR + IBL + SkyEnvironment)。 |

---

## 6. tests/ — 测试套件

### 6.1 测试基础设施

| 文件 | 说明 |
|------|------|
| `tests/CMakeLists.txt` | 测试构建配置。`vulkan_platform_sdl` 链接，Catch2 风格但自实现框架。 |
| `tests/README.md` | 测试说明文档。 |
| `tests/test_main.cpp` | 测试入口点。 |
| `tests/support/test_framework.hpp` | 轻量测试框架头文件 (TestRunner, Expect, Assert)。 |
| `tests/support/test_framework.cpp` | 测试框架实现。 |

### 6.2 测试用例 (`tests/cases/`)

#### ECS 组件/系统测试

| 文件 | 测试对象 |
|------|---------|
| `ecs_transform_camera_system_tests.cpp` | TransformSystem + CameraSystem 单元测试 |
| `ecs_bounds_system_tests.cpp` | BoundsSystem 单元测试 (AABB 变换) |
| `ecs_culling_system_tests.cpp` | CullingSystem 单元测试 (视锥体裁剪) |
| `ecs_geometry_component_tests.cpp` | GeometryComponent 单元测试 |
| `ecs_geometry_batch_system_tests.cpp` | GeometryBatchSystem 单元测试 |
| `ecs_geometry_runtime_system_tests.cpp` | GeometryRuntimeSystem 单元测试 |
| `ecs_surface_component_tests.cpp` | SurfaceComponent 单元测试 |
| `ecs_surface_batch_runtime_system_tests.cpp` | Surface 批次/运行时系统测试 |
| `ecs_surface_upload_plan_system_tests.cpp` | SurfaceUploadPlanSystem 测试 |
| `ecs_text_component_tests.cpp` | TextComponent 单元测试 |
| `ecs_text_runtime_system_tests.cpp` | TextRuntimeSystem 单元测试 |
| `ecs_text_render_3d_system_tests.cpp` | TextRender3DSystem 单元测试 |
| `ecs_appearance_component_tests.cpp` | AppearanceComponent 单元测试 |
| `ecs_appearance_system_tests.cpp` | AppearanceSystem 单元测试 |
| `ecs_appearance_link_system_tests.cpp` | AppearanceLinkSystem 单元测试 |
| `ecs_appearance_runtime_system_tests.cpp` | AppearanceRuntimeSystem 单元测试 |
| `ecs_animation_system_tests.cpp` | 动画系统单元测试 (Clock/Curve/Property/Material/Camera/Path 综合) |
| `ecs_animation_evaluation_system_tests.cpp` | 动画求值系统测试 (Property/Material/Camera/Path/Skeletal/Morph 求值) |
| `ecs_animation_deformation_evaluation_system_tests.cpp` | 动画变形求值系统测试 (Skeletal/Morph Vertex Transform) |
| `ecs_animation_vertex_frame_evaluation_system_tests.cpp` | 动画顶点帧求值系统测试 (VertexDeform/FrameSequence) |
| `ecs_light_component_tests.cpp` | LightComponent 单元测试 |
| `ecs_light_culling_system_tests.cpp` | LightCullingSystem 单元测试 |
| `ecs_light_runtime_system_tests.cpp` | LightRuntimeSystem 单元测试 |
| `ecs_shadow_component_tests.cpp` | ShadowComponent 单元测试 |
| `ecs_shadow_caster_system_tests.cpp` | ShadowCasterSystem 单元测试 |
| `ecs_shadow_runtime_system_tests.cpp` | ShadowRuntimeSystem 单元测试 |

#### 渲染器协调器测试

| 文件 | 测试对象 |
|------|---------|
| `render_appearance_frame_coordinator_tests.cpp` | AppearanceFrameCoordinator 测试 |
| `render_light_frame_coordinator_tests.cpp` | LightFrameCoordinator 测试 |
| `render_light_shadow_link_coordinator_tests.cpp` | LightShadowLinkCoordinator 测试 |
| `render_light_shadow_link_stage_tests.cpp` | LightShadowLinkStage 测试 |
| `render_shadow_frame_coordinator_tests.cpp` | ShadowFrameCoordinator 测试 |
| `render_shadow_atlas_binding_coordinator_tests.cpp` | ShadowAtlasBindingCoordinator 测试 |
| `shadow_renderer_3d_lifecycle_tests.cpp` | ShadowRenderer3D 生命周期测试 |

#### 集成测试

| 文件 | 测试对象 |
|------|---------|
| `freetype_host_tests.cpp` | FreeTypeHost 单元测试 |
| `glyph_atlas_host_tests.cpp` | GlyphAtlasHost 单元测试 |
| `frame_retire_host_tests.cpp` | FrameRetireHost 单元测试 |
| `vulkan_types_tests.cpp` | Vulkan 类型/结构体测试 |
| `surface_upload_host_policy_tests.cpp` | Surface 上传策略测试 |
| `runtime_configuration_tests.cpp` | RenderRuntimeHost 配置测试 |
| `runtime_integration_tests.cpp` | 运行时集成测试 |
| `runtime_text_renderer_integration_tests.cpp` | 2D TextRenderer 集成测试 |
| `runtime_text_renderer_3d_integration_tests.cpp` | 3D TextRenderer 集成测试 |
| `runtime_geometry_image_host_integration_tests.cpp` | GeometryImageHost 集成测试 |
| `runtime_geometry_appearance_host_tests.cpp` | GeometryAppearanceHost 测试 (183 行，替代旧 GeometryMaterialHost 测试) |
| `runtime_geometry_renderer_2d_integration_tests.cpp` | GeometryRenderer2D 集成测试 |
| `runtime_geometry_renderer_3d_integration_tests.cpp` | GeometryRenderer3D 集成测试 |
| `runtime_surface_upload_host_integration_tests.cpp` | SurfaceUploadHost 集成测试 |
| `runtime_surface_renderer_3d_integration_tests.cpp` | SurfaceRenderer3D 集成测试 |
| `runtime_scene_3d_unified_integration_tests.cpp` | 3D 统一场景集成测试 (Geometry+Surface+Text+Light+Shadow+RenderTarget+Bloom+Animation) |
| `runtime_frame_composer_integration_tests.cpp` | FrameComposerHost 集成测试 (场景合成→Swapchain)。 |
| `runtime_ibl_host_integration_tests.cpp` | IBLHost 集成测试 (Irradiance/Specular IBL 资源加载与绑定)。 |
| `runtime_ibl_bake_host_integration_tests.cpp` | IBLBakeHost 集成测试 (HDR 环境图烘焙流程)。 |
| `runtime_texture_host_integration_tests.cpp` | TextureHost 集成测试 (纹理上传/Mip/格式协商)。 |
| `animation_host_tests.cpp` | AnimationClipHost 单元测试 (片段创建、轨道管理、播放控制)。 |
| `animation_skeletal_morph_host_tests.cpp` | AnimationSkeletalHost + AnimationMorphHost 单元测试。 |
| `animation_vertex_frame_host_tests.cpp` | AnimationVertexDeformHost + AnimationFrameSequenceHost 单元测试。 |
| `render_target_types_tests.cpp` | RenderTarget 类型/结构体单元测试 (已大幅扩展至 1130 行)。 |

#### 粒子 ECS/系统测试

| 文件 | 测试对象 |
|------|---------|
| `ecs_particle_component_tests.cpp` | ParticleComponent + ParticleEmitterComponent 单元测试 |
| `ecs_particle_runtime_system_tests.cpp` | ParticleRuntimeSystem 2D/3D 单元测试 |
| `particle_simulation_host_tests.cpp` | ParticleSimulationHost 单元测试 |

#### 集成测试 (续)

| 文件 | 测试对象 |
|------|---------|
| `runtime_particle_renderer_2d_integration_tests.cpp` | ParticleRenderer2D 集成测试 (372 行) |
| `runtime_particle_renderer_3d_integration_tests.cpp` | ParticleRenderer3D 集成测试 (451 行) |
| `runtime_text_renderer_integration_tests.cpp` | 2D TextRenderer 集成测试 (新增, 307 行) |
| `runtime_configuration_tests.cpp` | Runtime/Profile 配置测试 (已扩展至 436 行) |
| `runtime_integration_tests.cpp` | 运行时集成测试 (已扩展至 281 行) |
| `runtime_sky_environment_integration_tests.cpp` | SkyEnvironmentGpuHost + SkyEnvironmentPass 集成测试 (645 行) |
| `runtime_scene_3d_unified_integration_tests.cpp` | 3D 统一场景集成测试 (已扩展至 +43 行，含 SkyEnvironment 集成) |
| `runtime_surface_renderer_2d_integration_tests.cpp` | SurfaceRenderer2D 集成测试 (380 行，新增) |
| `scene_background_core_tests.cpp` | Scene 背景核心测试 (666 行)。SpriteBackground + SkyEnvironment 组件测试。 |
| `bindless_resource_integration_tests.cpp` | BindlessResourceSystem 集成测试 (1188 行)。Bindless Table 创建/销毁、子系统 Configure 验证、BindlessSlot 解析/更新/销毁、Placeholder 完整性。 |
| `bindless_shader_contract_tests.cpp` | Bindless 着色器合约测试 (142 行)。验证 bindless.glsl 接口与 C++ 端 BindlessTableDesc 的一致性。 |
| `geometry_tangent_space_tests.cpp` | GeometryTangentSpace 单元测试 (102 行)。切线空间构建、normalized_existing/generated 状态、退化 UV 检测。 |
| `pbr_appearance_contract_tests.cpp` | PBR Appearance 合约测试 (259 行)。验证 Appearance→PBR 参数解析的完整性、SampledSurface 绑定合约。 |
| `pbr_shader_contract_tests.cpp` | PBR 着色器合约测试 (187 行)。验证 C++ 端 PBR 参数布局与 pbr.glsl 着色器接口的一致性。 |

---

## 7. bench/ — 基准测试套件

### 7.1 基础设施

| 文件 | 说明 |
|------|------|
| `bench/CMakeLists.txt` | 基准测试构建配置。可选 CrashTracer 集成。 |
| `bench/README.md` | 基准测试说明。 |
| `bench/bench_main.cpp` | 基准测试入口点。 |
| `bench/support/bench_framework.hpp` | 基准测试框架头文件 (BenchRunner, timer, 统计)。 |
| `bench/support/bench_framework.cpp` | 基准测试框架实现。 |
| `bench/support/crash_tracer_support.hpp` | 崩溃追踪器集成头文件。(已移除，功能迁移至 `src/runtime/crash_tracer_support.cpp`) |
| `bench/support/crash_tracer_support.cpp` | 崩溃追踪器集成实现。(已移除) |

### 7.2 基准测试用例 (`bench/cases/`)

| 文件 | 测试对象 |
|------|---------|
| `ecs_bounds_system_bench.cpp` | BoundsSystem 性能基准 |
| `ecs_culling_system_bench.cpp` | CullingSystem 性能基准 |
| `ecs_geometry_runtime_system_bench.cpp` | GeometryRuntimeSystem 性能基准 |
| `ecs_surface_runtime_system_bench.cpp` | SurfaceRuntimeSystem 性能基准 |
| `ecs_surface_upload_plan_system_bench.cpp` | SurfaceUploadPlanSystem 性能基准 |
| `ecs_text_component_bench.cpp` | TextComponent 性能基准 |
| `ecs_text_runtime_system_bench.cpp` | TextRuntimeSystem 性能基准 |
| `ecs_appearance_runtime_system_bench.cpp` | AppearanceRuntimeSystem 性能基准 |
| `render_light_shadow_link_coordinator_bench.cpp` | LightShadowLinkCoordinator 性能基准 |
| `render_shadow_atlas_binding_coordinator_bench.cpp` | ShadowAtlasBindingCoordinator 性能基准 |
| `freetype_host_bench.cpp` | FreeTypeHost 性能基准 |
| `glyph_atlas_host_bench.cpp` | GlyphAtlasHost 性能基准 |
| `frame_retire_host_bench.cpp` | FrameRetireHost 性能基准 |
| `frame_sync_bench.cpp` | FrameSyncHost 性能基准 |
| `runtime_text_renderer_bench.cpp` | TextRenderer 性能基准 |
| `vulkan_types_bench.cpp` | Vulkan 类型操作性能基准 |
| `ecs_particle_runtime_system_bench.cpp` | ParticleRuntimeSystem 性能基准 |
| `runtime_diagnostics_bench.cpp` | RuntimeDiagnostics 性能基准 |
| `runtime_diagnostics_bench.cpp` | RuntimeDiagnostics V2 性能基准 (442 行)。RenderGraphQueueTimeline 序列化、RenderGraphRuntimeDiagnostics 收集、Queue Timeline 视图构建性能测试。 |
| `runtime_steady_state_allocation_bench.cpp` | 运行时稳态内存分配基准 (354 行)。Text-heavy 场景下多帧运行的 Upload/Descriptor/RenderTarget/Bindless 稳态分配行为分析。 |
| `scene_background_runtime_bench.cpp` | Scene 背景运行时性能基准 (222 行) |

### 7.3 基准测试数据 (`bench/baselines/`)

| 文件 | 说明 |
|------|------|
| `ecs_geometry_runtime_gold.json` | 几何运行时的黄金基线数据 |
| `ecs_geometry_runtime_gate_gold_2026-05-07.json` | 几何运行时门控黄金基线 (387 行, 2026-05-07) |
| `ecs_core_cpu_baseline_*.json` | CPU 核心 ECS 基线快照 |
| `ecs_core_cpu_post_opt_*.json` | CPU 优化后快照 (含 `_snapshot.md`) |
| `ecs_font_quality_*.json` | 字体质量基准快照 |
| `ecs_geometry_runtime_*.json` | 几何运行时多阶段快照 |
| `ecs_surface_*.json` | Surface 系统基准快照 |
| `ecs_appearance_*.json` | Appearance 系统基准快照 |
| `ecs_light_shadow_*.json` | Light/Shadow 系统基准快照 |
| `ecs_phase*_refactor_*.{json,md}` | 多阶段重构对比快照 |
| `bench_progress_live.{log,err.log}` | 持续基准测试日志 |
| `render_graph_queue_timeline_gold.json` | RenderGraph 队列时间线黄金基线 (296 行)。定义标准 RenderGraph Queue Timeline 的预期结构。 |

---

## 8. docs/ — 文档

| 文件 | 说明 |
|------|------|
| `architecture_manual.md` | 项目架构手册。 |
| `file_index.md` | 代码文件索引 (本文件)。 |
| `appearance_unified_refactor_plan.md` | Appearance 语义统一重构计划 (832 行)。详细记录 Appearance 统一化、PBR 管线基础、Geometry/Surface 渲染器合并、材质→视觉重命名的完整重构路线和决策过程。 |
| `rendergraph_final_development_plan.md` | RenderGraph 最终开发规划书 (1702 行)。定义 Phase 5-12 的完整 RenderGraph 架构路线、Phase 门控策略、代码审查结论和最终架构目标 (干净重构方案)。 |
| `deep-research-report (16).md` | 深度研究报告 (465 行)。RenderGraph 设计相关的行业参考和技术调研 (Frostbite/Vulkan/DirectX 12 等)。 |

---

## 9. C++20 模块文件 (`feature/cpp20-modules` 分支)

以下文件仅存在于 `feature/cpp20-modules` 分支，包含所有核心库的模块接口单元。

| 文件 | 行数 | 说明 |
|------|------|------|
| `include/vr/detail/vr_module_fwd.hpp` | 6 | 全局模块片段共享头文件 (NOMINMAX + vulkan.h + McVector) |
| `include/vr/modules/vr.types/vr.types.cppm` | 463 | 基础类型模块 (McVector, spatial types, text/geometry/surface types) |
| `include/vr/modules/vr.context/vr.context.cppm` | 125 | VulkanContext 基础设施模块 |
| `include/vr/modules/vr.platform/vr.platform.cppm` | 365 | WindowSurface + RenderHost 平台模块 |
| `include/vr/modules/vr.resource/vr.resource.cppm` | 318 | GpuMemory/Buffer/Image/Sampler 资源模块 |
| `include/vr/modules/vr.render/vr.render.cppm` | 1540 | Swapchain, FrameSync, Upload, Descriptor, Pipeline, RenderLoop 渲染模块 |
| `include/vr/modules/vr.ecs/vr.ecs.cppm` | 1038 | 所有 ECS 组件 + 系统 (含 Appearance/Geometry/Surface) |
| `include/vr/modules/vr.text/vr.text.cppm` | 2239 | FreeType/Glyph + Text ECS 系统 |
| `include/vr/modules/vr.geometry/vr.geometry.cppm` | 696 | Geometry 渲染器模块 |
| `include/vr/modules/vr.surface/vr.surface.cppm` | 640 | Surface 渲染器模块 |
| `include/vr/modules/vr.runtime/vr.runtime.cppm` | 183 | RenderRuntimeHost 运行时模块 |
| `docs/module_conversion_report.md` | 123 | 模块转换进度报告 |

**模块依赖图 (有向无环)**:
```
vr.types
  └─► vr.context ──► vr.resource ──► vr.render ──► vr.runtime
        │                                │
        ├─► vr.ecs ◄─────────────────────┤
        ├─► vr.platform ────────────────-─┤
        ├─► vr.text ◄────────────────────-┤
        ├─► vr.geometry ◄────────────────-─┤
        └─► vr.surface ◄───────────────────┘
```

---

## 10. crash_reports/ — 崩溃日志

| 文件 | 说明 |
|------|------|
| `crash_reports/vr_bench_*.log` | Benchmark 崩溃追踪日志。包含 CrashTracer 捕获的堆栈回溯。 |

---

## 11. scripts/ — 辅助脚本

| 文件 | 说明 |
|------|------|
| `scripts/bench/README.md` | 基准测试脚本说明。 |
| `scripts/bench/run_bench_gate.ps1` | 基准测试门控脚本 (PowerShell)。对比当前结果与基线，门禁判断。 |
| `scripts/bench/new_golden_baseline.ps1` | 生成新黄金基线脚本。 |
| `scripts/testing/README.md` | 测试脚本说明 (97 行)。质量分级系统文档。 |
| `scripts/testing/quality_profiles.json` | 质量分级配置文件 (205 行)。定义 Critical/High/Medium/Low 质量门禁规则。 |
| `scripts/testing/run_quality_profile.ps1` | 质量分级运行脚本 (PowerShell)。按 Profile 筛选和运行测试。 |
| `scripts/testing/vr_quality_runner.py` | VR 质量测试运行器 (469 行)。Python 自动化测试编排、Profile 匹配、结果报告。 |

---

## 12. tools/ — 工具

| 文件 | 说明 |
|------|------|
| `tools/spv_to_header.py` | SPIR-V 二进制 → C++ 头文件转换器。将 `glslangValidator` 编译产物嵌入 `const uint32_t[]` 数组。用于 CMake 着色器编译管道。 |
| `tools/spv_reflect_to_json.py` | SPIR-V 反射工具。从 SPIR-V 二进制提取 Shader 接口信息 (Descriptor Set Layout、Push Constants、Entry Points) 并输出 JSON，用于着色器合约校验。 |
| `tools/shader_contract_check.py` | 着色器合约检查工具。对比 C++ 端的 Descriptor/管线定义与 SPIR-V 反射输出，验证一致性，检测绑定冲突。 |
| `tools/shader_contract_summary.py` | 着色器合约摘要工具。汇总所有着色器的合约信息，生成可读的接口报告。 |

---

## 13. 文件统计

| 类别 | 文件数 |
|------|--------|
| 公开头文件 (.hpp) | 210 |
| 源文件 (.cpp) | 62 |
| 着色器 (.vert/.frag/.comp) + GLSL 头文件 | 33 + 5 |
| 示例文件 | 14 |
| 测试用例 | 73 |
| 基准测试用例 | 22 (含 2 support files) |
| 文档 | 16 |
| 脚本/工具 | 8 + 4 |
| CMake Presets | 1 |
| C++20 模块文件 (feature 分支) | 12 |
| **总计** | **~440** |
