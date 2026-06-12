# Windows 串口真机验收清单

本文用于验证 Win32 native 串口后端。该后端不依赖 Qt SerialPort，也不依赖 .NET/C# 运行库。

## 构建边界

- Windows SDK: 使用 `CreateFileW` 打开 `\\.\COMx` 设备。
- 参数配置: 使用 `DCB`、`SetCommState` 映射波特率、数据位、校验位、停止位、RTS/CTS、XON/XOFF、DTR、RTS。
- 超时配置: 使用 `COMMTIMEOUTS`、`SetCommTimeouts` 设置读写超时。
- 读写: 使用阻塞式 `ReadFile`、`WriteFile`，不启用 .NET/C# 和 Qt SerialPort。
- 枚举: 使用 `QueryDosDeviceW` 发现 `COMx` 设备，后续如需 USB VID/PID 再接 SetupAPI。

## 硬件验收

每次把 Win32 native 串口后端接入 UI 或发布包前，应在 Windows 真机执行：

| 场景 | 步骤 | 期望结果 |
|------|------|----------|
| 端口枚举 | 插入 USB 转串口设备并刷新 | 端口列表出现 `COMx`，描述包含 Win32 设备路径 |
| 打开串口 | 选择 `115200, 8N1, 无流控` 后连接 | 连接成功，无 Qt DLL 参与 |
| 端口占用 | 用其他串口工具占用同一端口后再连接 | 显示“串口被占用或权限不足”的中文错误 |
| 发送数据 | 发送短帧和 4 KB 以上长帧 | 写入字节数等于待发送字节数 |
| 接收数据 | 使用回环线或外部设备返回数据 | 能收到完整字节流，超时不会误报为硬错误 |
| Modbus RTU | 对真实从站读取保持寄存器 | 请求帧、响应帧和候选分析结果与 Qt baseline 一致 |
| 热插拔 | 连接后拔出 USB 转串口 | 显示设备断开/串口 I/O 错误，可重新刷新端口 |
| 控制线 | 分别切换 DTR、RTS、RTS/CTS | 支持的芯片正常生效，不支持时显示可理解的中文错误 |

## 当前自动化覆盖

Linux/CI 可运行的硬件无关测试覆盖：

- 端口名裁剪、`COMx` 规范化、`\\.\COMx` 设备路径生成；
- 波特率、数据位、超时、读取缓冲区等参数校验；
- 常见 Win32 错误码到中文可操作诊断的映射；
- 中文参数名称输出。
