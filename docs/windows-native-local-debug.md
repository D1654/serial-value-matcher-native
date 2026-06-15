# Windows Native 本地调试

本文用于在 Debian 12 环境快速构建、打包和截图检查 Windows native 程序。正式发布包仍以 GitHub Actions 的 Windows MSVC 构建为准。

## 依赖

常用工具：

```bash
apt-get update
apt-get install -y \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
  p7zip-full python3-pefile wine64 wine32:i386 xvfb xdotool imagemagick fonts-noto-cjk
```

## 构建

```bash
scripts/build-windows-native-mingw.sh
```

输出：

```text
build-windows-native-mingw/svm-native-win32.exe
```

## 自测

```bash
env XDG_RUNTIME_DIR=/tmp/xdg-runtime-root \
  WINEPREFIX=/tmp/svm-native-wine64-ui2 \
  WINEARCH=win64 \
  wine build-windows-native-mingw/svm-native-win32.exe --self-test
```

## 本地打包

```bash
scripts/package-windows-native-mingw.sh
```

输出：

```text
artifacts/windows-native-mingw/SerialValueMatcherNative-win32-native-x64-mingw.zip
```

MinGW 包只用于本地诊断，不替代 GitHub Actions 的 MSVC 正式包。

## UI 截图闭环

```bash
SVM_WINEPREFIX=/tmp/svm-native-wine64-ui2 \
SVM_XDG_RUNTIME_DIR=/tmp/xdg-runtime-root \
SVM_WINE_UI_OUTPUT_DIR=/tmp/svm-native-wine-ui \
scripts/capture-windows-native-ui-wine.sh
```

脚本会：

- 启动 exe；
- 运行 self-test；
- 截取默认窗口；
- 切换单发、多发、文件、扫描、设置标签页；
- 再把窗口缩到 `760x520` 截一组紧凑截图；
- 检查截图非空、非全白。

Wine 截图只能发现明显 UI 问题，不能替代真实 Windows 串口设备测试。
