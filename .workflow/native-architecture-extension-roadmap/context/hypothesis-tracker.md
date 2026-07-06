# Hypothesis Tracker
<!-- Updated after each brainstorm and interview answer. Updated: 2026-07-06T16:21:45+08:00 -->

| ID | Hypothesis | Confidence | Source | Status |
|----|------------|------------|--------|--------|
| H1 | 新功能前应先收紧 `NativeMainWindow` 的功能边界，否则后续 UI、串口、Modbus、存储和报告会继续耦合。 | HIGH | Code review + local inventory + Agent A/B/C + Q1 | CONFIRMED |
| H2 | 生产布局改为消费 `NativeLayoutModel` 是 UI 扩展前的最高优先级之一，可降低测试和真实布局漂移。 | HIGH | Code review + local inventory + Q2 | CONFIRMED |
| H3 | Modbus 扫描应统一到 executor/transport 抽象，native worker 只作为适配和线程调度层。 | HIGH | Code review + Agent A/C + Q2 | CONFIRMED |
| H4 | 如果下一批功能涉及批量发送、命令序列或脚本化，异步串口写队列会成为性能稳定前置条件。 | MEDIUM | Code review + Agent A/B/C + Q2 | CONFIRMED |
| H5 | 本项目的市场差异化在中文优先、portable Win32、Modbus 数值匹配和证据报告闭环，而不是做成全功能重 dashboard。 | HIGH | Agent B + Q4 | CONFIRMED |
| H6 | 自动化应优先从声明式命令序列和断言开始，完整脚本引擎应等安全边界、证据模型和执行沙箱明确后再考虑。 | MEDIUM | Agent B | OPEN |
| H7 | 多文件 append 存储需要事务或 orphan recovery，尤其是在后续会话证据包、profile、报告和批量数据扩展后。 | MEDIUM | Code review + Agent A + Q2 | CONFIRMED |
| H8 | vcpkg 当前不应作为架构整洁性改动引入；只有新增真实第三方依赖时才值得用 manifest/baseline 治理。 | HIGH | Agent C + Q7 | CONFIRMED |
| H9 | 如果后续加入 device profile 和寄存器映射，必须提前设计 schema version、迁移和兼容策略。 | MEDIUM | Agent A/B + Q6 | CONFIRMED |
| H10 | GitHub Actions 编译 exe、Wine/Xvfb UI 检查、串口仿真压力和本地 CTest 仍应作为后续每阶段验收主干。 | HIGH | User baseline + local inventory + Q7 | CONFIRMED |
| H11 | 技术栈应维持 C++20 + Win32 native + CMake/CTest 主线；Qt/GoogleTest/vcpkg/SQLite 都必须分别受“参考、CI-only、依赖治理、存储 backend 决策门”约束。 | HIGH | BS-3 + Q7 + Q9 | CONFIRMED |
| H12 | 极致性能稳定应通过小型事件驱动策略实现：异步写队列、批量 UI/layout 提交、统一 Modbus 事务状态机、有界日志缓存和可恢复文件事务。 | HIGH | BS-4 + Q2 + Q8 | CONFIRMED |
| H13 | 本项目生产级架构应定义为本地 Windows exe 的 release/evidence/diagnostic/recovery/security 门禁，而不是云服务部署架构。 | HIGH | BS-8 + Q4 + Q8 | CONFIRMED |
| H14 | Phase 2 完整草案内部一致，可进入 Phase 3 详细计划；前提是 Phase 4 不实现 TCP runtime、不默认引入 SQLite、不做大规模目录搬迁。 | HIGH | BS-5 | CONFIRMED |
| H15 | 当前串口 PTY loopback 压力仍是 local pre-release evidence，不应被草案或 Phase 3 误写成已完全 CI-blocking 的 GitHub Actions 门禁。 | HIGH | Draft self-review + windows-native-package.yml | CONFIRMED |
