# VulkanRender_New Test Framework

本目录是项目的原生单元/集成测试框架（无第三方测试库依赖）。

---

## 1. 结构

```text
tests/
  support/
    test_framework.hpp
    test_framework.cpp
  cases/
    *_tests.cpp
  test_main.cpp
  CMakeLists.txt
```

---

## 2. 能力

- 静态注册：`VR_TEST_CASE(Name, "tag1;tag2")`
- 断言宏：
  - `VR_CHECK(expr)`
  - `VR_CHECK_MSG(expr, msg)`
  - `VR_REQUIRE(expr)`
  - `VR_REQUIRE_MSG(expr, msg)`
  - `VR_SKIP(reason)`
- 过滤执行：
  - `--filter <pattern>`（子串或 `* ?` 通配）
  - `--include-tag <tag>`（可重复）
  - `--exclude-tag <tag>`（可重复）
- 结果输出：
  - 终端可读 summary
  - `--report-json <path>` 机器可读报告
- CI 友好：
  - `--fail-on-empty-selection`
  - `--return-on-all-skipped <code>`

---

## 3. CTest 分流

- `vr_tests.unit`：`--exclude-tag integration`
- `vr_tests.integration`：`--include-tag integration --return-on-all-skipped 125`
  - CTest 设置了 `SKIP_RETURN_CODE 125`

---

## 4. 新增测试用例

1. 在 `tests/cases/` 新建 `xxx_tests.cpp`
2. 包含框架头：

```cpp
#include "support/test_framework.hpp"
```

3. 声明用例：

```cpp
VR_TEST_CASE(MyFeature_basic_path, "unit;core;my-module") {
    VR_CHECK(true);
}
```

4. 在 `tests/CMakeLists.txt` 中将该文件加入 `vr_tests` 源列表

---

## 5. 常用命令

```powershell
# 列出测试
.\build\vr_tests.exe --list

# 仅跑 unit
.\build\vr_tests.exe --exclude-tag integration

# 仅跑 integration（全 skip 时返回 125）
.\build\vr_tests.exe --include-tag integration --return-on-all-skipped 125

# 输出 JSON 报告
.\build\vr_tests.exe --report-json .\build\reports\tests.json
```

---

## 6. 推荐执行入口

推荐通过统一编排器执行（含 test + bench）：

```powershell
python scripts/testing/vr_quality_runner.py --profile test_unit
python scripts/testing/vr_quality_runner.py --profile test_integration
```
