# Domain Knowledge — Consolidated Pre-Research
<!-- Auto-generated from Phase 0 agents. Updated: 2026-07-05T17:27:00+08:00 -->

## Domain Summary

SerialValueMatcher Native 所在领域不是普通文本终端，而是面向开发测试工程师的实时通信观测、可控注入、协议解释、会话证据和报告生成工具。串口/Modbus 调试的核心价值在于连接稳定、收发低延迟、日志不丢、异常可定位、结果可复现。

后续扩展的关键不是堆更多按钮，而是建立能长期承载功能增长的分层边界: UI shell、功能 controller、串口 transport、协议/事务引擎、结构化 session log、存储、报告和验证门禁。当前 v1.0.4 已具备稳定基线，新增功能前应优先控制 Win32 主窗口膨胀、生产布局与测试布局漂移、Modbus 执行路径分叉、storage facade 过大和构建/版本元数据漂移。

## Competitive Landscape

| 方向 | 代表工具 | 用户期望 | 对本项目的启发 |
|------|----------|----------|----------------|
| 通用串口终端 | RealTerm, HTerm, CuteCom, PuTTY | 多格式收发、时间戳、文件发送、日志、轻量启动 | 保持 portable Win32 和基础串口体验，不要为了功能引入重运行时 |
| 终端与自动化 | Tera Term, RealTerm | 宏、命令行、脚本、会话记录 | 自动化应先从声明式命令序列和断言开始，暂缓完整脚本引擎 |
| 商业协议测试 | Docklight, Advanced Serial Port Monitor | 脚本、模拟、sniffer、双端口、协议分析 | 双口监听/模拟器可以作为后续高级模块，但需要先有 transport 边界 |
| Modbus 工具 | Modbus Poll, QModMaster | 扫描、读写、数据格式、缩放、异常处理 | 本项目应保留“已知值 -> 候选寄存器 -> 规则验证 -> 报告”的差异化 |
| 数据可视化 | Serial Studio | frame parser、仪表盘、记录、回放、导出 | 可做轻量趋势/回放，不宜过早转向重 dashboard |

本项目当前差异化机会是中文优先、轻量 Win32 native、串口终端 + Modbus 扫描 + 数值匹配 + 证据报告闭环。竞品的共同风险是功能堆叠导致 UI 复杂、脚本体系不透明、日志无界、协议路径分叉和发布依赖变重。

## Tech Ecosystem

| 技术 | 结论 |
|------|------|
| C++20 + Win32 API | 继续作为主交付路线，适合轻量、可控、低运行时依赖的 Windows native 包。 |
| Win32 communications APIs | 串口真实权威仍是 `CreateFile`、`DCB`、`COMMTIMEOUTS`、`ReadFile`、`WriteFile` 和 overlapped I/O；Windows Classic Samples 只适合作 UI/DPI/Shell 参考。 |
| Qt SerialPort | 保留为 legacy baseline 和行为参考，不应重新引入 native release 运行时。 |
| CMake / CTest | 应作为构建与验证主干，后续需要 native test helper、版本单源和 CI 产物一致性继续加强。 |
| GoogleTest | 适合未来更系统地覆盖纯逻辑、storage transaction、parser/matcher、layout model 和 fake serial adapter。当前可先评估是否引入，避免不必要依赖。 |
| vcpkg | 当前不建议为“整洁”引入；只有真实第三方依赖增长时，才用 manifest + baseline + triplet + CRT 策略治理。 |

Agent C 已完成 13 次 DeepWiki 查询，覆盖 `microsoft/Windows-classic-samples`、`Kitware/CMake`、`qt/qtserialport`、`google/googletest`、`microsoft/vcpkg`，fallback 数量为 0。

## Key Risks & Unknowns

1. 新功能如果继续进入 `NativeMainWindow`，会放大维护成本和 UI 回归风险。
2. 生产布局若不消费 `NativeLayoutModel`，新增控件时测试和真实界面会继续漂移。
3. native Modbus worker 与 core/Qt executor 双路线会导致后续重试、超时、异常码和协议扩展行为不一致。
4. UI 线程同步串口写入在高吞吐、长脚本、批量回放或设备无响应时可能再次造成卡顿。
5. 长会话/高波特率日志需要批量刷新、可控缓存、结构化存储和可恢复导出。
6. 多文件 append 存储若没有事务或 orphan recovery，异常退出/磁盘错误时可能产生孤儿记录。
7. 自动化能力必须先定义安全边界，避免一开始引入不可审计的脚本执行面。
8. 未来若加入 device profile、寄存器映射、数据类型、倍率、单位和端序，需要 schema version 和迁移策略。
9. 需要明确下一批功能是否涉及 Modbus TCP、设备模拟、双口 sniffer、批量产测或 CLI，否则架构边界可能设计过重或过轻。
10. 任何性能优化必须保留本地 Wine/Xvfb、GitHub Actions exe、串口仿真和 UI perf 的验收闭环。

## Interview Priority Topics

1. 下一批新增功能的具体类型和优先级。
2. 是否接受先做架构整备，再做功能扩展。
3. 扩展范围是否仍限于串口/Modbus RTU，还是考虑 TCP/UDP、Modbus TCP、HID、CAN 网关或模拟器。
4. 自动化是做命令序列/断言，还是做完整脚本引擎。
5. 高吞吐和长时间运行目标: 波特率、帧率、日志体量、扫描周期、UI 延迟。
6. 会话证据包需要包含哪些字段: 原始 TX/RX、解析帧、用户操作、扫描参数、匹配规则、报告、版本/SHA。
7. 数据安全边界: 是否需要脱敏、危险写入确认、客户现场数据保护。
8. UI 改动边界: 保持现有密集工具型布局，还是允许增加新的工作台页面/面板。
9. 架构整备优先级: layout model 生产化、controller 边界、Modbus executor 统一、storage 拆分、async write queue、CMake/version 工具化。
10. 验收基线: 本地测试、GitHub Actions artifact、Wine 截图、真实 Windows 手工测试、串口仿真压力各自是否阻塞。

## Preliminary Architecture Direction

后续草案应优先考虑一条“小步、可验证、不破坏 v1.0.4”的路线:

1. 建立功能模块边界，主窗口保留生命周期、消息分发和控件宿主，具体业务进入 controller/service/state。
2. 让生产布局消费 `NativeLayoutModel`，新增 UI 先扩展模型和测试，再映射到 HWND。
3. 将 native Modbus 扫描收敛到统一 executor/transport 适配层。
4. 引入或规划异步串口写队列，作为批量发送/脚本/高吞吐功能前置条件。
5. 将 native storage 从大 facade 逐步拆成窄接口，并补齐多文件写入事务或恢复策略。
6. 保持 CMake/CTest/GitHub Actions/Wine 自检为质量主干，避免新增功能绕过验证闭环。
