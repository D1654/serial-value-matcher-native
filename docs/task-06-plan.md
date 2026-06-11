# Task 06：目标值匹配与候选生成计划

## 1. 目标

在 Task 05 已完成 Modbus RTU 扫描、执行与 SQLite 观测保存的基础上，新增 UI 无关的“目标值候选生成”核心。

Task 06 的结果只能称为“候选”，不能称为“已确认”。单样本匹配必须保留证据来源，后续多样本确认、人工确认、保存协议规则和报告导出再进入后续任务。

## 2. 边界原则

- 不依赖 `MainWindow` / `QWidget` / `SerialPortService`；
- 不重新解析 raw response frame，直接消费 Task 05F 的寄存器观测事实；
- 不动态推导任意倍率，倍率必须来自白名单/显式配置；
- 不在 Task 06 直接落新 SQLite 候选表，先保持纯核心可测试；
- 候选必须携带 `sessionId`、`observationIds`、地址、block/attempt、slave/function，便于后续追溯 `scan_attempts` 原始请求/响应。

## 3. T06A：目标值候选生成核心（已完成）

新增：

- `src/matching/value_candidate_generator.h/.cpp`
- `src/matching/scan_observation_adapter.h/.cpp`
- `tests/value_candidate_generator_tests.cpp`

能力：

- 中性输入模型 `RegisterSample`，可由 `ScanObservationRecord` 转换而来；
- 目标值模型 `TargetValue`；
- 容差模型 `MatchTolerance`：`effectiveTolerance = max(absolute, abs(target) * relativeRatio)`；
- 倍率模型 `ScaleTransform`：`engineeringValue = decodedValue * multiplier + offset`；
- 候选类型：`UInt16` / `Int16` / `UInt32` / `Int32` / `Float32`；
- 单寄存器支持标准字节序与寄存器内字节交换；
- 双寄存器支持标准字节序、word swap、寄存器内字节交换、word swap + 字节交换；
- Float32 按 IEEE754 解码，NaN/Inf 不进入候选；
- 评分使用匹配质量、类型先验、字节序先验、倍率先验组合，排序后截断 `maxCandidates`；
- 候选保留 observation id、session、地址、block/attempt 等证据元数据；
- `registerSamplesFromScanObservations()` 将 T05F SQLite 观测记录转换为候选生成输入。

验收：

- UInt16 精确匹配；
- UInt16 byte swap；
- Int16 负数；
- 显式倍率匹配；
- UInt32 标准布局与 word swap；
- Float32 标准布局与 word swap；
- 非连续地址 / 混合功能码不组合为 32-bit；
- 容差过滤与近似评分；
- `maxCandidates` 生效；
- scan observation adapter 保留证据元数据。

## 4. T06B：与扫描持久化结果的集成测试（已完成）

已在 `modbus_scan_persistence_tests` 中补充端到端集成验证：

1. 用 fake Modbus executor 生成包含 Float32 寄存器事实的扫描结果；
2. 通过 `SessionStore::saveScanExecution()` 保存扫描会话、尝试记录和观测事实；
3. 重新读取 `scanObservations(sessionId)`；
4. 用 `registerSamplesFromScanObservations()` 转换为 `RegisterSample`；
5. 调用 `generateValueCandidates()` 生成目标值候选；
6. 验证候选能回溯到 observation/session/block/attempt/slave/function，并保留地址、寄存器数量和误差证据。

## 5. T06C：候选持久化表（已完成）

新增 SQLite 持久化能力：

- `match_runs`：保存一次目标值匹配运行的来源扫描会话、目标值、单位、采样时间、容差和候选数量；
- `match_candidates`：保存排序后的候选类型、字序/字节序、来源 session/slave/function、地址、寄存器数量、observation/block/attempt 证据、原始寄存器、解码值、倍率、工程值、误差、评分和证据文本；
- `SessionStore::saveMatchRun()`：同一 `runId` 重新保存时会先清理旧候选再写入新结果，避免重复脏数据；
- `SessionStore::matchRun()` / `matchCandidates()`：用于后续 UI、人工确认、多样本评分和报告导出读取候选证据链；
- `match_persistence_tests` 覆盖保存/读取、同 runId 替换旧候选、空 runId 中文错误。

## 6. 后续任务

- T07：多样本目标值确认与候选稳定性评分；
- T08：UI 接入，用户输入目标值/容差并查看候选证据链；
- 后续：BCD、Gray、BitFlags、私有协议字段规则保存与导出。
