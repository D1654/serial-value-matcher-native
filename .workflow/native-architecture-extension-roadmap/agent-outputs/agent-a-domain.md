# Domain Research — Serial/Modbus Native Debugging Tool
Generated: 2026-07-05T17:08:21+08:00

## Domain Overview

串口/Modbus 调试工具的本质是“实时通信观测 + 可控注入 + 可追溯分析”。目标用户通常在硬件、固件、PLC、网关、产测夹具、上位机之间排障，核心诉求不是华丽 UI，而是连接稳定、收发低延迟、日志不丢、异常可定位、结果可复现。

SerialValueMatcher Native 的架构扩展应围绕长期运行、高吞吐、协议正确性、中文工程工作流和 Windows native 稳定性设计。新增功能不应继续堆进主窗口类，而应沉到串口传输层、协议/事务层、会话日志层、报告层、UI 协调层之间的清晰边界。

从现有工具生态看，用户期望已不止“串口收发”：常见高级能力包括 Hex/文本/浮点显示、时间戳、捕获文件、发送快捷命令、脚本化自动测试、RS485 监听、协议触发器、模拟设备、TCP/UDP 网关、Modbus 支持和报告导出。

## Key Concepts & Terminology

- COM port: Windows 串口设备入口，通常通过 CreateFile 打开。
- DCB: Windows 串口配置结构，覆盖 baud rate、data bits、parity、stop bits 等。
- Overlapped I/O: Windows 异步 I/O 模式，用于避免串口读写阻塞 UI 或 worker。
- RX/TX buffer: 接收/发送缓冲区，高吞吐时必须有背压、批量刷新和丢包策略。
- Frame: 协议帧。串口原始字节需要被切分为可解释的帧。
- PDU/ADU: Modbus PDU 为功能码和数据；ADU 是带地址、CRC/LRC 或 TCP 头的完整传输单元。
- Modbus RTU/ASCII/TCP: 三类常见 Modbus 形态，RTU 对帧间静默时间和 CRC 敏感。
- Function code: Modbus 功能码，如读线圈、读保持寄存器、写单寄存器、写多寄存器。
- Coil / Discrete Input / Input Register / Holding Register: Modbus 四类核心数据表。
- Unit ID / Server ID: Modbus 设备地址或 TCP 网关后的单元标识。
- CRC/LRC: Modbus RTU/ASCII 的错误校验。
- Timeout / Retry: 请求响应事务控制，影响扫描速度、误判率和总线占用。
- Polling scan: 周期读取寄存器或设备地址，需处理取消、进度、失败分级和节流。
- Exception response: Modbus 异常响应，功能码高位置位并返回异常码。
- Half-duplex RS485: 半双工总线，监听、方向控制和节点归因都容易出错。
- Device profile: 设备寄存器含义、数据类型、倍率、单位、端序等元数据。
- Trigger / Sequence: 调试工具中常见的条件匹配和自动发送/记录机制。
- Session log: 一次调试会话的结构化记录，应能支撑回放、筛选和报告。

## Common Architecture Patterns

1. Layered native desktop architecture
   UI shell -> controller/presenter -> domain service -> transport/protocol/storage。优点是可测、可维护、能压缩 `NativeMainWindow` 责任；代价是需要明确事件和状态边界。
2. Event-driven serial core
   串口读写使用 overlapped I/O 或专用 worker，UI 线程只消费批量事件。优点是低卡顿；风险在取消、句柄生命周期、缓冲区所有权和错误恢复。
3. Protocol pipeline
   Raw bytes -> framing -> protocol decode -> semantic events -> log/report/view。适合同时支持原始串口、Modbus、未来自定义协议；关键是不要让 UI 直接解析字节流。
4. Transaction engine for Modbus
   用统一状态机管理 request、response、timeout、retry、cancel、progress、exception。可消除 native/core/Qt 双路线扫描逻辑分叉。
5. Append-only session model
   将收发、解析、匹配、用户操作、错误都记录为结构化事件，再生成视图和报告。优点是可追溯；必须补足原子保存和失败恢复。
6. Device profile and schema versioning
   将寄存器映射、倍率、端序、单位、阈值、显示名外置为 profile。优点是功能扩展快；风险是 schema 迁移和兼容策略复杂。
7. Test harness with virtual transport
   使用虚拟串口、loopback、mock transport、golden trace 验证协议和 UI 性能。适合保持 v1.0.4 稳定基线不回退。

## Typical Challenges & Pitfalls

1. High: UI 线程同步串口写入或日志刷新，导致长时间扫描、批量发送、设备无响应时卡顿。
2. High: Modbus RTU 帧边界、CRC、t1.5/t3.5 静默间隔、广播地址、异常码处理不严谨，产生“偶发误判”。
3. High: native worker 与 core/Qt 路线各自实现扫描逻辑，后续功能会出现行为漂移。
4. High: 无界日志文本控件和逐条 repaint，在高波特率或长会话下吞内存、掉帧、假死。
5. Medium: Overlapped I/O 的 `OVERLAPPED`、buffer、handle 生命周期处理错误，可能造成悬挂、重复释放或取消不完整。
6. Medium: RS485 半双工监听无法天然判断节点来源，需要协议触发器、方向信号或外部硬件辅助。
7. Medium: 寄存器地址基准、大小端、word order、signed/unsigned、float 编码未显式建模，用户报告会难复现。
8. Medium: 多文件 append 保存没有事务边界，崩溃或磁盘错误时可能产生孤儿记录或报告与会话不一致。
9. Medium: High DPI、多显示器、远程桌面场景下，Win32 手写布局容易模糊、错位或缩放后控件重叠。
10. Low: 调试工具如果缺少危险写入保护、广播写入提示、审计记录，现场使用风险会被低估。

## Interview Must-Cover Topics

1. 后续新增功能优先级：协议扩展、脚本自动化、报告增强、设备 profile、批量产测、性能优化分别排第几。
2. 典型工作流：手动调试、长时间抓包、自动化回归、产线测试、现场排障是否都要覆盖。
3. 传输范围：只做串口/Modbus RTU，还是未来包含 Modbus TCP、TCP/UDP、HID、CAN 网关或设备模拟。
4. 性能目标：最高波特率、每秒帧数、最长会话、最大日志量、扫描周期、UI 可接受延迟。
5. Modbus 细节：地址基准、寄存器类型、端序、异常码、重试、超时、广播、网关 Unit ID、RTU/TCP 是否统一建模。
6. 高吞吐发送：是否需要发送队列、速率限制、批量命令、取消、失败重放和进度可视化。
7. 日志与报告：需要原始字节、解析帧、匹配结果、用户操作、错误、环境信息、版本元数据哪些字段。
8. 数据安全：是否可能记录设备密钥、客户现场数据、个人路径；导出报告是否需要脱敏。
9. 自动化能力：是否需要命令序列、触发器、条件断言、pass/fail、CLI、脚本或可重复测试模板。
10. UI 可用性：中文术语、密集信息布局、错误提示、DPI、多显示器、键盘操作和高频刷新策略。
11. 存储模型：会话、profile、报告、配置、历史记录是否需要 schema version 和迁移。
12. 验收矩阵：哪些 Windows 版本、真实硬件、虚拟串口、CI 自检、Wine 截图和性能日志必须覆盖。

## Sources

WebSearch queries run: 4.

- Microsoft Learn — Communications Resources: https://learn.microsoft.com/en-us/windows/win32/devio/communications-resources
- Microsoft Learn — About Communications Resources: https://learn.microsoft.com/en-us/windows/win32/devio/about-communications-resources
- Microsoft Learn — Configuring a Communications Resource: https://learn.microsoft.com/en-us/windows/win32/devio/configuring-a-communications-resource
- Microsoft Learn — Monitoring Communications Events: https://learn.microsoft.com/en-us/windows/win32/devio/monitoring-communications-events
- Microsoft Learn — Synchronous and Asynchronous I/O: https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o
- Microsoft Learn — Messages and Message Queues: https://learn.microsoft.com/en-us/windows/win32/winmsg/messages-and-message-queues
- Microsoft Learn — High DPI Desktop Application Development on Windows: https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows
- Qt Documentation — Serial Terminal example: https://doc.qt.io/qt-6/qtserialport-terminal-example.html
- Modbus protocol summary and specification references: https://en.wikipedia.org/wiki/Modbus
- Docklight — Serial protocol testing, analysis and simulation: https://docklight.de/
- RealTerm — Serial/TCP terminal for engineering and debugging: https://sourceforge.net/projects/realterm/

## 中文摘要

串口/Modbus native 工具的扩展重点应放在异步通信、协议事务状态机、结构化会话日志、设备 profile、批量 UI 刷新和可验证测试矩阵上。后续访谈必须明确功能优先级、性能门槛、Modbus 细节、安全边界、报告字段和 Windows UI 验收标准。
