# 测试与基准框架架构（Test + Bench）

更新时间：2026-05-07

---

## 1. 目标

本框架围绕三条硬指标设计：

1. **可维护**：职责分层，执行流程配置化，减少脚本/命令散落。  
2. **可配置**：支持按标签、按 profile、按基线、按报告路径灵活运行。  
3. **可扩展**：新增测试集、新增性能门禁、新增 CI 阶段无需改核心 runner。  

---

## 2. 分层设计

### A) 执行内核层（C++）

- `vr_tests`（`tests/support/test_framework.*`）
- `vr_bench_runner`（`bench/support/bench_framework.*`）

职责：
- 用例注册、筛选、执行、结果汇总、JSON 报告生成。

### B) 编排配置层（JSON）

- `scripts/testing/quality_profiles.json`

职责：
- 定义“跑什么”“按什么顺序跑”“什么退出码视为成功”。

### C) 统一调度层（Python）

- `scripts/testing/vr_quality_runner.py`

职责：
- 读取 profile
- 注入变量
- 执行 step
- 收集 stdout/stderr 与 summary
- 形成单一入口（本地/CI 一致）

---

## 3. 关键原则

1. **Runner 保持稳定，策略外置**：  
   测试策略放在 profile 中，runner 保持“执行器”角色。

2. **显式失败策略**：  
   每个 step 都可声明 `allow_exit_codes` 与 `continue_on_failure`。

3. **可追溯性**：  
   每个 step 输出独立 `stdout/stderr` 日志，统一 summary JSON 记录执行元数据。

4. **渐进扩展**：  
   可先用 `test_unit`、`bench_smoke`，再扩展到 `quality_full` / nightly / release gate。

---

## 4. 标准产物

默认在 `${build_dir}/reports` 生成：

- `tests_*.json`：测试报告
- `bench_*.json`：基准报告
- `quality_*.json`：统一调度摘要
- `*.stdout.log` / `*.stderr.log`：step 日志

---

## 5. 典型执行流

### 本地快速回归

1. `test_unit`
2. `bench_smoke`

### 提交前回归

1. `test_unit`
2. `test_integration`
3. `bench_smoke`

### 性能门禁

1. `bench_gate_geometry`（与黄金基线比较）

---

## 6. 后续扩展建议

1. 新增 profile：`quality_nightly`、`quality_release`
2. 增加更多 bench gate：`text/surface/light/shadow`
3. 在 CI 中按分支策略选择 profile（PR 跑 fast，nightly 跑 full）
