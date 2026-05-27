# 下一阶段完整规划书

  ## 1. 当前阶段判断

  当前主线应判断为：

  > RenderGraph Phase 8《Descriptor Lowering 与 Shader Contract》已经完成关键收尾，下一阶段应进入 Phase 9《Transient
  > Alias Allocator》

  ### 为什么这么判断

  结合当前提交 31da238 和已完成内容，Phase 8 的关键目标已经基本闭环：

  - DescriptorBindingSource::external_buffer 已接入 graph descriptor lowering
  - DescriptorBindingKind::storage_buffer / uniform_buffer 已进入 shader contract 语义
  - external buffer resolver registry 已贯通 builder / compiled graph / executor
  - executor 已支持 graph-owned transient descriptor staging
  - GraphCommandContext 已带 frame_index
  - SurfaceRenderer2D / SurfaceRenderer3D / GeometryRenderer3D / SkyEnvironmentPass 的 graph path 已切到 graph-owned
    descriptor contract
  - SceneRecorder3D 的 sky pre/post opaque graph path 已能声明 descriptor bindings
  - compile 阶段已区分：
      - 完整 shader contract layout
      - 当前 pass 实际 descriptor write batch
  - executor 已能按 layout + writes 联合构造 transient descriptor set

  这些内容已经对应上 rendergraph_final_development_plan.md 中 Phase 8 的核心验收条目：

  - shader binding 与 graph resource declaration 一致
  - 未声明资源绑定会失败
  - 声明未绑定会失败
  - descriptor writes 批量化
  - 移除 renderer-owned transient descriptor assumptions

  ———

  ## 2. 下一阶段名称

  > Phase 9：Transient Alias Allocator

  一句话目标：

  > 让 RenderGraph 基于资源 liveness / compatibility / footprint 生成真实的 transient allocation plan，并让 Vulkan
  > backend 实际把不重叠生命周期的 transient buffer / opt-in transient image 绑定到共享物理内存页，同时输出 alias
  > barrier、memory timeline 和 saved bytes 诊断。

  ———

  ## 3. 为什么现在必须先做 Phase 9

  ## 3.1 它是 Phase 10 的前置条件

  Phase 10 是 Async Compute / Transfer / Advanced Scheduling。
  但多队列调度要想正确，必须先知道：

  - 资源 first use / last use
  - 哪些逻辑资源会共享物理页
  - alias 后的 hazard 如何同步
  - queue overlap 是否会踩共享 page

  如果先做 Phase 10，再补 alias allocator，后面还得反过来重写 queue overlap 判断。

  ### 结论

  Phase 9 是 Phase 10 的真实依赖，不是可选前置。

  ———

  ## 3.2 它也是 Phase 11 的语义基础

  Phase 11 是 Native Pass Fusion / TBR 优化。
  而这类优化依赖编译器知道：

  - attachment 是否是纯 transient
  - 是否可以 lazy transient attachment
  - 后续会不会被 sampled / storage / readback
  - 是否可以安全 drop storeOp
  - local read / pass merge 会不会破坏 lifetime 或 alias barrier

  这些都建立在 完整的 transient allocation / physical lifetime 之上。

  ### 结论

  Phase 11 必须建立在 Phase 9 之后。

  ———

  ## 3.3 它是 Phase 12 清理旧架构的替代能力前提

  Phase 12 要清理旧架构，包括：

  - Scene / Feature 不再依赖 RenderTargetPool
  - 所有 transient resources 由 graph 管理
  - Scene 层不再手动 acquire transient target

  如果现在直接清理旧架构，而 graph 还没有真正接管 transient 生命周期，就会把旧能力删掉、但新能力还没补齐。

  ### 结论

  Phase 12 不能先做，必须先把 Phase 9 做成可运行的替代能力。

  ———

  # 4. 本阶段总体目标 / 非目标 / 边界

  ## 4.1 总体目标

  Phase 9 完成后，系统必须具备以下能力：

  ### 目标 A：生成真实 transient allocation plan

  基于：

  - CompiledRenderGraph
  - resource liveness
  - compatibility class
  - backend footprint / memory requirement

  输出：

  - page
  - offset
  - alias group
  - logical bytes
  - physical bytes
  - peak bytes
  - saved bytes

  ### 目标 B：实现真实 physical alias

  策略：

  - device-local transient buffer 默认参与 alias
  - transient image 采用 opt-in
  - imported / persistent / host-visible / mapped / no-alias 不参与
  - depth / MSAA / 特殊 format image 先保守

  ### 目标 C：alias barrier 真正落地

  要求：

  - 当前 AliasBarrierDecision.realized = false 必须变成真实可执行
  - Vulkan lowering 能产生对应 barrier / dependency
  - executor 在正确 pass 前发出

  ### 目标 D：graph path 不再依赖 RenderTargetPool

  要求：

  - graph-facing APIs 不再依赖 pool
  - pool 可以暂留给 legacy path
  - graph-only path 的 transient ownership 必须由 graph 接管

  ### 目标 E：完整 diagnostics

  至少可见：

  - logical transient bytes
  - physical transient bytes
  - peak live bytes
  - saved bytes
  - page count
  - alias barrier count
  - non-alias reason

  ———

  ## 4.2 非目标

  本阶段不要做：

  - Phase 10 async compute / transfer advanced scheduling
  - Phase 11 native pass fusion / local read / TBR store elimination
  - Phase 12 彻底删除 legacy SceneRecorder / RenderTargetPool
  - 重做 descriptor lowering / shader contract
  - D3D12 / Metal 后端
  - 跨帧 history resource alias
  - imported / persistent / external buffer alias
  - aggressive subresource-level alias
  - 在 graph frontend 引入 Vulkan 类型

  ———

  ## 4.3 边界

  必须守住：

  - include/vr/render_graph/* 继续 backend-neutral
  - Vulkan-specific memory requirement / bind / page realization 只能在 backend/resource table 层
  - CompiledRenderGraph 可持有 backend-neutral allocation plan，但不能持有 VkBuffer / VkImage / VkDeviceMemory
  - resource object 与 memory page ownership 必须分离
  - retire 必须基于 submit completion value，不能用固定帧数延迟

  ———

  # 5. 依赖顺序总览

  1. Phase 9 数据契约
  2. compatibility / footprint / liveness 统一模型
  3. CPU-only allocation plan + interval coloring
  4. memory timeline / diagnostics
  5. Vulkan low-level alias page ownership
  6. Vulkan transient buffer alias realization
  7. Vulkan opt-in image alias realization
  8. alias barrier realized + lowering + executor emission
  9. 移除 graph-facing RenderTargetPool 依赖
  10. runtime smoke / benchmark / regression 收尾

  ———

  # 6. 详细实施步骤

  ———

  ## Step 1：建立 Phase 9 数据契约与文件边界

  ### 要改什么

  新增核心文件：

  - include/vr/render_graph/alias_allocator.hpp
  - src/render_graph/alias_allocator.cpp
  - tests/cases/render_graph_alias_allocator_tests.cpp

  新增 backend-neutral 数据结构，至少包括：

  - ResourceFootprint
  - TransientCompatibilityKey
  - TransientAllocationRecord
  - TransientMemoryPage
  - TransientMemoryTimeline
  - TransientAllocationPlan

  并让 CompiledRenderGraph 暴露：

  const TransientAllocationPlan& TransientAllocations() const noexcept;

  建议新增独立入口，而不是把 Vulkan footprint 查询塞进 Compile()：

  TransientAllocationPlan BuildTransientAllocationPlan(...);

  ### 为什么这样设计

  这一步的核心是把：

  - 逻辑生命周期
  - 物理内存布局

  彻底分开。

  graph frontend 不知道 Vulkan memory requirement。
  backend 才知道 exact footprint。
  allocator 算法必须 CPU-only 可测。

  ### 完成判据

  - 新类型编译通过
  - graph debug / JSON 至少能输出空 plan
  - 空 graph 返回空 allocation plan
  - alias_allocator.hpp 不出现 Vk* 类型

  ### 风险点

  - 如果 Compile() 直接依赖 Vulkan，将污染整个 graph frontend
  - 如果 allocation plan 只藏在 backend，不进入 graph diagnostics，调试价值不足

  ———

  ## Step 2：把现有 alias candidate 逻辑从 BarrierPlan 中抽出

  ### 要改什么

  当前 barrier_plan.cpp 已有一套 alias 诊断逻辑：

  - compatibility class match
  - resource allows alias
  - aggregate liveness
  - overlap 判断
  - alias candidates
  - alias barrier decision

  这些必须迁移为 alias_allocator.cpp 的统一规则源。

  BarrierPlan 后续只负责消费 alias 决策，不再重新做 compatibility 判定。

  ### 为什么这样设计

  不能让系统里同时存在两套 alias 真相：

  - diagnostics 一套
  - allocator 一套
  - barrier planner 又一套

  否则后面 debug 会极其痛苦。

  ### 完成判据

  - BarrierPlan 内不再维护独立 alias compatibility 逻辑
  - alias candidate 规则统一来自 alias allocator
  - 输出仍可诊断 non-alias reason

  ### 风险点

  - 现有 barrier tests 输出顺序可能变化
  - 需要固定 deterministic 排序规则

  ———

  ## Step 3：定义 compatibility class 与 footprint provider

  ### 要改什么

  实现：

  TransientCompatibilityKey BuildCompatibilityKey(...);

  并分开设计 buffer / image 策略。

  ### Buffer 策略

  默认可 alias，但排除：

  - 非 transient
  - allow_alias == false
  - host-visible
  - persistently mapped
  - imported / external
  - dedicated allocation required
  - memory type incompatible
  - usage incompatible

  ### Image 策略

  必须 opt-in，默认保守。

  建议：

  - BufferDesc::allow_alias 保持默认 true
  - TextureDesc::allow_alias 改为默认 false，或通过 allocator config 强制 texture 需要显式 opt-in

  并新增：

  bool prefer_lazy_memory = false;

  ### Footprint provider

  做两层：

  1. CPU-only test provider
  2. Vulkan exact provider

  ### 为什么这样设计

  buffer alias 是低风险默认能力。
  image alias 风险高，必须 opt-in。
  否则 Phase 9 会过于激进。

  ### 完成判据

  - 每个 transient resource 都能生成 compatibility key
  - 不可 alias 有明确 reason
  - buffer 默认 alias 规则可测
  - image 未 opt-in 不 alias
  - image opt-in 后才进入 candidate

  ### 风险点

  - 改 TextureDesc::allow_alias 默认值会牵动既有测试和 graph desc builders
  - Vulkan exact footprint 需要底层 unbound resource 支撑

  ———

  ## Step 4：实现 interval coloring 与 memory page assignment

  ### 要改什么

  实现 CPU-only 核心算法：

  按 compatibility class 分组
  -> 计算 aggregate lifetime
  -> 按 size desc / first pass / resource index 排序
  -> first-fit interval coloring
  -> 分配 page
  -> 计算 logical / physical / saved / peak

  ### 关键设计选择

  本阶段采用 aggregate resource liveness，不要引入 version-level physicalization。

  也就是说，按 ResourceHandle 聚合 lifetime，而不是按每个 SSA version 单独物理化。

  ### 为什么这样设计

  这是当前阶段最稳妥的边界：

  - 不扩大到 version-level physical resource table 重写
  - 先解决最核心的 transient alias capability
  - deterministic、易验证、收益直接

  ### 完成判据

  - non-overlap transient buffers 能共享 page
  - overlap resources 不能共享
  - saved bytes 计算正确
  - peak live bytes 计算正确
  - 同一 page 内 lifetime 不重叠
  - 输出 deterministic

  ### 风险点

  - 如果直接按 version 分配，会把本阶段范围炸开
  - 如果忽略 queue overlap，Phase 10 会埋雷

  ———

  ## Step 5：加入 memory timeline、diagnostics 和 graph debug export

  ### 要改什么

  扩展：

  - CompiledRenderGraph::BuildDebugString()
  - CompiledRenderGraph::BuildJson()
  - runtime diagnostics

  新增 graph transient memory counters，例如：

  - logical total bytes
  - physical total bytes
  - peak live bytes
  - saved bytes
  - transient resource count
  - aliased resource count
  - page count
  - alias barrier count

  ### 为什么这样设计

  设计参考文档已经明确：

  > Graph viewer / DOT / barrier dump / memory peak 是必要基础设施

  Phase 9 没 diagnostics，就算实现了 alias 也几乎不可维护。

  ### 完成判据

  - debug string 能看 page / allocation / saved bytes
  - JSON 能机读 timeline
  - runtime diagnostics 能暴露 counters
  - Detailed 级别才输出高成本字符串

  ### 风险点

  - diagnostics 不能反向驱动 allocator 逻辑
  - 不能为了格式化输出让每帧产生高额 CPU 开销

  ———

  ## Step 6：扩展低层 Vulkan resource ownership，支持“对象独立、内存 page 共享”

  ### 要改什么

  当前 BufferHost::CreateBuffer() / ImageHost::CreateImage() 都是假设：

  - 创建对象
  - 立即分配/绑定独占内存
  - 销毁时释放自己的 allocation

  这不支持 alias。

  需要新增窄接口，让 resource object 和 memory ownership 分离。

  例如对 Buffer / Image 增加：

  - object-only create
  - footprint query
  - bind existing page/slice
  - object-only destroy

  并为资源增加 ownership 语义，例如：

  - owns_allocation
  - external_alias_page
  - none

  shared alias page 统一由 graph transient allocator 或 resource table 退休、释放。

  ### 为什么这样设计

  真实 alias 的本质是：

  - 多个 VkBuffer/VkImage object
  - 绑定到同一 memory page / offset
  - lifetime 不重叠
  - 通过 alias barrier 保证顺序

  如果 ownership 不拆开，必然 double-free。

  ### 完成判据

  - 普通资源创建/销毁路径不受影响
  - aliased resource object 可独立销毁但不释放 shared page
  - shared page 只释放一次
  - retire 基于 submit completion value

  ### 风险点

  - double-free
  - use-after-submit
  - 语义若做成隐式约定，后续维护会踩雷

  ———

  ## Step 7：接入 Vulkan transient buffer alias realization

  ### 要改什么

  改造 VulkanResourceTable::Resolve() 的 transient buffer 路径。

  新流程应变成：

  1. 收集所有 transient buffers
  2. 用 Vulkan exact footprint provider 生成 exact allocation plan
  3. 为每个 page 分配 alias memory
  4. 为每个 logical transient buffer 创建 VkBuffer object
  5. 绑定到共享 page + offset
  6. 写入 physical buffer records
  7. 更新 stats / diagnostics

  ### 为什么先做 buffer

  因为 buffer alias：

  - 不涉及 image layout
  - 不涉及 render target view
  - 不涉及 lazy memory
  - 风险最低
  - 最适合先把 Phase 9 跑通

  ### 完成判据

  - non-overlap transient buffers 共享同一 alias page
  - diagnostics 中 saved_bytes > 0
  - executor 可正常使用 aliased buffers
  - 无 double-free
  - validation clean

  ### 风险点

  - CPU estimate 与 Vulkan exact requirement 可能不一致，必须以后者为准
  - external buffer descriptor resolver 不能被误归类为 graph-owned transient buffer

  ———

  ## Step 8：接入 opt-in transient image alias 与 lazy transient attachment

  ### 要改什么

  改造 VulkanResourceTable::Resolve() 的 transient texture 路径。

  graph transient texture 不再直接走旧 CreateTransientTarget(...) 路径，而是：

  1. query exact image footprint
  2. allocator 分配 page
  3. 创建 image object
  4. bind shared page
  5. 创建 default image view
  6. 注册到 RenderTargetHost 或等价 physical texture record
  7. page 由 graph allocator retire

  并在 graph transient image 创建点显式标记：

  .allow_alias = true
  .prefer_lazy_memory = true // 仅适合的 attachment

  ### 为什么这样设计

  image alias 风险明显高于 buffer：

  - image layout
  - attachment state
  - load/store
  - bindless image slot
  - depth / MSAA / format compatibility
  - tile GPU lazy allocation

  所以必须 opt-in。

  ### 完成判据

  - opt-in postprocess / bloom image 能进入 alias plan
  - non-overlap intermediate image 能共享 page
  - depth/MSAA 默认仍保守
  - lazy memory 请求能进入 diagnostics
  - graph transient image 不再独占内存创建
  - validation clean

  ### 风险点

  - first use 不能继承前一个 aliased image 的 layout/state
  - RenderTargetHost 原本可能隐含 owns allocation 假设，需要拆开 ownership
  - bindless image slot 必须按 image view 更新，不能因为 memory page 共享而偷懒

  ———

  ## Step 9：让 alias barrier 从 required 变为 realized，并接入 Vulkan lowering / executor

  ### 要改什么

  当前 alias barrier 还是诊断性质。
  Phase 9 必须让它成为真实执行语义。

  BuildBarrierPlan() 应该消费 TransientAllocationPlan.alias_barriers，并在 next_first_pass 前插入 alias barrier。

  Vulkan lowering 规则：

  - same queue：降为 VkMemoryBarrier2
  - different queue：生成 queue dependency，但 Phase 9 仍保持串行保守，不假装 async overlap
  - image first use：按 fresh resource 处理，不继承前一个 aliased image layout

  ### 为什么这样设计

  “共享 page 但不插 barrier” 不算 allocator 完成。
  physical alias 必须配合同步语义。

  ### 完成判据

  - alias pair 的 realized == true
  - barrier debug / JSON 可见
  - Vulkan lowering 可输出 alias barrier
  - executor 在正确 pass 前发出
  - validation clean

  ### 风险点

  - barrier 放错位置
  - image layout 状态误复用
  - 跨 queue alias 过早放开

  ———

  ## Step 10：从 graph-facing APIs 中移除 RenderTargetPool 依赖

  ### 要改什么

  注意：不是现在删除整个 RenderTargetPool 类。
  而是要把它从 graph-facing path 中剥离出去。

  要求：

  - graph runtime service 不依赖 pool
  - graph feature builders 不依赖 pool
  - graph-only Scene2D / Scene3D path 不再 acquire transient target
  - transient resource 必须由 RenderGraphBuilder::CreateTexture/CreateBuffer 创建
  - physicalization 只能由 VulkanResourceTable + TransientAliasAllocator 完成

  建议建立 grep gate：

  rg -n "RenderTargetPool|AcquireTransientTarget|CreateTransientTarget" include\vr\render_graph src\render_graph

  graph core 必须无命中。

  ### 为什么这样设计

  Phase 9 验收项明确要求：

  > RenderTargetPool 不再被 Scene/Feature 层引用

  但真正删除 legacy 是 Phase 12。
  所以这一阶段做的是graph path 断依赖，不是全局删旧类。

  ### 完成判据

  - graph-facing code 无 pool 依赖
  - graph-only scene path 不调用 acquire transient target
  - legacy path 可保留，但严格隔离
  - graph diagnostics 取代 graph path 的 pool counters

  ### 风险点

  - 不要顺手删整个 legacy pool
  - 不要留下“参数还在但不再使用”的假清理

  ———

  ## Step 11：完成 runtime smoke、memory bench 与 Phase 8 回归闭环

  ### 要改什么

  最后一步不是继续加功能，而是让 Phase 9 真正可提交。

  需要完成：

  1. memory bench
      - render_graph_alias_allocator_bench
      - runtime_render_graph_memory_bench
  2. diagnostics 收口
      - baseline
      - saved bytes
      - page count
      - alias barrier count
  3. Phase 8 回归保护
      - descriptor lowering 不倒退
      - external buffer bindings 不被误纳入 graph transient alias

  ### 为什么这样设计

  Phase 9 的交付标准不是“allocator 单测过了”，而是：

  - graph path 真用了
  - Vulkan backend 真 alias 了
  - barrier 真发了
  - smoke 真稳定
  - saved bytes 真可见
  - Phase 8 没回退

  ### 完成判据

  - 全部新增 Phase 9 tests 通过
  - Phase 8 descriptor regression 通过
  - runtime graph smoke 通过
  - 至少一个 postprocess 或 shadow scene 中 saved_bytes > 0
  - 无 overlap alias bug
  - validation clean

  ### 风险点

  - 不能只靠 CPU tests 自证完成
  - 如果 saved bytes 为 0，要回头检查 graph transient image 是否没有 opt-in，或 liveness 是否过宽

  ———

  # 7. 测试 / 验证矩阵

  | 层级 | 验证目标 | 建议命令 / 内容 | 通过标准 |
  |---|---|---|---|
  | Build | 基础编译 | cmake --build build_preset/qa_debug --target vr_tests -j 8 | 编译通过 |
  | CPU 单元：allocator | compatibility / liveness / interval coloring | vr_tests --filter RenderGraphAliasAllocator_
  --fail-on-empty-selection | overlap 不 alias，non-overlap alias |
  | CPU 单元：timeline | logical / physical / peak / saved | vr_tests --filter RenderGraphMemoryTimeline_ --fail-on-
  empty-selection | 数值精确可断言 |
  | CPU 单元：barrier | alias barrier realized | vr_tests --filter RenderGraphAliasBarrier_ --fail-on-empty-selection |
  barrier 插在 next first use 前 |
  | Backend：Vulkan | exact footprint / page sharing / ownership | vr_tests --filter RenderGraphVulkanBackend_ --fail-
  on-empty-selection | shared page 生效，无 double-free |
  | Phase 8 回归 | descriptor lowering 不回退 | vr_tests --filter RenderGraphDescriptor_ --fail-on-empty-selection | 全
  通过 |
  | Scene graph builder | topology / liveness 稳定 | vr_tests --filter SceneRecorder3D_build_render_graph_ --fail-on-
  empty-selection | sky / bloom / overlay 图正确 |
  | Runtime smoke：3D | graph-only record 正常 | vr_tests --filter
  RuntimeIntegration_geometry_renderer_3d_graph_only_record_path_smoke --fail-on-empty-selection | 可执行稳定 |
  | Runtime smoke：2D | 2D graph path 不回退 | vr_tests --filter
  RuntimeIntegration_surface_renderer_2d_bindless_scene_packet_smoke --fail-on-empty-selection | 可执行稳定 |
  | Pool 清理 gate | graph core 无 pool 依赖 | rg -n "RenderTargetPool|AcquireTransientTarget|CreateTransientTarget"
  include\vr\render_graph src\render_graph | 无命中 |
  | Diagnostics | transient memory stats 可见 | 新增 runtime diagnostics tests | counters / detailed 输出正确 |
  | 性能 / 显存收益 | alias 有真实收益 | memory bench | saved_bytes > 0 且 CPU 不明显退化 |

  ———

  # 8. 本阶段完成后的验收标准

  本阶段完成后，必须满足以下条目。

  ## 8.1 compatibility class

  - allocator 有统一 compatibility key
  - CPU-only 可测
  - buffer / image 分开处理
  - memory type / usage / host visibility / format / sample / extent / dedicated requirement 都参与判断
  - 不可 alias 有明确原因

  ## 8.2 interval coloring

  - non-overlap transient resources 可共享 page
  - overlap resources 不共享
  - 输出 deterministic plan
  - 同一 page 内无 lifetime overlap

  ## 8.3 memory pages

  - allocation plan 有 page 概念
  - page 有 size / alignment / compatibility key / resource list
  - Vulkan backend 能将 transient buffer 绑定到 alias page
  - Vulkan backend 能将 opt-in transient image 绑定到 alias page

  ## 8.4 alias barrier

  - required == true 的实际 alias pair 必须 realized == true
  - barrier plan / JSON / debug string 可见
  - Vulkan lowering 发出正确 barrier / dependency
  - executor 在正确位置执行

  ## 8.5 image alias opt-in

  - texture 默认不激进 alias
  - bloom / postprocess / shadow 等明确安全的 graph transient image 可显式 opt-in
  - depth / MSAA / storage-sensitive image 继续保守
  - image first use 不继承前一个 aliased image 的 layout/state

  ## 8.6 buffer alias default

  - device-local transient buffer 默认参与 alias
  - host-visible / persistently mapped / imported / external / persistent buffer 不参与 alias
  - external descriptor buffer resolver 不会被误纳入 graph-owned transient alias

  ## 8.7 lazy transient attachment

  - texture desc 支持 prefer_lazy_memory
  - Vulkan backend 能报告 lazy requested / realized / unavailable reason
  - 不支持 lazy 时必须明确诊断，不能假装成功

  ## 8.8 memory timeline export

  - graph JSON / debug dump 有 transient allocations
  - runtime diagnostics 有 graph transient memory counters
  - 至少包含：
      - logical total bytes
      - physical total bytes
      - peak live bytes
      - saved bytes
      - alias group / page count
      - alias barrier count

  ## 8.9 graph-facing APIs 脱离 RenderTargetPool

  - include/vr/render_graph / src/render_graph 无 RenderTargetPool
  - graph-only scene/feature path 不调用 AcquireTransientTarget
  - legacy path 可以暂留，但必须与 graph path 隔离

  ## 8.10 场景收益

  至少一个 postprocess 或 shadow graph scene 中出现：

  alias saved bytes > 0

  并且：

  - 无 overlap alias bug
  - validation clean
  - runtime smoke 全过

  ———

  # 9. 本轮不要做什么

  为了避免漂移，这一轮明确不要做：

  1. 不做 Phase 10 async compute / transfer advanced scheduling
  2. 不做 Phase 11 native pass fusion / local read / TBR store elimination
  3. 不彻底删除 RenderTargetPool
  4. 不重做 descriptor lowering
  5. 不把 Vulkan 类型引入 graph frontend
  6. 不默认激进开启 image alias
  7. 不做 subresource-level alias
  8. 不用固定帧数延迟释放 alias page
  9. 不允许 alias plan realize 失败后静默 fallback
  10. 不允许为了测试伪造 saved bytes

  ———

  # 10. 如果现在就开工，第一刀先切哪里

  > 第一刀先切 alias_allocator：新增 include/vr/render_graph/alias_allocator.hpp、src/render_graph/alias_allocator.cpp、
  > tests/cases/render_graph_alias_allocator_tests.cpp，把现有 BarrierPlan 里的 alias candidate 逻辑抽成 CPU-only 的
  > BuildTransientAllocationPlan()，先让 non-overlap transient buffers 通过 interval coloring 共享同一逻辑 memory page，
  > 并输出 saved bytes / alias barrier decision。

  这是最稳、最干净、解锁最多下游工作的切入点。