# Windows 原生瘦身路线

本文记录串口值匹配器的架构级瘦身边界。当前 Qt Widgets 版本继续作为稳定基线；Win32 原生小包目标在 Change #4 中分阶段落地。

## 当前体积基线

最近一次 Windows Qt 便携包基线：

| 指标 | 数值 |
|------|------|
| Artifact 内层 zip | `22,788,935` bytes |
| 解压后文件总量 | `54,953,747` bytes |
| 文件数 | `27` |
| 主程序 `svm-native.exe` | `721,920` bytes |

最大文件来自 Qt 和图形运行时：

| 文件 | 解压后大小 |
|------|------------|
| `opengl32sw.dll` | `20,639,888` bytes |
| `Qt6Gui.dll` | `9,285,768` bytes |
| `Qt6Widgets.dll` | `6,500,488` bytes |
| `Qt6Core.dll` | `6,159,496` bytes |
| `D3Dcompiler_47.dll` | `4,173,928` bytes |
| `sqldrivers/qsqlite.dll` | `1,978,504` bytes |

结论：当前包大不是业务代码大，而是 Qt 动态运行时路线天然携带较重依赖。仅裁剪 `windeployqt` 输出可以降低一部分体积，但无法达到传统串口工具常见的几百 KB/少数 MB 级别。

## 目标门禁

Win32 原生包必须满足以下门禁后才能成为主 Windows 发布包：

| 阶段 | Zip 体积 | 解压后体积 | 依赖规则 |
|------|----------|------------|----------|
| 第一阶段 | `<= 5 MB` | `<= 8 MB` | 不包含 `Qt6*.dll` |
| 冲刺阶段 | `<= 2 MB` | `<= 3 MB` | 不包含 `Qt6*.dll` |

如需追求几百 KB 级别，需要继续压缩 UI、存储和报告能力边界；这会作为后续专门任务处理，不在第一阶段承诺。

## 双目标策略

- `svm-native`：当前 Qt Widgets 稳定基线，继续用于功能回归、用户试用和风险兜底。
- 未来 Win32 native target：轻量 Windows 原生目标，逐步接入 Qt-free 核心、Win32 串口后端和小包发布链路。
- `svm_win32_serial`：已新增的 Qt-free Win32 串口后端库。Linux 下构建硬件无关参数/错误核心；Windows 下额外编译 `CreateFileW`/`ReadFile`/`WriteFile`/`SetCommState`/`QueryDosDeviceW` 实现。
- `svm_native_storage`：已新增的 Qt-free 文件存储库。第一阶段不引入 SQLite amalgamation，避免把 native 小包重新推向 MB 级依赖膨胀；后续如需复杂查询再单独评估 SQLite 静态链接。
- `svm-native-win32`：已新增的 Win32 native UI shell，默认关闭，覆盖刷新串口、连接、断开、文本/HEX 发送、接收日志和 raw I/O 存储。

默认构建仍保留 Qt 版本：

```bash
cmake -S . -B build-codex -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-codex --parallel 1
ctest --test-dir build-codex --output-on-failure
```

CMake 边界：

```text
SVM_BUILD_QT_APP=ON
SVM_BUILD_QT_TESTS=ON
SVM_BUILD_WIN32_APP=OFF
```

Windows native-only 构建边界：

```text
SVM_BUILD_QT_APP=OFF
SVM_BUILD_QT_TESTS=OFF
SVM_BUILD_WIN32_APP=ON
```

该配置只应在 Windows 上启用，目标是生成 `svm-native-win32.exe` 并通过 `--self-test` 做非交互验证。

## 发布规则

Qt baseline 包允许包含 Qt DLL，并必须生成体积摘要：

```text
SerialValueMatcherNative-win-x64.package-summary.txt
```

未来 Win32 native 包必须满足：

- 不包含 `Qt6*.dll`；
- 不依赖 C#/.NET Desktop Runtime；
- 解压即可运行；
- 保留中文界面、中文状态提示和中文错误诊断；
- 通过串口打开、发送、接收、断开、异常恢复的 Windows 验收；
- 通过 Modbus/候选分析的核心回归测试或明确标注预览范围。

## Win32 串口后端

Change #4 已开始替换 `Qt6SerialPort.dll` 路线。当前新增的后端包括：

- `src/win32/win32_serial_types.*`：不包含 Windows/Qt 头文件的参数、端口名、错误文案核心；
- `src/win32/win32_serial_port.*`：Windows 专用 RAII 串口句柄，负责打开、配置、读、写、等待接收、关闭；
- `src/win32/win32_serial_enumerator.*`：Windows 专用 `COMx` 枚举；
- `tests/native_win32_serial_tests.cpp`：硬件无关测试，覆盖参数校验、端口名规范化和 Win32 错误中文诊断。

真机验收步骤见 `docs/windows-serial-validation.md`。该后端目前仍是并行能力，尚未替换 Qt baseline UI 的串口入口；替换将在 Win32 native UI shell 和最终 parity/switch 任务中完成。

## Native 存储选择

Change #4 第一阶段选择标准库文件存储，而不是 SQLite：

- 目标是先移除 `Qt6Sql.dll` 和 `sqldrivers/qsqlite.dll`，并把 native 路线维持在最小依赖集合；
- 存储格式是长度前缀记录文件，可保存 UTF-8 中文文本和二进制串口帧，不依赖 JSON/SQLite 解析库；
- 当前覆盖 raw I/O、发送历史、串口配置、扫描结果、匹配候选、协议规则和规则验证结果；
- Qt baseline 仍继续使用现有 SQLite/Qt SQL `SessionStore`，迁移期间两条路径并存。

该选择牺牲了 SQL 查询能力，换取更小体积和更少运行时依赖。若 Win32 native UI 后续需要复杂筛选、跨表查询或大数据量索引，再以独立任务评估 SQLite amalgamation 的体积收益比。

## Win32 Native UI Shell

`svm-native-win32` 是第一阶段小包 UI：

- 使用 Win32 API 和 common controls，不使用 Qt Widgets；
- 启动参数 `--self-test` 可在 CI 中不弹窗执行；
- 主界面保持中文，提供串口刷新、连接/断开、文本/HEX 发送、接收日志和状态栏；
- 接收日志有上限，达到上限后自动清空并提示，避免长期运行无限增长；
- raw I/O 会写入 `NativeSessionStore`，为后续 Modbus/候选分析接入保留数据入口。

手工验收步骤见 `docs/windows-native-ui-validation.md`。
