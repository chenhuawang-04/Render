# Render Runtime v0.1 Contract

本文件只描述当前仓库中已经稳定下来的 **runtime / recorder / renderer** 约定，不涉及资产导入、UI、上层引擎调度。

## 1. 所有权边界

- `RenderRuntimeHost` 拥有：
  - platform window/surface
  - `VulkanContext`
  - `SwapchainHost`
  - `RenderLoopHost`
  - `GpuMemoryHost`
  - `UploadHost`
  - `DescriptorHost`
  - `PipelineHost`
  - `RenderTargetHost`
  - `RenderTargetPool`
  - `SamplerHost`
  - `FreeTypeHost`
  - `GlyphAtlasHost`
  - `GlyphUploadHost`

- 各类 renderer/recorder：
  - **不拥有** `VulkanContext`
  - **不拥有** runtime host 内部模块
  - 只在 `PrepareFrame/Record` 阶段借用 runtime 提供的上下文和模块指针

## 2. 生命周期顺序

稳定调用顺序：

1. `runtime.Initialize(...)`
2. `renderer.Initialize(...)`
3. 每帧 `runtime.Tick(recorder_or_renderer)`
4. `renderer.Shutdown(runtime.Context())`
5. `runtime.Shutdown()`

必须满足：

- renderer 的 `Shutdown(...)` **先于** runtime 的 `Shutdown()`
- runtime 关闭后，不可再访问任何 runtime 子模块引用

## 3. Text runtime feature contract

当前 `TextRenderer2D / TextRenderer3D` 明确要求：

- `required_vulkan13_features.dynamicRendering = VK_TRUE`
- `required_vulkan13_features.synchronization2 = VK_TRUE`

推荐入口：

```cpp
vr::render::RenderRuntimeHost<...>::CreateInfo create_info{};
vr::text::ApplyTextRuntimeFeatureContract(create_info);
```

或：

```cpp
auto create_info =
    vr::text::MakeDefaultTextRuntimeCreateInfo<
        vr::render::RenderRuntimeHost<...>::CreateInfo>();
```

## 4. Text runtime module contract

`TextRenderer2D / TextRenderer3D` 在 `PrepareFrame(...)` 阶段要求以下模块全部启用：

- `UploadHost`
- `DescriptorHost`
- `PipelineHost`
- `GpuMemoryHost`
- `FreeTypeHost`
- `GlyphAtlasHost`
- `GlyphUploadHost`

缺少模块或缺少 Vulkan 1.3 feature 时，会在 `PrepareFrame(...)` 直接抛出明确异常。

## 5. Submission contract

- ECS 只保存世界数据
- `RenderView / RenderScenePacket / SceneRecorder2D / SceneRecorder3D` 属于 runtime submission 层
- multi-view packet 当前已稳定支持：
  - `active_view`
  - `scene_view`
  - `overlay_view`
  - explicit scene/overlay targets

## 6. Upload contract

`UploadHost` 当前采用：

- per-frame command buffer
- persistent staging pages
- 可选 staging page growth

默认行为：

- 先使用基础 staging page
- 空间不足时，若允许增长，则自动补 staging page
- 若达到 page 上限，抛出带容量明细的异常

## 7. Diagnostics contract

`RenderRuntimeHost::CreateInfo::diagnostics.enable_frame_diagnostics = true` 时，
`RuntimeTickResult` 会回填：

- swapchain generation / image count / extent / format / present mode
- frame index / image index / submit values
- upload stats
- descriptor stats
- pipeline stats
- render target stats
- glyph atlas / glyph upload stats

默认关闭，关闭时不做 per-tick 诊断快照填充。
