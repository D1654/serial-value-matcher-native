# Task 05 计划：Modbus RTU 只读扫描内核

日期：2026-06-02

## 1. 背景

Task 03 / Task 04 已完成串口调试底座：串口连接、完整参数、Profile、自动重连、文本/HEX 发送、RX/TX 事件、SQLite、发送历史和中文错误诊断。

经过 `requirements-review-20260602.md` 回视，下一步不应优先做普通串口助手的搜索/过滤/宏，而应回到 SerialValueMatcher 的核心业务主线：

```text
已知现场目标值 -> 只读扫描设备数据 -> 生成候选 -> 多样本验证 -> 证据与导出
```

因此 Task 05 聚焦 **Modbus RTU 只读扫描内核**。本任务先做 UI 无关、可测试、可持久化的扫描基础，不急于做完整扫描页面。

## 2. 目标

建立 Native 版 Modbus RTU 只读扫描的第一批核心能力：

- 构造 FC03 / FC04 只读请求帧；
- 校验 CRC16/MODBUS；
- 解析基础响应帧；
- 建立扫描范围、安全约束和请求规划；
- 使用假响应/假传输做自动化测试；
- 落地最小扫描会话与寄存器观测持久化；
- 定义真实串口传输契约，避免 fake executor 与真实串口脱节；
- 为后续候选生成与 UI 工作流准备可复盘的数据结构。

## 3. 非目标

本任务不做：

- 写寄存器 / 控制命令；
- 广播从站 ID；
- 完整候选匹配算法；
- 多目标样本复读；
- 完整扫描 UI；
- 完整历史回放页面；
- 完整报告导出；
- 私有协议规则；
- 报告导出；
- Windows 打包。

## 4. 设计原则

### 4.1 只读安全优先

- 仅支持 FC03 / FC04；
- 拒绝 slave id `0`；
- 地址范围、块大小、请求数量必须可估算；
- 默认保守请求节奏；
- 所有危险能力保持缺省不可用。

### 4.2 UI 无关，优先核心测试

Task 05 先在 core 层完成：

- frame codec；
- scan planner；
- fake response tests；
- minimal scan result model。

UI 只在后续 Task 08 进入完整工作流页面。

### 4.3 为候选匹配保留事实

扫描结果不能只是临时显示文本，必须能落入 SQLite 或等价 repository，并转换成后续候选生成需要的事实：

- session id；
- slave id；
- function code；
- register address；
- raw register value；
- raw request / response bytes；
- timestamp；
- error / timeout 状态。

## 5. 子任务

### T05A：CRC16/MODBUS 与 RTU 帧模型（已完成）

新增：

- `ModbusRtuFrame` 或等价数据结构；
- CRC16/MODBUS 计算；
- 请求帧末尾 CRC LE 编码；
- 响应帧 CRC 验证。

验收：

- 已知向量测试通过；
- CRC 错误响应能被拒绝；
- 空帧/短帧能给出中文错误原因。

完成记录：

- 新增 `src/modbus/modbus_rtu_codec.h/.cpp`；
- 新增 `crc16Modbus()`、`appendCrc16Modbus()`、`validateRtuFrame()`；
- CRC 按 Modbus RTU 低字节在前写入帧尾；
- CRC 错误、短帧返回中文诊断；
- 新增 `modbus_rtu_codec_tests` 覆盖已知向量、CRC 追加、合法帧校验、短帧、CRC mismatch、诊断格式；
- 验证：`cmake --build build --parallel 1` 成功，`ctest` 10/10 passed。

### T05B：FC03 / FC04 请求构造（已完成）

新增：

- 只读功能码枚举；
- `buildReadRequest(slaveId, function, startAddress, quantity)`；
- slave id、地址、数量校验；
- 拒绝 slave id 0；
- 拒绝非 FC03 / FC04。

验收：

- FC03 请求帧测试；
- FC04 请求帧测试；
- 非法 slave id / quantity / address 测试；
- 中文错误提示测试。

完成记录：

- 新增 `src/modbus/modbus_read_request.h/.cpp`；
- 新增 `ModbusReadFunction`，支持 FC03 保持寄存器与 FC04 输入寄存器；
- `buildReadRequest()` 校验从站 ID、功能码、起始地址、读取数量和地址溢出；
- 拒绝广播从站 ID `0`，拒绝非 FC03/FC04，拒绝超出 1-125 的读取数量；
- 合法请求生成完整 RTU 帧并追加 CRC16/MODBUS；
- 新增 `modbus_read_request_tests` 覆盖 FC03、FC04、广播地址、非法功能码、非法数量、地址溢出、中文功能码描述；
- 验证：`cmake --build build --parallel 1` 成功，`ctest` 11/11 passed。

### T05C：基础响应解析（已完成）

新增：

- 解析正常读响应：slave id、function、byte count、register values；
- 解析异常响应：function | 0x80、exception code；
- 校验 byte count 与寄存器数量；
- 输出结构化寄存器观测。

验收：

- 正常响应解析测试；
- 异常响应解析测试；
- CRC 错误、长度不匹配、从站/功能码不匹配测试。

完成记录：

- 新增 `src/modbus/modbus_read_response.h/.cpp`；
- `parseReadResponse()` 校验 RTU CRC 后解析 FC03/FC04 正常响应；
- 输出寄存器值和带地址的 `RegisterObservation`；
- 结构化处理 Modbus 异常响应（function | 0x80 + exception code）；
- 校验从站 ID、功能码、byte count、寄存器数量和地址范围；
- 新增 `modbus_read_response_tests` 覆盖正常 FC03/FC04、异常响应、CRC 错误、从站/功能码不匹配、字节数不匹配、寄存器数量不匹配、异常码中文描述；
- 验证：`cmake --build build --parallel 1` 成功，`ctest` 12/12 passed。

### T05D：扫描范围与请求规划（已完成）

新增：

- `ScanRange` / `ScanPlan`；
- 起始地址、结束地址、块大小；
- 生成多个只读请求块；
- 估算请求数；
- 预留请求间隔和安全等级字段。

验收：

- 小范围单块；
- 大范围多块；
- 地址边界；
- 块大小边界；
- 请求数量估算。

完成记录：

- 新增 `src/modbus/modbus_scan_plan.h/.cpp`；
- `ScanPlanOptions` 支持从站 ID、FC03/FC04、扫描范围、块大小、请求间隔、重试次数和安全等级；
- `buildScanPlan()` 将范围拆分为多个 `ScanBlock`，每块直接生成可发送的只读 RTU 请求帧；
- 校验起始/结束地址、反向范围、块大小 1-64、最大单次计划 4096 寄存器、重试次数 0-5、广播地址和非只读功能码；
- `ScanPlan` 提供寄存器总数、请求块数、含重试的预计尝试数、请求间隔总耗时估算；
- 新增 `modbus_scan_plan_tests` 覆盖小范围单块、大范围多块、65535 地址边界、块大小边界、过大计划、安全等级中文描述和请求数量估算；
- 验证：`cmake --build build --parallel 1` 成功，`ctest` 13/13 passed。

### T05E：假传输扫描执行器（已完成）

新增：

- UI 无关扫描执行器；
- fake transport / fake response provider；
- 按 `ScanPlan` 发请求、收响应、产生观测；
- 失败响应记录错误，不直接丢弃上下文。

验收：

- 假设备返回多块寄存器，扫描结果完整；
- 某一块异常，不影响记录整体扫描状态；
- 请求与响应原始 bytes 可追溯。

完成记录：

- 新增 `src/modbus/modbus_rtu_transport.h`，定义 UI 无关 `ModbusRtuTransport` 抽象和 `ModbusTransportExchange`，用于后续 fake/真实串口 adapter 共用边界；
- 新增 `src/modbus/modbus_scan_executor.h/.cpp`，`ModbusScanExecutor` 按 `ScanPlan.blocks` 顺序发送请求、接收响应并调用 `parseReadResponse()` 生成 `ScanObservation`；
- 新增 `ScanAttemptResult` / `ScanBlockResult` / `ScanExecutionResult`，保留 blockIndex、attemptIndex、请求原始帧、响应原始帧、时间、endpoint、错误信息和 Modbus 异常信息，避免寄存器观测重复携带大块 bytes；
- 支持超时、传输错误、CRC/从站/功能码等解析错误、Modbus exception 的结构化记录；默认单块失败继续扫描后续块；
- 支持基于 `plan.retryCount` 的重试，默认重试超时和传输错误，不默认重试解析错误或 Modbus 异常；
- 新增 `modbus_scan_executor_tests`，覆盖单块成功、多块顺序扫描、异常块继续、CRC 错误、从站/功能码不匹配、超时、传输错误、重试后成功、全部重试失败、空计划防御和 timeout 参数传递；
- 验证：`cmake --build build --parallel 1` 成功，`ctest` 14/14 passed。

### T05F：最小扫描持久化落地（已完成）

本任务必须落地最小持久化，而不是只做设计检查。

新增：

- scan session 最小记录；
- register observation 最小记录；
- 请求/响应原始 bytes 与 `RawIoEvent` / communication record 的关联策略；
- error / timeout 记录方式；
- repository 或等价存取接口；
- 临时 SQLite 测试。

验收：

- fake executor 扫描后能保存 scan session；
- 每个寄存器观测能保存地址、功能码、原始值、采集时间和关联请求/响应；
- 异常块或超时能保存错误上下文；
- 重新打开 store 后仍能读取扫描事实；
- 后续 Task 06/07 可直接使用这些事实生成候选和证据。

完成记录：

- 新增 `src/storage/scan_persistence_records.h`，定义 `ScanSessionRecord`、`ScanAttemptRecord`、`ScanObservationRecord` 和一次扫描执行的持久化 DTO；
- `SessionStore` 新增 `scan_sessions`、`scan_attempts`、`scan_observations` 三张表和索引；
- 新增 `saveScanExecution()`、`scanSession()`、`scanAttempts()`、`scanObservations()`，用 SQLite transaction 保存一次扫描事实；
- `scan_attempts` 直接保存每次 attempt 的 request/response 原始 BLOB、状态、错误信息、Modbus 异常码/中文说明、时间和 endpoint，保证重开 SQLite 后可自包含追溯；
- `scan_observations` 保存 session、block/attempt、slaveId、functionCode、address、raw value、observedAtUtc，后续 Task 06/07 可直接用于候选生成；
- 新增 `modbus_scan_persistence_tests`，使用 T05E fake executor 扫描后保存并重新打开 SQLite 读回 session/attempt/observation，覆盖成功扫描和 Modbus 异常块上下文；
- 验证：`cmake --build build --parallel 1` 成功，`ctest --test-dir build --output-on-failure` 15/15 passed。

### T05G：真实串口传输契约与 adapter 边界（已完成）

新增：

- UI 无关传输接口，例如 `ModbusRtuTransport` 或等价抽象；
- fake transport 用于自动化测试；
- real serial adapter 边界，用于后续接入 `SerialPortService` / `QSerialPort`；
- 超时、响应等待、错误返回的统一结构；
- 明确不在 UI 线程阻塞等待响应。

验收：

- scan executor 只依赖传输接口，不直接依赖 `MainWindow`；
- fake transport 和 real adapter 边界使用同一请求/响应结构；
- 请求和响应仍可进入 `CaptureBus` / SQLite 事实链；
- 文档说明真实硬件验证留到后续 Windows/实机阶段。

完成记录：

- 新增 `src/modbus/modbus_rtu_byte_channel.h`，定义低层字节通道契约：open/endpoint/error/write/wait/read；
- 新增 `ModbusRtuSerialTransport`，实现 frame-level `ModbusRtuTransport::exchange()`，scan executor 仍只依赖 `ModbusRtuTransport&`，不依赖 `MainWindow` 或 `SerialPortService`；
- adapter 写出完整 request 后循环等待响应，按 request/response 前导字节推导正常 FC03/FC04 或异常响应的期望帧长；读到完整帧才返回 Success，解析正确性交给 `parseReadResponse()`；
- timeout 时保留已收到的 partial response bytes，partial write / channel closed / empty request 返回 TransportError；
- 可选接入 `CaptureBus` 发布 TX/RX `RawIoEvent`，endpoint 来自 byte channel，SQLite 仍通过 T05F `scan_attempts` 自包含保存 request/response BLOB；
- 新增 `modbus_rtu_serial_transport_tests`，用 fake byte channel 覆盖分片正常响应、5 字节异常响应、无响应超时、partial response 超时、closed channel、partial write 和 CaptureBus TX/RX 事件；
- 明确真实硬件验证、RTU 3.5 字符间隔、RS-485 方向控制、回显过滤、噪声重同步、设备 busy 退避等留到后续实机阶段；
- 验证：干净 `build-t05g` 单线程构建成功，`ctest --test-dir build-t05g --output-on-failure` 16/16 passed。

## 6. 推荐文件组织

建议新增目录：

```text
src/modbus/
  modbus_rtu_codec.h/.cpp
  modbus_read_request.h
  modbus_read_response.h
  scan_plan.h/.cpp
  scan_executor.h/.cpp
  modbus_rtu_transport.h

tests/
  modbus_rtu_codec_tests.cpp
  modbus_scan_plan_tests.cpp
  modbus_scan_executor_tests.cpp
  modbus_scan_persistence_tests.cpp
```

如实现时发现更合适的命名，可小范围调整，但不要把 Modbus 扫描逻辑塞进 `MainWindow` 或 `SerialPortService`。

## 7. 验证门禁

每个子任务后至少运行：

```bash
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

当前 VM 资源较低，Qt/CMake 默认并行构建可能被 SIGKILL；本项目优先使用 `--parallel 1`。

## 8. 完成标准

Task 05 完成时应达到：

- 可以用纯测试构造 FC03 / FC04 请求；
- 可以解析假设备响应；
- 可以规划扫描请求块；
- 可以通过 fake executor 得到寄存器观测结果；
- 可以把最小扫描会话和寄存器观测写入并读出 SQLite；
- scan executor 通过传输接口工作，后续可接真实串口 adapter；
- 所有失败都有结构化错误或中文说明；
- `ctest` 全量通过；
- 文档说明 Task 06 如何基于观测结果做目标值候选匹配。

## 9. 下一任务预告

Task 06 应进入：

**目标值匹配与候选生成**

预计包括：

- 目标值输入模型；
- UInt16 / Int16 / UInt32 / Float 等候选解释；
- BE/LE / word swap；
- 倍率和容差；
- 候选评分；
- 候选结果模型。
