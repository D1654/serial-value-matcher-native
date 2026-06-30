# 发布产物

状态：当前 Win32 native 文档骨架。详细发布和下载说明将在 Phase 5 Task 04 补全。

## 当前发布口径

- 正式包来自 GitHub Actions 的 Windows native package workflow。
- 主 artifact 名称为 `SerialValueMatcherNative-win32-native-x64`。
- 必须包含 zip、SHA256 sidecar、package summary 和相关验证日志。

## 必备证据

- `SerialValueMatcherNative-win32-native-x64.zip`
- `SerialValueMatcherNative-win32-native-x64.zip.sha256.txt`
- `SerialValueMatcherNative-win32-native-x64.package-summary.txt`
- `native-self-test.log`
- `native-ui-perf-test.log`
- `serial-pty-matrix.txt`
- UI capture workflow 的截图、`capture-status.txt`、`window-info.txt`、`ui-perf-test.log`

## 相关文档

- [测试与验证](testing-validation.md)
- [Windows 发布说明](windows-deployment.md)
