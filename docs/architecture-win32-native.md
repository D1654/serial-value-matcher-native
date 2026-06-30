# Win32 Native 架构

状态：当前 Win32 native 文档骨架。详细架构说明将在 Phase 5 Task 03 补全。

## 当前架构口径

Win32 native 是当前用户发布路线。核心目标是保持轻量包体、中文可读、串口调试稳定、UI 热路径可控。

## 主要边界

- `src/win32/`：Win32 UI、串口后端、native 状态和工作流。
- `src/core/`：协议、Modbus、分析和报告等 Qt-free 核心能力。
- `src/native_storage/`：不依赖 SQLite 插件的 native 存储。
- `scripts/`：构建、打包、截图、PTY 和审计脚本。
- `.github/workflows/`：Windows package、UI capture 和证据 artifact 工作流。

## 计划覆盖

- `NativeMainWindow` 的布局、消息、状态和帧调度边界。
- `NativeLayoutModel`、`NativeLayoutTransaction`、`NativeFrameScheduler` 和绘制策略。
- 串口 I/O、日志、发送、文件发送、Modbus 和分析链路。
- 包体审计、UI 截图、DPI、PTY 和发布证据如何支撑架构决策。

## 历史边界

Qt 路线说明移到 [历史 Qt 说明](legacy-qt-notes.md)。旧的 [architecture.md](architecture.md) 仅作为过渡参考。
