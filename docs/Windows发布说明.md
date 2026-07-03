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
7. 执行 `scripts/package-windows-native.ps1`；
8. 上传 zip、SHA256 和 package summary。

## 包门禁

打包检查会确认：

- 包内存在 `svm-native-win32.exe`；
- 不包含 `Qt6*.dll`；
- 不包含 `qsqlite.dll` 或 `sqldrivers`；
- 不导入 `.NET` 运行库；
- 中文 UTF-16 文本探针通过；
- 包内 `README.md` 和 `docs/*.md` 的相对 Markdown 链接不允许断链；
- zip 和解压体积不超过门禁。

v1.0.4 门禁结果以 Release 附件中的 package summary 为准。当前 GitHub Actions Release 包：

- zip：以 package summary 为准；
- 解压：以 package summary 为准；
- 文件数：以 package summary 为准；
- Package documentation links：passed；
- Gate status：passed。

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
- 下载的 artifact summary 为 `Gate status: passed`；
- Release 正文使用真正的 Markdown 多行文本，不使用字面量 `\n`。
