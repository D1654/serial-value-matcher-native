# 故障排查

状态：当前 Win32 native 文档骨架。详细排查流程将在 Phase 5 Task 04 补全。

## 计划覆盖

- 程序无法启动。
- 串口无法枚举、打开失败、端口占用、热插拔异常。
- 发送无响应、接收乱码、日志不滚动或筛选结果异常。
- UI 缩放、DPI、标签页、分割条和截图回归问题。
- Modbus 超时、取消、错误统计和候选分析结果异常。
- GitHub Actions 包体审计、UI capture、PTY matrix 和 artifact 下载失败。

## 首选证据

- 当前 exe 的 `--self-test` 输出。
- UI performance log。
- package summary。
- capture status。
- 串口 PTY matrix 输出或真机验收记录。
