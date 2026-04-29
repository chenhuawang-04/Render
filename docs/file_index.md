# VulkanRender_New 代码文件索引

> 统计: 约 55 个头文件, 24 个源文件, 12 个着色器, 8 个示例, 27 个测试文件, 17 个基准文件, 7 个文档, 3 个脚本, 1 个工具, 1+ 崩溃日志文件

---

## 1. 构建配置

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 顶层 CMake 构建脚本。定义项目、外部依赖 (Vulkan/SDL3/FreeType/MemoryCenter)、`vulkan_init` 静态库、`vulkan_platform_sdl` 接口库、所有 Demo 可执行文件、着色器编译管道、测试/基准子目录。 |
| `.gitignore` | Git 忽略规则：`build/`, CMake 产物, IDE 文件, bench 临时/快照文件。 |

---

## 2. include/ — 公开头文件 (`vr/` 命名空间)

### 2.1 核心 Vulkan 上下文

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `include/vr/vulkan_context.hpp` | `VulkanContext`, `VulkanInstanceCreateInfo`, `VulkanDeviceCreateInfo`, `QueueFamilyIndices` | Vulkan 实例/设备/队列族全生命周期管理。支持 Validation Layers、Synchronization2、单次命令提交辅助。 |

### 2.2 平台层 (`include/vr/platform/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `window_surface.hpp` | `WindowSurface<Sdl3BackendTag>`, `WindowCreateInfo`, `BackendTag`, `BackendKind`, `Sdl3BackendTag`, `ActiveBackendTag` | SDL3 窗口创建、VkSurfaceKHR 管理、事件处理 (close/quit/poll)、窗口属性查询 (framebuffer size)。 |
| `render_host.hpp` | `RenderHost<Sdl3BackendTag>`, `RenderHostCreateInfo` | 组合 `VulkanContext` + `WindowSurface` 的统一初始化/关闭模板。SDL3 特化完整实现。 |

### 2.3 资源层 (`include/vr/resource/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `gpu_memory_host.hpp` | `GpuMemoryHost`, `GpuMemoryHostCreateInfo` | GPU 内存 Buddy Allocator (64MB 块/256 分区)。AllocateAndBindBuffer/Image, Deallocate, Flush/Invalidate/Trim。 |
| `buffer_host.hpp` | `BufferHost`, `BufferCreateInfo`, `BufferResource` | GPU Buffer 创建/销毁/映射/Flush/Invalidate。全静态方法。 |
| `image_host.hpp` | `ImageHost`, `ImageCreateInfo`, `ImageResource` | GPU Image 创建/销毁/ImageView 创建。支持 mip/array/default view。 |
| `sampler_host.hpp` | `SamplerHost`, `SamplerDesc`, `SamplerId`, `SamplerHostCreateInfo`, `SamplerHostStats` | VkSampler 缓存 (哈希去重)。所有 Vulkan 采样器参数。 |

### 2.4 渲染框架层 (`include/vr/render/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `swapchain_host.hpp` | `SwapchainHost<WindowSurfaceT>`, `SwapchainCreateInfo`, `SwapchainImage`, `AcquireResult`, `PresentResult` | 交换链生命周期。创建/重建、AcquireNextImage/Present、表面格式/呈现模式选择、延迟销毁、自动 Framebuffer。 |
| `frame_sync_host.hpp` | `FrameSyncHost<framesInFlight>`, `FrameToken`, `FrameBeginResult`, `FrameSlot`, `FrameSubmitWait` | 帧同步。信号量/Fence 管理、Acquire/Submit/Present 流水线。支持 Vulkan 1.3 Synchronization2、额外等待信号量、提交值追踪。 |
| `frame_command_host.hpp` | `FrameCommandHost`, `FrameCommandCreateInfo`, `FrameCommandSlot` | 每帧 CommandPool + CommandBuffer 池。AcquirePrimary/BeginPrimary/EndCommandBuffer，按需增长。 |
| `frame_retire_host.hpp` | `FrameRetireHost`, `FrameRetireStats` | 延迟 GPU 资源回收。ImageView/Framebuffer/Swapchain/CommandPool 延迟销毁。Collect (按提交值) / Flush (强制清除)。 |
| `render_loop_host.hpp` | `RenderLoopHost<WindowSurfaceT, SwapchainT, framesInFlight>`, `RenderLoopCreateInfo`, `FrameRecordContext`, `TickResult`, `TickCode`, `FrameRecorder` concept | 主帧循环。组合 Sync + Swapchain + Command + Retire，Tick() 驱动。支持 `FrameRecorder` 和 `FrameContextRecorder` 两种录制器 concept。Swapchain 变更自动通知。 |
| `descriptor_host.hpp` | `DescriptorHost`, `DescriptorHostCreateInfo`, `DescriptorSetLayoutDesc`, `DescriptorSetLayoutId`, `DescriptorBufferWrite`, `DescriptorImageWrite`, `DescriptorTexelBufferWrite` | 描述符池管理 + Layout 缓存 (哈希去重)。AllocateSet/UpdateSet，每帧独立池。 |
| `pipeline_host.hpp` | `PipelineHost`, `PipelineHostCreateInfo`, `PipelineHostStats`, `ShaderModuleId`, `PipelineLayoutId`, `GraphicsPipelineId`, `ComputePipelineId`, `GraphicsPipelineDesc`, `ComputePipelineDesc` | 管线缓存。ShaderModule/PipelineLayout/GraphicsPipeline/ComputePipeline 注册与获取。延迟编译队列 (Enqueue + ProcessPendingCompiles)。VkPipelineCache 文件持久化。 |
| `upload_host.hpp` | `UploadHost`, `UploadHostCreateInfo`, `UploadAllocation`, `UploadSubmitInfo`, `UploadEndFrameResult`, `UploadFrameStats` | Staging Buffer 上传。Allocate/Write/RecordCopyBuffer/RecordCopyImage + Barrier。支持 Transfer Queue 异步上传、Synchronization2。 |
| `runtime_prepare_context.hpp` | `RuntimePrepareContext` | 帧准备上下文结构体，聚合所有运行时子系统指针，传递至 Recorder 的 PrepareFrame 回调。 |
| `render_runtime_host.hpp` | `RenderRuntimeHost<BackendTag, framesInFlight>`, `RuntimeModulesCreateInfo`, `RuntimeTickResult`, `RuntimeFramePreparer` concept | 顶层运行时。组合所有子系统，统一 Init/Shutdown 顺序。Tick() 协调 Upload、Pipeline 编译、RenderLoop、prepare 回调。可选启用各模块。 |

### 2.5 ECS 概念层 (`include/vr/ecs/concept/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `dimension.hpp` | `Dim2`, `Dim3`, `SceneDimension`, `DimensionTag` concept | 维度标签类型。`Dim2` = 2D, `Dim3` = 3D。`DimensionTag` concept 约束模板参数。 |

### 2.6 ECS 组件层 (`include/vr/ecs/component/`)

| 文件 | 主要结构/类 | 说明 |
|------|------------|------|
| `spatial_types.hpp` | `Float2`, `Float3`, `Float4`, `Quaternion`, `Affine2x3`, `Matrix4x4`, `CoreMatrix4x4` | 数学类型别名。`MMath::Vec2/3/4`, `MMath::Quat`, `MMath::Mat3`, `MMath::D3D::Mat4` (列主序 D3D 风格)。 |
| `transform_component.hpp` | `TransformComponent<Dim2/3>`, `TransformStyle2D/3D`, `TransformRuntime2D/3D`, `TransformHierarchyLink`, `TransformDirtyFlags` | 变换组件 (POD)。位置/旋转/缩放 + 4x4 矩阵 + 层级链接 + 修订追踪 + 脏标记。 |
| `camera_component.hpp` | `CameraComponent<Dim2/3>`, `CameraStyle2D/3D`, `CameraRuntimeData`, `CameraViewport`, `CameraProjectionMode` | 相机组件 (POD)。投影模式/参数 + View/Proj/VP 矩阵 + 裁剪掩码 + 视口。 |
| `bounds_component.hpp` | `BoundsComponent<Dim2/3>`, `BoundsStyle2D/3D`, `BoundsRuntime2D/3D`, `BoundsDirtyFlags` | 包围盒组件 (POD)。局部/世界 AABB + 中心/范围 + 半径 + 可见性掩码 + 修订追踪。 |
| `geometry_component.hpp` | `GeometryComponent<Dim2/3>`, `GeometryStyle2D/3D`, `GeometryRuntime2D/3D`, `GeometryPathInlineData`, `GeometryMeshRoute`, `GeometryRuntimeRoute`, 各枚举 | 几何组件 (POD)。2D: 路径数据 (inline 1024B) + 填充/描边样式 + 排序键。3D: 子网格路由 + PBR 材质 + 包围盒。 |
| `surface_component.hpp` | `SurfaceComponent<Dim2/3>`, `SurfaceStyle2D/3D`, `SurfaceRuntime2D/3D`, `SurfaceRuntimeRoute`, `Surface2DSourceRoute`, `Surface3DTextureRoute` | Surface 组件 (POD)。图像/精灵源 + 混合模式/纹理过滤 + 排序键 + 尺寸/轴心。 |
| `text_component.hpp` | `TextComponent<Dim2/3>`, `TextStyle2D/3D`, `TextBufferInlineUtf8`, `TextRuntimeBatchData`, `Rgba8`, `TextHorizontalAlign`, `TextVerticalAlign` | 文本组件 (POD)。UTF8 内联缓冲 (240B) + SDF/Outline 参数 + 字形范围 + 排序键。 |

### 2.7 ECS 系统层 (`include/vr/ecs/system/`)

| 文件 | 主要类/结构 | 说明 |
|------|------------|------|
| `spatial_math.hpp` | (free functions) | 数学工具函数：单位矩阵、TRS 组成/分解、矩阵乘法、求逆 (仿射)、四元数工具、正交/透视投影构建 (D3D 右手系)。 |
| `transform_system.hpp` | `TransformSystem<Dim>`, `TransformHierarchyScratch<Dim>` | 变换层级系统。父子链接 (AttachChild/DetachFromParent)、局部/世界矩阵重建、脏标记传播、层级遍历 (防循环)、平坦层级快速路径。 |
| `camera_system.hpp` | `CameraSystem<Dim>` | 相机系统。投影重建 (正交/透视/Reverse-Z)、View 矩阵从 Transform 逆、ViewProjection 合并。支持 Aligned 批量更新。 |
| `bounds_system.hpp` | `BoundsSystem<Dim>` | 包围盒系统。世界空间 AABB 变换 (中心+范围法)、多种 UpdateAligned 变体、点包含/相交测试。修订追踪与脏标记传播。 |
| `culling_system.hpp` | `CullingSystem<Dim>`, `CullingScratch<Dim>`, `CullingBuildOptions`, `CullingBuildStats`, `PreparedCamera`, `FrustumPlanes` | 裁剪系统。视锥体平面提取 (从 VP 矩阵)、球体拒绝 + AABB 细化、可见性掩码过滤、候选列表接口、可见集签名哈希、VisibilityStamp epoch 机制。模板展开的零分支扫描。 |
| `geometry_system.hpp` | `GeometrySystem<Dim>` | 几何基础系统。64 位排序键构建/解析、渲染路由设置 (geometry_id/material_id/batch_tag)、可见性筛选。 |
| `geometry_batch_system.hpp` | `GeometryBatchSystem<Dim>`, `GeometryBatchItem`, `GeometryBatchScratch<Dim>`, `GeometryBatchBuildStats` | 几何批次系统。收集可见几何组件、按排序键排序、去重。支持全量扫描和候选列表扫描。 |
| `geometry_mesh_system.hpp` | `GeometryMeshSystem` | 3D 网格路由操作 (submesh_index, lod, flags)。 |
| `geometry_path_system.hpp` | `GeometryPathSystem`, `GeometryPathCommandView`, `GeometryPathMoveToCommand`, `GeometryPathLineToCommand`, `GeometryPathQuadToCommand`, `GeometryPathCubicToCommand`, `GeometryPathCloseCommand`, `GeometryPathCommandType` | 2D 路径命令系统。内联路径数据解析、命令迭代 (ForEachCommandRaw)、路径数据哈希。 |
| `geometry_runtime_system.hpp` | `GeometryRuntimeSystem<Dim2/3>`, `Geometry2DPathPrimitive`, `Geometry3DGpuInstance`, `Geometry2DDrawBatch`, `Geometry3DDrawBatch`, 各种 RuntimeBuildConfig/BuildStats/Cache/Scratch | 几何运行时系统。2D: 路径→线段图元细分 (Quad/Cubic) + 样式打包 + 批次合并。3D: 组件→GPU 实例 (世界矩阵+PBR+包围盒) + 批次合并 + 增量变换更新。支持运行时缓存 (签名/epoch 追踪)。 |
| `surface_system.hpp` | `SurfaceSystem<Dim>` | Surface 基础系统。排序键布局 [pass:2][material:16][surface:16][minor:16][batch:14]。路由设置、可见性。 |
| `surface_batch_system.hpp` | `SurfaceBatchSystem<Dim>`, `SurfaceBatchItem`, `SurfaceBatchScratch<Dim>`, `SurfaceBatchBuildStats` | Surface 批次系统。收集与排序可见 Surface 组件。 |
| `surface_runtime_system.hpp` | `SurfaceRuntimeSystem<Dim2/3>` | Surface 运行时系统。GPU 实例生成 + 批次合并 + 缓存。 |
| `surface_upload_plan_system.hpp` | `SurfaceUploadPlanSystem` | Surface 上传计划系统。识别需要上传的组织组件、页面调度。 |
| `text_system.hpp` | `TextSystem<Dim>` | 文本基础系统。排序键布局 [pass:2][material:16][font:12][atlas:10][minor:16][batch:8]。字体/材质路由、UTF8 修订追踪、内联字符串操作。 |
| `text_batch_system.hpp` | `TextBatchSystem<Dim>`, `TextBatchItem`, `TextBatchScratch<Dim>`, `TextBatchBuildStats` | 文本批次系统。收集可见文本组件、排序。 |
| `text_runtime_system.hpp` | `TextRuntimeSystem<Dim2/3>`, `TextGlyphQuad` | 文本运行时系统。字形四边形生成 (x0/y0/x1/y1 + uv 坐标)、字形缓存、批次合并。 |
| `text_render_3d_system.hpp` | `TextRender3DSystem`, `Text3DGpuInstance` | 3D 文本特殊系统。3D 文本 GPU 实例 (rect、UV、world position、billboard 参数、颜色、大小限制)。 |

### 2.8 文本渲染 (`include/vr/text/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `text_types.hpp` | `GlyphRectU16`, `GlyphUvRect`, `GlyphAtlasRegion`, `k_invalid_glyph_page_index` | 文本渲染公共类型定义。字形像素矩形 (U16)、UV 矩形 (float)、图集页面指针。 |
| `freetype_host.hpp` | `FreeTypeHost`, `FreeTypeHostCreateInfo` | FreeType 库封装。字体 face 加载 (内存/文件)、字形度量查询、字形位图/SDF 渲染。 |
| `glyph_atlas_host.hpp` | `GlyphAtlasHost`, `GlyphAtlasCreateInfo` | 字形图集。动态页面分配、字形打包 (bin packing)、脏页追踪、双层缓存。 |
| `glyph_upload_host.hpp` | `GlyphUploadHost`, `GlyphUploadHostCreateInfo` | 字形上传。将 Atlas 脏页通过 UploadHost 传输至 GPU Image。 |
| `text_renderer_2d.hpp` | `TextRenderer2D` | 2D 文本渲染器。图集纹理绑定、字形顶点/索引缓冲生成、Push Constant (颜色/SDF/Outline 参数)。 |
| `text_renderer_3d.hpp` | `TextRenderer3D` | 3D 文本渲染器。世界空间文本、Billboard 矩阵、深度测试参数。 |

### 2.9 几何渲染 (`include/vr/geometry/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `geometry_types.hpp` | `GeometryMeshVertex`, `GeometrySubmeshRange`, `McVector` | 几何渲染公共类型定义。3D 网格顶点格式 (position/normal/uv)、子网格索引范围。 |
| `geometry_resource_host.hpp` | `GeometryResourceHost` | 几何资源注册表。Mesh/Image/Material ID 分配与管理。 |
| `geometry_material_host.hpp` | `GeometryMaterialHost` | 材质系统。PBR 参数 (albedo/metallic/roughness)，材质 UBO 上传。 |
| `geometry_image_host.hpp` | `GeometryImageHost` | 几何图像管理。纹理 Image/View/Sampler 绑定，Descriptor 写入。 |
| `geometry_upload_host.hpp` | `GeometryUploadHost` | 几何数据上传。顶点/索引缓冲上传，路径数据上传。 |
| `geometry_renderer_2d.hpp` | `GeometryRenderer2D` | 2D 几何渲染器。路径线段→顶点缓冲、描边/填充管线绑定、抗锯齿、Push Constant。 |
| `geometry_renderer_3d.hpp` | `GeometryRenderer3D` | 3D 几何渲染器。GPU 实例化绘制 (instanced draw)、PBR 材质绑定、管线状态。 |

### 2.10 Surface 渲染 (`include/vr/surface/`)

| 文件 | 类/结构 | 说明 |
|------|---------|------|
| `surface_types.hpp` | `SurfaceUploadPatch`, `SurfaceUploadRange`, `McVector` | Surface 渲染公共类型定义。上传补丁追踪、缓冲区范围描述。 |
| `surface_image_host.hpp` | `SurfaceImageHost` | Surface 图像管理。精灵/图集 Image/View 管理、采样器绑定。 |
| `surface_upload_host.hpp` | `SurfaceUploadHost` | Surface 数据上传。图像数据通过 UploadHost 上传至 GPU。 |
| `surface_renderer_2d.hpp` | `SurfaceRenderer2D` | 2D Surface 渲染器。精灵绘制、混合模式 (alpha/additive/multiply/screen)、UV 变换、tint color。 |
| `surface_renderer_3d.hpp` | `SurfaceRenderer3D` | 3D Surface 渲染器。世界空间贴图、纹理过滤 (linear/nearest/anisotropic)、寻址模式、双面渲染。 |

---

## 3. src/ — 源文件实现

### 3.1 核心

| 文件 | 说明 |
|------|------|
| `src/vulkan_context.cpp` | `VulkanContext` 完整实现：实例创建/销毁、Validation 层、Debug Messenger、物理设备选取、逻辑设备创建、队列族、命令池。 |

### 3.2 渲染框架 (`src/render/`)

| 文件 | 说明 |
|------|------|
| `src/render/frame_command_host.cpp` | `FrameCommandHost` 实现：Pool/CommandBuffer 创建、BeginPrimary、ResetFrame。 |
| `src/render/upload_host.cpp` | `UploadHost` 实现：Staging buffer 管理、Allocate/Write/RecordCopy、Transfer Queue 提交、Synchronization2。 |
| `src/render/descriptor_host.cpp` | `DescriptorHost` 实现：DescriptorPool 管理、Layout 缓存、Set 分配与更新。 |
| `src/render/pipeline_host.cpp` | `PipelineHost` 实现：ShaderModule/PipelineLayout/GraphicsPipeline/ComputePipeline 缓存、延迟编译、VkPipelineCache。 |

### 3.3 资源 (`src/resource/`)

| 文件 | 说明 |
|------|------|
| `src/resource/buffer_host.cpp` | `BufferHost` 实现：vkCreateBuffer + 内存绑定、映射/Flush。 |
| `src/resource/image_host.cpp` | `ImageHost` 实现：vkCreateImage + 内存绑定、vkCreateImageView。 |
| `src/resource/gpu_memory_host.cpp` | `GpuMemoryHost` 实现：Buddy 分配器初始化、AllocateAndBind、Deallocate、Flush/Invalidate。 |
| `src/resource/sampler_host.cpp` | `SamplerHost` 实现：vkCreateSampler + 哈希缓存。 |

### 3.4 文本子系统 (`src/text/`)

| 文件 | 说明 |
|------|------|
| `src/text/freetype_host.cpp` | `FreeTypeHost` 实现。 |
| `src/text/glyph_atlas_host.cpp` | `GlyphAtlasHost` 实现：图集页面分配、字形打包。 |
| `src/text/glyph_upload_host.cpp` | `GlyphUploadHost` 实现：脏页上传。 |
| `src/text/text_renderer_2d.cpp` | `TextRenderer2D` 实现：顶点/索引缓冲、纹理绑定、绘制命令。 |
| `src/text/text_renderer_3d.cpp` | `TextRenderer3D` 实现：3D 世界空间文本。 |

### 3.5 几何子系统 (`src/geometry/`)

| 文件 | 说明 |
|------|------|
| `src/geometry/geometry_resource_host.cpp` | `GeometryResourceHost` 实现。 |
| `src/geometry/geometry_material_host.cpp` | `GeometryMaterialHost` 实现。 |
| `src/geometry/geometry_image_host.cpp` | `GeometryImageHost` 实现。 |
| `src/geometry/geometry_upload_host.cpp` | `GeometryUploadHost` 实现。 |
| `src/geometry/geometry_renderer_2d.cpp` | `GeometryRenderer2D` 实现。 |
| `src/geometry/geometry_renderer_3d.cpp` | `GeometryRenderer3D` 实现。 |

### 3.6 Surface 子系统 (`src/surface/`)

| 文件 | 说明 |
|------|------|
| `src/surface/surface_image_host.cpp` | `SurfaceImageHost` 实现。 |
| `src/surface/surface_upload_host.cpp` | `SurfaceUploadHost` 实现。 |
| `src/surface/surface_renderer_2d.cpp` | `SurfaceRenderer2D` 实现。 |
| `src/surface/surface_renderer_3d.cpp` | `SurfaceRenderer3D` 实现。 |

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
| `examples/sdl_text_demo.cpp` | `sdl_text_demo` | 2D 文本渲染演示 (SDF + 图集)。 |
| `examples/sdl_text_3d_demo.cpp` | `sdl_text_3d_demo` | 3D 文本渲染演示 (Billboard + 深度)。 |

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

| 文件 | 测试对象 |
|------|---------|
| `tests/cases/ecs_transform_camera_system_tests.cpp` | TransformSystem + CameraSystem 单元测试 |
| `tests/cases/ecs_bounds_system_tests.cpp` | BoundsSystem 单元测试 (AABB 变换) |
| `tests/cases/ecs_culling_system_tests.cpp` | CullingSystem 单元测试 (视锥体裁剪) |
| `tests/cases/ecs_geometry_component_tests.cpp` | GeometryComponent 单元测试 |
| `tests/cases/ecs_geometry_batch_system_tests.cpp` | GeometryBatchSystem 单元测试 |
| `tests/cases/ecs_geometry_runtime_system_tests.cpp` | GeometryRuntimeSystem 单元测试 |
| `tests/cases/ecs_surface_component_tests.cpp` | SurfaceComponent 单元测试 |
| `tests/cases/ecs_surface_batch_runtime_system_tests.cpp` | Surface 批次/运行时系统测试 |
| `tests/cases/ecs_surface_upload_plan_system_tests.cpp` | SurfaceUploadPlanSystem 测试 |
| `tests/cases/ecs_text_component_tests.cpp` | TextComponent 单元测试 |
| `tests/cases/ecs_text_runtime_system_tests.cpp` | TextRuntimeSystem 单元测试 |
| `tests/cases/ecs_text_render_3d_system_tests.cpp` | TextRender3DSystem 单元测试 |
| `tests/cases/freetype_host_tests.cpp` | FreeTypeHost 单元测试 |
| `tests/cases/glyph_atlas_host_tests.cpp` | GlyphAtlasHost 单元测试 |
| `tests/cases/frame_retire_host_tests.cpp` | FrameRetireHost 单元测试 |
| `tests/cases/vulkan_types_tests.cpp` | Vulkan 类型/结构体测试 |
| `tests/cases/runtime_configuration_tests.cpp` | RenderRuntimeHost 配置测试 |
| `tests/cases/runtime_integration_tests.cpp` | 运行时集成测试 |
| `tests/cases/runtime_text_renderer_integration_tests.cpp` | 2D TextRenderer 集成测试 |
| `tests/cases/runtime_text_renderer_3d_integration_tests.cpp` | 3D TextRenderer 集成测试 |
| `tests/cases/runtime_geometry_image_host_integration_tests.cpp` | GeometryImageHost 集成测试 |
| `tests/cases/runtime_geometry_material_host_tests.cpp` | GeometryMaterialHost 测试 |
| `tests/cases/runtime_geometry_renderer_2d_integration_tests.cpp` | GeometryRenderer2D 集成测试 |
| `tests/cases/runtime_geometry_renderer_3d_integration_tests.cpp` | GeometryRenderer3D 集成测试 |
| `tests/cases/runtime_surface_upload_host_integration_tests.cpp` | SurfaceUploadHost 集成测试 |
| `tests/cases/surface_upload_host_policy_tests.cpp` | Surface 上传策略测试 |

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
| `bench/support/bench_crash_tracer.hpp` | 崩溃追踪器集成头文件。 |
| `bench/support/bench_crash_tracer.cpp` | 崩溃追踪器集成实现。 |

### 7.2 基准测试用例 (`bench/cases/`)

| 文件 | 测试对象 |
|------|---------|
| `bench/cases/ecs_bounds_system_bench.cpp` | BoundsSystem 性能基准 |
| `bench/cases/ecs_culling_system_bench.cpp` | CullingSystem 性能基准 |
| `bench/cases/ecs_geometry_runtime_system_bench.cpp` | GeometryRuntimeSystem 性能基准 |
| `bench/cases/ecs_surface_runtime_system_bench.cpp` | SurfaceRuntimeSystem 性能基准 |
| `bench/cases/ecs_surface_upload_plan_system_bench.cpp` | SurfaceUploadPlanSystem 性能基准 |
| `bench/cases/ecs_text_component_bench.cpp` | TextComponent 性能基准 |
| `bench/cases/ecs_text_runtime_system_bench.cpp` | TextRuntimeSystem 性能基准 |
| `bench/cases/freetype_host_bench.cpp` | FreeTypeHost 性能基准 |
| `bench/cases/glyph_atlas_host_bench.cpp` | GlyphAtlasHost 性能基准 |
| `bench/cases/frame_retire_host_bench.cpp` | FrameRetireHost 性能基准 |
| `bench/cases/frame_sync_bench.cpp` | FrameSyncHost 性能基准 |
| `bench/cases/runtime_text_renderer_bench.cpp` | TextRenderer 性能基准 |
| `bench/cases/vulkan_types_bench.cpp` | Vulkan 类型操作性能基准 |

### 7.3 基准测试数据 (`bench/baselines/`)

| 文件 | 说明 |
|------|------|
| `ecs_geometry_runtime_gold.json` | 几何运行时的黄金基线数据 |
| `ecs_geometry_runtime_gate_latest.json` | 最新门控检查结果 |
| `ecs_core_cpu_baseline_*.json` | CPU 核心 ECS 基线快照 |
| `ecs_core_cpu_post_opt_*.json` | CPU 优化后快照 (含 `_snapshot.md`) |
| `ecs_font_quality_*.json` | 字体质量基准快照 |
| `ecs_geometry_runtime_*.json` | 几何运行时多阶段快照 |
| `ecs_surface_*.json` | Surface 系统基准快照 |
| `bench_progress_live.{log,err.log}` | 持续基准测试日志 |
| `_tmp_*.json` | 临时对比文件 (gitignore) |

---

## 8. docs/ — 文档

| 文件 | 说明 |
|------|------|
| `docs/architecture_manual.md` | 项目架构手册。 |
| `docs/file_index.md` | 代码文件索引 (本文件)。 |
| `docs/ecs_surface_design.md` | ECS Surface 组件设计文档。 |
| `docs/ecs_surface_renderer_integration_2026-04-27.md` | Surface 渲染器集成记录。 |
| `docs/ecs_surface_runtime_upload_integration.md` | Surface 运行时上传集成记录。 |
| `docs/ecs_surface_upload_optimization_2026-04-27.md` | Surface 上传优化记录。 |
| `docs/ecs_surface_upload_pipeline.md` | Surface 上传管线设计。 |
| `docs/runtime_hardening_todo.md` | 运行时加固 TODO 列表。 |

---

## 9. crash_reports/ — 崩溃日志

| 文件 | 说明 |
|------|------|
| `crash_reports/vr_bench_20260427_192543_pid24976_tid14548.log` | Benchmark 崩溃追踪日志 (2026-04-27)。包含 CrashTracer 捕获的堆栈回溯。 |

---

## 10. scripts/ — 辅助脚本

| 文件 | 说明 |
|------|------|
| `scripts/bench/README.md` | 基准测试脚本说明。 |
| `scripts/bench/run_bench_gate.ps1` | 基准测试门控脚本 (PowerShell)。对比当前结果与基线，门禁判断。 |
| `scripts/bench/new_golden_baseline.ps1` | 生成新黄金基线脚本。 |

---

## 11. tools/ — 工具

| 文件 | 说明 |
|------|------|
| `tools/spv_to_header.py` | SPIR-V 二进制 → C++ 头文件转换器。将 `glslangValidator` 编译产物嵌入 `const uint32_t[]` 数组。用于 CMake 着色器编译管道。 |

---

## 12. 文件统计

| 类别 | 文件数 |
|------|--------|
| 公开头文件 (.hpp) | 55 |
| 源文件 (.cpp) | 24 |
| 着色器 (.vert/.frag) | 12 |
| 示例文件 | 8 |
| 测试用例 | 27 |
| 基准测试用例 | 17 (含 13 bench cases + 4 support) |
| 文档 | 7 (含当前 2 份) |
| 脚本/工具 | 3 |
| 崩溃日志 | 1+ |
| **总计** | **154+** |
