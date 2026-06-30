# 测试与验证

状态：当前 Win32 native 文档骨架。详细测试矩阵将在 Phase 5 Task 04 补全。

## 当前验证口径

工程交付以 GitHub Actions 编译出的 Win32 native 可执行文件和 artifact 为最终测试目标。真实 Windows 手工验收不是本轮阻塞条件，但仍作为发布前风险补充。

## 计划覆盖

- Native CTest、`--self-test` 和 `--ui-perf-test`。
- Windows UI capture workflow 截图、DPI、resize、标签页和分割条证据。
- 本地 Wine/PTY 串口矩阵：normal、reopen、timeout、cancel、stress。
- 包体审计：zip、SHA256、exe、导入表、Unicode probe 和 forbidden runtime。
- 本地 MinGW 诊断路径和 Windows Actions 正式路径差异。

## 相关文档

- [发布产物](release-artifacts.md)
- [故障排查](troubleshooting.md)
- [Windows 串口真机验收](windows-serial-validation.md)
