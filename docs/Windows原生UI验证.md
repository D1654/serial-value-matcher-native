# Windows Native UI 验收

本文用于验证 `svm-native-win32.exe` 的界面和基础交互。

## 基线证据口径

UI 验收以 GitHub Actions 编译出的 Windows native artifact 为最终对象。当前基线不是抽象指标，而是最后一个用户确认可用的 release/artifact 的截图、`--ui-perf-test` 输出和 `capture-status.txt` 场景集合。

任何 UI 改动合入前，应先确认本次证据仍覆盖：

- 默认窗口；
- 全部标签页；
- 紧凑窗口标签页；
- resize sweep；
- DPI smoke；
- 通信日志和标签页分割条拖动帧；
- Phase 1 UI regression closure 状态；
- `self-test.log`；
- `ui-perf-test.log`。

性能判断必须从当前 release/artifact 的 `--ui-perf-test` 输出派生，不得临时写入没有基线依据的阈值。缺少截图、`capture-status.txt`、`self-test.log`、`ui-perf-test.log` 或 `window-info.txt` 时，不能把该次 UI capture 视为通过。

## 自动检查

Windows：

```powershell
.\svm-native-win32.exe --self-test
```

期望：

- 退出码为 `0`；
- 不弹出窗口；
- 中文 UTF-8/UTF-16 往返正常；
- 关键布局自检通过；
- 不加载 Qt 或 .NET 运行库。

Debian/Wine 截图：

```bash
SVM_WINEPREFIX=/tmp/svm-native-wine64-ui2 \
SVM_XDG_RUNTIME_DIR=/tmp/xdg-runtime-root \
SVM_WINE_UI_OUTPUT_DIR=/tmp/svm-native-wine-ui \
scripts/capture-windows-native-ui-wine.sh
```

期望生成：

- `root.png`；
- `tab-single.png`、`tab-quick.png`、`tab-file.png`、`tab-scan.png`、`tab-settings.png`；
- `compact-tab-single.png`、`compact-tab-quick.png`、`compact-tab-file.png`、`compact-tab-scan.png`、`compact-tab-settings.png`；
- 对应 `*-fast.png` 快速帧；
- `resize-*.png`；
- `dpi-*-window.png`；
- `log-splitter-before.png`、`log-splitter-frame-01.png`、`log-splitter-frame-02.png`、`log-splitter-after.png`；
- `capture-status.txt`；
- `window-info.txt`；
- `self-test.log`；
- `ui-perf-test.log`。

截图不能全白，标签页必须实际切换成功；截图文件名和实际激活标签必须一致，例如 `compact-tab-scan.png` 必须停在“扫描”页。
Wine 截图只用于冒烟检查，不作为最终视觉结论；中文字体、输入框文字基线、按钮和下拉框渲染必须以真实 Windows 截图为准。

## 手工检查

启动：

- 标题、菜单、按钮、状态栏均为正常中文；
- 没有乱码；
- 右侧为串口参数栏；
- 左上为大通信日志区；
- 左下为单发、多发、文件、扫描、设置标签页。

小窗口：

- 窗口缩到接近最小尺寸时不崩溃；
- 控件不越出窗口；
- 状态栏 TX/RX/时间不互相覆盖；
- 放大后布局能恢复。

标签页：

- 单发页显示模式、编码、行尾、历史、输入框、发送按钮、定时发送；
- 多发页显示快捷发送槽位；
- 文件页显示文件路径、选择、发送、停止、延迟和进度；
- 扫描页显示扫描参数、进度、已知值匹配、候选和操作按钮；
- 设置页显示日志缓存、暂停滚动、清空等设置；
- 不同标签页之间不能残留对方控件。

日志：

- TX、RX、系统、Modbus 和错误日志颜色可区分；
- 日志格式和日志编码可切换；
- 筛选、搜索、复制、导出可用；
- 流式输出时上拉回看不会被强制跳回底部；
- 手动恢复跟随后能继续显示最新条目。
- 标签页内容区使用浅灰承托，输入框使用系统默认 Win32 外观，不做自绘边框、强制文本边距或弱边框方案。
- 全局 UI 字体应来自 Windows 系统消息字体，不强制指定宋体、雅黑或 Wine 字体替换结果。
- 标签页输入框不得单独套用大于基础 UI 字体的专用字体，避免文本在标准行高中出现视觉偏上。
- 标签页单行输入框应使用正常控件高度，文本不得明显贴近上边框或下边框。
- 输入框、下拉框和按钮在表单背景上边界清楚，不会和静态标签混成一片。
- 同一行内标签、输入框、下拉框和按钮应在视觉上基线一致，不出现明显上下错位。
- 底部标签页高度应优先保障扫描页完整显示，不应为单发、设置等轻量页的留白而压缩扫描页核心字段。
- 标签页标题、页内静态标签和占位提示中文必须清晰可读，不应因小字号、浅灰或强制抗锯齿而显得发虚。
- 小窗口下应通过控件优先级、宽度分配和低优先级标签让位处理空间不足，不应通过裁切文字或自绘输入框解决。

扫描：

- 扫描时有进度反馈；
- 可停止扫描；
- 扫描期间串口参数和 DTR/RTS 等入口按规则冻结；
- 变量名、当前值、单位、误差必须有稳定可见的字段标题和合理默认值，系统 cue banner 仅作为辅助提示；
- 最小窗口尺寸下，所有标签页不得出现中文缺字、文字贴边、输入框遮罩、控件越界或按钮遮挡；
- 最小窗口尺寸下，扫描页必须完整显示起始地址、结束地址、已知值匹配、候选结果和主要操作按钮；
- 标签页内单行输入框必须保持默认边框样式，输入文本不得明显贴近上边框或下边框。

## 不合格情况

出现以下情况应阻止发布：

- 中文乱码；
- 标签页切换白屏；
- 小窗口下核心控件被遮挡且无法恢复；
- 按钮或下拉框文字明显裁切；
- `capture-status.txt` 缺少 `PASS tab-set`、`PASS compact-tab-set`、`PASS resize-sweep`、`PASS splitter-drag-frames` 或 `PASS capture-complete`；
- `capture-status.txt` 缺少 `PASS phase-1-ui-regression-closure`；
- `self-test.log` 缺失或 self-test 未通过；
- `ui-perf-test.log` 缺失或没有通过当前 release/artifact 派生的性能门禁；
- UI capture artifact 缺少截图、`capture-status.txt` 或 `window-info.txt`；
- Release 包不应要求安装 .NET、Qt 或额外运行库；
- self-test 失败。
