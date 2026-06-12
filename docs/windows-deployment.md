# Windows 原生打包说明

本文记录串口值匹配器（SerialValueMatcher Native）的 Windows 打包流程。当前稳定发布路径是 Qt baseline 便携包，目标是产出不依赖 C#/.NET Desktop Runtime 的 Windows 原生便携软件，解压后可直接运行 `svm-native.exe`。

项目正在执行架构级瘦身：保留 Qt baseline 作为可运行基线，同时新增 Win32 native 小包路线。瘦身目标和体积门禁见 `docs/windows-native-slimming.md`。

## 推荐路径：GitHub Actions 自动出包

仓库包含两条 Windows 自动出包 workflow：

```text
.github/workflows/windows-qt-package.yml
.github/workflows/windows-native-package.yml
```

触发方式：

- 推送到 `main`
- 提交 pull request
- 在 GitHub Actions 页面手动运行 `workflow_dispatch`

Qt baseline workflow 会在 `windows-2022` runner 上执行：

1. 检出仓库。
2. 安装 Qt 6 x64 MSVC 和 `qtserialport` 模块。
3. 使用 Visual Studio 2022 x64 生成器配置 CMake，并显式设置 `SVM_BUILD_QT_APP=ON`、`SVM_BUILD_WIN32_APP=OFF`。
4. 编译 Release。
5. 运行 CTest。
6. 执行 `scripts/package-windows.ps1`。
7. 上传 artifact：`SerialValueMatcherNative-win-x64`。

Win32 native workflow 会在 `windows-2022` runner 上执行：

1. 检出仓库。
2. 不安装 Qt。
3. 使用 Visual Studio 2022 x64 生成器配置 CMake，并显式设置 `SVM_BUILD_QT_APP=OFF`、`SVM_BUILD_QT_TESTS=OFF`、`SVM_BUILD_WIN32_APP=ON`。
4. 编译 Release。
5. 运行 4 个 Qt-free native 测试。
6. 运行 `svm-native-win32.exe --self-test`。
7. 执行 `scripts/package-windows-native.ps1`。
8. 校验包内没有 `Qt6*.dll`、`qsqlite.dll` 和 `sqldrivers`，并执行第一阶段体积门禁。
9. 上传 artifact：`SerialValueMatcherNative-win32-native-x64`。

关键第三方 action 固定到审核过的提交 SHA；如需升级 checkout、Qt 安装或 artifact 上传 action，应先单独验证再更新 SHA。

artifact 内包含：

```text
SerialValueMatcherNative-win-x64.zip
SerialValueMatcherNative-win-x64.zip.sha256.txt
SerialValueMatcherNative-win-x64.package-summary.txt
```

这就是用于 Windows 端测试的便携软件包，不是安装器。

native artifact 内包含：

```text
SerialValueMatcherNative-win32-native-x64.zip
SerialValueMatcherNative-win32-native-x64.zip.sha256.txt
SerialValueMatcherNative-win32-native-x64.package-summary.txt
```

该包是瘦身迁移验证包，当前仍不替代 Qt baseline 主发布包。

仓库还提供手动/定期触发的 Windows 非硬件压力测试 workflow：

```text
.github/workflows/windows-qt-stress.yml
```

该 workflow 使用 Windows 2022、Qt 6 x64 MSVC 和 Release 配置构建 `quality_stress_tests`，只运行 `stress` 标签测试并输出运行耗时，用于补充 Windows 平台长期运行和资源占用证据。

## 前置条件

如果不使用 GitHub Actions，也可以在 Windows 构建机手动安装：

- CMake
- Ninja 或 Visual Studio 生成器
- C++ 编译器（MSVC 或 MinGW，需与 Qt 套件匹配）
- Qt 6，至少包含：
  - Qt Core
  - Qt Widgets
  - Qt SerialPort
  - Qt SQL
  - Qt Test（用于本地测试）
  - SQLite SQL driver（`qsqlite`）

确保 Qt 的 `bin` 目录可以找到 `windeployqt.exe`，或者执行脚本时用 `-QtBinDir` 指定。

## 构建

示例（单配置 Ninja）：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

如果使用 Visual Studio 或 Ninja Multi-Config，把构建和测试命令改为带配置参数：

```powershell
cmake --build build --config Release --parallel 1
ctest --test-dir build --output-on-failure -C Release
```

## 打包脚本

### Qt baseline

执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -BuildDir build -Config Release
```

如果 Qt bin 目录不在 PATH：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -BuildDir build -Config Release -QtBinDir "C:\Qt\6.x.x\msvcXXXX_64\bin"
```

脚本会：

1. 构建 `svm-native.exe`（除非指定 `-SkipBuild`）。
2. 调用 `windeployqt --release --compiler-runtime --no-translations --dir ...` 收集 Qt 运行时依赖。
3. 校验关键插件：
   - `platforms/qwindows.dll`
   - `sqldrivers/qsqlite.dll`
4. 复制 README 和 Windows 打包说明。
5. 生成 zip 包、SHA256 文本和体积摘要。

默认输出：

```text
artifacts/windows/SerialValueMatcherNative-win-x64.zip
artifacts/windows/SerialValueMatcherNative-win-x64.zip.sha256.txt
artifacts/windows/SerialValueMatcherNative-win-x64.package-summary.txt
```

体积摘要会记录 zip bytes、解压后 bytes、文件数量、最大文件和 Qt DLL 列表。Qt baseline 包允许包含 Qt DLL；未来 Win32 native 包必须不包含 `Qt6*.dll`。

### Win32 native 小包

配置和构建：

```powershell
cmake -S . -B build-windows-native -G "Visual Studio 17 2022" -A x64 -DSVM_BUILD_QT_APP=OFF -DSVM_BUILD_QT_TESTS=OFF -DSVM_BUILD_WIN32_APP=ON
cmake --build build-windows-native --config Release --parallel 1
ctest --test-dir build-windows-native --output-on-failure -C Release
```

执行自测：

```powershell
.\build-windows-native\Release\svm-native-win32.exe --self-test
```

打包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows-native.ps1 -BuildDir build-windows-native -Config Release -SkipBuild
```

默认输出：

```text
artifacts/windows-native/SerialValueMatcherNative-win32-native-x64.zip
artifacts/windows-native/SerialValueMatcherNative-win32-native-x64.zip.sha256.txt
artifacts/windows-native/SerialValueMatcherNative-win32-native-x64.package-summary.txt
```

`scripts/inspect-windows-package.ps1` 会检查 native 包：

- 不允许 `Qt6*.dll`；
- 不允许 `qsqlite.dll` 或 `sqldrivers`；
- zip 必须 `<= 5 MB`；
- 解压后必须 `<= 8 MB`。

## Win32 native 小包门禁

Win32 native 包成为主发布包前必须满足：

- 第一阶段：zip `<= 5 MB`，解压后 `<= 8 MB`。
- 冲刺阶段：zip `<= 2 MB`，解压后 `<= 3 MB`。
- 包内不得包含 `Qt6*.dll`。
- 仍需保持中文界面、中文错误诊断、解压即运行、不依赖 C#/.NET Desktop Runtime。

## 打包验收清单

- [ ] Linux/本地最终验证链路已通过：`scripts/check-env.sh`、CMake build、CTest。
- [ ] Windows Release 构建目录中的 `svm-native.exe` 来自与 `windeployqt.exe` 相同架构/编译器的 Qt 套件。
- [ ] `svm-native.exe` 可在干净 Windows 测试机启动。
- [ ] 启动后不会提示缺少 Qt platform plugin。
- [ ] SQLite 会话数据库可创建，说明 `sqldrivers/qsqlite.dll` 已部署。
- [ ] 串口列表刷新正常。
- [ ] 文本 / HEX 发送区可打开并操作，串口打开失败、DTR/RTS 设置失败和写入失败会显示中文诊断。
- [ ] Modbus 扫描入口可打开；实际设备扫描需在受控串口/设备环境中单独验收。
- [ ] SHA256 已记录并随包发布。
- [ ] 体积摘要已记录并随包发布。

## 常见问题

### 提示缺少 platform plugin

检查包内是否存在：

```text
platforms/qwindows.dll
```

若不存在，请确认 `windeployqt.exe` 来自同一套 Qt，并且脚本指向正确 Qt bin 目录。

### SQLite 打不开或无法创建会话数据库

检查包内是否存在：

```text
sqldrivers/qsqlite.dll
```

若不存在，请确认 Qt 安装中包含 SQLite SQL driver。

### MSVC / MinGW 运行时缺失

`windeployqt --compiler-runtime` 会尝试复制编译器运行时。若仍失败，请确认 Qt 套件、编译器和目标机器架构一致。
