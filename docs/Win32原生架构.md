# Win32 Native 架构

状态：当前 Win32 native 架构说明。本文描述 `svm-native-win32.exe` 的活动架构和维护边界。

## 架构目标

Win32 native 路线的目标是：

- 保持发布包轻量，不携带 Qt、SQLite 插件或 .NET/C# 运行库。
- 保持中文 UI 和本地串口调试体验稳定。
- 将协议、分析、报告和存储从 UI 依赖中拆出，便于测试。
- 对 resize、分割条拖动、标签切换、日志 flush 等 UI 热路径建立明确所有权和性能门禁。
- 以 GitHub Actions 编译 exe 和 artifact 作为工程交付验证目标。

## 分层结构

```text
svm-native-win32.exe
├── src/win32/             Win32 UI、消息、串口、日志、发送、扫描、偏好
├── src/core/              C++20 协议、Modbus、分析、报告核心
├── src/native_storage/    native 文件存储和记录编解码
├── src/transport/         串口契约、RTU adapter 和写队列
└── scripts + workflows    构建、截图、PTY、包体审计、artifact 证据
```

CMake 目标关系：

- `svm_slim_core`：由 `src/core/` 和 `src/transport/` 的无框架部分构成，给 Win32 native 和 native 测试复用。
- `svm_native_storage`：由 `src/native_storage/` 构成，负责 native 存储。
- `svm_win32_serial`：串口类型、端口枚举和 Win32 串口读写。
- `svm-native-win32`：最终 Win32 native GUI exe，链接以上目标和 `user32`、`gdi32`、`comctl32`、`comdlg32`、`shell32`。

## 主窗口边界

`NativeMainWindow` 是 Win32 窗口协调层，不应承载所有业务细节。它负责：

- 创建窗口、菜单和控件。
- 分发 Win32 消息、命令、定时器和自定义 UI frame 消息。
- 协调串口、发送、日志、文件发送、Modbus worker、分析和报告导出。
- 将布局、状态、绘制、日志过滤、串口 I/O 等职责委托给专门模块。

主窗口代码按职责拆分到 `main_window_*.cpp`：

- `main_window_messages.cpp`：消息入口和命令分发。
- `main_window_layout.cpp`：布局应用、分割条、frame 处理。
- `main_window_controls.cpp`：控件创建和初始化。
- `main_window_serial*.cpp`：串口连接、I/O、重连。
- `main_window_send.cpp`：单发、多发、文件发送和定时发送。
- `main_window_log.cpp`：日志队列、过滤、搜索、复制、导出。
- `main_window_modbus.cpp`：Modbus 扫描生命周期。
- `main_window_analysis.cpp`：候选、规则验证和报告导出。
- `main_window_preferences.cpp`：UI 偏好、日志缓存、原始记录保留。
- `main_window_self_test.cpp`：exe 自检和 UI 性能门禁。

## UI 热路径

UI 性能稳定性的核心链路是：

```text
Win32 event
  -> NativeFrameScheduler
  -> NativeLayoutModel
  -> NativeLayoutTransaction
  -> NativePaintPolicy
```

### NativeFrameScheduler

`NativeFrameScheduler` 合并高频 UI 请求。它记录 resize、分割条拖动、标签切换、日志 flush、状态、进度和 settle 等原因，只投递必要的自定义 frame 消息。

原则：

- 高频输入只请求 frame，不直接同步重排所有控件。
- 多个原因在同一 frame 中消费，避免重复 layout。
- 统计 `requestedFrames`、`coalescedRequests`、`consumedFrames` 和 `postFailures`，供自测和 UI perf 观察。

### NativeLayoutModel

`NativeLayoutModel` 是纯布局计算层。输入是 client 宽高、请求的 workbench 高度和当前 tab；输出是串口区、工作区、日志区、状态栏、当前页提示、标签页可见性和各控件矩形。

原则：

- 先计算完整模型，再应用到 HWND。
- 小窗口和极端尺寸必须给出稳定几何，不让控件出现负尺寸或不可达盲区。
- 标签页、当前页提示、日志区和分割条高度由同一模型约束。

### NativeLayoutTransaction

`NativeLayoutTransaction` 批量提交控件移动和显隐变化。它使用 deferred positioning，并统计请求、应用、跳过、失败和显隐操作数量。

原则：

- 避免散落 `MoveWindow` 引发多次重绘。
- 尺寸未变时跳过移动。
- 显隐变化和 z-order 调整集中提交。

### NativePaintPolicy

`NativePaintPolicy` 封装不同场景的 redraw flags：

- live region redraw。
- full refresh。
- log flush redraw。
- workbench tab redraw。
- workbench area redraw。
- no-redraw raise。

原则：

- 分割条拖动和标签切换尽量避免 erase background。
- 首次显示和必要全量刷新才使用更重的刷新路径。
- 控件提升、背景区刷新和日志 flush 分开处理，减少闪烁。

## 日志链路

日志链路分为三层：

- `NativeLogEntry` 和 `NativeLogFilterState`：日志数据、过滤、搜索和大小裁剪。
- `NativeLogView`：RichEdit/Edit 控件插入、滚动、选区和主题。
- `NativeMainWindow` 日志方法：排队、flush、重建、复制、导出、暂停滚动和跟随最新。

边界：

- 可见日志缓存限制 UI 渲染体量。
- native 存储保存原始 TX/RX 记录。
- 暂停滚动只影响 UI 跟随，不停止接收和存储。
- 日志 flush 走 `NativeFrameScheduler`，避免每行数据触发布局或完整刷新。

## 串口链路

串口链路由以下模块组成：

- `win32_serial_enumerator.*`：通过 Windows API 枚举串口描述。
- `transport/serial_session.h`：拆分串口生命周期、字节流和写调度三类窄契约。
- `win32_serial_session.*`：唯一生产会话所有者，拥有 Windows handle、写线程和串口参数实现。
- `transport/serial_rtu_transport.*`：将字节流契约映射为 Modbus RTU exchange，可由 fake byte stream 单测。
- `NativeSerialIoState`：限制当前 I/O 状态，避免并发发送/读写冲突。
- `NativeReconnectState`：异常断开后的自动重连状态。
- `main_window_serial_io.cpp`：轮询读取、错误处理、保存原始事件、更新 TX/RX 计数。

测试边界：

- `native_win32_serial_tests` 覆盖串口参数和基础状态。
- `serial_session_contract_tests` 覆盖串口会话生命周期、读写和队列语义。
- `native_modbus_transport_adapter_tests` 覆盖 RTU adapter 的分块响应、超时、取消和失败分类。
- `native_win32_serial_loopback_tests` 覆盖 normal、reopen、timeout、cancel、stress、close、stale 场景，需要 PTY 脚本提供端点。

## 发送链路

发送能力拆分为：

- `NativeSendCodec`：文本、HEX、十进制字节流、二进制字节流和编码转换。
- `NativeSendControlState`：发送模式、行尾、定时发送周期等 UI 状态。
- `NativeSendHistoryState`：发送历史。
- `NativeFileSendState`：文件分块发送、进度、停止和边界条件。
- `NativeMainWindow`：调用串口写入、写 TX 日志、保存原始事件。

原则：

- 发送前先转换为明确的字节 payload。
- 输入校验错误必须停留在发送层，不进入串口写入。
- 文件发送按块推进，避免长时间阻塞 UI。

## Modbus 和分析链路

Modbus 和分析链路分为：

- `src/core/modbus_core.*`：FC03/FC04 请求、响应和基础协议能力。
- `NativeModbusScanRequest`：从 UI 参数生成扫描请求。
- `NativeModbusScanWorker`：后台扫描 worker，向 UI 汇报进度、数据批次和完成结果。
- `NativeModbusScanUiState`：扫描运行状态、进度和取消状态。
- `NativeAnalysisWorkflow`：候选生成、规则保存、规则验证和报告输出协调。
- `src/core/analysis_core.*`、`report_core.*`：框架无关的分析和报告核心。
- `src/native_storage/`：保存扫描、候选、规则和验证结果。

原则：

- 扫描必须在串口连接后执行。
- 取消是请求式取消，当前请求结束后生效。
- 候选结果不能代替现场业务判断，报告只表达验证证据。

## 存储边界

`src/native_storage/` 提供 Win32 native 路线使用的本地存储。它覆盖：

- session。
- raw TX/RX 事件。
- 串口配置。
- UI 偏好。
- Modbus 扫描结果。
- 匹配候选。
- 协议字段规则。
- 规则验证结果。

该层不依赖 SQLite 插件，避免当前发布包重新携带 Qt/SQLite runtime。

## 自检和性能门禁

`svm-native-win32.exe --self-test` 覆盖：

- 核心协议、分析、报告。
- native 存储。
- 日志、发送、文件发送、历史、偏好。
- Modbus/候选/规则验证/报告导出。
- 分割条、布局、标签页基础行为。

`svm-native-win32.exe --ui-perf-test` 覆盖：

- 标签切换性能。
- layout pass 和 apply 次数。
- workbench 高度拖动帧。
- 日志批量 flush。
- 日志重建次数和可见行门禁。

UI capture 工作流和脚本在 exe 自检之外补充截图证据，覆盖默认窗口、小窗口、DPI、标签切换和分割条拖动。

## 维护规则

- 不把 Qt、SQLite 插件或 .NET runtime 引入 Win32 native 发布包。
- 不在 UI 热路径中做同步大重建、全窗口 erase 或无界日志拼接。
- 新状态优先设计成可单测的 `native_*_state` 或 core 模块。
- 新中文 UI 文案集中到 `ui_text.cpp`，避免散落硬编码。
- 新发布证据必须能被 Actions artifact、包体 summary 或测试日志复核。

## 相关文档

- [开发者指南](开发者指南.md)
- [测试与验证](测试与验证.md)
- [发布产物](发布产物.md)
- [过渡架构说明](架构说明.md)
