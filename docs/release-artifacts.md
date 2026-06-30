# 发布产物

状态：当前 Win32 native 发布产物说明。正式发布以 GitHub Actions 的 Windows native package artifact 为准。

## 主 artifact

工作流：

```text
.github/workflows/windows-native-package.yml
```

artifact 名称：

```text
SerialValueMatcherNative-win32-native-x64
```

artifact 保留时间：

```text
14 days
```

必备文件：

- `SerialValueMatcherNative-win32-native-x64.zip`
- `SerialValueMatcherNative-win32-native-x64.zip.sha256.txt`
- `SerialValueMatcherNative-win32-native-x64.package-summary.txt`
- `native-self-test.log`
- `native-ui-perf-test.log`
- `serial-pty-matrix.txt`

zip 解压后必须包含：

- `svm-native-win32.exe`
- `README.md`
- 当前打包脚本复制的 docs 子集

不应包含：

- `Qt6*.dll`
- `qsqlite.dll`
- `sqldrivers`
- `.NET/C# runtime` 文件

## UI capture artifact

工作流：

```text
.github/workflows/windows-native-ui-capture.yml
```

artifact 名称：

```text
windows-native-ui-screenshots
```

必备文件：

- `*.png` screenshot 文件。
- `capture-status.txt`
- `ui-perf-test.log`
- `window-info.txt`

关键 screenshot 集合：

- 默认窗口：`root.png`
- 标签页：`tab-single.png`、`tab-quick.png`、`tab-file.png`、`tab-scan.png`、`tab-settings.png`
- 小窗口标签页：`compact-tab-*.png`
- resize：`resize-*.png`
- DPI：`dpi-*-window.png`
- 分割条拖动：`log-splitter-before.png`、`log-splitter-frame-01.png`、`log-splitter-frame-02.png`、`log-splitter-after.png`

## 下载 artifact

查看最近运行：

```bash
gh run list --workflow windows-native-package.yml --branch main --limit 10
```

下载 package artifact：

```bash
gh run download <run-id> \
  --name SerialValueMatcherNative-win32-native-x64 \
  --dir artifacts/github-actions/windows-native-<run-id>
```

下载 UI capture artifact：

```bash
gh run list --workflow windows-native-ui-capture.yml --limit 10

gh run download <run-id> \
  --name windows-native-ui-screenshots \
  --dir artifacts/github-actions/windows-native-ui-<run-id>
```

下载失败时优先检查：

- 当前 `gh` 是否已登录且有仓库权限。
- run id 是否对应正确 workflow。
- artifact 名称是否完全匹配。
- artifact 是否超过 14 天保留期。

## 校验 zip 和 SHA256

Windows：

```powershell
$zip = "SerialValueMatcherNative-win32-native-x64.zip"
Get-FileHash -Algorithm SHA256 $zip
Get-Content "$zip.sha256.txt"
```

Linux：

```bash
sha256sum SerialValueMatcherNative-win32-native-x64.zip
cat SerialValueMatcherNative-win32-native-x64.zip.sha256.txt
```

判断标准：

- 计算出的 SHA256 必须出现在 `.sha256.txt` 中。
- package summary 中 `Zip sha256 file matches` 必须为 `yes`。

## 阅读 package summary

必须检查：

- `Package kind: Win32 native`
- `Zip sha256`
- `Zip sha256 file matches: yes`
- `Native exe present: yes`
- `Native exe sha256`
- `Imported DLLs`
- `Forbidden Qt/SQLite/.NET runtime files: none`
- `Unicode text probe: passed`
- `Gate status: passed`

如果 `Gate status` 不是 `passed`，该 artifact 不应作为发布候选。

## 阅读验证日志

`native-self-test.log`：

- 关注 self-test 是否完整完成。
- 失败时按最后一个失败标签定位到 core、storage、serial、send、log、Modbus、analysis 或 layout。

`native-ui-perf-test.log`：

- 关注 `ui-perf ok`。
- 核对 tabs、tab-ms、layout-pass、apply、revision、log-lines、log-ms、log-flush、log-rebuild。

`serial-pty-matrix.txt`：

- Windows runner 中当前为 local-only 说明文件。
- 完整 PTY normal/reopen/timeout/cancel/stress 需要本地 Linux/Wine 执行。

`capture-status.txt`：

- 关注 `PASS tab-set`、`PASS compact-tab-set`、`PASS resize-sweep`、`PASS splitter-drag-frames` 和 `PASS capture-complete`。
- 若有 `FAIL`，配合同目录 screenshot 和 `window-info.txt` 排查。

## 发布候选判定

可以作为发布候选的最低条件：

- package workflow 成功。
- package artifact 完整。
- zip SHA256 匹配。
- package summary 为 `Gate status: passed`。
- `native-self-test.log` 存在且无失败。
- `native-ui-perf-test.log` 存在且通过。
- UI capture workflow 成功。
- UI capture artifact 中 `capture-status.txt` 无 FAIL。

不能作为发布候选的情况：

- 手工复制本地 exe，缺少 Actions 证据。
- 只有 zip，没有 SHA256 或 package summary。
- package summary 缺少导入表检查或 Unicode probe。
- UI capture 缺少 screenshot 或 `capture-status.txt`。
- 发现 Qt/SQLite/.NET runtime 文件进入 native 包。

## 本地 MinGW 产物

本地 MinGW 产物用于诊断和快速验证：

```bash
scripts/build-windows-native-mingw.sh
scripts/package-windows-native-mingw.sh
```

默认本地包名：

```text
SerialValueMatcherNative-win32-native-x64-mingw
```

本地 MinGW 包不能替代正式 Actions package artifact；用于定位问题、复现 Wine/UI/PTY 失败和提前发现包体审计问题。

## 当前未实现的发布硬化

以下不是当前 release artifact 的既有能力：

- 代码签名。
- 供应链 attestation。
- WPR/WPA 性能采样报告。
- 8 小时或 24 小时长跑压力报告。

发布说明中不得把这些写成已完成证据。

## 相关文档

- [测试与验证](testing-validation.md)
- [故障排查](troubleshooting.md)
- [用户指南](user-guide.md)
