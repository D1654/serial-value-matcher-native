# Task 08：UI 接入候选证据链与稳定性排序计划

## 1. 目标

在 Task 06/07 已完成候选生成、候选持久化、多样本稳定性评分和稳定性结果持久化后，开始把分析结果接入 Qt Widgets UI。

本阶段必须小步推进，避免一次性大改界面导致可启动性和布局回归。当前先做只读入口，把最近一次稳定性分析结果展示出来；后续再逐步接入目标值输入、扫描结果选择、人工确认和规则保存。

## 2. 边界原则

- 不重构主窗口整体布局；
- 不影响当前串口连接、发送、接收、历史和自动重连功能；
- 不在 UI 层重新计算候选或稳定性分数，只读取 `SessionStore` 已持久化结果；
- 无数据时必须给中文空状态说明，而不是空白页面；
- 表格默认只读，先保证可查看、可排序依据清楚，再做编辑/确认流程。

## 3. T08A：分析工作区只读入口（已完成）

新增：

- 工具栏“分析工作区”动作连接到 `MainWindow::showAnalysisWorkspace()`；
- `SessionStore::latestStabilityRun()`，用于读取最近一次稳定性分析运行；
- 只读弹窗“分析工作区 - 稳定候选”。

显示内容：

- 最近稳定性分析 run id；
- 来源匹配次数；
- 稳定候选数量；
- 创建时间；
- 稳定候选表格：置信等级、稳定分、样本数、类型、地址、寄存器数量、字序/字节序、平均/最大误差、证据摘要。

空状态：

- 如果还没有稳定性分析结果，显示中文提示：需要先完成扫描、目标值候选生成和多样本稳定性分析。

验收：

- `svm-native` app 目标编译通过；
- `latestStabilityRun()` 有持久化测试覆盖；
- 单线程全量 `ctest` 通过；
- UI 改动仅限工具栏入口和一个只读弹窗，不改主窗口核心布局。

## 4. T08B：目标值/容差输入 UI（已完成）

在“分析工作区 - 稳定候选”弹窗顶部新增候选生成输入区：

- 目标值输入；
- 单位输入；
- 绝对容差输入；
- “基于最近扫描生成候选”按钮；
- 候选生成状态提示。

行为：

1. 读取 `SessionStore::latestScanSession()` 获取最近扫描会话；
2. 读取该扫描会话的 `scanObservations(sessionId)`；
3. 通过 `registerSamplesFromScanObservations()` 转换为候选生成输入；
4. 调用 `generateValueCandidates()`；
5. 通过 `SessionStore::saveMatchRun()` 保存 match run 与候选；
6. 无扫描会话、无观测、候选生成失败、保存失败时均显示中文提示。

验收：

- `SessionStore::latestScanSession()` 有持久化测试覆盖；
- `svm-native` app 目标编译通过；
- 单线程全量 `ctest` 通过；
- UI 改动仍局限于分析工作区弹窗，不重构主窗口。

## 5. T08C：扫描会话选择与候选生成流程串联（已完成）

在 T08B 默认使用最近扫描会话的基础上，升级为显式扫描会话选择：

- `SessionStore::recentScanSessions()`：读取最近扫描会话列表；
- “分析工作区”候选生成区新增“扫描会话”下拉框；
- 下拉项显示完成时间、从站、功能码、地址范围和状态；
- “基于所选扫描生成候选”按钮使用用户选中的扫描会话；
- 无扫描会话时按钮禁用并显示中文提示；
- 扫描会话不存在、无观测、候选生成失败、保存失败时均显示中文提示。

验收：

- `recentScanSessions()` 有持久化测试覆盖；
- `svm-native` app 目标编译通过；
- 单线程全量 `ctest` 通过；
- UI 改动仍局限于分析工作区弹窗，不重构主窗口。

## 6. T08D：稳定候选详情面板（已完成）

在稳定候选表格下方新增只读详情面板：

- 点击/选择稳定候选行后，展示该候选的置信等级、稳定性评分、样本数、类型、字序/字节序、地址、寄存器数量、来源 match runs、来源扫描会话和证据摘要；
- `SessionStore::scanObservationsByIds()`：按 observation id 列表恢复原始扫描观测事实；
- 详情面板展示 observation / block / attempt 证据链，包括 observation id、session、blockIndex、attemptIndex、slave、function、address、value 和观测时间；
- 无法找到 observation 时显示中文提示。

验收：

- `scanObservationsByIds()` 有持久化测试覆盖；
- `svm-native` app 目标编译通过；
- 单线程全量 `ctest` 通过；
- UI 改动仍局限于分析工作区弹窗，不重构主窗口。

## 7. T08E：Modbus 扫描薄 UI 纵向闭环（已完成）

在主工具栏新增“Modbus 扫描”入口，让用户可以从 UI 直接发起一次只读扫描，而不是只能消费已有扫描会话。

能力：

- 输入从站 ID、FC03/FC04、起始地址、结束地址、块大小、响应超时、重试次数和请求间隔；
- 参数校验复用 `buildScanPlan()`，非法输入给中文提示；
- 按当前主界面的串口参数临时打开串口执行扫描；
- 使用 `ModbusRtuSerialTransport` + `ModbusScanExecutor` 执行真实 RTU 请求；
- 通过 `SessionStore::saveScanExecution()` 保存 scan session、attempts 和 observations；
- 扫描完成后可在分析工作区选择该扫描会话继续生成候选；
- 如果当前串口调试连接已打开，执行扫描前会提示并临时断开，避免同一串口被重复打开。

验收：

- `svm-native` app 目标编译通过；
- Modbus 扫描相关定向测试通过；
- 全量 `ctest` 通过；
- UI 改动仍局限于工具栏入口和轻量对话框，不重构主窗口整体布局。

## 8. 后续任务

- T09：人工确认候选并保存为协议字段规则；
- 后续：规则导出、报告导出、BCD/Gray/BitFlags 扩展。
