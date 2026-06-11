# Task 03 计划：成熟串口调试体验第一批功能

日期：2026-06-01

## 目标

在当前 Native 骨架基础上，把最小串口工作台推进到“可以作为基础串口调试器继续扩展”的状态。

本任务不做 Analysis Extension，不做复杂协议推断；只补齐成熟串口调试工具的第一批基础体验。

## 当前基线

已完成：

- C++20 + Qt6 + CMake + QSerialPort + SQLite 工程骨架；
- `RawIoEvent` / `CaptureBus`；
- `SerialPortEnumerator`；
- `SerialPortService`；
- `SessionStore` 写入 `raw_io_events`；
- `ConsoleModel` RX/TX 显示模型；
- 最小中文 Qt Widgets UI；
- 测试：`checksum_tests`、`session_store_tests`、`console_model_tests`；
- 当前验证：3/3 passed。

## 非目标

- 不接入拟合扫描/字段分析；
- 不做大规模 UI 重构；
- 不做 Windows 打包；
- 不做多窗口/多会话；
- 不做 Modbus/CRC 全量协议能力；
- 不引入 GPL/禁商用候选项目代码。

## 子任务拆分

### T03A：发送编码与行尾模型（已完成）

新增 UI 无关核心模型：

- `PayloadCodec`
  - 文本 → bytes；
  - HEX 字符串 → bytes；
  - bytes → HEX；
  - HEX 输入严格校验，非法字符给中文错误；
- `LineEnding`
  - None；
  - CR；
  - LF；
  - CRLF。

验收：

- 已新增 `payload_codec_tests`；
- 已覆盖：
  - 文本发送；
  - HEX 发送；
  - 空格分隔 HEX；
  - 奇数长度 HEX 报错；
  - 非法字符 HEX 报错；
  - CR/LF/CRLF 追加；
- `ctest` 全通过：当前 4/4 passed。

### T03B：UI 接入发送模式与行尾（已完成）

在 `MainWindow` 上小步增加：

- 发送模式选择：文本 / HEX；
- 行尾选择：无 / CR / LF / CRLF；
- 使用 `PayloadCodec` 统一生成 bytes；
- 错误提示中文化，不再静默失败。

验收：

- 构建通过；
- 不改变底层 `SerialPortService`；
- UI 仍只通过 `writeBytes(QByteArray)` 发送；
- 当前 `ctest` 4/4 passed。

### T03C：暂停滚动与清空接收区（已完成）

新增基础控制：

- 暂停滚动：继续接收、继续写 SQLite，但 UI 暂不追加；
- 恢复滚动：之后事件继续追加，不回放暂停期间积压，避免大流量卡顿；
- 清空接收区：只清 UI，不删 SQLite 记录。

验收：

- UI 逻辑清晰；
- 不影响 `CaptureBus` 与 `SessionStore`；
- 已通过 `ConsoleModel` 清空测试覆盖核心逻辑；
- 当前 `ctest` 4/4 passed。

### T03D：发送历史（已完成）

新增 UI 无关模型与 SQLite 存储：

- `send_history` 表；
- 保存最近发送内容、模式、行尾、时间；
- 去重：相同内容移到最新；
- 限制数量：默认 100；
- UI 提供简单下拉/历史复用入口。

验收：

- 已新增 `send_history_tests`；
- 已覆盖保存、去重、数量限制、读取排序；
- 当前 `ctest` 5/5 passed。

### T03E：串口配置 Profile（已完成）

新增串口配置持久化：

- `serial_profiles` 表；
- 保存：端口名、波特率、数据位、校验位、停止位、流控、DTR、RTS；
- 启动时恢复最近 profile；
- UI 暂先支持波特率/端口，预留完整字段。

验收：

- 已新增 `serial_profile_tests`；
- 已覆盖保存/读取/更新最近 profile；
- 不和 `raw_io_events` 混表；
- 当前 `ctest` 6/6 passed。

### T03F：中文错误诊断第一版（已完成）

新增 `SerialErrorTranslator`：

- PermissionError：提示串口被占用/权限不足；
- DeviceNotFoundError：提示设备不存在/已拔出；
- OpenError：提示打开失败并建议检查占用；
- ResourceError：提示设备断开或驱动异常；
- TimeoutError：提示设备无响应。

验收：

- 已新增 `serial_error_translator_tests`；
- `SerialPortService` 的错误信号优先输出中文诊断；
- 不吞掉原始 Qt errorString，保留用于调试；
- 当前 `ctest` 7/7 passed。

## 执行顺序

推荐顺序：

1. T03A：先做纯核心 `PayloadCodec`；
2. T03B：接 UI 发送模式/行尾；
3. T03C：暂停滚动/清空；
4. T03D：发送历史；
5. T03E：串口配置 profile；
6. T03F：中文错误诊断；
7. 全量构建和测试；
8. 更新 README / architecture；
9. commit。

## 质量门槛

每个子任务必须满足：

- 不复制 GPL / 禁商用候选代码；
- 不让 UI 直接解析协议；
- 不破坏 `RawIoEvent -> CaptureBus -> UI/SQLite` 主链路；
- 每次改动后运行：

```bash
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

## 完成定义

Task 03 完成时应达到：

- 可选择文本/HEX 发送；
- 可设置行尾；
- HEX 输入有严格校验与中文错误；
- 可暂停滚动、清空显示；
- 发送历史可保存/复用；
- 串口配置 profile 可保存；
- 串口错误有基础中文诊断；
- 所有新增能力有测试；
- `ctest` 7/7 passed。

## Task 03 总结

Task 03 已完成：文本/HEX 发送、行尾设置、暂停滚动、清空接收区、发送历史、串口配置 Profile、中文错误诊断均已落地，并通过 CMake 构建与 7 个 QtTest。
