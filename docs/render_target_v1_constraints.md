# RenderTarget v1 Constraints

本文档定义 RenderTarget v1 的工程边界，作为当前第一阶段实现的强约束。

## 1. 系统定位

- RenderTarget 属于 `vr/render` 运行时基础设施层。
- ECS 不拥有 Vulkan 图像本体；ECS 最多持有 `RenderTargetHandle`。
- `RenderTargetHost` 负责 persistent / history / imported target。
- `RenderTargetPool` 负责 transient target，但 v1 仅建立池化边界与生命周期语义，不实现同帧 aliasing。
- 所有 renderer 通过 Runtime 注入消费 RenderTarget 设施，不直接拥有 `VkImage` / `VkImageView` 的长期生命周期。

## 2. v1 状态跟踪粒度

- v1 仅实现 **whole-image state tracking**。
- 状态跟踪默认覆盖整个 image 的全部 mip / layer / aspect range。
- 虽然公开结构保留 `RenderTargetViewDesc` / `RenderTargetSubresourceRange`，但 v1 不保证复杂子资源状态一致性。
- v1 目标：
  - scene color
  - scene depth
  - swapchain imported target
  - postprocess ping-pong
  - basic offscreen depth

## 3. Imported swapchain ownership

swapchain import 必须遵循以下所有权规则：

- `SwapchainHost` 仍拥有 swapchain image 本体。
- v1 默认也由 `SwapchainHost` 拥有 swapchain image view。
- `RenderTargetHost` 只包装 imported image/view，不销毁外部拥有的对象。
- 若未来需要由 `RenderTargetHost` 为 imported image 创建 view，则必须显式使用：
  - `RenderTargetOwnership::imported_image_owned_view`
- 若 image 与 view 都由外部持有，则使用：
  - `RenderTargetOwnership::imported_image_imported_view`

因此，resize / shutdown 时必须区分：

- owned target：Host 负责 retire + destroy
- imported image + owned view：Host 只 retire/destroy view
- imported image + imported view：Host 不 destroy image/view

## 4. Descriptor cache key 约束

RenderTarget 相关 descriptor cache 不允许只以 `target + view` 作为 key。

v1 及后续阶段都必须把 **期望状态/布局语义** 纳入 key：

- `RenderTargetHandle`
- `RenderTargetViewDesc`
- `RenderTargetStateKind expected_state`
- `VkDescriptorType descriptor_type`

示例：

- `SceneColor` 作为 sampled image
- `SceneColor` 作为 storage image
- `SceneDepth` 作为 sampled depth

这些必须视为不同 descriptor 语义，不得混用。

## 5. Resize 语义

### 5.1 Persistent / History

- handle 保持稳定
- 底层 `VkImage` / `VkImageView` 可重建
- `resource_revision` 必须递增
- 旧资源通过 retire 语义延迟销毁

### 5.2 Imported swapchain target

- imported swapchain handle 视为 **frame-local / current-image only**
- swapchain recreate 后旧 imported handle 失效
- `FrameRecordContext` / runtime 每帧提供当前 imported target 句柄

### 5.3 Transient

- resize 后 transient pool 中旧资源必须标记过期
- 旧资源在安全 submit value 后 retire
- v1 不做同帧 aliasing

## 6. Transient aliasing 约束

v1 明确禁止：

- 同一帧内基于生命周期推断的 image memory aliasing
- 未经 RenderGraph / dependency proof 的显式 aliasing

v1 允许：

- 跨帧复用 transient target
- 按 desc bucket 复用等价资源

## 7. Dynamic Rendering 兼容签名

`RenderPassPreset` 是高层语义对象，不直接等价于 Vulkan `VkRenderPass`。

用于 PipelineHost 动态渲染兼容性判断的低层结构必须是：

- `RenderTargetPipelineSignature`

其至少包含：

- color formats
- color attachment count
- depth format
- stencil format
- sample count

PipelineHost 后续必须把它纳入 `VkPipelineRenderingCreateInfo` 兼容判断。

## 8. v1 不做的内容

以下内容在 v1 显式不实现：

- 完整 RenderGraph
- 同帧 aliasing
- 完整 per-subresource layout tracker
- 自动 dependency analysis
- 完整 mip-chain / array-subresource 可见性管理
- 强制迁移现有 `ShadowAtlasHost` / `SurfaceImageHost` / `GlyphAtlasHost`

## 9. v1 成功标准

RenderTarget v1 进入可用状态的最低标准：

- 可创建 persistent color/depth target
- 可 import swapchain image/view
- 可安全 resolve 到默认 view
- 具备 retire 语义
- runtime 可注入 `RenderTargetHost*` / `RenderTargetPool*`
- 可作为后续 offscreen + postprocess 链路的基础设施
