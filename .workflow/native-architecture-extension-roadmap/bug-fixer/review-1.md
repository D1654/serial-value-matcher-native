# Phase 1 里程碑复审报告

- Workflow: `native-architecture-extension-roadmap`
- 触发点: Phase 1 完成后的用户确认项 `E`
- 范围: Phase 1 UI / Architecture Foundation 相关源码、测试、UI 截图脚本、GitHub Actions UI artifact 链路
- 基线提交: `b473af9 test: close phase 1 native ui regression gates (Phase 1, Task 08)`
- 审查时间: 2026-07-07T06:13:16+08:00
- 修复状态: 3 项已修复，验证通过

## 结论

Phase 1 没发现阻断 Phase 2 的核心功能缺陷，也没有发现明显的串口/Modbus/窗口布局主路径崩溃点。复审发现的 1 个中风险验证链路问题和 2 个低风险质量问题已处理，后续 UI/性能回归证据更可靠，Workbench tab 重绘策略也已回到模型契约内。

## 发现

### MEDIUM-1 默认窗口截图证据可能被持久化窗口尺寸污染

- 文件:
  - `scripts/capture-windows-native-ui-wine.sh:138`
  - `scripts/capture-windows-native-ui-wine.sh:142`
  - `scripts/capture-windows-native-ui-wine.sh:421`
  - `scripts/capture-windows-native-ui.ps1:653`
  - `scripts/capture-windows-native-ui.ps1:673`
  - `scripts/capture-windows-native-ui.ps1:688`
- 问题:
  - 应用已经支持记住上次窗口尺寸。
  - Wine 脚本在强制 `1212x753` 之前就采集 `root.png` 并写入 `PASS default-window`。
  - Windows PowerShell 脚本同样只 restore/foreground 后采集 `root.png`，随后才在 `Capture-TabSet` 中设置默认尺寸。
  - 因此 `root.png` / `window-info.txt` 可能实际来自上一次持久化的小窗口，却被 artifact 标记成默认窗口证据。
- 影响:
  - 不直接影响用户运行时功能。
  - 但会削弱 release artifact 作为最终 UI 审查依据的可信度，可能导致默认布局问题被误判。
- 建议:
  - 在首次 `root.png`、`window-info.txt`、默认窗口 detail 生成前，显式设置窗口到标准默认尺寸。
  - `window-info.txt` 同时记录实际捕获宽高，避免只写期望尺寸。
- 状态: fixed
- 修复:
  - Wine capture 在默认截图前强制设置 `1212x753`，并在 `capture-status.txt` 中记录 `requested` 和 `actual`。
  - Windows PowerShell capture 增加 `Set-CaptureWindowSize` / `Get-NativeWindowSize`，默认截图前设置标准尺寸，并写入 `DefaultCaptureSize`。

### LOW-1 Wine UI 脚本用 `eval` 解析 xdotool geometry

- 文件:
  - `scripts/capture-windows-native-ui-wine.sh:217`
  - `scripts/capture-windows-native-ui-wine.sh:240`
  - `scripts/capture-windows-native-ui-wine.sh:257`
  - `scripts/capture-windows-native-ui-wine.sh:345`
- 问题:
  - `xdotool getwindowgeometry --shell` 的输出目前来自本地工具链，正常环境风险很低。
  - 但 `eval "$geometry"` 是不必要的 shell 执行面，属于验证脚本硬化欠账。
- 影响:
  - 低风险；主要影响 CI/本地验证脚本的攻击面和可维护性。
- 建议:
  - 改为白名单解析 `X/Y/WIDTH/HEIGHT`，并要求数值字段通过校验后再参与 crop/点击坐标计算。
- 状态: fixed
- 修复:
  - Wine capture 增加 `load_window_geometry()`，只接受 `X/Y/WIDTH/HEIGHT` 数值字段，不再使用 `eval`。

### LOW-2 Workbench tab 应用计划的重绘策略没有被调用层消费

- 文件:
  - `src/win32/native_workbench_tab_state.cpp:57`
  - `tests/native_workbench_tab_state_tests.cpp:36`
  - `src/win32/main_window_workbench.cpp:319`
  - `src/win32/main_window_workbench.cpp:347`
- 问题:
  - `NativeWorkbenchTabState::beginApply()` 已计算 `redrawAfterApply`，测试也把它作为模型契约覆盖。
  - `NativeMainWindow::applyWorkbenchTabVisibility()` 没使用该策略位，而是在成功显示目标标签页控件后总是调度 `scheduleWorkbenchTabRepaint()`。
- 影响:
  - 当前有 `workbenchTabRepaintPending_` 合并机制，暂未构成明显性能故障。
  - 但模型契约和调用层行为不一致，会降低后续扩展 UI 状态机时的可读性，并可能让未来优化误判真实重绘策略。
- 建议:
  - 明确 `redrawAfterApply` 的语义：
    - 若它是“仅布局变化需要区域重绘”，调用层应按计划消费；
    - 若 tab 切换也必须 repaint，则重命名/扩展计划字段，让模型契约准确表达调用层需求。
- 状态: fixed
- 修复:
  - `NativeWorkbenchTabState::beginApply()` 将 `redrawAfterApply` 明确定义为“本次应用产生控件可见性变化，且当前允许重绘”。
  - `NativeMainWindow::applyWorkbenchTabVisibility()` 只消费 `plan.redrawAfterApply` 调度 tab repaint。
  - `native_workbench_tab_state_tests` 增加首显、切换、隐藏页、可重绘/暂停重绘场景覆盖。

## 已执行检查

- 代码审查: Phase 1 相关 Win32 UI、layout transaction、paint policy、workbench tab/log split、serial send/log、Modbus analysis/preference 边界。
- 脚本审查: Windows/Wine UI capture、GitHub Actions artifact summary、docs artifact consistency gate。
- 本地命令:
  - `python3 scripts/check-docs-artifact-consistency.py` -> `docs consistency ok`
  - `bash -n scripts/capture-windows-native-ui-wine.sh` -> passed
  - `cmake --build build-codex --target native_workbench_tab_state_tests` -> passed
  - `cmake --build build-windows-native-mingw --target svm-native-win32 native_workbench_tab_state_tests` -> passed
  - `ctest --test-dir build-codex --output-on-failure` -> 49/49 passed
  - `ctest --test-dir build-windows-native-mingw --output-on-failure` -> 27/27 passed
  - `env SVM_WINEPREFIX=/tmp/svm-phase1-bugfix-ui2-prefix SVM_WINE_UI_OUTPUT_DIR=/tmp/svm-phase1-bugfix-ui2-out SVM_WINE_UI_CAPTURE_FAST_FRAMES=0 scripts/capture-windows-native-ui-wine.sh` -> passed
- Wine UI capture 证据:
  - `capture-status.txt`: `PASS default-window file=root.png requested=1212x753 actual=1212x753`
  - `capture-status.txt`: `PASS phase-1-ui-regression-closure tabs=5 compact-tabs=5 resize-sweep=true splitter-drag-frames=true wine-smoke=true`
  - `ui-perf-test.log`: `ui-perf ok tabs=300 tab-ms=4137 layout-pass=12 apply=324 revision=12 log-lines=1200 log-ms=1324 log-flush=2 log-rebuild=1`

## 未执行项

- 未重新触发 GitHub Actions，也未下载新的 Actions artifact。
- 当前 Linux 环境没有 `pwsh`，所以 PowerShell capture 脚本未在本机执行；其 Windows 端实际验证仍以 GitHub Actions 为准。
