# Task 10：将协议字段规则应用到新扫描数据

## 1. 目标

Task 09 已经能把稳定候选人工确认成协议字段规则，并提供查看、编辑和删除入口。Task 10 的目标是把这些规则从“静态资产”推进为“可复用验证能力”：选择一组扫描观测数据，按已确认规则解码字段，得到当前工程值，并说明哪些规则已验证、哪些缺少观测、哪些类型暂不支持。

## 2. 边界原则

- 不自动修改已确认规则；
- 不把验证失败直接判定为设备异常，先区分缺少观测、类型不支持和解码失败；
- 第一阶段先做 UI 无关核心，避免把算法和 Qt 界面绑死；
- 保留中文状态和证据文本，后续可直接用于 UI、报告和导出；
- BCD、Gray、BitFlags 等扩展类型后续按规则类型逐步补齐。

## 3. T10A：协议字段规则验证核心（已完成）

新增：

- `src/matching/protocol_rule_verifier.h`
- `src/matching/protocol_rule_verifier.cpp`
- `tests/protocol_rule_verifier_tests.cpp`

核心能力：

- 输入已确认的 `ProtocolFieldRuleRecord` 列表；
- 输入扫描观测转换后的 `RegisterSample` 列表；
- 按规则的 slave/function/startAddress/registerCount 寻找最新观测；
- 按规则的 candidateType、wordOrder、byteOrder 解码；
- 应用 scaleMultiplier/scaleOffset 得到工程值；
- 输出每条规则的验证结果、原始寄存器、observation ids、工程值、状态和中文证据文本；
- 汇总总规则数、已验证数、缺少观测数和暂不支持/解码失败数。

当前支持类型：

- UInt16
- Int16
- UInt32
- Int32
- Float32

测试覆盖：

- UInt16 + 倍率验证，并选择同地址最新观测；
- Float32 高字在前/大端验证；
- 缺少连续地址观测时给中文状态；
- 暂不支持类型时给中文状态。

验收：

- `protocol_rule_verifier_tests` 定向通过；
- `svm-native` app 目标编译通过；
- 全量 `ctest` 通过。

## 4. T10B：分析工作区规则验证入口（已完成）

在“分析工作区”的已确认规则列表下方新增规则验证入口：

- 新增“使用规则验证所选扫描”按钮；
- 使用顶部当前选择的扫描会话作为验证数据源；
- 从数据库读取当前已确认规则和所选扫描观测；
- 使用 `registerSamplesFromScanObservations()` 转换扫描观测；
- 调用 `verifyProtocolFieldRules()` 完成规则验证；
- 在界面中显示验证结果表；
- 表格字段包括结果、字段、工程值、单位、类型/地址、原始寄存器和证据；
- 状态栏和状态标签显示总规则数、已验证数、缺少观测数、暂不支持/失败数；
- 无扫描会话、无规则、无观测时均显示中文提示。

验收：

- `svm-native` app 目标编译通过；
- 全量 `ctest` 通过；
- UI 改动仍局限于分析工作区弹窗。

## 5. T10C：规则验证结果持久化（已完成）

新增规则验证运行和明细持久化：

- `src/storage/rule_verification_persistence_records.h`
- SQLite 表 `rule_verification_runs`
- SQLite 表 `rule_verification_results`
- `SessionStore::saveRuleVerificationRun()`
- `SessionStore::ruleVerificationRun()`
- `SessionStore::latestRuleVerificationRun()`
- `SessionStore::ruleVerificationResults()`
- `tests/rule_verification_persistence_tests.cpp`

保存内容：

- 验证运行 ID；
- 来源扫描会话；
- 总规则数、已验证数、缺少观测数、暂不支持/失败数；
- 每条规则的 rule id、字段名、单位、来源扫描会话、验证状态、中文状态；
- slave/function/address/registerCount；
- observation ids、原始寄存器、解码值、工程值、观测时间和证据文本。

UI 同步更新：

- 分析工作区执行“使用规则验证所选扫描”后，会自动保存一次验证运行；
- 状态信息中显示保存后的 verification run id；
- 如果保存失败，界面给出中文错误提示。

测试覆盖：

- 保存并读取验证运行；
- 保存并读取验证明细；
- 同一 verification run id 重算时替换旧明细；
- 空验证运行 ID / 空来源扫描会话中文错误；
- 空 observation/raw register 列表会保存为空字符串而不是 NULL，避免 SQLite NOT NULL 约束失败。

验收：

- `rule_verification_persistence_tests` 定向通过；
- `svm-native` app 目标编译通过；
- 全量 `ctest` 通过。

## 6. T10D-A：规则验证 Markdown 报告生成核心（已完成）

新增 UI 无关报告生成器：

- `src/report/rule_verification_report.h`
- `src/report/rule_verification_report.cpp`
- `tests/rule_verification_report_tests.cpp`

能力：

- 输入 `RuleVerificationRunRecord` 和 `RuleVerificationResultRecord` 列表；
- 输出中文 Markdown 报告；
- 报告包含验证摘要、规则验证明细和说明；
- 明细包括结果、字段、工程值、单位、从站/功能码、地址、原始寄存器、Observation IDs 和证据；
- 自动转义 Markdown 表格中的 `|`；
- 自动把证据中的换行转换为 `<br>`；
- 空明细时输出中文空状态。

测试覆盖：

- 中文标题/摘要/明细/说明；
- Markdown 表格字段；
- 字段名中的 `|` 转义；
- 多行证据换行转换；
- 原始寄存器以 `0x` + 大写十六进制数字显示；
- 空明细状态。

验收：

- `rule_verification_report_tests` 定向通过；
- `protocol_rule_verifier_tests` 回归通过；
- 全量 `ctest` 通过。

## 7. T10D-B：分析工作区导出验证报告入口（已完成）

在“分析工作区”的规则验证区域新增“导出最近验证报告”按钮：

- 若存在最近一次规则验证运行，按钮可直接使用；
- 若没有验证运行，点击后显示中文提示；
- 导出时读取 `latestRuleVerificationRun()` 和 `ruleVerificationResults()`；
- 调用 `renderRuleVerificationMarkdownReport()` 生成 Markdown；
- 使用文件保存对话框选择 `.md` 路径；
- 以 UTF-8 写入 Markdown 文件；
- 写入失败或写入不完整时显示中文错误；
- 规则验证成功保存后会自动启用导出按钮。

同时同步修正分析工作区验证结果表中的原始寄存器格式，保持 `0x` 前缀 + 大写十六进制数字。

验收：

- `svm-native` app 目标编译通过；
- 全量 `ctest` 通过；
- UI 改动仍局限于分析工作区弹窗。

## 8. 后续任务

- T11：扩展规则类型支持（PackedBCD、Gray、BitFlags、枚举/报警位等）；
- 后续：验证历史对比、批量报告、趋势分析。
