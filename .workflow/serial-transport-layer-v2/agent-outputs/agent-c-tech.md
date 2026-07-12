# Agent C 技术生态与候选方案报告

## 1. 研究范围与结论

- 目标是下一阶段的统一**串口**传输层，不添加 TCP、UDP、网络服务、Qt、插件或默认运行时依赖。
- 当前仓库已经有 `SerialWritePort`、`SerialTransport`、`Win32SerialPort` 和 `SerialRtuTransport`；主窗口、Modbus worker、命令序列和测试均使用这些边界。
- 推荐继续以 Win32 communications API 为唯一生产后端，先做契约、所有权、取消和错误模型的分层；不要在本阶段引入 Boost.Asio。
- WIL 可以作为后续可选的头文件辅助库，但必须经过独立的 MinGW/包体/许可证与供应链门禁；它不是串口异步 I/O 实现。
- 如果要把当前同步 worker 演进为真正异步，应优先采用小范围、事件驱动的 Win32 overlapped I/O，而不是直接把整个应用改造成通用 executor/IOCP 框架。

## 2. DeepWiki 与回退记录

按 Phase 0 Agent C 协议对每个候选执行 `structure` 与 `ask`。DeepWiki MCP 在本环境均返回退出码 `6` 且没有正文；对 Boost.Asio 和 WIL 又各执行了一次重试，结果相同。候选与命令如下：

| 候选 | DeepWiki 尝试 | 结果 | 回退 |
| --- | --- | --- | --- |
| `boostorg/asio` | `DEEPWIKI_RETRIES=1 ... deepwiki.sh structure "boostorg/asio"`；两次 `ask`（含重试） | 退出码 6，无输出 | WebSearch 工具返回 `stream error: failed to decode search response`；随后读取 Boost 官方文档 URL |
| `microsoft/wil` | `... structure "microsoft/wil"`；两次 `ask`（含重试） | 退出码 6，无输出 | WebSearch 同上；随后读取 WIL 官方 GitHub API/README 与头文件 |
| `microsoft/Windows-classic-samples` | `... structure`；`ask`（含重试） | 退出码 6，无输出 | WebSearch 同上；随后读取官方样例树、`IoCancellation.c` 与 Microsoft Learn |

DeepWiki 没有可缓存的答案，因此以下判断只使用可访问的第一方资料、仓库现有实现和已有回归证据。未将 DeepWiki 失败当作候选方案本身的否定证据。

## 3. 候选评估

### A. C++ 标准库加薄 Win32 适配

标准库可以提供 `std::jthread`、`std::stop_token`、互斥/条件变量、时间和容器，但不能打开 COM 设备、配置 `DCB`/`COMMTIMEOUTS`、调用 `ReadFile`/`WriteFile`，也不能替代 `CancelIoEx` 或 overlapped completion。因此“标准库-only”只能覆盖核心契约和调度层，不能成为串口后端。

适配方式：核心保持平台无关的 `SerialTransport`/`SerialWritePort`/协议接口，Win32 层封装句柄、事件、`OVERLAPPED` 和错误码。`stop_token` 可作为上层请求取消信号，但底层仍必须把它映射到 `CancelIoEx`、唤醒事件或 worker join。

优点：零第三方依赖、当前 CMake/MinGW/Wine/包体门禁无需改变、fake transport 易于测试。缺点：需要自行保证句柄和 buffer 生命周期、短写、超时、取消竞态和错误分类。

结论：作为 v2 的默认方向；把重复的 Win32 生命周期逻辑封装在一个后端 session 中，而不是把 Win32 类型泄露到协议层。

### B. Boost.Asio `serial_port`

Boost 官方串口概览说明 `serial_port` 以 `io_context` 为执行上下文，可配合 `read`/`async_read`/`write`/`async_write`，并提供波特率、流控、校验位、停止位和字符宽度选项。Windows 串口支持依赖 I/O completion port backend，并可用 `BOOST_ASIO_HAS_SERIAL_PORT` 检测；官方参考还标明共享同一个 serial object 不安全。

`basic_serial_port::cancel()` 会取消该串口关联的异步操作；关闭也必须与 handler 生命周期协调。`async_read_some` 明确要求底层 buffer 在 completion handler 调用前保持有效，并且可能只读取部分字节；其文档列出 Windows 上的 per-operation cancellation 类型。这些语义可以解决部分生命周期问题，但会把当前同步/队列 API 迁移到 Asio completion token、executor、handler 和 `error_code` 模型。

部署影响：Boost 头文件和 Boost.System/CMake 集成需要被固定并纳入 MinGW 构建；即使最终没有 DLL，也会扩大供应链、缓存、许可证和包审计面。Windows IOCP 与现有同步 `Win32SerialPort`、UI message loop、队列结果模型之间没有零迁移成本的适配层。

结论：技术上可行，适合需要多个异步设备和统一 executor 的产品；对当前只有串口、单读者/单写者、已有稳定测试和严格轻量包体的项目，收益不足以抵消迁移风险。本工作流不引入。

官方资料：

- [Boost.Asio serial ports overview](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/serial_ports.html)
- [Boost.Asio `serial_port` reference](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/serial_port.html)
- [`basic_serial_port::cancel`](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/basic_serial_port/cancel.html)
- [`basic_serial_port::async_read_some`](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/basic_serial_port/async_read_some.html)

### C. Microsoft WIL

WIL 官方 README 将其定义为 header-only C++ library，提供 `wil::unique_handle` 等 RAII 资源封装、Win32 helper、错误处理宏和部分同步/线程池资源类型。它可以降低 `HANDLE`、event、thread-pool object 和 Win32 error cleanup 的样板代码；不会提供 COM 配置、串口读写、帧组装、写队列或协议取消语义。

虽然没有运行时 DLL，官方仍要求为每个目标架构安装/暴露 WIL 包；README 给出了 Git 子模块、复制文件、NuGet 和 vcpkg 的消费方式。对本仓库而言，这会增加依赖来源、版本锁定、包审计和 MinGW 编译兼容性门禁。WIL 文档和仓库主要以 MSVC/Windows SDK 生态为中心，MinGW 兼容性不能仅凭 header-only 属性假定成立。

结论：不作为 v2 必选依赖。若后续明确需要，可只在 Win32 backend 内以 vendored、固定版本的方式使用 RAII/error helper，并保留无 WIL 的编译路径；先做 x64 MinGW/Wine 和包体双门禁。

官方资料：

- [Microsoft WIL README](https://github.com/microsoft/wil/blob/master/README.md)
- [WIL RAII resource wrappers](https://github.com/microsoft/wil/wiki/RAII-resource-wrappers)
- [WIL error-handling helpers](https://github.com/microsoft/wil/wiki/Error-handling-helpers)

### D. 直接 Win32 overlapped I/O（推荐后端演进方向）

Microsoft 的通信资源文档把串口定义为双向异步数据流，并以 communications resource handle、`DCB`、`COMMTIMEOUTS`、`ReadFile`/`WriteFile` 为基础。Microsoft 的取消指南强调：`CancelIoEx` 只是请求取消，不等待完成；调用方必须等待 overlapped 完成后才能复用或释放 `OVERLAPPED`/buffer，最终状态要检查 `ERROR_OPERATION_ABORTED`。取消也可能因为驱动或操作状态而不成立。

官方 Windows classic samples 中的 `IoCancellation` 展示了跨线程取消与 critical-section 状态保护；它针对同步 I/O 的 `CancelSynchronousIo`，不能直接替代 serial overlapped design，但清楚说明了“进入可取消区段、发出取消、离开后确认最终状态”的生命周期原则。

实现选择：

- **事件式 overlapped（首选）**：一个 reader、一个 writer；每个 operation 拥有自己的 `OVERLAPPED`、event 和 byte buffer。超时或 stop 请求调用 `CancelIoEx(handle, &overlapped)`，随后等待 completion，再发布结果；关闭顺序是 stop → cancel → drain → join → close handle。
- **IOCP（暂缓）**：可扩展多个并发 operation，但会引入 completion-port 线程、completion key、operation context 和更复杂的 shutdown protocol。Boost 文档也把 Windows serial support 与 IOCP backend 绑定；在单串口桌面工具中没有足够收益证明这次迁移。
- **当前同步 worker（兼容基线）**：短读超时加 worker 仍可保留为第一步；先把 stop/cancel/error ownership 固化，再单独切换 overlapped backend，避免同时改变读路径和 UI 行为。

官方资料：

- [Communications resources](https://learn.microsoft.com/en-us/windows/win32/devio/communications-resources)
- [Canceling pending I/O operations](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)
- [`CancelIoEx` function](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- [Windows classic samples: I/O cancellation](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winbase/io/cancel/create)

## 4. 对 v2 架构的具体建议

1. 保留一个兼容性的 `SerialTransport` façade，但内部拆成四个窄边界：生命周期/配置、byte-stream 读写、写调度/背压、取消与 operation result。协议适配器只依赖 byte-stream 或 exchange contract。
2. 建立 `SerialSession` 单一所有权：它拥有 handle、read/write worker 或 overlapped state、wake/cancel event、buffer 和 endpoint。禁止 UI、Modbus worker 或 command sequence 保存裸 handle 或跨线程借用 backend 内部 buffer。
3. 明确并发规则：一个活动 reader、一个串行化 writer；所有结果通过带 request id、bytes transferred、timeout/cancel/error code 的 terminal event 发布。队列满、短写、关闭中的 enqueue 要有稳定分类。
4. 将 `std::stop_token`/项目取消标志作为上层意图，Win32 `CancelIoEx` 或 wake event 作为后端动作；取消后必须 drain completion，再回收 operation context。
5. 把 Win32 numeric error code 与 transport error kind 保留到边界，中文用户文案只在 UI adapter 生成；这样 PTY/Wine 和 fake 测试可以断言稳定类别，而不依赖本地化字符串。
6. 先实现同步兼容 backend 的 contract tests，再新增 overlapped backend 的 focused tests；不要在同一个任务中同时引入 Boost/WIL、改变协议 framing 和重写 UI。

## 5. 依赖与决策门

| 决策 | 默认结论 | 必须通过的门禁 |
| --- | --- | --- |
| C++ 标准库 | 采用 | Linux native build、MinGW/Wine、无新增包体依赖 |
| 直接 Win32 API | 采用 | overlapped/close/cancel race、PTY timeout/cancel/reopen、handle leak 检查 |
| Boost.Asio | 不采用 | 只有在需要多设备/统一 executor 时重新提案；届时固定版本、CMake/vcpkg、MinGW、包审计和性能基线 |
| WIL | 暂不采用 | 若引入，固定 vendored 版本、两架构编译、无 DLL、许可证/供应链审计，并保留可禁用开关 |
| TCP/UDP | 明确排除 | 不进入本工作流计划或实现 |

## 6. 风险与验证清单

- 最大风险不是 API 选择，而是 close/cancel 与 `OVERLAPPED`/buffer 生命周期竞态；每次取消都必须等最终 completion。
- 需要增加 close while read pending、cancel while write pending、reopen after cancellation、partial write、queue saturation、driver timeout 和 repeated open/close 的回归用例。
- 继续使用 fake transport 做核心契约测试，用 PTY loopback 做真实 Windows backend 测试，用 MinGW/Wine 包体审计确认没有隐式 Qt/Boost/WIL runtime。
- 任何第三方库引入都必须先更新依赖清单、构建矩阵和包审计规则，再进入实现阶段；本报告不授权添加依赖。
