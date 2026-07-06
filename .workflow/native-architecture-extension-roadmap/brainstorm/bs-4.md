# BS-4 — Algorithm & Design Strategy

Generated: 2026-07-06T10:04:28+08:00
Mode: Layer 1 — Context-Enriched Self-Reflection

## Research Findings

Search/Open: Microsoft Win32 asynchronous I/O and `PostMessage`

- Microsoft documents that synchronous I/O blocks the calling thread until completion, while asynchronous/overlapped I/O allows work to proceed and completion to be handled later.
- `PostMessage` posts a message to a thread's message queue and returns without waiting for the message to be processed, making it suitable for worker-to-UI completion notification when paired with ownership/lifetime discipline.
- Sources: https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o, https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagea

Search/Open: Microsoft Win32 repaint, resize, and DPI references

- Win32 apps must explicitly handle `WM_SIZE`, `WM_PAINT`, invalidation, and DPI-related layout behavior; uncontrolled repaint/background erase is a common source of resize flicker.
- DPI-aware apps need explicit scaling/layout behavior; this supports making `NativeLayoutModel` and layout transactions part of the algorithmic strategy, not only UI cleanup.
- Sources: https://learn.microsoft.com/en-us/windows/win32/gdi/wm-paint, https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-size, https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows

Search/Open: Modbus application protocol and serial-line framing references

- Modbus behavior is transaction-oriented: a request maps to a response or exception, with timing/timeout and function-code semantics handled consistently.
- For RTU-style workflows, scan/match/report logic should be driven by a single executor/state machine rather than duplicated per UI path.
- Sources: https://www.modbus.org/modbus-specifications

DeepWiki: `qt/qtserialport`

- Qt SerialPort maps Windows serial reads/writes to overlapped I/O, uses completion notification integrated into its event loop, and exposes structured timeout/error propagation.
- It also reinforces a single-thread/event-affinity model for serial objects; this supports a native design where serial I/O is isolated from UI and reports state through controlled events.
- Result: https://deepwiki.com/search/what-implementation-patterns-d_bb8c5dc9-d53b-4346-91b0-f7226c1fcdba

DeepWiki: `microsoft/Windows-classic-samples`

- Windows native samples reinforce `WM_SIZE` handling, efficient `WM_PAINT` with `BeginPaint`/`EndPaint`, background/queued UI notifications, and DPI-aware layout adjustment.
- The samples do not provide a full serial-domain algorithm, but they validate the UI-side strategy: batch layout/repaint work and keep background work out of the message handler.
- Result: https://deepwiki.com/search/what-implementation-patterns-i_195fad89-70e6-414b-a2fd-21f09c983b6b

DeepWiki: `google/googletest`

- Useful test patterns include parameterized parser/state-machine tests, fake transport tests, mock interfaces, fixtures for transaction setup/teardown, and matchers for parsed frame/result validation.
- This supports designing algorithms around testable pure functions and small interfaces instead of HWND-dependent behavior.
- Result: https://deepwiki.com/search/what-test-design-patterns-are_3df6fe63-8d41-4fd4-a5b8-17f2d4fe85db

## Multi-Perspective Evaluation

Evaluating: event-driven algorithms with async serial write queue, unified Modbus transaction state machine, batched UI/layout updates, bounded log buffering, and recoverable storage transactions.

👤 User/Product: The strategy directly targets the user's reported pain points: UI flicker, resize instability, tab/control regressions, and uncertainty about function closure. It keeps the visible experience stable while preparing for command sequence and evidence features.

💻 Developer: Algorithms become testable if protocol parsing, matching, storage transaction, layout model, and fake transport behavior are isolated from HWND. The risk is writing a generalized async framework instead of the small queues/state machines the product actually needs.

🏗️ Architect: A single Modbus executor and transport port prevent duplicate retry/timeout/exception behavior. Bounded event queues and layout transactions align with BS-2's controller/service layering.

🔒 Security: Dangerous writes, batch commands, and Modbus broadcast writes must pass explicit confirmation and audit logging. The algorithm should default to no hidden network behavior and no unbounded script execution.

⚙️ Ops/SRE: CI can stress the algorithms without hardware by using fake serial transport, deterministic timeouts, session-store fault injection, and UI perf scripts. Production readiness depends on preserving those tests as gates, not manual checks.

🔮 Future Maintainer: New features should plug into existing command/event/transaction patterns. If each new feature invents a different queue, timer, or retry model, the architecture will degrade quickly.

## Self-Interrogation

Initial recommendation: Use a small event-driven design: one serial write queue, one transport event stream, one Modbus transaction state machine, batched UI/layout commits, bounded log buffering, and recoverable file-storage transactions.

❓ Challenge 1: If the event/queue layer is implemented too broadly, then the code may become a homegrown framework that is harder to debug than the current direct calls.
💬 Response: The queue should be scoped to real hot paths: serial writes, background RX/TX events, UI log/layout batching, and command-sequence execution. It should not become a generic reactive framework or plugin bus.
📊 Verdict: Recommendation holds with scope constraint.

❓ Challenge 2: If all serial writes become asynchronous, then users may lose confidence about whether a write was actually sent, especially for manual single-send workflows.
💬 Response: The write queue must emit explicit accepted/sent/failed/timeout/cancelled events, and UI affordances should reflect those states. Manual single-send still uses the queue, but the result is surfaced immediately in log/status.
📊 Verdict: Recommendation holds with UX/status requirement.

❓ Challenge 3: If storage transactions are delayed until evidence packages are implemented, then later evidence/report work may be built on an unreliable append model.
💬 Response: Storage transaction/orphan recovery belongs before evidence package expansion. The first implementation can remain file-based, but must define commit markers, schema version, and recovery scanning.
📊 Verdict: Recommendation holds with ordering requirement.

## Decision

✅ Decision: Adopt these algorithm/design strategies for the roadmap:

1. UI responsiveness: no serial write/read wait on UI thread; all long work reports through queued events.
2. Serial send: introduce a bounded async write queue with per-command timeout, cancellation, result events, and backpressure.
3. RX/log handling: use bounded buffers and batched UI flushes; preserve raw TX/RX evidence before formatting.
4. Layout/resize: production UI must consume `NativeLayoutModel`; resize commits use layout transactions and redraw minimization.
5. Modbus: converge scans/reads/matching on one transaction executor/state machine with shared timeout, retry, exception, and data-format handling.
6. Matching/report: keep parsing, candidate scoring, validation rules, and report evidence as pure/testable core services.
7. Storage: keep compatible file backend, but add schema version, atomic-ish commit discipline, and orphan recovery before growing evidence data.
8. Automation: start with declarative command sequence + assertions; defer full scripting until sandbox/audit/security boundaries exist.

🎯 Confidence: High

📚 Key evidence:

- Win32 and QtSerialPort evidence both support overlapped/background I/O with event-style completion rather than UI-thread waits.
- GoogleTest evidence supports designing parser/state/storage/transport behavior as testable interfaces independent of HWND.

⚠️ Open risks:

- The async/event layer can become overbuilt if it is not tied to current serial/log/layout hot paths.
- Batched UI updates must be tuned so performance gains do not hide important immediate feedback.

❓ Need to verify with user: None at this gate; the strategy follows confirmed requirements for stability, performance, small size, and future command/evidence features.
