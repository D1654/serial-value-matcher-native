# Windows Native 功能对照与发布决策

本文记录 Win32 native 小包与 Qt baseline 的功能对照。结论：native 小包已经满足体积目标，但 Windows 实测暴露过中文乱码和 UI parity 不足；Change #5 已把这些问题作为主线修复目标，native 仍需继续通过真机验收后才能替代 Qt baseline。

## CI 与体积结果

最近一次 `main` 验证：

| 项目 | 结果 |
|------|------|
| Linux Qt workflow | success, run `27410685490` |
| Windows Qt Portable Package workflow | success, run `27410685444` |
| Windows Native Size-Gated Package workflow | success, run `27410685405` |
| Native artifact | `SerialValueMatcherNative-win32-native-x64` |
| Native 体积 | 以 artifact 内 `SerialValueMatcherNative-win32-native-x64.package-summary.txt` 为准 |
| Native 主程序 | `svm-native-win32.exe` |
| Native 文件数 | `6` |
| 禁止运行时 | `Qt6*.dll`: none, `qsqlite.dll`: none, `sqldrivers`: none |
| 门禁 | first-stage gate passed |

## 功能对照

| 能力 | Qt baseline | Win32 native | 主发布切换要求 | 当前结论 |
|------|-------------|--------------|----------------|----------|
| 解压即运行 | 是 | 是，CI 出包 | 必须 | native 达标 |
| 不依赖 C#/.NET Desktop Runtime | 是 | 是 | 必须 | native 达标 |
| 不携带 Qt DLL | 否 | 是 | 必须 | native 达标 |
| 串口枚举 | 是 | 是，`QueryDosDeviceW` | 必须 | native 达标，需真机补验 |
| 串口连接/断开 | 是 | 是，Win32 `CreateFileW`/`CloseHandle` | 必须 | native 达标，需真机补验 |
| 菜单栏 | 是，Qt 工具栏/主窗口命令 | 是，Win32 菜单“文件/串口/工具/分析/帮助” | 必须 | Change #5 补齐 |
| 串口完整参数 | 是 | 是，波特率/数据位/校验/停止位/流控/DTR/RTS | 必须 | Change #5 补齐，需真机补验 |
| 文本/HEX/行尾发送 | 是 | 是 | 必须 | Change #5 扩展，需真机补验 |
| 发送历史 | 是 | 是，native 存储 | 主发布前必须 | Change #5 补齐 |
| 接收日志 | 是 | 是，有日志上限和暂停滚动 | 必须 | Change #5 扩展，需真机补验 |
| 中文 UI/状态提示 | 是 | 是，强制 MSVC `/utf-8`，self-test 覆盖 UTF-8/UTF-16 | 必须 | Change #5 修复乱码风险，需用户复测 |
| raw I/O 持久化 | SQLite/Qt SQL | native length-prefixed file store | 必须 | native 达标 |
| 串口配置 Profile UI | 是 | 是，保存/恢复默认 Profile | 主发布前必须 | Change #5 补齐 |
| 自动重连 | 是 | 是，基础端口恢复重连 | 主发布前必须 | Change #5 补齐，需真机补验 |
| Modbus RTU 扫描 UI | 是 | 是，基础 FC03/FC04 扫描入口 | 主发布前必须 | Change #5 初步接入，需真机补验和交互深化 |
| 候选分析 UI | 是 | Qt-free core 有，native UI 未接入 | 主发布前必须 | native 缺口 |
| 稳定性分析/规则验证 UI | 是 | core/storage 部分有，native 有显式入口和缺口说明 | 主发布前必须 | native 深度缺口 |
| 报告导出 UI | 是 | Qt-free renderer 有，native 有显式入口和缺口说明 | 主发布前必须 | native 深度缺口 |
| Windows 串口真机验收 | 部分通过既有使用 | 清单已建立，未记录真实硬件结果 | 主发布前必须 | native 待补证据 |

## 发布决策

- **当前主 Windows 发布包**：继续使用 Qt baseline `SerialValueMatcherNative-win-x64`。
- **当前 native 包状态**：`SerialValueMatcherNative-win32-native-x64` 是 size-gated preview，可用于验证极小体积、启动、自测、中文 UI 修复、基础串口终端和基础 Modbus 扫描工作流。
- **不执行主发布切换的原因**：native shell 的候选分析、稳定性分析、规则验证和报告导出仍未达到 Qt baseline 的完整交互深度，且需要真实 Windows 串口设备复测中文显示和串口长期运行。
- **后续切换条件**：native 包必须补齐上表所有“主发布前必须”能力，并完成真实 Windows 串口设备验收后，才能替代 Qt baseline。

这意味着架构级瘦身已经拿到了可运行的小包证据，但完整产品发布仍采用双轨策略，避免用小包体积换取静默功能回退。
