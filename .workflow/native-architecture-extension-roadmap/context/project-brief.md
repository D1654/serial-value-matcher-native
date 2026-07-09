# Project Brief — Native Architecture Extension Roadmap

Updated: 2026-07-09T20:21:21+08:00

## 用户原始输入

> 当前项目的最新release我用笔记本电脑测试了，感觉还不错。现在我想请你审查当前项目的源码是否干净易读易于维护，然后审查本项目的扩展性，因为我要准备增加新的功能了。

> 我认为应该启用workflow architect 进行分析和规划任务以及实际整改和扩展，你觉得呢

> 确认

## 初始解析

本工作流目标不是继续修一个具体 UI bug，而是在 v1.0.4 稳定 release 基线上，为后续新增功能建立架构路线、质量门禁和可扩展实施计划。当前阶段为 Phase 0 Pre-Research，只允许研究、归纳和规划，不允许修改项目源码。

## 已确认事实基线

- 目标用户默认中文母语，文档、提示和交付说明优先使用中文。
- 当前最新稳定基线为 SerialValueMatcher Native v1.0.4。
- 用户已在笔记本电脑本地测试最新 release，主观体验良好。
- GitHub Actions 编译的 Windows native 可执行程序是最终测试目标。
- 后续功能面向开发测试工程师/调试人员，不是营销型或泛办公用户。
- 必须保持现有功能、UI 体验、性能稳定性和 release 闭环，不接受为重构而破坏可用性。

## 最近代码审查结论摘要

来源: `.review/report.md`，生成时间 2026-07-05T16:20:47+08:00。

- 当前源码总体可维护，v1.0.4 可以作为继续开发的稳定基线。
- 本地验证通过: `ctest --test-dir build-codex --output-on-failure` 为 49/49，通过文档产物一致性检查。
- 主要维护压力集中在 Win32 GUI 协调层、原生存储大 facade、双路线 Modbus 扫描逻辑、发布元数据和构建脚本工具化边界。
- 审查发现 10 项问题: Critical 0, High 0, Medium 6, Low 4。

## 架构热点

1. `NativeMainWindow` 仍是过大的窗口协调类，后续新功能不应继续直接塞入主窗口。
2. 生产布局逻辑尚未消费 `NativeLayoutModel`，测试模型和真实布局存在漂移风险。
3. Win32 native Modbus 扫描 worker 与 core/Qt 路线存在执行逻辑分叉。
4. 多文件 append 保存缺少完整事务边界，极端失败时可能产生孤儿记录。
5. `NativeSessionStore` 对外仍是大仓库类，后续持久化实体扩展会增加维护压力。
6. 高吞吐发送功能前需要考虑异步串口写队列，避免 UI 线程同步串口写入扩大卡顿风险。
7. 版本元数据存在漂移，CMake、RC、README/release 说明需要单一版本源。
8. native CMake 测试注册、格式化、静态检查仍缺少统一工具化约束。

## 本工作流的设计约束

- Phase 0 必须完成三路预研，其中 Agent C 必须尝试 DeepWiki。
- Phase 1 必须补齐需求覆盖，尤其是后续新增功能类型、扩展优先级、兼容边界、性能门槛、安全边界和验收矩阵。
- Phase 2 必须先设计生产级架构，再决定任务拆解。
- Phase 3 必须把执行计划落盘。
- Phase 4 才允许改源码，每个完成任务必须验证并提交。

## 初始成功标准

- 形成可执行的架构扩展路线，明确哪些边界先整备、哪些功能可以后接入。
- 新功能接入路径具备清晰层次: 纯逻辑/状态类 -> 单元测试 -> Win32 UI 接入 -> 自检/UI 性能/文档/CI。
- 保持 v1.0.4 当前功能和 UI 体验不回退。
- 后续代码更干净、更易读、更易测、更易扩展，并保留极致性能稳定性目标。

## Phase 0 预研摘要

三路预研已完成。领域研究认为串口/Modbus 调试工具的核心是实时通信观测、可控注入、可追溯分析和稳定证据链；竞品分析认为本项目的差异化是中文优先、portable Win32、Modbus 数值匹配、规则验证和报告闭环；技术生态研究确认应继续以 C++20 + Win32 native + CMake/CTest + GitHub Actions/Wine 验证为主线，Qt SerialPort 作为行为参考，vcpkg 暂缓到真实依赖增长时再引入。

后续需求访谈应重点确认下一批新增功能类别、是否先做架构整备、自动化边界、性能目标、会话证据字段、UI 改动边界和验收基线。

## Phase 2 执行进展

- Phase 1 已完成并经过 Bug Fixer 里程碑复审，截图证据链和 Workbench repaint 策略问题已修复。
- Phase 2 已进入 Backend Consistency，Task 01 完成了纯标准 C++ bounded serial write queue 契约、native 串口 IO 状态快照和 focused/full CTest 验证。
- Phase 2 Task 04 完成了纯 C++ Modbus scan executor 抽取：Qt executor 变为兼容适配层，native worker 变为 Win32 transport/UI/storage 薄适配层；build-codex 50/50、MinGW native 28/28 和 native DLL 依赖快检均通过。
- Phase 2 Task 05 完成了 Modbus 响应解析、扫描计划构建、native attempt UI 映射的稳定结果分类；focused 3/3、build-codex 50/50、MinGW native 28/28 和 native DLL 依赖快检均通过。
- Phase 3 Task 01 完成了 Qt-side 本地结构化 session evidence model、raw TX/RX 证据优先捕获、source subsystem、单调顺序、metadata/隐私字段和 focused 测试；build-codex 51/51、MinGW native 28/28、本地 MinGW package/Wine gate 均通过。
- Phase 3 Task 03 完成了纯 C++ 本地声明式命令序列基础，覆盖串口写入、延时、等待响应、Modbus read 和断言；实现静态安全限制、取消、超时、结构化执行证据，并明确不引入脚本引擎或任意代码执行面。build-codex 53/53、MinGW native 30/30、本地 MinGW package/Wine gate 均通过。
- Phase 3 Task 04 完成了危险操作确认与审计：定时发送、文件发送、命令序列写入、Modbus 广播写和寄存器写按策略分类；Win32 UI 在执行前 fail-closed 确认；确认/取消/提示失败均记录脱敏安全审计。build-codex 54/54、MinGW native 31/31、本地 MinGW package/Wine gate 均通过。
- Phase 3 Task 05 完成了单一版本元数据源：`cmake/svm_version.cmake` 统一 CMake project version、Win32 VERSIONINFO、包审计 summary、README/发布文档和本地 MinGW 包名；当前 VERSIONINFO 已对齐 v1.0.4。build-codex 55/55、MinGW native 32/32、本地 MinGW package/Wine gate 均通过。
