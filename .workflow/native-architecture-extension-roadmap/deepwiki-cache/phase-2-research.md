# Phase 2 DeepWiki Research — Backend Consistency

- Generated: 2026-07-07T16:04:57+08:00
- Phase objective: unify serial write, Modbus transaction, and native storage behavior behind deterministic, testable, non-blocking paths.
- Runtime dependency decision: no TCP runtime, no SQLite backend, no Qt runtime added to the Win32 native release in this phase.

## Repositories Queried

### `microsoft/Windows-classic-samples`

Question: Core Win32 serial communication patterns and pitfalls relevant to bounded non-blocking serial write queues, timeout handling, cancellation, and UI-thread avoidance.

Key findings:

- Non-blocking behavior should be modeled around asynchronous I/O and worker-side completion, not UI-thread waiting.
- Completion-oriented designs use an operation context with an `OVERLAPPED`-style payload and process completion away from the caller.
- Timeout should be explicit in the operation model. Win32 examples commonly use wait APIs with timeout paths rather than implicit blocking.
- Cancellation should be represented as a first-class outcome. Windows cancellation patterns include cancelling pending I/O from the responsible worker/thread context.
- A bounded queue is application-level policy: the platform provides async primitives, but queue depth, FIFO ordering, backpressure, and cancellation semantics must be owned by project code.

Task implication:

- Task 01 should define a pure request/result queue contract first. Win32 handle/`HWND` integration belongs in Task 02.

### `qt/qtserialport`

Question: Behavioral reference for serial write buffering, write completion, timeout, cancellation, and error reporting relevant to designing a C++ bounded serial write queue contract without depending on Qt runtime.

Key findings:

- Qt SerialPort uses an internal write buffer and emits completion events when bytes are written.
- A bounded buffer may accept only part of a payload or force caller-visible backpressure. For this project, all-or-reject request enqueue is simpler and easier to test.
- `waitForBytesWritten()` timeout behavior maps to an explicit timeout/error outcome, but this native project should avoid blocking UI paths.
- Closing/clearing the port can cancel pending writes. The project should represent cancelled writes distinctly from failed or timed-out writes.
- Error reporting is structured as an enum plus message. The native queue should follow that pattern without importing Qt runtime types.

Task implication:

- Define accepted, rejected/backpressure, sent, failed, timeout, and cancelled outcomes with optional diagnostic text.

### `Kitware/CMake`

Question: CTest and CMake target patterns for adding a small C++ unit test executable and registering focused tests in an existing native project.

Key findings:

- Add a small test executable with `add_executable()`.
- Register it with `add_test(NAME ... COMMAND ...)`.
- Link only project libraries needed by the test. For pure C++ state/queue tests, no external test framework is required.
- Focused verification can use `ctest --test-dir <build> -R "<pattern>" --output-on-failure`.

Task implication:

- Add `serial_write_queue_tests` next to existing native state tests and keep it Qt-free.

## Summary for Task Execution

- Write queue contract must be pure standard C++ and independent of Win32/Qt/UI.
- Queue behavior must be bounded and deterministic: FIFO, explicit capacity, explicit backpressure, request ids, timeout metadata, cancellation, and completion result.
- Native serial IO state should expose queue status without owning actual queue storage or HWND details.
- Task 02 will perform actual native send integration; Task 01 only creates the tested contract and state visibility.
