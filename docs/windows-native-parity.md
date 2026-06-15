# Windows Native 功能对照与发布决策

本文记录 Win32 native 小包与 Qt baseline 的功能对照。结论：native 小包已经满足体积目标，且 Change #19/#20/#22 后进一步收敛为 ATK-XCOM 类“主日志区 + 右侧串口窄栏 + 底部功能标签页 + 单行日志工具条 + 分段状态栏 + 经典紧凑控件指标”布局；默认尺寸和 `760x520` 紧凑尺寸下的底部标签页内容完整性、切换快速帧、控件密度和非全白截图已纳入 Wine UI 验收。native 仍需继续通过真实 Windows 串口设备长期运行验收后才能替代 Qt baseline。

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
| Native 文件数 | 以 native artifact summary 为准；本地 MinGW 单 exe 构建为 `1` |
| 本地 MinGW exe | strip 后约 `1.6 MB` |
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
| 成熟串口助手布局 | 部分 | 是，主通信日志最大化、右侧串口选择窄栏、底部发送/扫描标签页、单行日志工具条、分段状态栏，小窗口下按真实客户区和 TabCtrl 内容区约束控件边界；Change #22 将字体、行高、按钮/下拉框宽度和间距进一步收敛到接近 ATK-XCOM / WinForms 经典工具的紧凑指标；默认尺寸和 `760x520` 紧凑尺寸已用 Wine 标签页矩阵覆盖单发、多发、文件、扫描、设置，并通过快速帧检查明显白屏；Change #21 后单发格式控件不再泄漏到其他标签页 | 主发布前必须 | 仍需真实 Windows 审美与设备复测 |
| 串口完整参数 | 是 | 是，波特率/数据位/校验/停止位/流控/DTR/RTS；Modbus 扫描期间会冻结这些会影响串口句柄或参数的入口 | 必须 | Change #20 加固并发边界，需真机补验 |
| 文本/HEX/行尾发送 | 是 | 是 | 必须 | Change #5 扩展，需真机补验 |
| 十进制/二进制字节流发送 | 是 | 是，多字节流解析 | 必须 | Change #14 统一发送解析路径 |
| 定时发送 | 是 | 是，按当前格式周期发送 | 主发布前必须 | Change #14 补齐 |
| 快捷多条发送 | 是 | 是，10 槽位并持久化 | 主发布前必须 | Change #14 补齐 |
| 文件分块发送 | 是 | 是，1KB 分块、延迟、进度、停止 | 主发布前必须 | Change #14 补齐 |
| 发送历史 | 是 | 是，native 存储 | 主发布前必须 | Change #5 补齐 |
| 接收日志 | 是 | 是，有 200K 到 100M 可见日志缓存档位、暂停滚动和历史回看不强制跳底；1000M 级日志需要后续虚拟/分页或磁盘背书日志视图，不能直接依赖 RichEdit 可见文本框 | 必须 | Change #23 安全扩容，需真机高负荷补验 |
| 中文 UI/状态提示 | 是 | 是，强制 MSVC `/utf-8`，self-test 覆盖 UTF-8/UTF-16 | 必须 | Change #5 修复乱码风险，需用户复测 |
| raw I/O 持久化 | SQLite/Qt SQL | native length-prefixed file store | 必须 | native 达标 |
| 串口配置 Profile UI | 是 | 是，保存/恢复默认 Profile | 主发布前必须 | Change #5 补齐 |
| 自动重连 | 是 | 是，基础端口恢复重连 | 主发布前必须 | Change #5 补齐，需真机补验 |
| Modbus RTU 扫描 UI | 是 | 是，基础 FC03/FC04 扫描入口，后台执行并可停止；扫描页提供按块数推进的进度条和状态栏进度；扫描期间点击断开会在取消完成后真正关闭串口 | 主发布前必须 | Change #23 补齐进度反馈，需真机补验 |
| 候选分析 UI | 是 | 是，基于最近扫描生成候选并持久化 | 主发布前必须 | Change #6 轻量接入，Change #7 改善入口组织 |
| 稳定性分析/规则验证 UI | 是 | 规则验证已轻量接入；稳定性分析和规则编辑表格仍不完整 | 主发布前必须 | native 仍有深度交互缺口 |
| 报告导出 UI | 是 | 是，导出规则验证 Markdown 报告 | 主发布前必须 | Change #6 轻量接入 |
| Windows 串口真机验收 | 部分通过既有使用 | 清单已建立，未记录真实硬件结果 | 主发布前必须 | native 待补证据 |

## 发布决策

- **当前主 Windows 发布包**：继续使用 Qt baseline `SerialValueMatcherNative-win-x64`。
- **当前 native 包状态**：`SerialValueMatcherNative-win32-native-x64` 是 size-gated preview，可用于验证极小体积、启动、自测、中文 UI、基础串口终端、定时/快捷/文件发送、带进度反馈的基础 Modbus 扫描、扫描中断开、轻量候选生成、规则验证和报告导出工作流。
- **不执行主发布切换的原因**：native shell 的稳定性分析、规则编辑表格和证据浏览仍未达到 Qt baseline 的完整交互深度，且需要真实 Windows 串口设备复测中文显示、DTR/RTS、文件发送、扫描停止和串口长期运行。
- **后续切换条件**：native 包必须补齐上表所有“主发布前必须”能力，并完成真实 Windows 串口设备验收后，才能替代 Qt baseline。

这意味着架构级瘦身已经拿到了可运行的小包证据，但完整产品发布仍采用双轨策略，避免用小包体积换取静默功能回退。
