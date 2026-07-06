# BS-2 — Architecture Design

Generated: 2026-07-06T08:20:00+08:00
Mode: Layer 1 — Context-Enriched Self-Reflection

## Research Findings

Search/Open: Microsoft Learn — Managing Application State

- Microsoft describes that a window procedure is inherently stateless and warns that global variables work only for small programs but become confusing and error-prone in larger programs.
- Microsoft shows an object-oriented Win32 pattern where the static window procedure stores/retrieves a per-window object pointer via window instance data and delegates to `HandleMessage`.
- Source: https://learn.microsoft.com/en-us/windows/win32/learnwin32/managing-application-state-

Search/Open: Microsoft Learn — Using Window Procedures

- Microsoft documents the standard `WndProc` switch pattern for `WM_CREATE`, `WM_PAINT`, `WM_SIZE`, `WM_DESTROY`, and fallback to `DefWindowProc`.
- This supports keeping `NativeMainWindow` as a message/lifecycle shell, but not as the owner of all business logic.
- Source: https://learn.microsoft.com/en-us/windows/win32/winmsg/using-window-procedures

Search/Open: Microsoft Learn — Synchronous and Asynchronous I/O / High DPI

- Microsoft documents that synchronous I/O blocks the calling thread until completion/cancel/error, supporting the requirement to isolate long-running serial work from UI-thread operations.
- Microsoft documents that Win32/common-control apps do not automatically handle all DPI scenarios, supporting production use of a tested layout model and transaction-based HWND movement.
- Sources: https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o, https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows

DeepWiki: `microsoft/Windows-classic-samples`

- Win32 samples typically use central `WndProc` / window-class dispatch, with painting in `OnPaint`, layout on resize, and worker threads for long jobs.
- DeepWiki found no direct serial-debugging architecture sample; the evidence is useful for Win32 UI/message separation, not for serial-domain logic itself.
- Result: https://deepwiki.com/search/what-architecture-patterns-do_e08c77d2-a672-47d8-93af-8d4498dd6803

DeepWiki: `Kitware/CMake`

- CMake supports target-centric organization, CTest registration, package metadata/version propagation, runtime dependency filtering, and package-size/dependency governance through build scripts.
- This supports a build architecture where production code stays small while tests/tools remain outside release artifacts.
- Result: https://deepwiki.com/search/what-cmake-architecture-patter_1925a3aa-960b-4b13-9fb0-a82bae55dc1c

## Multi-Perspective Evaluation

Evaluating: layered native architecture with a thin Win32 shell, feature controllers/services, transport/protocol/storage ports, and tested layout model.

👤 User/Product: This keeps the current fast native UI and Chinese local workflow while reducing the risk that new features create more tab/control regressions. It avoids a heavy plugin/dashboard architecture that users did not ask for.

💻 Developer: The structure lowers `NativeMainWindow` pressure and gives new features an obvious path: state/model/service tests first, then controller, then HWND binding. The risk is creating too many tiny abstractions if boundaries are not tied to real hot spots.

🏗️ Architect: Ports/adapters fit the confirmed requirements: serial now, TCP later by decision gate, storage file backend now, SQLite later by decision gate. The architecture should remain modular, not become a generic plugin framework.

🔒 Security: A thin local shell plus explicit transport boundaries preserves the minimum attack surface. TCP-capable interfaces must remain non-instantiated and non-UI in this phase to avoid silently expanding runtime network behavior.

⚙️ Ops/SRE: Target-centric CMake and package-size gates support release governance. The architecture should make validation artifacts first-class: CTest, self-test, UI perf, package audit, and dependency/size checks.

🔮 Future Maintainer: The future maintainer benefits if each boundary has a single reason to exist: layout drift, main-window size, Modbus duplication, storage consistency, or send I/O blocking. Abstracting beyond these reasons would make the code harder, not easier.

## Self-Interrogation

Initial recommendation: Use a layered native shell + feature controller + ports/adapters architecture, with no full plugin framework in this phase.

❓ Challenge 1: If future multi-protocol work grows quickly, then this architecture may be too conservative because it avoids a full plugin framework.
💬 Response: The user explicitly wants small size and stable delivery. A port boundary for transport/protocol is enough for Phase 4; a plugin framework can be a future decision when real external protocol modules appear.
📊 Verdict: Recommendation holds.

❓ Challenge 2: If `NativeMainWindow` remains the owner of HWNDs, then controllers may still become coupled to UI details.
💬 Response: Controllers should own feature state and commands, not HWNDs. The shell maps model/controller output to HWNDs. This must be made explicit in Section 2 and Phase 3 tasks.
📊 Verdict: Recommendation holds with constraint.

❓ Challenge 3: If production layout modelization is delayed, then other controller work may still inherit UI drift risk.
💬 Response: Phase 1 should begin with `NativeLayoutModel` productionization and layout transaction integration before broad feature-controller extraction.
📊 Verdict: Recommendation holds with ordering requirement.

## Decision

✅ Decision: Adopt a layered native architecture:

1. `NativeMainWindow` as Win32 shell: message routing, lifetime, HWND ownership, frame scheduling.
2. Feature controllers for serial, send, log, workbench, Modbus, analysis, preferences.
3. Core services for protocol, transport transactions, storage, evidence/report generation.
4. Ports/adapters for serial transport now, TCP transport later behind a decision gate.
5. `NativeLayoutModel` as production layout calculation source.
6. CMake/CTest/package gates as first-class production architecture.

🎯 Confidence: High

📚 Key evidence:

- Microsoft Win32 docs support per-window state/object dispatch rather than global/god-window state.
- DeepWiki confirms Win32 samples use message dispatch, resize/paint separation, and worker threads, but not heavy architecture frameworks.

⚠️ Open risks:

- Controller extraction can over-abstract if not tied to existing hot spots.
- TCP/storage future backends must remain explicit decision gates, not hidden runtime behavior.
