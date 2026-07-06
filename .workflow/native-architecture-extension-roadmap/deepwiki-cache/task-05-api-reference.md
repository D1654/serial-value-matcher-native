# API Reference — Task 05: Define Main Window Shell Seams
Generated: 2026-07-06T18:16:20+08:00

| API | Library | Source | Confidence |
|-----|---------|--------|------------|
| `WndProc` / `LRESULT CALLBACK` window procedure | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local source read-only review | High |
| `CreateWindowExW` `lpParam` -> `WM_NCCREATE` / `CREATESTRUCTW::lpCreateParams` | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window_lifecycle.cpp` | High |
| `GetWindowLongPtrW` / `SetWindowLongPtrW` with `GWLP_USERDATA` | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window_lifecycle.cpp` | High |
| Message dispatch via `switch` plus handler helpers | Win32 API pattern | DeepWiki: `microsoft/Windows-classic-samples`; Phase 1 cache; local `main_window.cpp` | High |
| `DefWindowProcW` fallback for unhandled messages | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window.cpp` and `main_window_lifecycle.cpp` | High |
| Window lifecycle messages: `WM_NCCREATE`, `WM_CREATE`, `WM_CLOSE`, `WM_DESTROY`, `WM_NCDESTROY` | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local lifecycle/message files | High |
| Message loop: `GetMessageW`, `TranslateMessage`, `DispatchMessageW` | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window_lifecycle.cpp` | High |

## WndProc

**Signature:** `LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)`

**Callback expectations:**
- The procedure must be callable by Win32 as a free/static callback registered in `WNDCLASSEXW::lpfnWndProc`.
- It receives every message for the registered window class. It should return the documented result for messages it handles and delegate unhandled messages to `DefWindowProcW`.
- In C++ class-based windows, the static callback commonly maps `HWND` to an instance pointer through `GWLP_USERDATA`, then forwards to an instance method.
- `WM_NCCREATE` is the right early hook for storing the instance pointer passed through `CreateWindowExW(..., lpParam)`, because it arrives before `WM_CREATE` and before normal feature initialization.

**Task 05 implication:** Keep `NativeMainWindow::windowProc` thin. It should attach/retrieve `NativeMainWindow*`, assign `window_`, and forward to `handleMessage`. Feature-specific work belongs behind message, command, lifecycle, or feature helper boundaries.

## Message Dispatch

**Pattern:** `handleMessage(UINT message, WPARAM wParam, LPARAM lParam)` switches on `message`, handles shell-level messages directly, and delegates feature categories to narrow helpers.

**Expected dispatch behavior:**
- Return `0` or another documented `LRESULT` for handled messages.
- Return `TRUE` only where Win32 expects truthy handling, such as cursor handling.
- For `WM_NOTIFY`, `WM_COMMAND`, and `WM_TIMER`, use helper methods that return `std::optional<LRESULT>` so unrecognized IDs fall through cleanly.
- Call `DefWindowProcW(window_, message, wParam, lParam)` for anything not explicitly handled.

**Current local shape:**
- `main_window.cpp` already centralizes raw message dispatch.
- `main_window_messages.cpp` handles create, notify, colors, timers, and destroy.
- `main_window_commands.cpp` splits command routing into quick, control, serial, log, send, file, analysis, menu, view, and help handlers.

**Task 05 implication:** Preserve this staged dispatch structure. The seam should make categories explicit, not introduce a new framework or broad file movement.

## Lifecycle

**Typical lifecycle sequence:**
1. Register the class with `RegisterClassExW`.
2. Create the top-level window with `CreateWindowExW`, passing the C++ object as `lpParam`.
3. On `WM_NCCREATE`, read `CREATESTRUCTW::lpCreateParams`, store it in `GWLP_USERDATA`, and capture the real `HWND`.
4. On `WM_CREATE`, create menus/child controls, open shell-owned resources, apply persisted UI state, and start shell timers.
5. During the message loop, `GetMessageW` feeds `TranslateMessage` and `DispatchMessageW`.
6. On `WM_CLOSE`, call `DestroyWindow` if the app accepts normal close.
7. On `WM_DESTROY`, stop timers, shut down feature activity coordinated by the shell, release shell-owned GDI/module resources, and call `PostQuitMessage`.
8. If object lifetime is tied to the window, use `WM_NCDESTROY` for final pointer clearing/release. In this project the object owns the window, so `WM_NCDESTROY` is optional unless future ownership changes require final detachment.

**Task 05 implication:** Do not move creation, destruction, or message-loop ownership out of `NativeMainWindow`. Feature helpers may participate in cleanup through explicit methods, but the shell coordinates order.

## HWND Ownership

**Ownership rule:** `NativeMainWindow` owns the top-level `HWND` and child control `HWND`s. Feature helpers/controllers should not become owners of these handles.

**Allowed seam shape:**
- A `native_main_window_context.h` can expose a small shell context needed by multiple helpers, such as selected stable handles, instance/window access, or shell services already owned by `NativeMainWindow`.
- The context should be a borrowed view, not an ownership transfer. Prefer pointer/reference members with clear lifetime comments over containers or factories that imply ownership.
- Avoid storing long-lived context copies in worker threads. UI handles remain thread-affine to the window/UI thread.

**Do not move into feature behavior:**
- `RegisterClassExW`, `CreateWindowExW`, `ShowWindow`, the message loop, top-level `DestroyWindow`, and final `PostQuitMessage`.
- Timer IDs and shell frame messages should remain coordinated by the shell unless a later task introduces a narrower scheduler abstraction.
- HWND creation/destruction for the existing main window controls should remain in `NativeMainWindow` during this task.

## Common Pitfalls

- Skipping `DefWindowProcW` for unhandled messages can break default non-client, sizing, focus, keyboard, and control behavior.
- Storing `this` too late, such as only in `WM_CREATE`, can leave early messages without an instance pointer. `WM_NCCREATE` is safer for class-based dispatch.
- Clearing or deleting the C++ object before late destruction messages finish can cause use-after-free. If ownership changes later, detach in `WM_NCDESTROY`.
- Letting feature helpers call `DestroyWindow`, `PostQuitMessage`, or own timers directly blurs lifecycle order and makes shutdown regressions harder to reason about.
- Calling feature behavior directly from broad `WM_COMMAND` branches makes the main window keep growing. Keep ID classification separate from actual feature operations where the existing structure allows it.
- Moving HWNDs into worker/background code violates UI thread affinity. Cross-thread work should post messages or marshal data back to the UI thread.
- Adding a broad "context" object with every control and every state member recreates the large `NativeMainWindow` surface under a new name.

## Project-Specific Guidance

**Current shell responsibilities to make explicit:**
- `NativeMainWindow::windowProc`: static Win32 callback, `GWLP_USERDATA` attachment, forwarding only.
- `NativeMainWindow::handleMessage`: message classification, shell message handling, and delegation to narrow handlers.
- `main_window_lifecycle.cpp`: class registration, top-level creation, show/first paint, message loop.
- `main_window_messages.cpp`: shell lifecycle side effects (`WM_CREATE`, timers, `WM_DESTROY`) and shared message categories.
- `main_window_commands.cpp`: command ID classification and routing from menu/control events to existing feature methods.
- `NativeMainWindow` data members: owner of top-level/child HWNDs, HINSTANCE, menu, timers, UI font/module resources, and shell-coordinated UI state.

**Recommended seam for Task 05:**
- Create `src/win32/native_main_window_context.h` as a small borrowed context for shared shell dependencies. Keep it plain, non-owning, and Win32-specific.
- Use it only where multiple feature helpers need the same shell access and the existing direct calls make responsibility unclear.
- Keep function names aligned with current categories: message dispatch, command routing, lifecycle, HWND/control access, status/log/scheduler coordination.
- Prefer helper functions that accept the narrow context they need over helpers that receive `NativeMainWindow&`.

**Boundary guidance:**
- Shell may call feature methods such as serial, send, log, file send, Modbus scan, and analysis operations.
- Feature methods should not register the window class, own top-level HWND lifetime, run the message loop, or decide process quit.
- Command routing can remain in `main_window_commands.cpp`; the goal is clearer seams and less cross-feature coupling, not a new plugin architecture.
- Preserve the existing CTest/native self-test surface. Any seam should be verifiable without requiring broad source relocation.
