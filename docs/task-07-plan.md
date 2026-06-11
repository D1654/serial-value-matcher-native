# Task 07：多样本候选稳定性评分计划

## 1. 目标

在 Task 06 已完成单样本候选生成、扫描到候选集成测试、候选持久化的基础上，新增 UI 无关的“多样本候选稳定性分析”核心。

Task 07 仍不把结果称为“已确认规则”。它只把多个 match run 中重复出现、误差稳定、解码方式一致的候选提升为“高/中/低置信候选”，为后续人工确认、规则保存、报告导出提供排序依据。

## 2. 边界原则

- 不依赖 `MainWindow` / `QWidget` / `SerialPortService`；
- 不重新解析 raw response frame；
- 不重新生成单样本候选，而是消费 Task 06 的持久化候选或中性候选观测；
- 分组身份只使用字段位置与解码方式：类型、字序、字节序、slave/function、起始地址、寄存器数量、倍率/偏移；
- 来源扫描 session 只作为证据列表，不参与稳定候选分组，避免跨多次扫描无法聚合；
- 单样本不升级为高置信，即使它本身分数很高。

## 3. T07A：稳定性分析核心（已完成）

新增：

- `src/matching/candidate_stability_analyzer.h/.cpp`
- `tests/candidate_stability_analyzer_tests.cpp`

能力：

- 中性输入模型 `CandidateObservation`；
- `candidateObservationsFromMatchRecords()` 将 `MatchRunRecord` + `MatchCandidateRecord` 转换为稳定性分析输入；
- 按候选类型、字序、字节序、slave/function、起始地址、寄存器数量、倍率/偏移分组；
- 输出 `StableCandidate`：样本数、是否达到最小样本数、runIds、sourceScanSessionIds、observationIds、地址、平均目标值、平均工程值、平均误差、最大误差、平均候选分、误差质量、样本质量、稳定性评分、置信等级、证据摘要；
- 评分组合：单样本候选均分、误差质量、样本数量质量；
- 样本数低于 `minimumSampleCount` 时强制低置信，稳定性评分封顶；
- 输出按稳定性评分、样本数、平均误差排序。

验收：

- 多样本同一字段/同一解码方式可聚合为高置信候选；
- 单样本即使满分也保持低置信；
- 不同字序或不同起始地址会拆成不同稳定候选；
- 可从持久化记录转换为稳定性分析输入；
- 无输入、候选类型缺失等错误返回中文诊断。

## 4. T07B：稳定性结果持久化（已完成）

新增 SQLite 持久化能力：

- `stability_runs`：保存一次稳定性分析运行的来源 match run 列表、最小样本数、强样本数、稳定候选数量和创建时间；
- `stable_candidates`：保存排序后的稳定候选身份、样本数、是否达到最小样本数、置信等级、runIds、sourceScanSessionIds、observationIds、地址、平均/最大误差、平均候选分、误差质量、样本质量、稳定性评分和证据摘要；
- `SessionStore::saveStabilityRun()`：同一 `stabilityRunId` 重新保存时会先清理旧稳定候选再写入新结果，避免重复脏数据；
- `SessionStore::stabilityRun()` / `stableCandidates()`：用于后续 UI、人工确认和报告导出读取稳定候选证据链；
- `stability_persistence_tests` 覆盖保存/读取、同 stabilityRunId 替换旧结果、空 stabilityRunId 中文错误。

## 5. 后续任务

- T08：UI 接入，用户输入目标值/容差并查看候选证据链与稳定性排序；
- T09：人工确认候选并保存为协议字段规则；
- 后续：BCD、Gray、BitFlags、私有协议字段规则保存与导出。
