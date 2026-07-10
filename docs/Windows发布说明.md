# Windows 发布说明

当前推荐发布物是 GitHub Actions 生成的 Win32 native x64 便携包。

## 正式发布包

Release 页面：

```text
https://github.com/D1654/serial-value-matcher-native/releases
```

下载：

```text
SerialValueMatcherNative-win32-native-x64.zip
```

解压后运行：

```text
svm-native-win32.exe
```

该包不是安装器，不写系统目录，不要求用户安装 .NET Desktop Runtime。

## GitHub Actions 出包

Windows native workflow：

```text
.github/workflows/windows-native-package.yml
```

触发方式：

- 推送到 `main`；
- Pull request；
- GitHub Actions 页面手动运行。

主要步骤：

1. Windows 2022 runner 检出仓库；
2. 使用 Visual Studio 2022 x64 配置 CMake；
3. 只打开 `SVM_BUILD_WIN32_APP=ON`；
4. 编译 Release；
5. 运行 CTest；
6. 运行 `svm-native-win32.exe --self-test`；
7. 运行 `svm-native-win32.exe --ui-perf-test`；
8. 执行 `scripts/package-windows-native.ps1`；
9. 上传 zip、SHA256、package summary、CTest、自检、UI 性能、后端闭环和串口 PTY local-only summary。

UI capture workflow：

```text
.github/workflows/windows-native-ui-capture.yml
```

该 workflow 生成 `windows-native-ui-screenshots`，必须包含 screenshot、`capture-status.txt`、`self-test.log`、`ui-perf-test.log`、`window-info.txt` 和 `ui-evidence-summary.txt`。

## 包门禁

打包检查会确认：

- 包内存在 `svm-native-win32.exe`；
- exe 的 Windows `VERSIONINFO` 与 `cmake/svm_version.cmake` 一致；
- package summary 包含 `Native exe file version`、`Native exe product version`、`Native exe product name` 和 `Native exe original filename`；
- package summary 包含 `Unexpected DLL files: none`、`Required package files: passed` 和 `Package documentation file set: passed`；
- 不包含 `Qt6*.dll`；
- 不包含 `qsqlite.dll` 或 `sqldrivers`；
- 不导入 `.NET` 运行库；
- 中文 UTF-16 文本探针通过；
- 包内 `README.md` 和 `docs/*.md` 的相对 Markdown 链接不允许断链；
- zip 和解压体积不超过门禁。
- package artifact 包含 `native-ctest.log`、`native-self-test.log`、`native-ui-perf-test.log`、`serial-pty-matrix.txt` 和 `serial-pty-matrix-summary.txt`。

v1.0.4 门禁结果以 Release 附件中的 package summary 为准。当前 GitHub Actions Release 包：

- zip：以 package summary 为准；
- 解压：以 package summary 为准；
- 文件数：以 package summary 为准；
- Package documentation links：passed；
- Gate status：passed。

UI artifact 还应确认：

- `capture-status.txt` 不含 `FAIL`；
- `ui-perf-test.log` 包含 `ui-perf ok`；
- `ui-evidence-summary.txt` 包含 `GateStatus=passed`；
- job summary 中记录 UI artifact 的 `artifact-digest`。

## 本地 Windows 构建

在 Windows 开发机上可手动执行：

```powershell
cmake -S . -B build-windows-native -G "Visual Studio 17 2022" -A x64 `
  -DSVM_BUILD_QT_APP=OFF `
  -DSVM_BUILD_QT_TESTS=OFF `
  -DSVM_BUILD_WIN32_APP=ON

cmake --build build-windows-native --config Release --parallel 1
.\build-windows-native\Release\svm-native-win32.exe --self-test
.\scripts\package-windows-native.ps1 -BuildDir build-windows-native -Config Release -SkipBuild
```

## Release 维护

推荐版本号从当前 `v1.0.4` 继续递增。创建 Release 前应确认：

- `main` 已推送；
- Windows native workflow 已成功；
- Windows native UI capture workflow 已成功；
- 下载的 artifact summary 为 `Gate status: passed`；
- `ui-evidence-summary.txt` 为 `GateStatus=passed`；
- `serial-pty-matrix-summary.txt` 明确 `GateStatus=documented-local-only`，涉及串口 I/O 改动时另附本地 PTY summary；
- Release 正文使用真正的 Markdown 多行文本，不使用字面量 `\n`。

### 发布操作清单

1. 确认 `cmake/svm_version.cmake`、README、发布说明和预期 tag 一致。
2. 确认 `main` 分支包含本次提交，且没有未提交的发布文档改动。
3. 运行或等待 `windows-native-package.yml` 成功，记录 run id。
4. 运行或等待 `windows-native-ui-capture.yml` 成功，记录 run id。
5. 下载 package artifact，核对 zip SHA256 sidecar。
6. 阅读 package summary，确认 `Gate status: passed`、`Unexpected DLL files: none`、`Required package files: passed`、`Package documentation file set: passed`。
7. 阅读 UI artifact，确认 `ui-evidence-summary.txt` 为 `GateStatus=passed`，并人工查看默认窗口、标签页、紧凑标签页、resize、DPI 和分割条截图。
8. 如果本次改动涉及串口 I/O、超时、取消、重连、批量发送或异步写队列，运行本地 PTY normal/reopen/timeout/cancel/stress 矩阵并保存 summary。
9. 运行 `python3 scripts/check-docs-artifact-consistency.py`，确认输出 `docs consistency ok`。
10. 创建或更新 Release，上传 zip、SHA256 sidecar、package summary；Release 正文写明 run id、SHA256、主要变更、已知边界和回滚指引。

Release 附件不要求上传 UI screenshot artifact，但 Release 正文应记录 UI capture run id，便于事后追溯。

## 回滚和重新发布

如果 release artifact 被发现有 UI、串口、包体或文档证据问题：

1. 不要覆盖本地 zip，也不要手工替换 release 附件。
2. 在 Release 正文顶部标注该版本不推荐继续下载，并说明阻塞问题。
3. 回退到上一个已验证 release，保留其 zip、SHA256、package summary 和 UI capture 证据。
4. 修复后重新触发 package workflow 和 UI capture workflow。
5. 下载新 artifact，重新核对 SHA256、package summary、`ui-evidence-summary.txt`、`serial-pty-matrix-summary.txt` 和 docs consistency。
6. 使用递增版本号重新发布；如果必须重发同一标签，必须在 Release 正文写明替换原因、新 run id、新 SHA256 和旧附件处理方式。

回滚判断以 artifact 证据为准，而不是本地临时 exe。只要 package summary、UI evidence summary 或 SHA256 sidecar 不完整，就不应作为发布候选。
