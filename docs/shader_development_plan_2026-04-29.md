# Shader 开发规划（2026-04-29）

> 目标：在不破坏当前 ECS/Runtime 分层的前提下，把 Shader 资产链升级到“可扩展、可验证、可持续优化”的工程级体系。

---

## 1. 规划边界（本轮落地）

本轮优先做 **Phase-1 基础设施**，不直接改动大规模运行时渲染逻辑：

1. 编译产物增加反射 JSON（SPIR-V -> Reflection）。
2. 增加 GLSL 与反射结果的一致性校验（set/binding/push_constant）。
3. 在 CMake 中接线开关，保证可关闭时零额外运行时开销。

---

## 2. CMake 开关策略

新增/使用开关：

- `VR_ENABLE_SHADER_REFLECTION`
  - 作用：生成 `*.reflect.json`。
  - 默认：仅在检测到 `spirv-cross` 时默认开启。
- `VR_ENABLE_SHADER_ABI_CHECK`
  - 作用：构建期执行严格 contract check（失败即中断）。
  - 默认：开启；但若关闭 reflection，会自动禁用并给出提示。
- `VR_ENABLE_SHADER_OPTIMIZATION`
  - 作用：在 glslang 后执行 `spirv-opt`（优化最终 SPIR-V 再进入后续反射与嵌入）。
  - 默认：关闭（便于调试）；可在性能构建中启用。
- `VR_SHADER_OPTIMIZATION_LEVEL`
  - 作用：配置 `spirv-opt` 优化参数（例如 `-O`、`-Os`）。
  - 默认：`-O`。

说明：

- 这些开关仅影响 **构建期** shader 资产链。
- 对运行时帧循环、ECS 热路径、renderer 分发路径无额外成本。

---

## 3. 产物布局

每个 shader 会生成：

- `build/generated/<symbol>.spv`
- `build/generated/<symbol>.reflect.json`（可选）
- `build/generated/<symbol>.contract.json`（可选）
- `build/generated/vr/.../<symbol>.hpp`

其中：

- `reflect.json`：供工具链、CI、后续自动布局生成使用。
- `contract.json`：供 ABI 审查与回归对比使用。
- `*.raw.spv`：glslang 原始输出（作为优化输入）。

---

## 4. 工具脚本职责

### `tools/spv_reflect_to_json.py`

- 调用 `spirv-cross --reflect`。
- 归一化输出结构（descriptor、push constants、entry points）。
- 保证输出可排序、可比对、可作为后续代码生成输入。

### `tools/shader_contract_check.py`

- 从 GLSL 源提取声明的 `(set,binding)` 与 `push_constant`。
- 与 `reflect.json` 对比：
  - 缺失绑定
  - 额外绑定
  - push constant 数量不一致
- 支持 `--strict` 失败即返回非 0。

---

## 5. 阶段路线（后续）

### Phase-2（建议）

1. 抽取共享 shader include（光照/阴影采样逻辑去重）。
2. 为关键 pass 增加 layout/static_assert 对齐审计。
3. 引入 `spirv-opt` release 优化链（可配置）。

### Phase-3（建议）

1. 增加 compute shader 路线（cluster build/prepare）。
2. Shader 变体规则从“手工散落”收敛到统一 key 管理。
3. 将 reflection 结果接入 CI artifact 与回归基线。

### Phase-4（建议）

1. 评估 Slang 或 SPIR-V-first 多后端资产策略。
2. 建立 Vulkan/D3D/Metal/WebGPU 的统一参数块约束文档。

---

## 6. 需要准备的工具库（你可以先准备）

### 当前立即需要

1. **spirv-cross**
   - 用途：反射 JSON 生成。
   - 建议：使用与 Vulkan SDK 同版本工具链。

2. **glslangValidator**
   - 用途：GLSL -> SPIR-V。
   - 当前项目已依赖 Vulkan SDK，自带即可。

### 下一阶段建议准备（非阻塞）

1. **spirv-opt**（SPIRV-Tools）
   - 用途：release 构建的 shader IR 优化。

2. **slangc**（如进入多后端阶段）
   - 用途：统一 shader 源 + 多后端生成。

---

## 7. 质量门禁（建议纳入 CI）

1. `vr_generated_shaders` 必须可在无手工步骤下生成完整产物。
2. `contract.json` 必须全部 `passed=true`。
3. 任意 shader 改动必须伴随 reflect/contract 产物可追踪变化。
4. 渲染回归以功能 demo + benchmark 双通道验证。

---

## 8. Phase-2B 已落地项（当前仓库状态）

1. **Shader include 模块化接线已完成**
   - `glslangValidator` 编译命令增加 include 搜索路径：
     - `shaders/`
     - `shaders/include/`
   - 新增 `shaders/include/**/*.glsl` 自动依赖追踪（include 文件修改可触发重编译）。

2. **首批公共 shader 片段已落地**
   - `shaders/include/vr/common/math.glsl`
   - `shaders/include/vr/text/text_shading.glsl`
   - `text_2d.frag` / `text_3d.frag` 已改为复用统一文本着色逻辑，避免重复维护。

3. **默认链路与优化链路双验证通过**
   - 默认：`VR_ENABLE_SHADER_OPTIMIZATION=OFF`
   - 优化：`VR_ENABLE_SHADER_OPTIMIZATION=ON`
   - 两条链路均通过 `vr_generated_shaders`，并保持 ABI contract 全通过。
