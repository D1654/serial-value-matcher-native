# Local Architecture Inventory

Updated: 2026-07-05T17:12:00+08:00

## 读取范围

本文件由主线程在 Phase 0 期间读取本地仓库结构、关键头文件和最近审查报告后整理。它只记录事实和初步风险，不代表最终方案。

## 当前技术栈

- C++20
- Win32 API native release 路线
- Qt6 legacy baseline
- CMake / CTest
- GitHub Actions Windows native package
- Wine/Xvfb 本地 UI 验证

## 目录职责观察

- `src/core`: Qt-free 协议、Modbus、分析和报告基础逻辑。
- `src/modbus`: Modbus RTU 编解码、扫描计划、执行器和 transport 抽象。
- `src/matching`: 候选值生成、数值解码、协议规则解释和验证。
- `src/native_storage`: Win32 native 路线使用的本地文件存储。
- `src/win32`: Windows native UI、串口、状态类、布局、日志、发送、Modbus、分析和自检入口。
- `src/app` / `src/storage`: Qt legacy baseline 相关实现。
- `tests`: 当前存在大量面向 core、modbus、matching、native state、native storage、Win32 serial、layout 和 UI 性能的单元/回归测试。
- `scripts`: Windows native 包检查、UI 截图、串口 PTY loopback、文档产物一致性等验证脚本。
- `.github/workflows`: native/Qt 构建、压力和 UI 捕获工作流。

## 已具备的工程基础

- 纯逻辑层和 UI 层已有一定分离，新增协议/分析逻辑有可测试入口。
- Win32 native 已引入多个小状态类，例如连接状态、发送控制、日志滚动、候选缓存、Modbus 扫描 UI 状态、工作台标签状态等。
- UI 性能已经有 `NativeFrameScheduler`、`NativeLayoutTransaction`、`NativePaintPolicy` 和 `--ui-performance-test` 相关闭环。
- 文档目录已中文化，README 和 release 说明面向中文用户。
- CI 和本地验证覆盖构建、CTest、自检、UI 性能、串口 loopback、包体审计和文档一致性。

## 扩展阻力

1. `NativeMainWindow` 的头文件仍集中大量窗口、控件、消息、串口、发送、日志、Modbus、分析、存储、偏好和状态栏职责。虽然 `.cpp` 已拆分，但类型边界仍偏厚。
2. `NativeLayoutModel` 目前更像测试模型，生产布局计算仍主要在 `main_window_layout.cpp` 内完成。新增 UI 控件时存在测试与真实布局漂移风险。
3. Win32 native Modbus 扫描 worker 仍有独立执行循环，和 `src/modbus/modbus_scan_executor.*` 形成双路线。
4. `NativeSessionStore` 对外暴露 raw events、发送历史、串口配置、UI 偏好、扫描、匹配、规则、验证等多类实体，后续持久化功能继续追加会增加变更半径。
5. CMake 原生测试注册仍较手写，版本元数据尚未完全单源。

## 后续访谈优先级

- 下一批新增功能属于哪一类: 协议解析、串口发送、Modbus 扫描、候选分析、报告导出、批处理/脚本、UI 工作台、数据管理还是自动化验证。
- 新功能是否需要高吞吐、后台任务、长时间运行、批量数据和可恢复持久化。
- 是否允许在新增功能前先做架构整备任务，例如生产布局模型化、主窗口 controller 边界、Modbus transport 适配、storage facade 拆分、CMake helper 和版本单源。
- 用户可接受的重构粒度和 release 节奏: 小步提交优先，还是先打一条较大的内部架构线。
