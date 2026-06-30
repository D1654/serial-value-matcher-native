# 测试与验证

状态：当前 Win32 native 测试与验证说明。工程交付以 GitHub Actions 编译出的 `svm-native-win32.exe` 和 artifact 为最终测试目标。

真实 Windows 手工验收不是本轮阻塞条件，但仍是发布前风险补充，尤其是具体 USB 转串口硬件、DTR/RTS、硬件流控和长时间现场运行。

## 阻塞门禁

当前 Win32 native 发布必须通过以下门禁。

### Windows native package workflow

工作流：

```text
.github/workflows/windows-native-package.yml
```

artifact：

```text
SerialValueMatcherNative-win32-native-x64
```

阻塞检查：

- CMake native-only 配置：`SVM_BUILD_QT_APP=OFF`、`SVM_BUILD_QT_TESTS=OFF`、`SVM_BUILD_WIN32_APP=ON`。
- Release 构建：`svm-native-win32.exe`。
- CTest：`ctest --test-dir build-windows-native --output-on-failure -C Release`。
- exe 自检：`svm-native-win32.exe --self-test`，日志写入 `native-self-test.log`。
- UI 性能门禁：`svm-native-win32.exe --ui-perf-test`，日志写入 `native-ui-perf-test.log`。
- package 审计：zip、SHA256、exe、导入表、Unicode probe、forbidden runtime、体积门禁。
- package summary 中必须出现 `Gate status: passed`。
- 上传 artifact 必须包含 zip、SHA256 sidecar、package summary、自检日志、UI 性能日志和 `serial-pty-matrix.txt`。

Windows runner 不提供 POSIX PTY 和 Wine dosdevices，因此 `serial-pty-matrix.txt` 在该 workflow 中记录为 local-only 证据文件；真实 PTY 压力由本地 Linux/Wine 环境执行。

### Windows native UI capture workflow

工作流：

```text
.github/workflows/windows-native-ui-capture.yml
```

artifact：

```text
windows-native-ui-screenshots
```

阻塞检查：

- CMake native-only 配置和 Release 构建。
- CTest。
- `--self-test`。
- `--ui-perf-test`。
- `scripts/capture-windows-native-ui.ps1` screenshot capture。
- 默认窗口、小窗口、标签页切换、resize sweep、DPI smoke、分割条拖动帧。
- `capture-status.txt` 中对应场景必须为 PASS。
- `ui-perf-test.log` 必须随 artifact 上传。
- `window-info.txt` 必须记录 DPI 和窗口指标。

UI capture 不是只看截图是否存在，还会用图像差异验证标签切换和分割条拖动帧，防止“点中才显示控件”或“拖动无真实变化”的假通过。

## Native CTest 范围

核心和状态测试覆盖：

- 协议、Modbus、分析和报告：`native_protocol_modbus_tests`、`native_analysis_report_tests`。
- native 存储：`native_storage_tests`。
- 发送、历史、文件发送：`native_send_codec_tests`、`native_send_control_state_tests`、`native_send_history_state_tests`、`native_file_send_state_tests`。
- 日志、滚动、偏好：`native_log_filter_state_tests`、`native_log_scroll_state_tests`、`native_ui_preferences_tests`。
- 连接、重连、状态计数：`native_connection_ui_state_tests`、`native_serial_io_state_tests`、`native_reconnect_state_tests`、`native_status_counters_state_tests`。
- Modbus 扫描 UI 状态和请求：`native_modbus_scan_ui_state_tests`、`native_modbus_scan_request_tests`。

Win32/UI 架构测试覆盖：

- 布局指标：`native_layout_metrics_tests`。
- 布局模型：`native_ui_layout_model_tests`。
- 布局事务：`native_ui_layout_transaction_tests`。
- 帧调度：`native_frame_scheduler_tests`。
- 绘制策略：`native_paint_policy_tests`。
- Win32 分析工作流：`native_win32_analysis_workflow_tests`。
- Win32 串口基础：`native_win32_serial_tests`。
- Win32 串口 loopback：`native_win32_serial_loopback_tests`。

## exe 自检

命令：

```powershell
build-windows-native\Release\svm-native-win32.exe --self-test
```

可设置日志路径：

```powershell
$env:SVM_NATIVE_SELF_TEST_LOG = "artifacts\windows-native\native-self-test.log"
build-windows-native\Release\svm-native-win32.exe --self-test
```

覆盖重点：

- UI 文案和窗口创建基础能力。
- 串口状态、发送状态、历史、文件发送。
- 日志过滤、搜索、复制/导出相关状态。
- native 存储读写。
- Modbus 扫描结果、候选生成、规则验证、Markdown 报告。
- 分割条和布局基础行为。

## UI 性能门禁

命令：

```powershell
$env:SVM_NATIVE_SELF_TEST_LOG = "artifacts\windows-native\native-ui-perf-test.log"
build-windows-native\Release\svm-native-win32.exe --ui-perf-test
```

日志重点字段：

- `tabs`：标签切换次数。
- `tab-ms`：标签切换耗时。
- `layout-pass`：布局 pass 数。
- `apply`：布局应用数量。
- `revision`：布局 revision。
- `log-lines`：日志压力行数。
- `log-ms`：日志压力耗时。
- `log-flush`：日志 flush 次数。
- `log-rebuild`：日志重建次数。

失败时优先检查 `NativeFrameScheduler`、`NativeLayoutModel`、`NativeLayoutTransaction`、`NativePaintPolicy` 和日志 flush 路径。

## UI screenshot capture

Windows runner：

```powershell
.\scripts\capture-windows-native-ui.ps1 `
  -BuildDir build-windows-native-ui `
  -Config Release `
  -OutputDir artifacts\windows-native-ui `
  -SkipUiPerfTest
```

Linux/Wine 辅助诊断：

```bash
scripts/capture-windows-native-ui-wine.sh
```

输出证据：

- `root.png`
- `tab-single.png`、`tab-quick.png`、`tab-file.png`、`tab-scan.png`、`tab-settings.png`
- `compact-tab-*.png`
- `resize-*.png`
- `dpi-*-window.png`
- `log-splitter-before.png`
- `log-splitter-frame-01.png`
- `log-splitter-frame-02.png`
- `log-splitter-after.png`
- `capture-status.txt`
- `window-info.txt`
- `ui-perf-test.log`

关键状态：

- `tab-set ... switching=clicked-frame-diff-validated-by-ui-perf`
- `resize-sweep`
- `dpi-smoke-*`
- `splitter-drag-frames ... live=true diff-gated=true`
- `capture-complete`

## PTY 串口矩阵

本地 Linux/Wine 辅助验证：

```bash
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress \
python3 scripts/run-windows-native-serial-pty-loopback.py
```

默认行为：

- `normal`：正常请求/响应。
- `reopen`：默认重开 3 次。
- `timeout`：无响应超时。
- `cancel`：观察请求后不返回响应，验证取消。
- `stress`：默认 5000 次交易。

常用环境变量：

- `SVM_SERIAL_LOOPBACK_EXE`：指定 `native_win32_serial_loopback_tests.exe`。
- `SVM_WINEPREFIX` 或 `WINEPREFIX`：指定 Wine prefix。
- `SVM_SERIAL_LOOPBACK_COM`：默认 `COM5`。
- `SVM_SERIAL_LOOPBACK_STRESS_ITERATIONS`：压力交易次数。

## package 审计

Windows 正式路径：

```powershell
.\scripts\package-windows-native.ps1 `
  -BuildDir build-windows-native `
  -Config Release `
  -PackageDir artifacts\windows-native
```

Linux/MinGW 辅助路径：

```bash
scripts/package-windows-native-mingw.sh
```

审计字段：

- `Zip sha256`
- `Zip sha256 file matches: yes`
- `Native exe present: yes`
- `Native exe sha256`
- `Imported DLLs`
- `Forbidden Qt/SQLite/.NET runtime files`
- `Unicode text probe`
- `Gate status: passed`

失败即阻塞发布。常见失败见 [故障排查](troubleshooting.md)。

## 文档一致性

当前 Phase 5 Task 05 会新增 docs consistency gate，用于检查文档中的 artifact 名称、路径和当前 Win32 native 口径。Task 04 期间该门禁尚未作为阻塞检查接入；接入后应和 package/UI capture workflow 一起维护。

## 未来增强

以下能力未作为当前阻塞门禁：

- 代码签名。
- 发布 attestation。
- WPR/WPA 性能采样。
- 8 小时或 24 小时长跑压力。
- 多型号真实 USB 转串口硬件矩阵。

这些项目可以作为后续发布硬化，不应在当前文档中写成已经实现。

## 相关文档

- [发布产物](release-artifacts.md)
- [故障排查](troubleshooting.md)
- [开发者指南](developer-guide.md)
- [Windows 串口真机验收](windows-serial-validation.md)
