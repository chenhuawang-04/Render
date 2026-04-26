# VulkanRender_New 测试框架说明

## 目标

该目录提供一个**无第三方依赖**的轻量测试框架，重点满足：

- 可维护：注册、执行、报告职责分离；
- 可扩展：支持标签筛选、名称筛选、JSON 报告；
- 可集成：可直接接入 CTest（unit / integration 分流）。

---

## 目录结构

```text
tests/
  support/
    test_framework.hpp
    test_framework.cpp
  cases/
    *.cpp
  test_main.cpp
  CMakeLists.txt
```

---

## 核心能力

- `VR_TEST_CASE(name, "tag1;tag2")`：静态注册测试
- 断言宏：
  - `VR_CHECK(expr)`
  - `VR_CHECK_MSG(expr, msg)`
  - `VR_REQUIRE(expr)`
  - `VR_REQUIRE_MSG(expr, msg)`
  - `VR_SKIP(reason)`
- CLI 筛选：
  - `--filter <pattern>`（支持子串和 `* ?` 通配）
  - `--include-tag <tag>`
  - `--exclude-tag <tag>`
  - `--fail-on-empty-selection`
- 报告：
  - 终端人类可读
  - `--report-json <path>` 机器可读
- CTest：
  - `vr_tests.unit`：排除 integration 标签
  - `vr_tests.integration`：仅 integration，支持 `SKIP_RETURN_CODE`

---

## 新增测试用例

1. 在 `tests/cases/` 新建 `xxx_tests.cpp`
2. 包含头文件：

```cpp
#include "support/test_framework.hpp"
```

3. 添加用例：

```cpp
VR_TEST_CASE(MyFeature_basic_path, "unit;core") {
    VR_CHECK(true);
}
```

4. 在 `tests/CMakeLists.txt` 将该文件加入 `vr_tests` 源列表

---

## 常用命令

```powershell
# 列出测试
.\build\tests\vr_tests.exe --list

# 只跑 unit
.\build\tests\vr_tests.exe --exclude-tag integration

# 只跑 integration（全部 skip 时返回 125）
.\build\tests\vr_tests.exe --include-tag integration --return-on-all-skipped 125

# 生成 JSON 报告
.\build\tests\vr_tests.exe --report-json .\build\reports\tests.json

# 当筛选后没有测试时返回非零（适合 CI）
.\build\tests\vr_tests.exe --filter "NoSuchCase*" --fail-on-empty-selection
```
