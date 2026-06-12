# Windows Native 功能对照与发布决策

本文记录 Win32 native 小包与 Qt baseline 的功能对照。结论：native 小包已经满足体积目标并可作为预览包测试，但尚未达到完整产品主发布切换条件。

## CI 与体积结果

最近一次 `main` 验证：

| 项目 | 结果 |
|------|------|
| Linux Qt workflow | success, run `27410685490` |
| Windows Qt Portable Package workflow | success, run `27410685444` |
| Windows Native Size-Gated Package workflow | success, run `27410685405` |
| Native artifact | `SerialValueMatcherNative-win32-native-x64`, artifact ID `7589432078` |
| Native zip | `220,675` bytes |
| Native 解压后 | `447,970` bytes |
| Native 主程序 | `svm-native-win32.exe`, `418,304` bytes |
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
| 文本/HEX 发送 | 是 | 是 | 必须 | native 达标，需真机补验 |
| 接收日志 | 是 | 是，有日志上限 | 必须 | native 达标，需真机补验 |
| 中文 UI/状态提示 | 是 | 是，第一版 shell | 必须 | native 基础达标 |
| raw I/O 持久化 | SQLite/Qt SQL | native length-prefixed file store | 必须 | native 达标 |
| 串口配置 Profile UI | 是 | 存储层有，UI 未完整恢复 | 主发布前必须 | native 缺口 |
| 自动重连 | 是 | 未接入 | 主发布前必须 | native 缺口 |
| Modbus RTU 扫描 UI | 是 | Qt-free core 有，native UI 未接入 | 主发布前必须 | native 缺口 |
| 候选分析 UI | 是 | Qt-free core 有，native UI 未接入 | 主发布前必须 | native 缺口 |
| 稳定性分析/规则验证 UI | 是 | core/storage 部分有，native UI 未接入 | 主发布前必须 | native 缺口 |
| 报告导出 UI | 是 | Qt-free renderer 有，native UI 未接入 | 主发布前必须 | native 缺口 |
| Windows 串口真机验收 | 部分通过既有使用 | 清单已建立，未记录真实硬件结果 | 主发布前必须 | native 待补证据 |

## 发布决策

- **当前主 Windows 发布包**：继续使用 Qt baseline `SerialValueMatcherNative-win-x64`。
- **当前 native 包状态**：`SerialValueMatcherNative-win32-native-x64` 是 size-gated preview，可用于验证极小体积、启动、自测和基础串口终端工作流。
- **不执行主发布切换的原因**：native shell 尚未覆盖 Modbus 扫描、候选分析、规则验证、报告导出、自动重连和完整配置恢复。
- **后续切换条件**：native 包必须补齐上表所有“主发布前必须”能力，并完成真实 Windows 串口设备验收后，才能替代 Qt baseline。

这意味着架构级瘦身已经拿到了可运行的小包证据，但完整产品发布仍采用双轨策略，避免用小包体积换取静默功能回退。
