# 开发者指南

状态：当前 Win32 native 文档骨架。详细开发流程将在 Phase 5 Task 03 补全。

适用对象：继续维护 Win32 native 路线的开发者和测试工程师。

## 当前开发口径

- 当前交付重点是 Win32 native 桌面程序。
- Qt 代码仍在仓库中，但属于历史基线和部分测试参考。
- 新增用户可见能力应优先沉到 Qt-free core 或 Win32 native 模块。

## 计划覆盖

- 仓库目录和构建目标。
- CMake 选项、MSVC 构建和 MinGW 交叉构建。
- 本地自测、UI 性能门禁、PTY 串口矩阵和包体审计。
- 代码所有权边界和热路径注意事项。
- 提交、验证、Actions artifact 下载和问题定位流程。

## 相关文档

- [Win32 native 架构](architecture-win32-native.md)
- [测试与验证](testing-validation.md)
- [历史 Qt 说明](legacy-qt-notes.md)
