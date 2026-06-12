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

默认构建仍保留 Qt 版本：

```bash
cmake -S . -B build-codex -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-codex --parallel 1
ctest --test-dir build-codex --output-on-failure
```

CMake 边界：

```text
SVM_BUILD_QT_APP=ON
SVM_BUILD_WIN32_APP=OFF
```

`SVM_BUILD_WIN32_APP` 当前是保留开关，后续任务会把它接到真正的 Win32 原生目标。

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
