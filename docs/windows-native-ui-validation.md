# Windows Native UI 验收

本文用于验证 `svm-native-win32.exe` 的界面和基础交互。

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
- `self-test.log` 内容为 `ok`。

截图不能全白，标签页必须实际切换成功。

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
- 标签页内容区使用浅灰承托，输入框使用系统默认 Win32 外观，不做自绘边框或特殊高度补丁。
- 输入框、下拉框和按钮在表单背景上边界清楚，不会和静态标签混成一片。
- 同一行内标签、输入框、下拉框和按钮应在视觉上基线一致，不出现明显上下错位。
- 标签页标题、页内静态标签和占位提示中文必须清晰可读，不应因小字号、浅灰或强制抗锯齿而显得发虚。
- 小窗口下应通过控件优先级、宽度分配和低优先级标签让位处理空间不足，不应通过压薄输入框或自绘输入框解决。

扫描：

- 扫描时有进度反馈；
- 可停止扫描；
- 扫描期间串口参数和 DTR/RTS 等入口按规则冻结；
- 变量名、当前值、单位有可见灰色提示，提示文字不会作为真实输入参与分析或规则保存。

## 不合格情况

出现以下情况应阻止发布：

- 中文乱码；
- 标签页切换白屏；
- 小窗口下核心控件被遮挡且无法恢复；
- 按钮或下拉框文字明显裁切；
- Release 包要求安装 .NET、Qt 或额外运行库；
- self-test 失败。
