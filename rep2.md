# Render 项目 Bindless 全量重构规划书

## 0. 总结论

我建议把这次改造定义为 **“资源绑定层重构”**，而不是在现有 renderer 里局部替换 descriptor set。你的目标是“不计代价、最优设计”，那么正确终态应当是：

**所有可采样资源进入全局 Bindless Resource Table；所有渲染对象、材质、粒子实例、Surface 实例只携带资源 slot index；renderer 在 pass/pipeline 级别绑定一次全局表；shader 通过 `nonuniformEXT`/NonUniformResourceIndex 访问资源。**

参考文档已经指出：当前项目并不是真正 bindless，而是“按纹理/图像缓存 `VkDescriptorSet`，录制时按 batch 绑定”的传统模型；我又按当前 `master` 关键路径核查了一遍，结论仍然成立。当前 `master` 已是 `29fc1b2b789e59109c1145b8d5942276941e768f`，而参考文档基于更早的 `master@80ec57f`；这次 master 主要更新了 SkyEnvironment/Scene/IBL 相关文档与结构说明，并没有改变 bindless 主矛盾。 

---

# 1. 当前项目的真实状态

## 1.1 可以保留的基础设施

### VulkanContext：保留，并扩展为能力中心

`VulkanDeviceCreateInfo` 已经包含 `VkPhysicalDeviceVulkan12Features`、`VkPhysicalDeviceVulkan13Features` 和额外 `required_features_pnext`；`VulkanContext` 也已经暴露 `EnabledVulkan12Features()` / `EnabledVulkan13Features()`。这说明 descriptor indexing 特性链不需要推倒设备创建流程，只需要在其上增加查询、缓存、校验和启用策略。

### DescriptorHost：保留 layout cache，但重构生命周期

`DescriptorSetLayoutDesc` 已经有 `bindings`、`binding_flags`、`flags`，这对 `PARTIALLY_BOUND`、`VARIABLE_DESCRIPTOR_COUNT`、`UPDATE_AFTER_BIND` 等 bindless flag 非常关键；但 `DescriptorHost` 当前核心仍是 `AllocateSet(s)` + `UpdateSet()` + frame arena，不是全局 slot table。

`src/render/descriptor_host.cpp` 中 `BeginFrame` 会对当前 frame arena 内所有 descriptor pool 执行 `vkResetDescriptorPool`，这决定了现有 `VkDescriptorSet` 是 frame-local/transient 生命周期，不能承载长期存在的 bindless table。

### TextureHost / SurfaceImageHost：保留资源管理，但挂接 bindless slot

`TextureHost::TextureRecord` 当前管理 `TextureId`、image 信息、layout、`ImageResource`、revision、CPU snapshot 等，但没有 `BindlessSlot`、generation、descriptor write revision。

`SurfaceImageHost::ImageRecord` 同样管理 image id、尺寸、format、usage、layout、resource、revision，但也没有全局 descriptor slot。

这两者不是要推倒，而是要接入一个统一的 `BindlessResourceSystem`。

---

## 1.2 必须推倒的部分

### ParticleRenderer2D 的 per-frame texture set cache 必须删除

`ParticleRenderer2D` 现在有 `TextureSetEntry { texture_id, VkDescriptorSet }`、`AcquireTextureDescriptorSet()`、`frame_texture_sets`、`descriptor_set_bind_count` / `descriptor_set_update_count` 统计。

实现中 `PrepareFrame` 会清空本帧 `frame_texture_sets`，`Record` 逐 batch 调 `AcquireTextureDescriptorSet(active_frame_index, batch.texture_id)`，然后在 set0 调 `vkCmdBindDescriptorSets`；pipeline layout 里 set0 仍是 `descriptorCount = 1` 的 `COMBINED_IMAGE_SAMPLER`。这条路径必须整体替换为 `texture_slot`。

### SurfaceRenderer2D 的贴图 descriptor set cache 必须删除

`SurfaceRenderer2D` 当前也是 `TextureSetEntry { image_id, VkDescriptorSet }` + `AcquireTextureDescriptorSet()` + `frame_texture_sets`；同时保留了 lighting 的 frame-local descriptor set。

实现中甚至有一段注释明确说明：`DescriptorHost::BeginFrame` 会 reset 当前 frame arena，因此之前为该 frame 分配的 `VkDescriptorSet` 会失效，必须在每帧 Prepare 阶段强制失效本地缓存。这正是 bindless 持久表不能复用当前路径的直接证据。

### SurfaceRenderer3D 的 binding_key descriptor set cache 必须删除

`SurfaceRenderer3D` 当前用 `TextureSetEntry { binding_key, VkDescriptorSet }`，并通过 `AcquireTextureDescriptorSet(frame_index, texture_id, sampler_id)` 按 texture+sampler 组合缓存 descriptor set。好消息是 `Surface3DGpuInstance` / draw batch 已经有 `texture_id` 和 `sampler_id` 字段，非常适合直接替换成 `texture_slot` / `sampler_slot`。

`SurfaceRuntimeSystem` 的 GPU instance 结构也已经按 POD 数据流组织，说明把资源 id 写入 GPU instance 是当前架构认可的设计方式；bindless 化只是在这里把“资源 id”升级为“GPU 可直接索引的资源 slot”。

---

# 2. 终态架构设计

## 2.1 总体架构

推荐新增一个核心服务：

```cpp
vr::render::BindlessResourceSystem
```

它不是 renderer 的工具类，而是和 `DescriptorHost`、`TextureHost`、`SurfaceImageHost`、`SamplerHost` 同级的资源绑定中心。

终态分层如下：

```text
VulkanContext
  └── DescriptorIndexingCaps / DescriptorBufferCaps

DescriptorHost
  ├── TransientDescriptorChannel        // 保留现有 per-frame descriptor set
  └── PersistentBindlessChannel         // 新增：长期存在的全局资源表

BindlessResourceSystem
  ├── BindlessTable sampled_images_2d[]
  ├── BindlessTable sampled_images_cube[]
  ├── BindlessTable samplers[]
  ├── BindlessTable storage_images[]       // 后续
  ├── BindlessTable storage_buffers[]      // 后续 GPU-driven
  └── DeferredSlotRecycler

TextureHost / SurfaceImageHost / SkyEnvironmentGpuHost / IBLHost
  └── 持有 BindlessSlot，不直接持有 descriptor set

ParticleRenderer / SurfaceRenderer / BackgroundPass / SkyEnvironmentPass
  └── 只绑定全局表，并把 slot index 写入 GPU instance/material
```

Khronos 的 descriptor indexing 示例把 bindless 描述为：把 descriptor memory 看成巨大数组，绑定巨大 descriptor set 后在 shader 中索引资源；这正是本项目应采用的模式。([docs.vulkan.org][1])

---

## 2.2 Bindless 后端抽象

为了“不计代价、最优设计”，不要把当前设计锁死在 `VK_EXT_descriptor_indexing` 上。应当定义一个后端接口：

```cpp
class IBindlessTableBackend {
public:
    virtual BindlessTableHandle CreateTable(const BindlessTableDesc&) = 0;
    virtual BindlessSlot AllocateSlot(BindlessTableHandle) = 0;
    virtual void FreeSlotDeferred(BindlessTableHandle, BindlessSlot, uint64_t retire_value) = 0;
    virtual void QueueImageWrite(BindlessTableHandle, BindlessSlot, VkImageView, VkImageLayout) = 0;
    virtual void QueueSamplerWrite(BindlessTableHandle, BindlessSlot, VkSampler) = 0;
    virtual void FlushWrites(VulkanContext&, uint64_t safe_point) = 0;
    virtual VkDescriptorSet DescriptorSet(BindlessTableHandle) const = 0;
};
```

第一后端：`DescriptorIndexingBackend`。
第二后端：`DescriptorBufferBackend`，未来用 `VK_EXT_descriptor_buffer` 实现。Khronos 对 descriptor buffer 的说明明确提到，bindless 通过巨大 descriptor arrays 和普通整数索引减少 descriptor set 绑定，并把 draw/dispatch 与具体 descriptor binding 解耦；descriptor buffer 更适合后续全 bindless/GPU-driven 架构，但需要额外工具链和生态适配，所以它应作为后端，而不是第一阶段强依赖。([The Khronos Group][2])

---

# 3. Vulkan 能力设计

## 3.1 必需能力

必须新增：

```cpp
struct DescriptorIndexingCaps {
    bool supported = false;

    bool runtime_descriptor_array = false;
    bool descriptor_binding_partially_bound = false;
    bool descriptor_binding_variable_descriptor_count = false;

    bool sampled_image_array_non_uniform_indexing = false;
    bool sampler_array_non_uniform_indexing = false;

    bool sampled_image_update_after_bind = false;
    bool update_unused_while_pending = false;
    bool null_descriptor = false;

    uint32_t max_sampled_image_slots = 0;
    uint32_t max_sampler_slots = 0;
    uint32_t max_variable_descriptor_count = 0;
    uint32_t max_update_after_bind_sampled_images = 0;
};
```

最低要求：

```text
runtimeDescriptorArray
descriptorBindingPartiallyBound
descriptorBindingVariableDescriptorCount
shaderSampledImageArrayNonUniformIndexing
```

可选增强：

```text
descriptorBindingSampledImageUpdateAfterBind
descriptorBindingUpdateUnusedWhilePending
nullDescriptor
```

Vulkan descriptor indexing 相关特性需要通过 `VkDeviceCreateInfo::pNext` 启用；`runtimeDescriptorArray`、`descriptorBindingPartiallyBound`、`descriptorBindingVariableDescriptorCount` 等都是明确的 feature 位。([docs.vulkan.org][3])

## 3.2 必须查询 layout support

不能把容量写死为 64K 或 128K。必须在创建 bindless layout 前调用 `vkGetDescriptorSetLayoutSupport`，并读取 `VkDescriptorSetVariableDescriptorCountLayoutSupport::maxVariableDescriptorCount`。variable descriptor count 的实际分配还需要在 `VkDescriptorSetAllocateInfo::pNext` 挂 `VkDescriptorSetVariableDescriptorCountAllocateInfo`。([docs.vulkan.org][4])

推荐容量策略：

```text
Debug:
  sampled_image_2d: min(device_limit, 8192)
  sampler:          min(device_limit, 512)
  开启强校验、slot generation 检查、placeholder 检查

Release:
  sampled_image_2d: min(device_limit, 65536 或项目配置)
  sampler:          min(device_limit, 2048)
  只保留低成本 generation 检查和统计
```

---

# 4. DescriptorHost 重构方案

## 4.1 双生命周期模型

现有 `DescriptorHost` 保留，但内部拆成两条通道：

```cpp
class DescriptorHost {
public:
    // 旧路径：继续服务 UBO、lighting、临时 set、回退路径
    VkDescriptorSet AllocateSet(VulkanContext&, uint32_t frame_index, DescriptorSetLayoutId);
    void BeginFrame(VulkanContext&, uint32_t frame_index);

    // 新路径：bindless 持久表
    BindlessTableId CreateBindlessTable(VulkanContext&, const BindlessTableDesc&);
    VkDescriptorSet GetBindlessDescriptorSet(BindlessTableId) const;

    BindlessSlot AllocateBindlessSlot(BindlessTableId);
    void FreeBindlessSlotDeferred(BindlessTableId, BindlessSlot, uint64_t retire_value);

    void QueueBindlessImageWrite(BindlessTableId, BindlessSlot, VkImageView, VkImageLayout);
    void QueueBindlessSamplerWrite(BindlessTableId, BindlessSlot, VkSampler);
    void FlushBindlessWrites(VulkanContext&, uint64_t completed_submit_value);
};
```

关键规则：

```text
BeginFrame 只能 reset transient pools。
Persistent bindless pool 永不被 BeginFrame reset。
Bindless descriptor set 的销毁只能发生在 Shutdown 或完整 device idle 后。
Slot 回收必须走 retire value，不允许立即复用。
```

## 4.2 BindlessTable 内部结构

```cpp
struct BindlessSlot {
    uint32_t index = 0;
    uint32_t generation = 0;

    bool IsValid() const noexcept {
        return generation != 0;
    }
};

struct BindlessTable {
    DescriptorSetLayoutId layout_id{};
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;

    VkDescriptorType descriptor_type{};
    uint32_t capacity = 0;
    uint32_t live_count = 0;

    mc_vector<uint32_t> generations;
    mc_vector<uint32_t> free_list;
    mc_vector<uint8_t> initialized;
    mc_vector<uint64_t> last_write_revision;

    mc_vector<PendingDescriptorWrite> pending_writes;
    mc_vector<DeferredSlotFree> deferred_frees;
};
```

slot 0 必须永久保留为 placeholder：

```text
slot 0 = white texture / fallback normal / fallback sampler
invalid slot -> slot 0
removed resource -> slot 0
未加载完成资源 -> slot 0
```

原因很简单：`PARTIALLY_BOUND` 不是“可以随便写 null descriptor”，而是“未被动态访问到的 descriptor 可以不写”。为了 Debug 和跨驱动稳定，slot 0 placeholder 是最稳策略。Vulkan Guide 也明确区分了 partially bound、update-after-bind 和 non-uniform indexing 的具体规则。([docs.vulkan.org][5])

---

# 5. 资源层改造方案

## 5.1 TextureHost

新增：

```cpp
struct TextureBindlessState {
    BindlessSlot image_slot{};
    BindlessSlot default_sampler_slot{};
    uint32_t image_revision_written = 0;
    uint32_t sampler_revision_written = 0;
    uint64_t retire_value = 0;
};

struct TextureRecord {
    ...
    TextureBindlessState bindless{};
};
```

行为规则：

```text
UploadTexture 首次创建:
  1. 创建 ImageResource
  2. 上传并 barrier 到 shader_read_layout
  3. 分配 bindless image slot
  4. QueueBindlessImageWrite(slot, image_view, shader_read_layout)

UploadTexture 资源重建但 TextureId 不变:
  1. 复用 slot
  2. 更新 descriptor 到新 image view
  3. 旧 ImageResource 走 RetireBus

RemoveTexture:
  1. 将 slot descriptor 更新为 placeholder
  2. 延迟释放 slot
  3. 到 completed_submit_value 后 generation++ 并回收 index
```

## 5.2 SurfaceImageHost

`SurfaceImageHost` 做同样改造：

```cpp
struct SurfaceImageBindlessState {
    BindlessSlot image_slot{};
    uint32_t revision_written = 0;
};

struct ImageRecord {
    ...
    SurfaceImageBindlessState bindless{};
};
```

短期不建议把 `TextureHost` 和 `SurfaceImageHost` 合并。当前项目已有明确的资产纹理与 Surface image 两条数据流，直接合并会波及过大。最优折中是新增一个共用 adapter：

```cpp
class ImageBindlessAdapter {
public:
    static BindlessSlot EnsureTextureSlot(TextureHost&, TextureId);
    static BindlessSlot EnsureSurfaceImageSlot(SurfaceImageHost&, uint32_t image_id);
};
```

长期可以把它们统一到：

```cpp
GpuImageRegistry
  ├── AssetTextureDomain
  ├── SurfaceImageDomain
  ├── GlyphAtlasDomain
  ├── ShadowAtlasDomain
  ├── RenderTargetDomain
  └── EnvironmentDomain
```

---

# 6. Renderer 迁移方案

## 6.1 ParticleRenderer2D

当前必须删除：

```cpp
TextureSetEntry
frame_texture_sets
LowerBoundTextureSetIndex()
AcquireTextureDescriptorSet()
descriptor_image_write_scratch   // texture path 用不到
```

新增：

```cpp
struct Particle2DGpuInstance {
    ...
    uint32_t texture_slot;
    uint32_t sampler_slot;
};
```

或者，如果当前 batch 保证同一 batch 同纹理，可以先把 `texture_slot` 放入 draw batch，再通过 push constant 传入；但为了最终 GPU-driven 和 indirect draw，**更推荐实例数据直接携带 slot**。

Record 路径变为：

```cpp
vkCmdBindPipeline(...);

VkDescriptorSet bindless_sets[] = {
    bindless_system.SampledImageSet(),
    bindless_system.SamplerSet()
};

vkCmdBindDescriptorSets(
    cmd,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipeline_layout,
    0,
    std::size(bindless_sets),
    bindless_sets,
    0,
    nullptr
);

// 后续每个 batch 只 draw，不再 AcquireTextureDescriptorSet，不再 per texture bind
```

预期收益：

```text
descriptor_set_bind_count:
  旧：O(unique_texture_batches)
  新：O(pass_count / pipeline_layout_switch_count)

descriptor_set_update_count:
  旧：renderer 每帧按 texture 更新
  新：BindlessResourceSystem 按 changed resources 更新
```

## 6.2 SurfaceRenderer2D

推荐 pipeline layout：

```text
set 0 = global bindless sampled image table
set 1 = global bindless sampler table
set 2 = frame-local lighting set
```

也可以为了最小改动使用：

```text
set 0 = global bindless combined image sampler table
set 1 = lighting set
```

但最优设计是分离 image/sampler，原因是：

```text
combined image sampler 会把同一 image + 多 sampler 复制成多个 descriptor slot；
separate image/sampler 可以复用 sampler slot；
Surface3D 已有 texture_id + sampler_id 数据形态，天然适合分离表。
```

`Surface2DGpuInstance` 现在已有 `surface_id`、`material_id`、`atlas_page_id`、`source_kind` 等字段；建议新增明确字段，而不是强行复用语义：

```cpp
uint32_t image_slot;
uint32_t sampler_slot;
uint32_t material_slot;   // 后续材质表
uint32_t flags;
```

lighting set 暂时保留 transient descriptor，因为它是 per-frame buffer/cluster/shadow payload，生命周期和全局纹理表不同。不要一开始把 lighting、shadow、IBL、postprocess 全塞进同一张大表。

## 6.3 SurfaceRenderer3D

`Surface3DGpuInstance` 当前已经有：

```cpp
uint32_t texture_id;
uint32_t sampler_id;
```

直接替换为：

```cpp
uint32_t texture_slot;
uint32_t sampler_slot;
```

删除：

```cpp
TextureSetEntry { binding_key, descriptor_set }
frame_texture_sets
AcquireTextureDescriptorSet(texture_id, sampler_id)
```

Surface3D 是 bindless 收益最大的路径之一，因为 PBR/材质/IBL/透明排序最终都需要索引化资源。第一步只迁移 base color/default texture；第二步再接 normal/metallic/roughness/occlusion/emissive 材质槽。

## 6.4 ParticleRenderer3D

Particle3D 与 Particle2D 迁移一致，只是需要注意：

```text
depth pass / transparent pass 不能重复绑定 per texture set；
GPU simulation path 的 indirect draw 数据中也应携带 texture_slot；
排序 key 保持 blend/depth/texture locality。
```

## 6.5 SkyEnvironment / IBL / Background / Shadow

这些不应在 Phase 1 里强行一起迁移，但最终设计必须覆盖：

```text
SkyEnvironment:
  cubemap_slot
  equirect_slot
  atmosphere LUT slot

IBL:
  irradiance_cubemap_slot
  prefiltered_env_slot
  brdf_lut_slot

Background2D:
  sprite/image slot

ShadowAtlas:
  shadow_atlas_slot
  shadow_sampler_slot
```

当前 master 已经有 SkyEnvironmentGpuHost、SkyEnvironmentPass、IBL lazy pipeline 等方向性更新；这些模块未来很适合统一接入 bindless，但应在 Surface/Particle 主路径稳定后推进。

---

# 7. Shader ABI 设计

## 7.1 最小 combined sampler 版本

第一阶段可以采用：

```glsl
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D g_Textures[];

vec4 SampleTexture2D(uint textureSlot, vec2 uv) {
    return texture(g_Textures[nonuniformEXT(textureSlot)], uv);
}
```

## 7.2 最优 separate image/sampler 版本

终态推荐：

```glsl
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D g_Textures2D[];
layout(set = 1, binding = 0) uniform sampler   g_Samplers[];

vec4 SampleTexture2D(uint textureSlot, uint samplerSlot, vec2 uv) {
    return texture(
        sampler2D(
            g_Textures2D[nonuniformEXT(textureSlot)],
            g_Samplers[nonuniformEXT(samplerSlot)]
        ),
        uv
    );
}
```

Arm 对 descriptor indexing 的实践文章也强调：当一个 draw/dispatch 内不同 invocation 可能访问不同资源时，需要用 `nonuniformEXT` 或 HLSL 的 `NonUniformResourceIndex` 告知编译器；性能也会受索引发散影响。([Arm Community][6])

## 7.3 Shader 组织方式

新增：

```text
shaders/include/bindless.glsl
shaders/include/material_handles.glsl
shaders/include/resource_slots.glsl
```

不要让每个 shader 手写 `nonuniformEXT`。应封装：

```glsl
struct Texture2DHandle {
    uint textureSlot;
    uint samplerSlot;
};

vec4 Texture2DSample(Texture2DHandle h, vec2 uv);
vec3 Texture2DSampleNormal(Texture2DHandle h, vec2 uv);
```

这样后续从 descriptor indexing backend 切到 descriptor buffer backend，不会污染所有 shader。

---

# 8. 性能设计

## 8.1 CPU 侧收益

目标是把：

```text
每 batch:
  AcquireTextureDescriptorSet
  UpdateSet
  vkCmdBindDescriptorSets
```

变为：

```text
每 pass / pipeline layout:
  vkCmdBindDescriptorSets(global bindless tables)

每 changed resource:
  QueueDescriptorWrite
```

这正好符合 bindless 的核心价值：CPU 不再在 draw call 之间反复重绑资源，而是预先把资源放进大表，通过索引访问。Khronos sample 和 Arm 文章都把这一点作为 descriptor indexing/bindless 的主要动机。([docs.vulkan.org][1])

## 8.2 GPU 侧风险：索引发散

bindless 不是免费午餐。非一致索引会带来潜在发散成本。策略：

```text
Surface opaque:
  排序 key 优先 pipeline/material/texture locality，再考虑 depth

Surface transparent:
  必须保持正确透明排序，但可在同 depth bucket 内保留 material locality

Particle:
  blend mode + texture slot 聚类，减少同 wave 内随机纹理访问

3D material:
  material_slot 作为主索引，texture slots 存在 material buffer 中
```

## 8.3 Descriptor write 性能

不要每次资源上传都立即 `vkUpdateDescriptorSets`。推荐：

```cpp
struct PendingBindlessWrite {
    BindlessTableId table;
    uint32_t dst_array_element;
    VkDescriptorImageInfo image_info;
    uint32_t revision;
};
```

然后在 safe point 合批 flush：

```text
BeginFrame after fence completed:
  Collect retired slots
  Coalesce pending writes
  vkUpdateDescriptorSets batched
```

Khronos 的 stable descriptor update 教程强调，把 descriptor 更新集中在已知 safe point 能显著降低 synchronization/lifetime 复杂度；这与当前项目的 frame-in-flight/retire 体系最匹配。([docs.vulkan.org][7])

---

# 9. Debug / Release 双路径

## 9.1 Debug 路径

Debug 目标不是最高性能，而是快速发现错误：

```text
启用 GPU-assisted validation
slot generation 强校验
未初始化 slot 访问统计
所有 invalid handle 映射 slot 0
descriptor write revision 记录
resource_id -> slot -> image_view 反查表
每帧输出 bindless stats
RenderDoc-friendly debug names
```

统计：

```cpp
struct BindlessStats {
    uint32_t image_slot_capacity;
    uint32_t image_slot_live_count;
    uint32_t image_slot_pending_write_count;
    uint32_t image_slot_recycled_count;
    uint32_t stale_handle_reject_count;
    uint32_t placeholder_resolve_count;
    uint32_t descriptor_write_count;
};
```

Arm 文章也建议在 descriptor indexing 开发中配合 GPU-assisted validation 和 RenderDoc，因为 bindless 让“访问错资源”的问题更容易发生。([Arm Community][6])

## 9.2 Release 路径

Release 目标是最小 CPU overhead：

```text
slot handle 只做轻量 generation 检查
descriptor writes 合批
无锁或低锁 freelist
renderer 不保存 VkDescriptorSet cache
shader slot 直接来自 GPU instance/material buffer
```

Release 不应保留 renderer 层 texture descriptor cache。否则 bindless 改造会被旧模型拖回去。

---

# 10. 分阶段实施计划

## Phase 0：能力探测与基准插桩

目标：先让项目知道自己能不能 bindless，以及当前 descriptor bind/update 成本是多少。

改动：

```text
VulkanContext:
  + DescriptorIndexingCaps
  + descriptor indexing feature/property query
  + layout support query helper

RuntimeDiagnostics:
  + descriptor_set_bind_count 全局统计
  + descriptor_update_count 全局统计
  + per renderer bind/update heatmap
```

验收：

```text
能打印当前 GPU 是否支持 bindless required features。
能打印每帧 Particle/Surface descriptor bind/update 次数。
不改变渲染结果。
```

---

## Phase 1：DescriptorHost persistent bindless channel

目标：不迁移 renderer，先把全局 bindless table 做出来。

改动：

```text
DescriptorHost:
  + Persistent pool
  + BindlessTable
  + Variable descriptor count allocation
  + Slot allocator
  + Deferred slot recycle
  + Batched descriptor writes

Tests:
  + create bindless table
  + allocate/free slot
  + generation check
  + placeholder slot 0
  + vkGetDescriptorSetLayoutSupport capacity clamp
```

验收：

```text
BeginFrame 不影响 persistent bindless descriptor set。
slot 分配/回收通过 generation 防悬挂。
validation layer 无 descriptor lifetime error。
```

---

## Phase 2：TextureHost / SurfaceImageHost 接入 slot

目标：资源上传后自动进入 bindless table。

改动：

```text
TextureRecord:
  + TextureBindlessState

SurfaceImageHost::ImageRecord:
  + SurfaceImageBindlessState

Resource upload:
  + QueueBindlessImageWrite

Resource remove:
  + placeholder rewrite
  + deferred slot free
```

验收：

```text
上传纹理后可拿到 stable texture_slot。
重建纹理不改变 slot index，只更新 descriptor。
删除纹理后旧 slot 不会被立即复用。
```

---

## Phase 3：ParticleRenderer2D bindless 化

目标：第一条真实渲染路径落地。

改动：

```text
删除:
  TextureSetEntry
  frame_texture_sets
  AcquireTextureDescriptorSet

新增:
  Particle2DGpuInstance.texture_slot
  Particle2DGpuInstance.sampler_slot
  bindless.glsl include
  bindless particle shader

Record:
  每 pass 绑定一次 global table
  每 batch 不再 bind texture descriptor set
```

验收：

```text
渲染结果一致。
descriptor_set_bind_count 从 unique texture batch 级下降到 pass 级。
descriptor_set_update_count 不再由 ParticleRenderer2D 产生。
```

---

## Phase 4：SurfaceRenderer2D bindless 化

目标：迁移主 2D surface 路径，同时保留 lighting transient set。

改动：

```text
set0/set1:
  bindless texture/sampler table

set2:
  frame lighting descriptor set

Surface2DGpuInstance:
  + image_slot
  + sampler_slot

删除:
  texture descriptor set cache
```

验收：

```text
贴图、透明、混合、lighting、shadow atlas 功能不回退。
lighting descriptor set 仍按 frame 管理。
texture descriptor set bind 下降到 pass 级。
```

---

## Phase 5：ParticleRenderer3D / SurfaceRenderer3D bindless 化

目标：完成 3D 主路径。

改动：

```text
Surface3DGpuInstance.texture_id -> texture_slot
Surface3DGpuInstance.sampler_id -> sampler_slot

删除:
  binding_key -> descriptor_set cache
```

验收：

```text
opaque/transparent/depth pass 正确。
IBL 暂时可继续旧 descriptor set。
3D surface 不再按 texture+sampler 组合绑定 descriptor set。
```

---

## Phase 6：环境、IBL、Shadow、后处理统一接入

目标：完成“全项目资源绑定统一”。

迁移对象：

```text
SkyEnvironment:
  cubemap/equirect/atmosphere LUT

IBL:
  irradiance/prefiltered/brdf LUT

Background2D:
  sprite/surface image

Shadow:
  shadow atlas

Postprocess:
  bloom/composite/history buffers
```

此阶段可以开始引入：

```text
material_slot
environment_slot
shadow_atlas_slot
postprocess_source_slot
```

---

## Phase 7：Update-after-bind 与 Descriptor Buffer 后端

目标：进入最终高性能形态。

`update-after-bind` 需要同时满足 pool flag、layout flag、binding flag 和对应 feature。Vulkan Guide 对这三层 flag 的要求写得很明确：descriptor pool 要有 `UPDATE_AFTER_BIND` pool flag，layout 要有对应 create flag，binding 要有 `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`。([docs.vulkan.org][5])

推荐策略：

```text
默认:
  safe-point descriptor update

高端路径:
  update-after-bind streaming

终极路径:
  descriptor buffer backend
```

不要把 update-after-bind 当成 Phase 1 的前提。它是流送优化，不是 bindless 的第一步。

---

# 11. 最终代码形态示例

## 11.1 新增核心类型

```cpp
enum class BindlessTableKind : uint16_t {
    sampled_image_2d,
    sampled_image_cube,
    sampler,
    storage_image,
    storage_buffer,
};

struct BindlessHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
    BindlessTableKind table = BindlessTableKind::sampled_image_2d;

    bool IsValid() const noexcept {
        return generation != 0;
    }
};

struct GpuTextureHandle {
    uint32_t texture_slot = 0;
    uint32_t sampler_slot = 0;
};
```

## 11.2 Renderer 不再请求 descriptor set

旧：

```cpp
VkDescriptorSet descriptor_set =
    AcquireTextureDescriptorSet(frame_index, batch.texture_id);

vkCmdBindDescriptorSets(..., &descriptor_set, ...);
```

新：

```cpp
// Prepare 阶段
instance.texture_slot = bindless.ResolveTextureSlot(batch.texture_id).index;
instance.sampler_slot = bindless.ResolveSamplerSlot(default_sampler).index;

// Record 阶段
VkDescriptorSet sets[] = {
    bindless.SampledImageSet(),
    bindless.SamplerSet(),
};

vkCmdBindDescriptorSets(
    cmd,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipeline_layout,
    0,
    2,
    sets,
    0,
    nullptr
);
```

---

# 12. 测试与验收清单

| 测试                      | 目的                                         | 通过标准                                |
| ----------------------- | ------------------------------------------ | ----------------------------------- |
| Caps 探测测试               | 验证 descriptor indexing feature/property 查询 | 缺失必需 feature 时自动回退旧路径               |
| Layout support 测试       | 验证 variable descriptor count 容量钳制          | 超容量请求被钳制或拒绝                         |
| Persistent set 生命周期测试   | 验证 `BeginFrame` 不 reset bindless set       | 多帧后 set 仍有效                         |
| Slot generation 测试      | 防止 stale handle                            | 旧 generation 被拒绝或映射 placeholder     |
| Texture recreate 测试     | 验证 slot 稳定                                 | 同 `TextureId` 重建后 slot index 不变     |
| Texture remove 测试       | 验证 deferred free                           | GPU 完成前不复用 slot                     |
| Particle2D 集成测试         | 验证第一条路径                                    | descriptor bind 降为 pass 级           |
| Surface2D 集成测试          | 验证 lighting 共存                             | texture bindless，lighting transient |
| Surface3D 集成测试          | 验证 texture/sampler slot                    | 删除 binding_key descriptor cache     |
| GPU-assisted validation | 捕获错误索引/未初始化 descriptor                     | Debug 下可定位 slot/resource            |
| RenderDoc 检查            | 检查 descriptor table 内容                     | slot 0 placeholder，资源 slot 正确       |
| 压力测试                    | 大量纹理/频繁替换                                  | 无 use-after-free、无 descriptor 泄漏    |
| 跨厂商测试                   | NVIDIA/AMD/Intel                           | 结果一致，性能趋势正确                         |

---

# 13. 最重要的设计决策

## 决策 A：不要每个 renderer 一张 bindless 表

每 renderer 一张表会导致：

```text
TextureHost 无法统一管理 slot
同一 texture 在多个 renderer 重复占 slot
资源删除/重建需要广播到多个表
debug 难度成倍上升
```

应当是：

```text
全局 image table
全局 sampler table
renderer 只消费 slot
```

## 决策 B：不要一开始全量 update-after-bind

第一阶段只做 safe-point 更新。理由：

```text
现有项目已有 frame-in-flight / retire / completed_submit_value 体系；
safe-point 更新能最大化复用当前生命周期模型；
update-after-bind 会立刻放大资源销毁、descriptor 写入、pending command 的同步复杂度。
```

## 决策 C：不要长期使用 combined image sampler 作为最终形态

combined image sampler 适合第一阶段快速落地，但最终应切到：

```text
texture2D[] + sampler[]
```

这样能减少 sampler 重复，提高材质系统、3D PBR、IBL 的扩展性。

## 决策 D：旧 DescriptorHost 不删，但降级为 transient descriptor allocator

旧路径仍然有价值：

```text
per-frame lighting UBO/SSBO
调试回退路径
某些小型临时 pass
不支持 descriptor indexing 的设备
```

但 renderer 的贴图路径不能再依赖它。

---

# 14. 推荐最终里程碑

## 里程碑 A：项目具备 bindless 能力中心

```text
VulkanContext 能报告 DescriptorIndexingCaps
DescriptorHost 能创建 persistent bindless table
slot 0 placeholder 可用
```

## 里程碑 B：资源 Host 拥有 slot 生命周期

```text
TextureHost / SurfaceImageHost 上传资源后自动写入 bindless table
删除资源走 deferred slot free
```

## 里程碑 C：2D 路径完全 bindless

```text
ParticleRenderer2D 不再按 texture 绑定 descriptor set
SurfaceRenderer2D 不再按 image 绑定 descriptor set
lighting set 暂时保留 transient
```

## 里程碑 D：3D 路径完全 bindless

```text
ParticleRenderer3D / SurfaceRenderer3D 删除 frame_texture_sets
Surface3D texture_id/sampler_id 改为 slot
```

## 里程碑 E：全项目资源绑定统一

```text
SkyEnvironment / IBL / ShadowAtlas / Background / Postprocess 接入 bindless
```

## 里程碑 F：高端后端

```text
update-after-bind streaming
descriptor buffer backend
GPU-driven material/resource indirection
```

---

# 15. 工作量估计

按“不计代价、追求最优终态”估算：

| 阶段       | 工作内容                                           |      估计 |
| -------- | ---------------------------------------------- | ------: |
| Phase 0  | Caps、诊断、基准插桩                                   |  2–3 人日 |
| Phase 1  | DescriptorHost persistent bindless channel     |  5–8 人日 |
| Phase 2  | TextureHost / SurfaceImageHost slot 接入         |  4–6 人日 |
| Phase 3  | ParticleRenderer2D 迁移                          |  3–5 人日 |
| Phase 4  | SurfaceRenderer2D 迁移                           |  5–8 人日 |
| Phase 5  | Particle3D / Surface3D 迁移                      | 6–10 人日 |
| Phase 6  | SkyEnvironment / IBL / Shadow / Postprocess 接入 | 8–14 人日 |
| Phase 7  | update-after-bind + descriptor buffer backend  | 8–15 人日 |
| 测试与跨厂商验证 | validation、RenderDoc、压力、回归                     | 6–12 人日 |

**完整最优版总量：约 47–81 人日。**

如果只做“2D bindless + 旧路径回退”，可以压到 **18–30 人日**；但这不满足你这次“全面完整彻底”的要求。

---

# 16. 最终建议

这次 bindless 重构的核心不是 shader，也不是把 `descriptorCount = 1` 改成 `descriptorCount = 65536`。核心是建立一套新的资源绑定真相源：

```text
BindlessResourceSystem 是资源绑定唯一真相源。
TextureHost / SurfaceImageHost 负责资源对象。
DescriptorHost 负责 Vulkan descriptor 机制。
Renderer 只消费 slot，不拥有 texture descriptor set。
Shader 只通过 slot 访问资源。
```

真正应该推倒的是 renderer 层的 `frame_texture_sets` / `AcquireTextureDescriptorSet` / `binding_key -> descriptor_set` 模型；真正应该保留并增强的是 Host / Runtime / PrepareView 的分层思想。这样改完后，项目会从“传统 descriptor-set 渲染器”升级为“资源索引驱动的现代 Vulkan renderer”，并且为后续 GPU-driven、材质系统、纹理流送、IBL/Shadow 统一绑定和 descriptor buffer 后端留出完整架构空间。

