# Tech Ecosystem Research — SerialValueMatcher Native
Generated: 2026-07-05T17:12:30+08:00
DeepWiki queries run: 13
DeepWiki fallbacks: 0

## Candidate Libraries

| Library | GitHub Repo | Category | DeepWiki Status |
|---|---|---|---|
| Windows Classic Samples | microsoft/Windows-classic-samples | Win32 desktop/API reference corpus | Queried; serial-specific coverage limited |
| CMake / CTest | Kitware/CMake | Build, test, packaging orchestration | Queried |
| Qt Serial Port | qt/qtserialport | Serial API reference and Qt6 legacy baseline | Queried |
| GoogleTest | google/googletest | C++ unit/regression testing | Queried |
| vcpkg | microsoft/vcpkg | C++ dependency governance | Queried |

## DeepWiki Findings

### Windows Classic Samples

Documentation structure covers Win32 fundamentals, DPI awareness, UI/Shell integration, file/device APIs, networking, security, and system-management samples. It is useful for native desktop patterns: `RegisterClassEx`, `CreateWindowEx`, message loops, `WM_PAINT`, `WM_DESTROY`, `WM_DPICHANGED`, per-monitor DPI handling, Shell integration, and resource loading.

Limitation: DeepWiki found no direct serial-port sample coverage in this repo. For SerialValueMatcher Native, use it as a Win32 UI/DPI/Shell reference, not as the primary serial I/O authority. Native serial work should instead follow Win32 communications APIs such as `CreateFile` on COM ports, `DCB`, `COMMTIMEOUTS`, `SetCommState`, `SetCommTimeouts`, `ReadFile`, `WriteFile`, and overlapped I/O.

### CMake / CTest

DeepWiki highlighted CMake’s core role in explicit C++ standard configuration, generator selection, policies, `find_package`, generator expressions, Qt integration, and CTest registration. Relevant practices: keep `CMAKE_CXX_STANDARD 20` explicit, use `BUILD_TESTING`, register tests through CTest, preserve CI-friendly command lines, and avoid build-script drift by making CMake the single coordination layer.

Limitation: GitHub Actions, Wine, and Xvfb are outside CMake’s core docs, so their behavior should remain validated by actual CI/self-test scripts rather than inferred from CMake alone.

### Qt Serial Port

DeepWiki identifies `QSerialPort` and `QSerialPortInfo` as the key API pair. Useful capabilities include port enumeration, baud/data/parity/stop/flow configuration, async signals (`readyRead`, `bytesWritten`, `errorOccurred`), blocking calls for worker threads, DTR/RTS control, read-buffer sizing, and Windows implementation over Win32 handles/overlapped I/O.

Limitations noted: no terminal echo/CR-LF features, no direct text-mode transfer display, limited direct timeout/delay configuration, and no direct notification for pinout signal changes. For this project, Qt SerialPort should remain a behavioral reference and legacy baseline, not a reason to pull Qt back into the native delivery path.

### GoogleTest

DeepWiki confirms the core APIs: `TEST`, `TEST_F`, `EXPECT_*`, `ASSERT_*`, parameterized tests, typed tests, death tests, matchers, fixtures, XML/JSON reporting, and CMake integration via `gtest_discover_tests`. Best fit: pure domain logic, parsers, matching algorithms, storage transactions, layout model behavior, and fake serial adapters.

Key constraints: GoogleTest now requires at least C++17, test names should avoid underscores, and Windows CRT linkage must match the parent project. For this C++20 project that is acceptable.

### vcpkg

DeepWiki identifies manifest mode, baselines, version databases, triplets, `vcpkg.json`, `vcpkg_from_github`, and CMake toolchain integration as the governance model. It is strongest when third-party dependencies grow and reproducible CI builds matter.

Limitations: version constraints are mostly lower-bound oriented, Windows CRT/debug-release mismatches are common failure points, and adding vcpkg has operational cost. Do not introduce it only for cleanliness; introduce it when external dependencies become real.

### Cross-Repo Findings

Qt SerialPort is easier and more portable for serial I/O, but native Win32 gives tighter control and avoids Qt runtime coupling. Since the release target is Win32 native, the recommended path is a native serial abstraction with overlapped I/O and test fakes, while keeping Qt behavior as a regression oracle.

CMake + CTest + GoogleTest is the right spine for tests. vcpkg should be optional until dependency pressure justifies manifest/baseline governance.

## Ecosystem Health

| Library | Stars / Signal | Release / Tag Signal | Maintenance | Community |
|---|---:|---|---|---|
| Windows Classic Samples | 5.6k stars | `MicrosoftDocs-Samples`, published 2021; assets updated 2026 | Not archived; pushed 2026-03-26 | Official sample corpus, moderate issue load |
| CMake | 8.0k stars | v4.3.3 published 2026-05-21 | Very active; pushed 2026-07-05 | Mature core build ecosystem |
| Qt Serial Port | 121 stars | GitHub latest release absent; tags include v6.12.0-beta1, v6.11.1 | Active Qt module; pushed 2026-07-02 | Small repo mirror, backed by Qt ecosystem |
| GoogleTest | 38.8k stars | v1.17.0 published 2025-04-30 | Active; pushed 2026-06-30 | Very broad C++ test adoption |
| vcpkg | 27.2k stars | 2026.06.24 release; 2849 ports reported | Very active; pushed 2026-07-05 | Large package ecosystem, high issue volume |

## Recommended Stack

Keep the primary delivery stack as C++20 + Win32 API + CMake/CTest + GitHub Actions + Wine/Xvfb validation. Do not re-platform the native app to Qt.

Recommended architecture direction:

1. Put native serial I/O behind a narrow service boundary, with overlapped read/write and an async write queue before adding high-throughput send features.
2. Treat Qt SerialPort as a legacy behavior reference for configuration, error handling, and cross-checking, not as the native runtime dependency.
3. Keep CMake/CTest as the build and verification spine; centralize test registration and version metadata to reduce release drift.
4. Use GoogleTest for pure logic, storage, parser/matcher, layout-model, and fake serial tests; reserve Wine/Xvfb for UI smoke/performance verification.
5. Delay vcpkg adoption unless new external dependencies are introduced; if adopted, require manifest mode, pinned baseline, explicit triplets, and CRT-linkage policy.
6. Before feature expansion, split `NativeMainWindow` responsibilities, make production layout consume `NativeLayoutModel`, consolidate Modbus scan logic, and add transaction boundaries around multi-file append storage.

## Sources

- DeepWiki structure/ask commands run with `/root/.codex/skills/workflow-architect/assets/scripts/deepwiki.sh`
- DeepWiki result links:
  - https://deepwiki.com/search/what-are-the-core-apis-capabil_7c34675c-f0ab-4929-8777-7b77ff0f7104
  - https://deepwiki.com/search/which-win32-desktop-ui-dpi-awa_923d3d7a-d078-4332-a1da-eec653d011b8
  - https://deepwiki.com/search/what-are-the-core-apis-capabil_92f92b7a-cf00-4cba-9167-8a156f6cd947
  - https://deepwiki.com/search/what-are-the-core-apis-capabil_680c0fcb-2f53-403b-a2e7-f1143d09684d
  - https://deepwiki.com/search/what-are-the-core-apis-capabil_d0261b23-8b4f-4ff9-a153-0cf303f14108
  - https://deepwiki.com/search/what-are-the-core-apis-capabil_42facece-224f-4c6e-960e-2f571be54bc7
  - https://deepwiki.com/search/compare-qt-serialport-and-nati_ea11890c-0636-4e14-a658-b0f713a7ec65
  - https://deepwiki.com/search/how-should-a-c20-windows-nativ_f268faac-c9e0-4ccb-950b-8df66faedcbd
- GitHub API metadata:
  - https://api.github.com/repos/microsoft/Windows-classic-samples
  - https://api.github.com/repos/Kitware/CMake
  - https://api.github.com/repos/qt/qtserialport
  - https://api.github.com/repos/google/googletest
  - https://api.github.com/repos/microsoft/vcpkg
- Official docs:
  - https://learn.microsoft.com/en-us/windows/win32/devio/communications-resources
  - https://doc.qt.io/qt-6/qtserialport-index.html
  - https://cmake.org/cmake/help/latest/module/GoogleTest.html
  - https://google.github.io/googletest/
  - https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode

## 中文摘要

本轮完成 5 个候选仓库 DeepWiki 研究，未触发失败 fallback。建议保持 C++20 + Win32 native 主路线，用 CMake/CTest/GoogleTest 强化测试闭环；Qt SerialPort 只作行为参考；vcpkg 暂缓到真实依赖增长时再引入。
