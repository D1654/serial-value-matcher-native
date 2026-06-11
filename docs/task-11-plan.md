# Task 11：扩展规则类型支持

## 背景

T10 已完成协议字段规则验证闭环：确认规则、选择扫描、执行验证、保存验证运行、查看结果并导出 Markdown 报告。T11 在此基础上扩展更贴近私有协议现场的编码形态。

## 优先顺序

1. PackedBCD
2. Gray16
3. BitFlags
4. 枚举 / 报警位规则

## T11-A：PackedBCD 支持

目标：先支持无符号 Packed BCD 数值规则，覆盖候选生成与规则验证两条主链路。

### 范围

- `NumericCandidateType` 新增 `PackedBCD`。
- 候选生成器新增 `includePackedBCD` 选项，默认启用。
- 单寄存器 PackedBCD：每个 16 位寄存器按 4 个 BCD 数字解析，如 `0x1234 -> 1234`。
- 双寄存器 PackedBCD：支持高字在前 / 低字在前，以及寄存器内字节序交换。
- 非法 BCD 半字节（A-F）不生成候选，规则验证时给出中文解码失败状态。
- 协议规则验证器支持 `candidateType = "PackedBCD"`，并继续应用倍率与偏移。

### 非范围

- 暂不支持带符号 BCD、压缩小数点元数据、可变长度 BCD 字段。
- 暂不支持 BitFlags、Gray16、枚举/报警位，这些留给后续小步任务。

### 验证

- `value_candidate_generator_tests` 覆盖单寄存器、双寄存器 word swap、非法 BCD 半字节。
- `protocol_rule_verifier_tests` 覆盖 PackedBCD 规则验证、字节序、倍率和非法 BCD。
- 完成后运行全量 `ctest --test-dir build --output-on-failure`。

## 当前状态

- 2026-06-05：T11-A 开始执行。

## T11-B：Gray16 支持

目标：支持 16 位格雷码字段，覆盖候选生成与规则验证。

### 范围

- `NumericCandidateType` 新增 `Gray16`。
- 候选生成器新增 `includeGray16` 选项，默认启用。
- 单寄存器 Gray16：按标准格雷码转二进制数值，例如二进制 `100` 的 Gray16 编码为 `0x0056`。
- 支持寄存器内字节序交换，例如原始 `0x5600` + LittleEndian 解码为 `100`。
- 协议规则验证器支持 `candidateType = "Gray16"`，并继续应用倍率与偏移。

### 非范围

- Gray32 或多寄存器 Gray 编码暂不纳入本步。
- BitFlags、枚举/报警位留给后续任务。

### 验证

- `value_candidate_generator_tests` 覆盖 Gray16 标准字节序与字节交换。
- `protocol_rule_verifier_tests` 覆盖 Gray16 规则验证。

## 当前状态更新

- 2026-06-05：T11-A `PackedBCD` 已完成并提交。
- 2026-06-05：T11-B `Gray16` 开始执行。

## T11-C：BitFlags 支持

目标：先支持单寄存器 16 位位标志掩码，让候选生成和规则验证能识别 `BitFlags`。具体 bit 名称、枚举含义、报警位描述留给下一步“枚举/报警位规则”。

### 范围

- `NumericCandidateType` 新增 `BitFlags`。
- 候选生成器新增 `includeBitFlags` 选项，默认启用。
- 单寄存器 BitFlags：按 16 位无符号位掩码解码，如 `0x0055 -> 85`。
- 支持寄存器内字节序交换，如 `0x5500` + LittleEndian 解码为 `0x0055`。
- 协议规则验证器支持 `candidateType = "BitFlags"`，并沿用倍率/偏移字段以兼容现有存储结构；默认场景下倍率为 1、偏移为 0。

### 非范围

- 暂不维护每一位的中文名称、报警含义、状态解释。
- 暂不支持多寄存器位域或跨字节 bit range。

### 验证

- `value_candidate_generator_tests` 覆盖 BitFlags 候选生成与字节交换。
- `protocol_rule_verifier_tests` 覆盖 BitFlags 规则验证。

## 当前状态更新

- 2026-06-05：T11-C `BitFlags` 开始执行。
- 2026-06-05：T11-C `BitFlags` 已完成并提交。

## T11-D：枚举 / 报警位规则

目标：在 T11-C `BitFlags` 基础上补充中文解释元数据，让单寄存器枚举值和报警位能在规则验证结果、界面和 Markdown 报告中直接展示“这是什么意思”。

### 范围

- `ProtocolFieldRuleRecord` 新增 `interpretationMap`，用于保存解释映射文本。
- `ProtocolRuleVerificationResult` 与持久化结果新增 `interpretationText`，用于保存本次验证得到的中文解释。
- `BitFlags` 解释格式：`bit=名称|未置位说明|置位说明`，支持多行或分号分隔，例如：
  - `0=运行允许|未允许|已允许`
  - `1=报警|正常|报警触发`
- `EnumMap` 规则类型支持单寄存器枚举值解释，解释格式：`数值=中文含义`，例如：
  - `0=停止`
  - `1=运行`
  - `2=故障`
- 规则验证器支持 `candidateType = "EnumMap"`，按单寄存器无符号值解码，再根据 `interpretationMap` 生成中文解释；未定义枚举值给出“未定义枚举值”的解释文本。
- 分析工作区：确认 / 编辑 `BitFlags` 或 `EnumMap` 规则时可录入解释映射；编辑规则时可把单寄存器数值规则切换为 `EnumMap`，用于把稳定的 UInt16 候选产品化为枚举规则；规则验证表新增“解释”列。
- Markdown 验证报告新增“解释”列。
- SQLite 结构兼容升级：老库自动补 `protocol_field_rules.interpretation_map` 与 `rule_verification_results.interpretation_text` 文本列。

### 非范围

- 暂不做复杂可视化位编辑器。
- 暂不支持多寄存器位域、跨寄存器 bit range 或枚举值分组。
- 暂不支持报警级别、颜色、确认/复位动作等报警管理字段。

### 验证

- `protocol_rule_verifier_tests` 覆盖 BitFlags 位解释和 EnumMap 枚举解释。
- `protocol_rule_persistence_tests` 覆盖规则解释映射保存 / 读取。
- `rule_verification_persistence_tests` 覆盖验证解释文本保存 / 读取。
- `rule_verification_report_tests` 覆盖 Markdown 报告“解释”列。
- 完成后运行全量 `ctest --test-dir build --output-on-failure`。

## 当前状态更新

- 2026-06-06：T11-D `枚举 / 报警位规则` 已完成实现并通过定向与全量测试。
