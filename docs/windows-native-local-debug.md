# Windows Native 本地构建与调试

本文记录在 Debian 12 上本地交叉构建、检查和冒烟运行 `svm-native-win32.exe` 的流程。该流程用于快速诊断和自动化分析；正式 Windows 发布包仍以 GitHub Actions 的 MSVC 构建为准。

## 工具链

推荐安装：

```bash
dpkg --add-architecture i386
apt-get update
apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-tools icoutils python3-pefile osslsigncode unzip p7zip-full libimage-exiftool-perl wine64 wine32:i386 xvfb xdotool imagemagick fonts-noto-cjk
```

关键命令：

- `x86_64-w64-mingw32-g++`：Windows x64 交叉编译器；
- `x86_64-w64-mingw32-objdump`：PE 导入和节表分析；
- `x86_64-w64-mingw32-strip`：本地调试包瘦身；
- `wrestool` / `exiftool`：资源和版本信息检查；
- `python3-pefile`：Python PE 导入表解析；
- `wine` / `xvfb-run`：无界面 self-test 冒烟运行和真实 UI 截图；
- `xdotool` / `xwd` / `convert`：定位窗口并生成截图；
- `fonts-noto-cjk`：在 Wine/Xvfb 截图中正确显示中文。

## 本地构建

```bash
scripts/build-windows-native-mingw.sh
```

默认输出：

```text
build-windows-native-mingw/svm-native-win32.exe
```

该构建使用：

```text
SVM_BUILD_QT_APP=OFF
SVM_BUILD_QT_TESTS=OFF
SVM_BUILD_WIN32_APP=ON
```

MinGW 链接参数会尽量静态链接 GCC/C++ 运行库，避免本地包依赖 `libgcc_s_seh-1.dll`、`libstdc++-6.dll` 或 `libwinpthread-1.dll`。

## 本地打包与检查

```bash
scripts/package-windows-native-mingw.sh
```

默认输出：

```text
artifacts/windows-native-mingw/SerialValueMatcherNative-win32-native-x64-mingw.zip
artifacts/windows-native-mingw/SerialValueMatcherNative-win32-native-x64-mingw.package-summary.txt
```

检查内容：

- zip 体积和解压后体积；
- 最大文件列表；
- PE 导入 DLL；
- 是否包含 `Qt6*.dll`、`qsqlite.dll`、`sqldrivers` 或 .NET 运行时导入；
- 中文 UTF-16LE 文本探针；
- 门禁状态。

## Wine/Xvfb 冒烟运行

打包脚本默认会在工具存在时运行：

```bash
xvfb-run -a wine build-windows-native-mingw/svm-native-win32.exe --self-test
```

该检查只能证明 Windows exe 能在 Wine 下执行非交互自测，不能替代真实 Windows 串口设备验收。

当前脚本默认把 Wine 作为软门禁：如果宿主机不允许 Wine 创建运行时目录，或缺少 32 位兼容组件但本次只需要静态包检查，脚本会继续执行并在终端给出警告。需要把 Wine 失败作为硬门禁时：

```bash
SVM_STRICT_WINE_TEST=1 scripts/package-windows-native-mingw.sh
```

如需跳过：

```bash
scripts/package-windows-native-mingw.sh --skip-wine
```

## Wine/Xvfb UI 截图闭环

本地可直接启动 Windows exe 并截取真实 UI：

```bash
scripts/capture-windows-native-ui-wine.sh
```

默认输出：

```text
/tmp/svm-native-wine-ui/root.png
/tmp/svm-native-wine-ui/tab-single.png
/tmp/svm-native-wine-ui/tab-quick.png
/tmp/svm-native-wine-ui/tab-file.png
/tmp/svm-native-wine-ui/tab-scan.png
/tmp/svm-native-wine-ui/tab-settings.png
/tmp/svm-native-wine-ui/compact-tab-single.png
/tmp/svm-native-wine-ui/compact-tab-quick.png
/tmp/svm-native-wine-ui/compact-tab-file.png
/tmp/svm-native-wine-ui/compact-tab-scan.png
/tmp/svm-native-wine-ui/compact-tab-settings.png
/tmp/svm-native-wine-ui/window-info.txt
/tmp/svm-native-wine-ui/self-test.log
```

该脚本会：

- 初始化 64 位 Wine prefix；
- 为 `Microsoft YaHei UI` 配置 `Noto Sans CJK SC` 字体替换，避免中文显示成方块；
- 运行 `svm-native-win32.exe --self-test`；
- 在 Xvfb 中启动真实窗口，等待稳定帧并截图；
- 自动切换“单发 / 多发 / 文件 / 扫描 / 设置”五个标签页并截图；
- 将窗口缩放到 `760x520` 后再次捕获五个紧凑尺寸标签页。

常用参数：

```bash
SVM_WINEPREFIX=/tmp/svm-native-wine64-ui2 \
SVM_WINE_UI_OUTPUT_DIR=/tmp/svm-native-wine-ui \
SVM_WINE_UI_SCREEN=1280x900x24 \
scripts/capture-windows-native-ui-wine.sh
```

## 布局韧性策略

窗口尺寸测试分三档：

| 档位 | 示例 | 验收重点 |
|------|------|----------|
| 支持范围 | `760x520`、`1040x720`、`1366x768` | 完整交互可用，主要区域不越过状态栏，底部标签页核心控件可见 |
| 强制小尺寸 | `640x400`、`480x320`、`320x240` | 不崩溃、不出现负宽高、工具栏和输入区域布局有界 |
| 极小退化 | `1x1` 等异常输入 | 布局计算稳定，内部按最小安全尺寸钳制 |

原因：Win32 已通过 `WM_GETMINMAXINFO` 给出推荐最小拖拽尺寸，但 DPI、恢复窗口、自动化测试或系统边界情况仍可能让布局收到更小 client size。成熟产品不能假设这些输入永远不会出现。

## 与正式 MSVC 包的区别

本地 MinGW 包用于分析和快速回归，可能与 MSVC 包在 exe 体积、运行库实现、优化结果上不同。交付给用户长期下载或正式验收的包，应继续使用 GitHub Actions `Windows Native Size-Gated Package` 生成的 MSVC artifact。
