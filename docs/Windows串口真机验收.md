# Windows 串口真机验收

本文用于真实 Windows 设备上的串口验收。Wine 和 CI 只能覆盖非硬件路径，不能替代真机测试。

## 自动化证据边界

| 证据 | 执行位置 | 是否替代真机验收 | 用途 |
|------|----------|------------------|------|
| Windows native package workflow | GitHub Actions `windows-2022` | 否 | 阻断构建、CTest、自检、UI 性能、package 审计、docs consistency 和证据文件缺失 |
| `serial-pty-matrix.txt` | GitHub Actions artifact | 否 | 记录 PTY 矩阵仍是 local-only，不代表 CI 已执行 PTY normal/reopen/timeout/cancel/stress |
| `serial-pty-matrix-summary.txt` | GitHub Actions artifact | 否 | 机器可读记录 `GateStatus=documented-local-only`、`ExpectedScenarios=normal,reopen,timeout,cancel,stress` 和本地命令 |
| PTY loopback 矩阵 | 本地 Linux/Wine | 否 | 发布候选前补充验证串口 normal、reopen、timeout、cancel、stress 路径 |
| 真实 USB 转串口设备 | 真实 Windows 10/11 | 是 | 验证端口枚举、驱动、控制线、硬件流控、热插拔和现场长时间运行 |

只要本地 PTY 或真机发现串口收发、超时、取消、热插拔或日志问题，即使 GitHub Actions 通过，也应视为发布风险并先修复。

本地 PTY 矩阵建议同时输出 summary 文件：

```bash
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress \
SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/serial-pty-matrix-summary.txt \
python3 scripts/run-windows-native-serial-pty-loopback.py
```

成功输出必须包含 `python serial matrix summary gate-status=passed classification=local-only-release-candidate-evidence`，summary 文件必须包含 `GateStatus=passed` 和 `Classification=local-only-release-candidate-evidence`。

## 准备

- Windows 10/11 x64；
- `SerialValueMatcherNative-win32-native-x64.zip`；
- 至少一个 USB 转串口设备；
- 回环线或可控串口设备；
- 如需 Modbus 测试，准备真实从站或可靠模拟器。

## 基础验收

| 场景 | 步骤 | 期望 |
|------|------|------|
| 启动 | 双击 `svm-native-win32.exe` | 正常启动，无乱码 |
| 端口枚举 | 插入 USB 转串口，点击刷新 | 出现 `COMx` 和可区分的设备描述 |
| 打开串口 | 选择 115200 8N1 无流控后连接 | 连接成功，状态栏更新 |
| 端口占用 | 用其他工具占用同一端口后连接 | 显示中文错误，不崩溃 |
| 发送 | 发送文本、HEX、十进制、二进制 | 日志显示 TX，设备收到正确字节 |
| 接收 | 回环或设备返回数据 | 日志显示 RX，字节完整 |
| DTR/RTS | 切换 DTR、RTS | 支持的设备控制线生效 |
| 热插拔 | 连接后拔出设备 | 显示断开或 I/O 错误，可重新刷新 |

## 日志验收

- 连续收发至少 30 分钟；
- 日志持续追加时 UI 仍可响应；
- 上拉回看历史不会自动跳底；
- 搜索、筛选、复制、导出正常；
- 切换日志格式和编码不崩溃。

## Modbus 验收

| 场景 | 步骤 | 期望 |
|------|------|------|
| FC03 | 扫描保持寄存器 | TX/RX 帧正确，有进度 |
| FC04 | 扫描输入寄存器 | TX/RX 帧正确，有结果 |
| 停止扫描 | 扫描中点击停止 | 当前请求结束后停止，UI 仍可用 |
| 错误设备 | 错误从站或超时 | 显示失败统计，不崩溃 |
| 候选分析 | 输入已知当前值和误差 | 候选结果可生成，可验证规则 |

## 发布前结论

每次正式发布前至少记录：

- GitHub Actions package artifact 名称和 run id；
- `serial-pty-matrix.txt` 是否仍声明为 local-only；
- `serial-pty-matrix-summary.txt` 是否记录 `GateStatus=documented-local-only`；
- 如涉及串口 I/O 改动，本地 PTY loopback 矩阵命令和结果；
- Windows 版本；
- 串口芯片型号；
- 测试波特率；
- 是否通过基础收发；
- 是否通过长时间日志；
- 是否通过 Modbus 场景；
- 发现的问题和是否阻塞发布。
