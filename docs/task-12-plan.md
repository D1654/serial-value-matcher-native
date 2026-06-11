# Task 12：规则编辑体验增强

## 背景

T11-D 已让 `BitFlags` / `EnumMap` 具备中文解释能力，但解释映射目前仍是人工输入的文本。如果没有保存前校验，现场很容易因为漏写等号、位号越界、枚举值重复等小错误，导致验证报告解释缺失或不符合预期。

T12 先继续小步增强规则编辑体验，避免一次性做复杂可视化编辑器。

## T12-A：解释映射校验与预览

目标：把解释映射解析逻辑抽成可复用模块，并在保存规则前给出中文校验，减少现场填错格式的概率。

### 范围

- 新增 `src/matching/protocol_rule_interpretation.h/.cpp`：
  - `BitFlags` 位定义解析。
  - `EnumMap` 枚举定义解析。
  - 解释文本生成。
  - `validateInterpretationMap()` 保存前校验与预览。
- `ProtocolRuleVerifier` 改用统一解释模块，避免验证器和 UI 各自维护一套解析规则。
- 分析工作区保存规则前校验 `interpretationMap`：
  - `BitFlags`：提示缺少等号、位号非 0-15、重复 bit。
  - `EnumMap`：提示缺少等号、枚举值非整数、重复枚举值、枚举含义为空。
  - 校验通过后在状态栏显示解释映射预览。
- 新增 `protocol_rule_interpretation_tests` 覆盖有效映射、错误映射、十六进制枚举值和非解释型规则提示。

### 非范围

- 暂不做复杂表格式编辑器。
- 暂不做位状态颜色、报警等级、确认/复位动作。
- 暂不改变数据库结构；继续复用 T11-D 的 `interpretationMap` / `interpretationText`。

### 验证

- 定向测试：
  - `protocol_rule_interpretation_tests`
  - `protocol_rule_verifier_tests`
- 完成后运行全量 `ctest --test-dir build --output-on-failure`。

## 当前状态

- 2026-06-06：T12-A 开始执行。
- 2026-06-06：T12-A 已完成实现；构建通过，定向测试 2/2 passed，全量回归 25/25 passed。
- 2026-06-06：README 已同步当前 Native 能力与测试基线，明确项目已具备串口调试、Modbus RTU 扫描、候选值稳定性分析、协议字段规则保存、BitFlags / EnumMap 规则解释和 Markdown 验证报告能力；当前全量 QtTest 基线为 25/25 passed。

## 已落地文件

- `src/matching/protocol_rule_interpretation.h`
- `src/matching/protocol_rule_interpretation.cpp`
- `src/matching/protocol_rule_verifier.cpp`
- `src/app/main_window.cpp`
- `tests/protocol_rule_interpretation_tests.cpp`
- `README.md`

## 后续建议

- 解释映射仍采用文本输入；后续如现场频繁填错，可再做表格式编辑器。
- BitFlags / EnumMap 已具备基础中文解释；报警等级、确认/复位动作、颜色规则暂不纳入当前闭环。
