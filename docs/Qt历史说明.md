# 历史 Qt 说明

状态：legacy / 历史参考。本文不描述当前主发布包。

## 当前结论

仓库仍保留 Qt Widgets 路线源码和测试，但当前面向用户交付的轻量发布物是 Win32 native 包。当前发布包不携带 Qt DLL、SQLite 插件、`sqldrivers` 或 .NET/C# 运行库。

开发和文档判断口径：

- 新用户文档默认描述 Win32 native。
- 新功能默认优先沉到 Qt-free core、`src/native_storage/` 或 `src/win32/`。
- Qt 内容可作为能力基线、迁移参考和历史测试资源。
- 不应把 Qt 路线写成当前正式 release 路线。

## Qt 路线范围

主要历史目录：

- `src/app/`：Qt Widgets 历史 UI。
- `src/storage/`：SQLite 存储路线。
- `src/transport/`：Qt 串口服务和错误翻译。
- `src/modbus/`：Qt 路线 Modbus RTU 请求、响应、扫描执行器。
- `src/matching/`：候选生成、数值解码、稳定性分析和规则解释。
- `src/report/`：Qt 路线报告和文本写入。
- `src/session/`、`src/capture/`、`src/analysis/`：历史工作流和模型支撑。

主要历史目标和工作流：

- CMake 选项：`SVM_BUILD_QT_APP`、`SVM_BUILD_QT_TESTS`。
- Qt GUI 目标：`svm-native`。
- Windows Qt 打包脚本：`scripts/package-windows.ps1`。
- Qt workflow：`.github/workflows/windows-qt-package.yml`、`.github/workflows/windows-qt-stress.yml`、`.github/workflows/linux-qt.yml`、`.github/workflows/linux-qt-stress.yml`。

## 与当前 Win32 native 的关系

可复用：

- 协议语义、候选分析思路、规则验证概念。
- 旧测试中对 Modbus、匹配、报告和持久化的行为期望。
- 历史文档中明确标注为旧路线的背景说明。

不应复用为当前发布事实：

- Qt Widgets UI 截图和交互描述。
- Qt SerialPort、Qt SQL、SQLite 插件和 windeployqt 打包说明。
- `svm-native.exe` 作为用户主程序的描述。
- 依赖 Qt DLL 或 `sqldrivers` 的发布口径。

迁移原则：

- 业务算法迁移到 `src/core/`，保持 Qt-free。
- 本地记录迁移到 `src/native_storage/`，保持当前 native 包轻量。
- UI 行为迁移到 `src/win32/`，遵守 NativeFrameScheduler、NativeLayout、NativePaintPolicy 的热路径边界。
- 文档迁移时保留历史价值，但必须标注 legacy 或过渡参考。

## 什么时候看 legacy 文档

适合阅读 legacy 内容的场景：

- 需要理解最早的功能设计背景。
- 需要对照 Qt baseline 和 Win32 native 的功能差异。
- 需要迁移旧测试覆盖的业务语义。
- 需要判断某个旧说法是否已经被 Win32 native 文档替代。

不适合阅读 legacy 内容的场景：

- 给最终用户说明当前可执行文件如何使用。
- 判断当前 Release 或 Actions artifact 的包体内容。
- 判断当前 UI 标签页、日志栏、分割条和截图回归是否正常。
- 判断当前 `svm-native-win32.exe` 的自测、UI perf、PTY 和包体审计结果。

## 相关旧文档

- [Win32 Native 与 Qt 基线对照](Win32原生与Qt基线对照.md)
- [过渡架构说明](架构说明.md)

这些文档只作为过渡参考。当前用户入口、开发入口和发布入口分别是 [用户指南](用户指南.md)、[开发者指南](开发者指南.md) 和 [发布产物](发布产物.md)。
