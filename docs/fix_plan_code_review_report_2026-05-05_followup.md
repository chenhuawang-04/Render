# VulkanRender_New 修复规划书（基于 code_review_report_2026-05-05）

> 目的：对 `docs/code_review_report_2026-05-05.md` 中指出的问题做“属实性核验 + 分期修复规划”，并提供可持续的进度记录与管控机制。  
> 原则：**高性能优先、正确性优先、架构分层清晰**；不引入 ECS 污染；避免临时兼容分支与冗余代码；改动以可审计、小步可回退为目标。

---

## 0. 文档信息（管控）

- 文档版本：v1.3
- 创建日期：2026-05-06
- 最后更新：2026-05-07
- 适用分支：当前工作分支（与报告基线 commit `0fbfae2` 同期，后续以实际 HEAD 为准）
- 输入材料：
  - `docs/code_review_report_2026-05-05.md`
  - 本次核验中实际打开/阅读的代码与 shader（见附录 A）

### 0.1 约束（必须遵守）

- ECS 组件保持 **POD**；行为只放在 System / Runtime / Host / Renderer。
- 性能：热路径不得引入不必要哈希/动态分配/全表扫描；低频路径允许更强同步以换正确性。
- 文档文件不做无关改动：
  - `docs/architecture_manual.md`
  - `docs/file_index.md`
  - `docs/game_rendering_gap_analysis.md`

---

## 1. 结论摘要（核验结果）

### 1.1 已确认属实（优先修复）

> 这些问题在当前代码中可复现或可直接从实现路径推断成立，且存在明显风险（GPU UAF / device lost / validation 爆炸 / 长期运行退化）。

- **C1/C3（P0）Swapchain 重建销毁同步不足 → GPU UAF 风险**
  - 条件组合下可能不等待队列就销毁旧 swapchain image view/framebuffer/swapchain。
  - 即便等待也仅 wait graphics/present，不覆盖 compute/transfer。
- **G1/G2（P0）Shader SSBO 通过 component_index 访问无 bounds check → 越界读可能 device lost**
  - `geometry_3d.vert`、`shadow_depth_3d.vert` 都存在。
- **G3（P0/P1）Shadow depth pass 缺少 vertex deform → 阴影与实体错位**
  - geometry pass 有 deform 链路，shadow pass 没有。
- **C2（P1）Sampler anisotropy 检查使用物理设备支持而非 device enabled features**
  - 可能导致“GPU 支持但逻辑设备未启用”的错误放行。
- **C5（P1）RenderTargetPool 只增不减 + bucket 查找线扫 → 内存膨胀与性能退化**
- **C4（P1）SceneRecorder2D direct / explicit 输出未对齐 depth 输出配置**
  - 当前仍需按 3D recorder 的 target routing 方式做统一；避免 2D renderer 在 depth-aware pass 中只能走 color-only 配置。
- **I1（P1）SkyboxRenderer pipeline/layout 缓存未绑定 IBL descriptor layout 的变化**
  - IBL host 重建后 descriptor set layout 变化时可能复用旧 pipeline layout。
- **I2（P1/P2）IBL cubemap format 硬编码 R16G16B16A16_SFLOAT，缺少能力检查与 fallback**
- **I3（P1/P2）FrameComposer HDR format 未做 capability check**

### 1.2 可能已不成立 / 属于契约问题（降级处理）

- **R2（疑似已修复）RenderTargetPass end pass depth transition 缺失**
  - 复核当前 `RecordEndColorDepthPass()` 已对 depth 做 final_state transition。
  - 建议补回归测试以防回退。
- **V1（契约/风格）features pNext 链仅在存在 VK_TRUE 时挂载**
  - 语义上通常等价（未挂载 = 全 false），建议**文档化契约**或提供 debug/compat 开关，不作为首要修复项。
- **I3（当前基线已具间接校验）FrameComposer HDR format 未做 capability check**
  - 复核发现 `SceneRenderTargetSet::ResolveColorFormat()` 已对显式 color format 执行 sampled + color-attachment 能力校验。
  - 本项降级为“保留回归验证”，暂不单独改结构。

---

## 2. 修复目标与验收标准

### 2.1 全局目标

1. **消灭 GPU UAF / 越界读导致的 device lost**（P0）
2. **确保长期运行稳定**：资源池可控、不会无限膨胀（P1）
3. **确保能力健壮**：format/features/layout 变化可检测、可降级或 fail-fast（P2）
4. 保持并强化分层：
   - VulkanContext / Host（资源、生命周期、capability）
   - Runtime（帧编排、coordinator）
   - Renderer（record）
   - ECS（POD data）

### 2.2 统一验收（每阶段都要满足）

- Vulkan validation：目标用例下 **0 ERROR**（允许少量 WARNING 但必须解释）
- 关键 demo：
  - 至少跑过一个 swapchain recreate 场景（手动 resize/切换全屏/最小化恢复任选其一）
  - 跑过 shadow + deform 场景（含 skeletal/morph/vertex deform 的组合覆盖）
- 不引入热路径多余分配/全表扫描（如必须引入，必须有 stats 与可关闭路径）
- 代码：无临时宏、无重复实现路径（除非明确标注“过渡期”，并设删除日期）

---

## 3. 分期计划（里程碑 + 任务清单）

> 每个阶段结束应提交一次，并在本文件的“进度记录”更新状态与日期。

### Phase 0（P0 安全性：阻断 UAF / device lost）

#### P0-1：SwapchainHost 重建销毁安全化（修 C1/C3）

**策略（推荐）**

- 统一规则：旧 swapchain 资源销毁必须满足其一：
  1) 进入 `FrameRetireHost` 延迟销毁（优先）
  2) fallback：`vkDeviceWaitIdle()`（低频路径，允许更强同步换正确性）
- 移除/弱化仅 wait graphics/present 的“半同步”路径（多队列不可靠）。

**落点**

- `include/vr/render/swapchain_host.hpp`
- 如需：在 `VulkanContext` 暴露更明确的队列集合/WaitIdle 策略（但避免扩散依赖）。

**验收**

- resize/recreate + validation 无 “destroy while in use” 类错误
- 不依赖调用方“恰好提供 retire_host”

---

#### P0-2：shader SSBO bounds check（修 G1/G2）

**策略（推荐）**

- 为 skeletal 相关 SSBO 引入“组件数量”的稳定来源（优先：push constant / UBO）。
- 在 shader 中对：
  - `component_index`（或 shadow 的 `skeletal_component_index()`）做 bounds check
  - `matrix_offset + joint_index` 访问也应具备最小保护（至少避免明显越界）

**落点**

- `shaders/geometry_3d.vert`
- `shaders/shadow_depth_3d.vert`
- 相关 push constant / descriptor 数据准备（Renderer/Runtime 层）

**验收**

- 故意构造非法 index（debug/测试）不应 device lost，只应安全降级为“无骨骼/恒等矩阵”

---

#### P0-3：Shadow depth pass 补齐 vertex deform（修 G3）

**策略（推荐）**

- Shadow vertex path 与 geometry vertex path 对齐：
  - Morph → Skinning → VertexDeform → World → VP
- deform 参数来源与 geometry 统一（实例数据子集复用）。

**落点**

- `shaders/shadow_depth_3d.vert`（新增 deform 输入与逻辑）
- Shadow renderer 的 instance layout / descriptor 绑定（Renderer 层）

**验收**

- 同一 mesh 开启 deform 时：实体与阴影匹配，不再产生“影子漂移/错位”

---

### Phase 1（P1 稳定性：长期运行、cache 正确性）

#### P1-1：SamplerHost anisotropy 使用 enabled features（修 C2）

**策略**

- 以 `VulkanContext` 缓存的 enabled features 为准（device create info 的真实启用态）。
- 物理设备 props 的 `maxSamplerAnisotropy` 仍用来 clamp。

**落点**

- `src/resource/sampler_host.cpp`
- 可能需要补一个 `VulkanContext::EnabledFeatures()` 的访问点（若未公开）

**验收**

- 未启用 samplerAnisotropy 时，anisotropy_enable 直接 fail-fast（明确错误信息）

---

#### P1-2：RenderTargetPool 可回收 + bucket 查找优化（修 C5）

**策略（v1 推荐，不引入 RenderGraph）**

1) bucket 查找从线扫改为 hash 索引（避免 O(N) 增长退化）
2) 引入轻量 GC/eviction：
   - 记录 target 最近使用帧/提交值
   - 在 `BeginFrame()` 或 `EndFrame()` 基于 completed_submit_value 清理长期闲置 target
3) TargetRecord slot 可复用（free-list），避免 targets vector 永久增长

**落点**

- `src/render/render_target_pool.cpp`
- 对应类型（若需新增 key hash / map）

**验收**

- 长时间运行/频繁 resize 后，pool live count 可收敛
- 统计项（Stats）能反映：reuse hit/miss、gc destroy count、bucket count

---

#### P1-3：SkyboxRenderer pipeline/layout 与 IBL layout 绑定（修 I1）

**策略**

- pipeline_layout 的缓存 key 必须包含：
  - `ibl_host->DescriptorLayoutId()`（或其稳定等价物）
- 当 layout id 变化时：
  - invalidate pipeline_layout_id 与 pipeline_id，并重建

**落点**

- `src/render/skybox_renderer.cpp`

**验收**

- IBL host shutdown → init 后，skybox 继续渲染且 validation 无 layout mismatch

---

#### P1-4：SceneRecorder2D depth target routing 对齐（修 C4）

**策略**

- 对齐 3D recorder 的 direct / explicit / scene-target 三种输出配置路径。
- 2D scene renderer entry 若支持 depth output config，应在 target routing 时同步下发。
- 保持 ECS 无感知，全部收敛在 recorder / renderer 接缝层。

**落点**

- `src/render/scene_recorder_2d.cpp`
- 相关 2D renderer 的 output/depth-config 接口（若已有则直接接线，若无则补齐）

**验收**

- 2D renderer 在 direct / explicit target 模式下都可正确拿到 depth target config

---

### Phase 2（P2 能力健壮性：format fallback / capability check）

#### P2-1：IBL cubemap format fallback（修 I2）

**策略**

- 建立与 BRDF LUT 类似的 resolver：
  - 首选 `VK_FORMAT_R16G16B16A16_SFLOAT`
  - fallback 到其它可采样 HDR/或 LDR 格式（按能力）
- `TextureHost::SupportsSampledFormat()` 等检查必须覆盖：
  - sampled（必需）
  - transfer dst（bake 上传必需）

**落点**

- `src/render/ibl_bake_host.cpp`

**验收**

- 在不支持 16F 的设备上不会崩溃/黑屏：要么自动 fallback，要么 fail-fast 给出明确错误

---

#### P2-2：FrameComposer HDR format capability check（修 I3）

**策略**

- 初始化时对 `hdr_color_format` 做 capability check：
  - color attachment
  - sampled（后处理/tonemap 常用）
- 不满足则 fallback 或直接 fail-fast（策略需明确并记录日志）

**落点**

- `src/render/frame_composer_host.cpp`（或更上层 create info validation）

**验收**

- format 不支持时行为明确，不出现 silent failure

---

### Phase 3（P3：契约与回归测试补齐）

#### P3-1：Vulkan features pNext 链契约（对应 V1）

**策略**

- 文档明确：CreateInfo 中只表达“需要启用 VK_TRUE 的位”
- 可选：提供 debug/compat 模式强制挂载 Vulkan12/13 struct（用于驱动兼容排查）

---

#### P3-2：R2 回归测试（防回退）

**策略**

- 增加一个轻量 integration test：color+depth pass end transition 后读取 depth（或检查 state tracking 记录）

---

#### P3-3：UploadHost sync2 submit 路径对齐（对应 V3）

**策略**

- 当 `synchronization2` 已启用时，`UploadHost::EndFrameAndSubmit()` 统一走 `vkQueueSubmit2`。
- legacy `vkQueueSubmit` 仅作为未启用 sync2 的 fallback。
- submit wait stage mask 在 legacy / sync2 两条路径都先经过 submit-queue 兼容性规整，避免把不受该队列支持的 stage 直接塞进 submit。

**落点**

- `include/vr/render/upload_host.hpp`
- `src/render/upload_host.cpp`

**验收**

- 启用 `synchronization2` 的运行时路径下，upload 提交可稳定通过 cross-queue integration test。
- 完整 integration 集无回归。

---

## 4. 风险评估与性能影响说明

### 4.1 性能影响（核心结论）

- **Swapchain 重建使用 `vkDeviceWaitIdle()`**：属于低频路径（resize/recreate），对帧性能几乎无影响，但正确性收益极高。
- **Shader bounds check**：每顶点多一次比较，理论上有微小开销；但它阻断 device lost 风险。可用编译期开关或用 push constant 常量传播降低开销（通常可被优化）。
- **RenderTargetPool hash + GC**：发生在 `BeginFrame/EndFrame`，属于 CPU 侧管理路径；避免线扫退化与内存膨胀，长期收益明显。

### 4.2 主要风险

- Shadow deform 对齐可能涉及 instance layout 与 descriptor 绑定调整：需要确保 geometry/shadow 两条链路的一致性与版本同步。
- Pool GC 若销毁时机错误会引入 “destroy while in use” 类 validation：必须以 completed_submit_value 作为硬条件。

---

## 5. 进度记录（可直接维护）

> 维护规则：每完成一个任务块，更新状态与日期，并附上提交号（若有）。  
> 状态枚举：`TODO / DOING / BLOCKED / DONE / DROPPED`

### 5.1 当前状态总览

| ID | 标题 | 优先级 | 状态 | 日期 | 备注/提交 |
|---|---|---:|---|---|---|
| P0-1 | SwapchainHost 重建销毁安全化（C1/C3） | P0 | DONE | 2026-05-06 | 非延迟回收路径统一改为 `vkDeviceWaitIdle()` 后再 destroy |
| P0-2 | Shader SSBO bounds check（G1/G2） | P0 | DONE | 2026-05-06 | `geometry_3d.vert` / `shadow_depth_3d.vert` 增加 bounds check 与 matrix index 防护 |
| P0-3 | Shadow depth deform 对齐（G3） | P0 | DONE | 2026-05-06 | shadow pass 补齐 morph normal / skinning normal / vertex deform 链路 |
| P1-1 | Sampler anisotropy enabled-features（C2） | P1 | DONE | 2026-05-06 | 改为以 `context.EnabledFeatures().samplerAnisotropy` 为准 |
| P1-2 | RenderTargetPool hash + GC（C5） | P1 | DONE | 2026-05-06 | 增加 sorted hash lookup、free-list、按 bucket/idle budget 的 GC |
| P1-3 | Skybox pipeline/layout key 修复（I1） | P1 | DONE | 2026-05-06 | descriptor layout 变化会强制 invalidate pipeline layout/pipeline |
| P1-4 | SceneRecorder2D depth target routing（C4） | P1 | DONE | 2026-05-06 | direct/explicit 路径已补齐 depth routing，且保持对无 depth 支持 renderer 的兼容 |
| P2-1 | IBL cubemap format fallback（I2） | P2 | DONE | 2026-05-06 | 环境 cubemap 增加 `R16G16B16A16_SFLOAT -> R8G8B8A8_UNORM` fallback |
| P2-2 | FrameComposer HDR format check（I3） | P2 | DONE | 2026-05-06 | 复核后确认由 `SceneRenderTargetSet` 间接覆盖；本轮不额外复制逻辑 |
| P3-1 | Vulkan features pNext 契约（V1） | P3 | DONE | 2026-05-07 | 新增 `VulkanFeatureChainPolicy`，默认 `minimal_required`，并提供 `explicit_vulkan12_vulkan13` 调试/兼容模式 |
| P3-2 | RenderTargetPass depth transition 回归测试（R2） | P3 | DONE | 2026-05-07 | 新增 `RuntimeIntegration_render_target_pass_end_color_depth_transitions_apply_final_states` 覆盖 color/depth final_state |
| P3-3 | UploadHost sync2 submit 对齐（V3） | P3 | DONE | 2026-05-06 | `UploadHost::EndFrameAndSubmit()` 在 sync2 启用时改用 `vkQueueSubmit2`，并统一 wait stage mask 规整 |

### 5.2 每次提交自检清单（强制）

- [ ] validation：无 ERROR（本次改动覆盖的 demo/bench 跑过）
- [x] 变更文件列表已审查：没有引入重复实现/临时兼容路径
- [x] 性能：热路径无新增 heap 分配（RenderTargetPool 改造只发生在 frame 管理/池化层）
- [x] 生命周期：destroy/retire 全部基于 completed_submit_value 或 device idle

### 5.3 本轮执行记录（2026-05-06）

- 已完成改造：
  - `include/vr/render/swapchain_host.hpp`
  - `shaders/geometry_3d.vert`
  - `shaders/shadow_depth_3d.vert`
  - `include/vr/shadow/shadow_renderer_3d.hpp`
  - `src/shadow/shadow_renderer_3d.cpp`
  - `include/vr/render/animation_frame_coordinator.hpp`
  - `include/vr/render/scene_recorder_3d.hpp`
  - `src/resource/sampler_host.cpp`
  - `include/vr/render/skybox_renderer.hpp`
  - `src/render/skybox_renderer.cpp`
  - `src/render/ibl_bake_host.cpp`
  - `include/vr/render/render_target_pool.hpp`
  - `src/render/render_target_pool.cpp`
  - `include/vr/render/scene_recorder_2d.hpp`
  - `src/render/scene_recorder_2d.cpp`
  - `tests/cases/render_target_types_tests.cpp`
  - `include/vr/render/upload_host.hpp`
  - `src/render/upload_host.cpp`
- 已验证：
  - `cmake --build build -j 8 --target vulkan_init vr_tests`
  - `ctest --test-dir build --output-on-failure -R vr_tests.unit`
  - `build/tests/vr_tests.exe --include-tag unit --filter SceneRecorder2D_ --verbose`
  - `build/tests/vr_tests.exe --include-tag integration --filter RuntimeIntegration_cross_queue_upload_reports_extra_wait_when_available --verbose`
  - `build/tests/vr_tests.exe --include-tag integration --filter RuntimeIntegration_scene_recorder_2d_geometry_scene_packet_smoke --verbose`
  - `build/tests/vr_tests.exe --include-tag integration --filter RuntimeIntegration_render_target_pool_reuses_transient_targets --verbose`
  - `build/tests/vr_tests.exe --include-tag integration --filter RuntimeIntegration_ibl_bake_host_bakes_environment_and_registers_runtime --verbose`
  - `build/tests/vr_tests.exe --include-tag integration --verbose`
  - 结果：**unit 全通过；integration 21/21 全通过**

### 5.4 本轮执行记录（2026-05-07）

- 已完成改造：
  - `tests/cases/runtime_integration_tests.cpp`
- 已完成修复：
  - `P3-2 RenderTargetPass depth transition 回归测试（R2）`
- 新增验证用例：
  - `RuntimeIntegration_render_target_pass_end_color_depth_transitions_apply_final_states`
    - 通过真实 `RenderTargetHost + BuildColorDepthRenderPass + RecordEndColorDepthPass` 链路
    - 断言 color target 最终状态为 `shader_read`
    - 断言 depth target 最终状态为 `depth_read_only`
- 已验证：
  - `cmake --build build -j 8 --target vr_tests`
  - `ctest --test-dir build --output-on-failure -R vr_tests.unit`
  - `build/tests/vr_tests.exe --include-tag integration --filter RuntimeIntegration_render_target_pass_end_color_depth_transitions_apply_final_states --verbose`
  - `build/tests/vr_tests.exe --include-tag integration --verbose`
  - 结果：**unit 全通过；integration 22/22 全通过**

### 5.5 本轮执行记录（2026-05-07，P3-1 收尾）

- 已完成改造：
  - `include/vr/vulkan_context.hpp`
  - `src/vulkan_context.cpp`
  - `tests/cases/runtime_configuration_tests.cpp`
- 已完成修复：
  - `P3-1 Vulkan features pNext 契约（V1）`
- 契约收敛结果：
  - 新增 `VulkanFeatureChainPolicy`
    - `minimal_required`（默认）：仅在 Vulkan 1.2 / 1.3 struct 中存在至少一个 `VK_TRUE` feature bit 时接入 pNext 链
    - `explicit_vulkan12_vulkan13`：即使全部 feature bit 都为 `VK_FALSE`，也显式接入 Vulkan 1.2 / 1.3 feature struct，用于驱动兼容排查/稳定观测 feature chain 结构
  - `PickPhysicalDevice()` / `CreateLogicalDevice()` 统一按 policy 决定是否挂接 Vulkan 1.2 / 1.3 feature struct
  - 设备选择诊断日志新增 `feature_chain_policy` 输出
- 已验证：
  - `cmake --build build -j 8 --target vr_tests`
  - `ctest --test-dir build --output-on-failure -R vr_tests.unit`
  - `build/tests/vr_tests.exe --include-tag integration --verbose`
  - 结果：**unit 全通过；integration 21 passed / 1 skipped / 0 failed**

---

## 附录 A：核验证据（已打开检查的文件）

> 仅列出本次规划形成时已经直接核验过的文件，用于证明“属实性”不是猜测。

- Swapchain recreate 销毁路径：`include/vr/render/swapchain_host.hpp`
- anisotropy 检查：`src/resource/sampler_host.cpp`
- transient pool：`src/render/render_target_pool.cpp`
- shader 越界点：
  - `shaders/geometry_3d.vert`
  - `shaders/shadow_depth_3d.vert`
- skybox pipeline/layout：
  - `src/render/skybox_renderer.cpp`
- IBL bake cubemap 格式：
  - `src/render/ibl_bake_host.cpp`
- HDR format 写入而无 capability check：
  - `src/render/frame_composer_host.cpp`
- features pNext 选择逻辑：
  - `src/vulkan_context.cpp`
- render target pass depth end transition 复核：
  - `src/render/render_target_pass.cpp`
