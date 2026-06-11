# 架构说明

```text
Qt Widgets App / MainWindow
  ├─ SerialPortService / SerialReconnectPolicy
  ├─ ModbusScanWorker (QThread)
  │   └─ QtSerialByteChannel -> ModbusRtuSerialTransport -> ModbusScanExecutor
  ├─ CaptureBus
  ├─ ConsoleModel
  ├─ SessionStore(SQLite)
  │   ├─ schema / raw events / send history / serial profile
  │   ├─ scan sessions / attempts / observations
  │   ├─ matching / stability persistence
  │   └─ protocol rules / rule verification persistence
  ├─ Modbus Core
  │   ├─ RTU codec / read request / read response
  │   └─ scan plan / scan executor / serial transport
  ├─ Matching Core
  │   ├─ NumericDecoder
  │   ├─ ValueCandidateGenerator
  │   ├─ CandidateStabilityAnalyzer
  │   └─ ProtocolRuleVerifier
  └─ Report
      └─ RuleVerificationReport / TextFileWriter
```

## 核心原则

- 串口服务只负责传输，不直接做协议推断。
- UI 只展示事件，不直接拥有分析逻辑。
- 日志与分析都从 `CaptureBus` 订阅同一份 `RawIoEvent`。
- 任何私有协议规则都必须能保存、回放、测试、导出证据。

## 当前架构快照

- `MainWindow` 保留 UI 编排职责，串口基础连接仍由 `SerialPortService` 管理，Modbus 扫描通过 `ModbusScanWorker` 放到工作线程执行。
- `QtSerialByteChannel` 是扫描专用的阻塞式串口字节通道，集中处理串口打开、DTR/RTS 设置、写入完整性、等待响应和 `QSerialPort::errorString()` 诊断。
- `ModbusRtuSerialTransport` 负责 RTU 请求/响应交换和 `CaptureBus` 原始事件发布；`ModbusScanExecutor` 负责扫描计划执行、重试和取消检查。
- `SessionStore` 公共接口集中在 `session_store.h`，实现拆分到 `session_store_schema.cpp`、`session_store_scan.cpp`、`session_store_matching.cpp`、`session_store_rules.cpp`，降低单文件热点风险。
- 扫描读取、规则读取和规则验证读取会通过 `clearReadError()` / `setReadError()` 区分“没有数据”和“数据库读取失败”；`scanObservationsByIds()` 使用生成占位符和 `bindValue()`，不把 ID 列表直接拼入 SQL 值。
- `NumericDecoder` 统一候选生成与协议规则验证的数值解码；候选生成使用有界 Top-N，规则验证预索引每个 `(slaveId, functionCode, address)` 的最新观测样本。
- Linux CI 与本地最终验证使用同一条基本链路：`scripts/check-env.sh`、CMake configure、`cmake --build --parallel 1`、CTest。

## 2026-06-01 Task 02 进展

已落地的第一批串口调试器内核能力：

- `SerialPortEnumerator`：基于 `QSerialPortInfo::availablePorts()` 枚举端口，输出端口名、系统路径、描述、厂商、序列号、VID/PID。
- `SerialPortService`：负责打开/关闭串口、发送字节、接收字节，并将 RX/TX 都转换为 `RawIoEvent`。
- `CaptureBus`：统一事件总线，UI、SQLite、后续 Analysis Extension 都从这里订阅。
- `SessionStore`：使用 SQLite 保存 `raw_io_events`，包含 session、direction、timestamp、endpoint、payload。
- `ConsoleModel`：将 `RawIoEvent` 格式化为中文 UI 可显示的 RX/TX 行，同时保留 HEX 和文本预览。
- `MainWindow`：最小中文工作台，支持刷新串口、选择波特率、连接/断开、发送 UTF-8 文本、显示事件。

验证：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

当前测试：

- `checksum_tests`
- `session_store_tests`
- `console_model_tests`

结果：3/3 passed。


## 2026-06-01 Task 03A 进展

已新增 UI 无关发送编码模型：

- `PayloadMode`：Text / Hex；
- `LineEnding`：None / CR / LF / CRLF；
- `PayloadCodec::encode()`：统一将 UI 输入转换为待发送 bytes；
- `PayloadCodec::bytesToHex()`：统一 HEX 显示格式；
- HEX 输入严格校验，非法字符与奇数长度会返回中文错误。

验证：新增 `payload_codec_tests`，当前 `ctest` 4/4 passed。


## 2026-06-01 Task 03B 进展

已将发送编码模型接入最小中文 UI：

- 发送模式下拉：文本 / HEX；
- 行尾下拉：无行尾 / CR / LF / CRLF；
- 发送按钮统一调用 `PayloadCodec::encode()`；
- HEX 输入错误通过中文提示展示；
- 底层仍只通过 `SerialPortService::writeBytes(QByteArray)` 发送，不让 UI 直接碰 `QSerialPort`。

验证：`cmake --build build --parallel 1` 成功，`ctest` 4/4 passed。


## 2026-06-01 Task 03C 进展

已增加接收区基础操作：

- 工具栏“暂停滚动”：暂停时仍继续接收、继续写入 SQLite、继续进入 `ConsoleModel`，但 UI 不再追加文本；
- 工具栏“恢复滚动”：恢复后只显示新的通信事件，不回放暂停期间积压，避免大流量卡顿；
- 工具栏“清空接收区”：清空 UI 与 `ConsoleModel`，不删除 SQLite 原始通信记录。

验证：新增 `ConsoleModel::clear()` 覆盖测试，`cmake --build build --parallel 1` 成功，`ctest` 4/4 passed。


## 2026-06-01 Task 03D 进展

已增加发送历史能力：

- SQLite 新增 `send_history` 表；
- `SessionStore::saveSendHistory()`：保存发送内容、发送模式、行尾、时间；
- 相同内容 + 模式 + 行尾去重，重复发送会移动到最新；
- 默认保留最近 100 条；
- `SessionStore::recentSendHistory()`：按最近发送倒序读取；
- UI 新增“发送历史”下拉，选择历史项会恢复内容、文本/HEX 模式和行尾。

验证：新增 `send_history_tests`，覆盖保存、去重、数量限制、读取排序；当前 `ctest` 5/5 passed。


## 2026-06-01 Task 03E 进展

已增加串口配置 Profile 持久化：

- SQLite 新增 `serial_profiles` 表；
- `SerialProfile` 保存端口名、波特率、数据位、校验位、停止位、流控、DTR、RTS；
- `SessionStore::saveSerialProfile()` 保存/更新配置；
- `SessionStore::latestSerialProfile()` 读取最近配置；
- UI 连接成功后保存当前 profile；
- 启动时在端口刷新后恢复最近 profile 中的端口与波特率。

验证：新增 `serial_profile_tests`，当前 `ctest` 6/6 passed。


## 2026-06-01 Task 03F 进展

已增加串口中文错误诊断第一版：

- 新增 `SerialErrorTranslator`；
- 覆盖 `PermissionError`、`DeviceNotFoundError`、`OpenError`、`NotOpenError`、`WriteError`、`ReadError`、`ResourceError`、`UnsupportedOperationError`、`TimeoutError`；
- `SerialPortService::open()` 打开失败时使用中文诊断；
- `SerialPortService::onErrorOccurred()` 运行期错误使用中文诊断；
- 中文提示保留 Qt 原始 `errorString()`，便于后续调试。

验证：新增 `serial_error_translator_tests`，当前 `ctest` 7/7 passed。

Task 03 已完成。


## 2026-06-02 Task 04A 进展

已把连接区扩展为完整串口参数 UI：

- 数据位：8 / 7 / 6 / 5；
- 校验位：None / Even / Odd / Space / Mark；
- 停止位：1 / 1.5 / 2；
- 流控：None / Hardware / Software；
- DTR / RTS 开关。

这些 UI 选择会进入 `SerialOpenOptions`，打开串口时传给 `SerialPortService`，连接成功后写入 `SerialProfile`，下次启动时恢复。

验证：`cmake --build build --parallel 1` 成功，`ctest` 7/7 passed。


## 2026-06-02 Task 04B 进展

已完善端口刷新后的选择保持策略：

- 新增 `SerialPortSelectionPolicy`，把端口选择规则从 UI 中抽出，便于单元测试；
- 刷新串口时优先保留当前仍存在的端口；
- 当前端口消失时，尝试恢复最近 `SerialProfile` 中的端口；
- 当前端口和 Profile 端口都不可用时，选择第一个可用端口；
- 无可用串口时不强行制造选择，状态栏提示“未发现可用串口”。

验证：`cmake --build build` 成功，`ctest` 8/8 passed。


## 2026-06-02 Task 04C 进展

已增加自动重连骨架：

- 连接区增加“自动重连”开关，默认关闭；
- `SerialPortService` 在保留 `errorOccurred(QString)` 的同时新增 `serialErrorOccurred(QSerialPort::SerialPortError, QString)`，便于 UI 区分设备断开类错误；
- 新增 `SerialReconnectPolicy`，集中管理自动重连开关、上次成功连接参数、等待状态和“一次尝试”限制；
- `ResourceError` / `DeviceNotFoundError` 且已有上次成功参数时进入等待重连；
- 等待期间每 2 秒刷新端口；目标端口重新出现后，按上次成功的完整串口参数尝试一次重连；
- 重连失败后停止等待，不做无限循环弹窗。

验证：默认并行构建在低内存 VM 上曾被 SIGKILL；改用 `cmake --build build --parallel 1` 成功，`ctest` 9/9 passed。


## 2026-06-02 Task 04D 进展

已细化连接区 Profile 持久化：

- 连接区新增“保存配置”按钮，用户无需先连接成功，也能主动保存当前默认串口配置；
- 保存内容使用 `currentOpenOptions()`，覆盖串口、波特率、数据位、校验位、停止位、流控、DTR、RTS；
- 启动时继续通过 `applyLatestSerialProfile()` 恢复最近 Profile，并覆盖完整串口参数；
- `serial_profile_tests` 增强同名 Profile 更新测试，验证完整串口字段都会被新配置覆盖。

验证：`cmake --build build --parallel 1` 成功，`ctest` 9/9 passed。

Task 04 已完成。


## 2026-06-02 Task 05A 进展

已启动 Modbus RTU 只读扫描内核，完成第一步 RTU 帧校验模型：

- 新增 `src/modbus/modbus_rtu_codec.h/.cpp`；
- `crc16Modbus()` 支持 CRC16/MODBUS 计算；
- `appendCrc16Modbus()` 按 Modbus RTU 小端顺序把 CRC 追加到帧尾；
- `validateRtuFrame()` 可校验短帧和 CRC mismatch，并返回中文错误原因；
- `modbus_rtu_codec_tests` 覆盖已知向量、CRC 追加、合法帧校验、短帧、CRC 错误和诊断格式。

验证：`cmake --build build --parallel 1` 成功，`ctest` 10/10 passed。


## 2026-06-02 Task 05B 进展

已完成 Modbus RTU FC03 / FC04 只读请求构造：

- 新增 `src/modbus/modbus_read_request.h/.cpp`；
- `ModbusReadFunction` 支持 FC03 保持寄存器和 FC04 输入寄存器；
- `buildReadRequest()` 生成完整 RTU 请求帧并追加 CRC16/MODBUS；
- 校验从站 ID、功能码、起始地址、读取数量和地址溢出；
- 明确拒绝广播地址 `0` 和非 FC03/FC04 功能码；
- 中文描述功能码与错误原因，避免裸英文协议枚举。

验证：`cmake --build build --parallel 1` 成功，`ctest` 11/11 passed。


## 2026-06-02 Task 05C 进展

已完成 Modbus RTU 基础响应解析：

- 新增 `src/modbus/modbus_read_response.h/.cpp`；
- `parseReadResponse()` 在 CRC 校验后解析 FC03/FC04 正常读响应；
- 输出寄存器值列表和带地址的 `RegisterObservation`；
- 结构化处理 Modbus 异常响应，保留异常码和中文说明；
- 校验从站 ID、功能码、byte count、寄存器数量和地址范围；
- 失败响应不伪装为成功，后续扫描执行器可记录错误上下文。

验证：`cmake --build build --parallel 1` 成功，`ctest` 12/12 passed。
