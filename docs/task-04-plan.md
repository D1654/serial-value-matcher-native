# Task 04 计划：串口连接配置完整化

日期：2026-06-02

## 目标

把连接区从“端口 + 波特率”的最小可用状态，扩展为成熟串口调试器常见的完整配置区。

## 当前基线

Task 03 已完成：文本/HEX 发送、行尾设置、暂停滚动、清空接收区、发送历史、串口配置 Profile、中文错误诊断，验证 `ctest` 7/7 passed。

## 子任务

### T04A：完整串口参数 UI（已完成）

增加：

- 数据位：5 / 6 / 7 / 8；
- 校验位：None / Even / Odd / Space / Mark；
- 停止位：1 / 1.5 / 2；
- 流控：None / Hardware / Software；
- DTR 开关；
- RTS 开关。

验收：

- UI 选择项能写入 `SerialOpenOptions`；
- 连接成功后保存到 `SerialProfile`；
- 启动时从最近 Profile 恢复；
- 构建与测试通过：当前 `ctest` 7/7 passed。

### T04B：端口刷新保持选择（已完成）

改进：

- 刷新端口时优先保持当前选择；
- 当前选择不存在时，尝试恢复 profile 里的端口；
- 都不存在时选择第一个可用端口。

验收：

- 新增 `SerialPortSelectionPolicy`，策略独立于 UI，可测试；
- `MainWindow::refreshPorts()` 已按“当前选择 → Profile → 第一个可用端口”恢复下拉选择；
- 新增 `serial_port_selection_tests` 覆盖当前保留、Profile 恢复、首端口兜底、大小写匹配、空列表；
- 构建与测试通过：当前 `ctest` 8/8 passed。

### T04C：自动重连骨架（已完成）

增加：

- 自动重连开关；
- 记录上次成功连接参数；
- ResourceError / DeviceNotFound 后进入等待重连状态；
- 端口重新出现后按原配置尝试连接；
- 初版只做保守重连，避免循环弹窗。

验收：

- UI 增加“自动重连”开关，默认关闭，避免用户不知情自动动作；
- `SerialPortService` 保留原中文错误信号，并新增带 `QSerialPort::SerialPortError` 的类型化错误信号；
- 新增 `SerialReconnectPolicy`，记录上次成功连接参数，ResourceError / DeviceNotFound 后进入等待；
- 等待期间 2 秒保守刷新端口，端口重新出现后按上次成功参数尝试一次重连；
- 重连失败后停止等待，不循环弹窗；
- 新增 `serial_reconnect_policy_tests` 覆盖开关、可恢复错误、非可恢复错误、端口出现后尝试一次、关闭清理等待状态；
- 构建与测试通过：`cmake --build build --parallel 1` 成功，当前 `ctest` 9/9 passed。

### T04D：连接区配置持久化细化（已完成）

增加：

- 配置变化后可保存 profile；
- profile 恢复覆盖完整串口参数；
- 数据库测试覆盖完整字段。

验收：

- 连接区新增“保存配置”按钮，可在不连接串口的情况下保存当前默认 Profile；
- 保存内容覆盖串口、波特率、数据位、校验位、停止位、流控、DTR、RTS；
- 启动时 `applyLatestSerialProfile()` 已恢复完整串口参数；
- `serial_profile_tests` 增强同名 Profile 覆盖测试，验证完整字段都会被新配置覆盖；
- 构建与测试通过：`cmake --build build --parallel 1` 成功，当前 `ctest` 9/9 passed。

## 非目标

- 不做多串口并发；
- 不做 Profile 管理页面；
- 不做 Windows 打包；
- 不接 Analysis Extension。
