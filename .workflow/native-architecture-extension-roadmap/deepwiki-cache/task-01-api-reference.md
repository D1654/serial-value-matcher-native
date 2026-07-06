# API Reference - Task 01: Establish UI Baseline Gates

Generated: 2026-07-06T17:06:08+08:00

DeepWiki status: succeeded. No fallback used.

| API | Library | Source | Confidence |
|-----|---------|--------|------------|
| `upload-artifact@v4` inputs and outputs | GitHub Actions artifact flow | DeepWiki: `actions/upload-artifact` | High |
| `upload-artifact@v4` path matching | GitHub Actions artifact flow | DeepWiki: `actions/upload-artifact` | High |
| `upload-artifact@v4` immutability and unique artifact names | GitHub Actions artifact flow | DeepWiki: `actions/upload-artifact` | High |
| `WM_SIZE` / child layout | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | Medium-High |
| `WM_DPICHANGED` / DPI layout evidence | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | Medium-High |
| `WM_ERASEBKGND` / `WM_PAINT` | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | Medium-High |
| `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | Medium-High |

## `upload-artifact@v4` Inputs and Outputs

**Source:** DeepWiki `actions/upload-artifact`

**Signature/Inputs:**

```yaml
- uses: actions/upload-artifact@v4
  with:
    name: <artifact-name>
    path: <file | directory | glob | multiline paths>
    if-no-files-found: warn | error | ignore
    retention-days: <1..90, subject to repo settings>
    compression-level: <0..9>
    overwrite: true | false
    include-hidden-files: true | false
```

**Parameters:**

| Parameter | Required | Default | Notes |
|-----------|----------|---------|-------|
| `name` | No | `artifact` | Artifact name. In v4, names should be unique per workflow run/job unless `overwrite: true` is intentional. |
| `path` | Yes | None | File, directory, wildcard, or multiline path list to upload. Supports exclusions. |
| `if-no-files-found` | No | `warn` | Use `error` for required UI screenshots, logs, perf output, and validation reports. |
| `retention-days` | No | Repository default | Valid range is typically 1-90 days unless repository settings override. |
| `compression-level` | No | `6` | Zlib compression level. `0` is useful for large or already-compressed evidence. |
| `overwrite` | No | `false` | If true, deletes existing artifact with the same name and creates a new immutable artifact. |
| `include-hidden-files` | No | `false` | Hidden files are excluded by default to reduce accidental sensitive file upload. |

**Returns/Effects:**

- Uploads selected files as a GitHub Actions artifact.
- Produces outputs: `artifact-id`, `artifact-url`, `artifact-digest`.
- In v4, each successful upload creates an immutable artifact with a unique ID.

**Errors/Failure behavior:**

- If `path` matches no files, `warn` succeeds with warning, `error` fails, and `ignore` succeeds silently.
- Duplicate artifact names can fail because v4 artifacts are immutable.
- `overwrite: true` deletes an existing artifact first; the replacement has a new artifact ID.

**Example:**

```yaml
- name: Upload native UI evidence
  uses: actions/upload-artifact@v4
  with:
    name: native-ui-evidence-${{ github.run_id }}
    path: |
      artifacts/native-ui/screenshots/**
      artifacts/native-ui/*.log
      artifacts/native-ui/*.json
    if-no-files-found: error
    retention-days: 14
    compression-level: 6
```

**Gotchas:**

- Required screenshots and perf logs should use `if-no-files-found: error`.
- Avoid reusing one artifact name across matrix jobs.
- Do not rely on v3-style mutable artifact behavior.
- Keep artifact names stable enough for docs, but unique enough to avoid v4 conflicts.

## `upload-artifact@v4` Path Matching

**Source:** DeepWiki `actions/upload-artifact`

**Signature/Inputs:**

```yaml
with:
  path: |
    <include-path-or-glob>
    !<exclude-path-or-glob>
```

**Parameters:**

| Parameter | Required | Default | Notes |
|-----------|----------|---------|-------|
| `path` | Yes | None | Supports files, directories, wildcard glob patterns, multiline paths, and exclusion patterns. |
| `include-hidden-files` | No | `false` | Controls whether dotfiles and files under hidden directories are included. |

**Returns/Effects:**

- Resolves matching files using glob processing.
- Relative paths are resolved from the workflow current working directory.
- Wildcards preserve hierarchy after the first wildcard.

**Errors/Failure behavior:**

- Empty match behavior depends on `if-no-files-found`.
- Hidden files are excluded unless `include-hidden-files: true`.

**Example:**

```yaml
with:
  path: |
    artifacts/windows-native-ui/**
    !artifacts/windows-native-ui/**/*.tmp
  if-no-files-found: error
```

**Gotchas:**

- A path that works locally may not match in Actions if the working directory differs.
- For UI evidence, prefer explicit artifact directories produced by the capture script.
- Do not use broad hidden-file inclusion unless the artifact directory is tightly controlled.

## `upload-artifact@v4` Immutability and Unique Name Pitfalls

**Source:** DeepWiki `actions/upload-artifact`

**Signature/Inputs:**

```yaml
with:
  name: <unique-name>
  overwrite: false
```

**Parameters:**

| Parameter | Required | Default | Notes |
|-----------|----------|---------|-------|
| `name` | No | `artifact` | Must be unique when multiple uploads happen in one run. |
| `overwrite` | No | `false` | Delete-then-create behavior when replacing an existing artifact is intended. |

**Returns/Effects:**

- v4 artifacts are immutable after creation.
- Multiple jobs should upload separate artifacts, then merge if needed through the v4 merge flow.

**Errors/Failure behavior:**

- Duplicate artifact names can fail.
- `overwrite: true` changes the artifact ID, so downstream references to old IDs become stale.

**Example:**

```yaml
with:
  name: native-ui-${{ matrix.capture_mode }}-${{ github.run_id }}
  path: artifacts/native-ui/${{ matrix.capture_mode }}/**
  if-no-files-found: error
```

**Gotchas:**

- Do not let screenshot and perf-log uploads accidentally target the same artifact name from different jobs.
- If docs mention artifact names, prefer predictable prefixes plus run-specific suffixes.

## `WM_SIZE` / Child Layout

**Source:** DeepWiki `microsoft/Windows-classic-samples`

**Signature/Inputs:**

```cpp
case WM_SIZE:
    // LOWORD(lParam) = new client width
    // HIWORD(lParam) = new client height
    return 0;
```

**Parameters:**

| Parameter | Meaning |
|-----------|---------|
| `wParam` | Resize type, such as minimized/maximized/restored. |
| `LOWORD(lParam)` | New client width. |
| `HIWORD(lParam)` | New client height. |

**Returns/Effects:**

- Application recomputes child bounds after the parent client size changes.
- For tabbed UI, resize tab control and active tab content based on one computed content rectangle.

**Errors/Failure behavior:**

- Recomputing layout in scattered places can cause drift between capture evidence and real UI.
- Moving children one by one with immediate redraw can produce visible flicker.
- Ignoring minimized size can produce invalid or tiny child rectangles.

**Example:**

```cpp
case WM_SIZE:
    LayoutMainWindow(hwnd, LOWORD(lParam), HIWORD(lParam));
    return 0;
```

**Gotchas:**

- Resize evidence should include screenshots, tab visibility, prompt visibility, and perf output.
- UI capture should include resized states, not only first-launch state.

## `WM_DPICHANGED` / DPI Layout Evidence

**Source:** DeepWiki `microsoft/Windows-classic-samples`

**Signature/Inputs:**

```cpp
case WM_DPICHANGED:
    // HIWORD(wParam) = new DPI Y
    // LOWORD(wParam) = new DPI X
    // lParam = suggested RECT*
    return 0;
```

**Parameters:**

| Parameter | Meaning |
|-----------|---------|
| `LOWORD(wParam)` | New X DPI. |
| `HIWORD(wParam)` | New Y DPI. |
| `lParam` | Suggested window rectangle for the new DPI. |

**Returns/Effects:**

- DPI-aware apps should resize/reposition the window and recompute scaled layout metrics.
- Baseline evidence should distinguish normal resize checks from DPI/scaling checks.

**Errors/Failure behavior:**

- Ignoring DPI changes can make controls overlap or clip under scaling.
- Using fixed pixel constants without scaling can invalidate UI evidence on high-DPI systems.

**Example:**

```cpp
case WM_DPICHANGED: {
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    SetWindowPos(hwnd, nullptr,
                 suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
}
```

**Gotchas:**

- Do not invent new DPI performance thresholds during baseline documentation.
- DPI evidence should be treated as local/manual or scripted only if the current workflow actually captures it.

## `WM_ERASEBKGND` / `WM_PAINT`

**Source:** DeepWiki `microsoft/Windows-classic-samples`

**Signature/Inputs:**

```cpp
case WM_ERASEBKGND:
    return TRUE;

case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    // draw invalid region
    EndPaint(hwnd, &ps);
    return 0;
}
```

**Parameters:**

| API | Parameter | Meaning |
|-----|-----------|---------|
| `WM_ERASEBKGND` | `wParam` | HDC for background erase. |
| `BeginPaint` | `HWND`, `PAINTSTRUCT*` | Begins painting and validates update region. |
| `EndPaint` | `HWND`, `PAINTSTRUCT*` | Ends painting. |

**Returns/Effects:**

- Returning `TRUE` from `WM_ERASEBKGND` can suppress redundant background erase when `WM_PAINT` draws the full area.
- `BeginPaint` / `EndPaint` validate the invalid region during `WM_PAINT`.

**Errors/Failure behavior:**

- Default erase plus later paint can create resize flicker.
- Drawing outside `WM_PAINT` without careful invalidation can create stale or torn visuals.
- Returning `TRUE` is only correct if paint logic covers the necessary background/content.

**Example:**

```cpp
case WM_ERASEBKGND:
    return TRUE; // WM_PAINT owns full redraw for this surface.
```

**Gotchas:**

- Task 01 documents flicker as observable evidence; code changes belong to later tasks.
- Baseline docs should avoid claiming flicker is fixed unless capture/perf evidence proves it.

## `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos`

**Source:** DeepWiki `microsoft/Windows-classic-samples`

**Signature/Inputs:**

```cpp
HDWP hdwp = BeginDeferWindowPos(count);
hdwp = DeferWindowPos(hdwp, hwndChild, hwndInsertAfter,
                      x, y, cx, cy, flags);
EndDeferWindowPos(hdwp);
```

**Parameters:**

| Parameter | Meaning |
|-----------|---------|
| `count` | Estimated number of windows to reposition. |
| `hwndChild` | Child window being moved/resized. |
| `hwndInsertAfter` | Z-order target, often unchanged with `SWP_NOZORDER`. |
| `x`, `y`, `cx`, `cy` | New position and size. |
| `flags` | Common flags include `SWP_NOZORDER`, `SWP_NOACTIVATE`, `SWP_NOMOVE`, `SWP_NOSIZE`. |

**Returns/Effects:**

- Batches multiple child-window position changes into one update sequence.
- Reduces resize flicker and inconsistent intermediate states.

**Errors/Failure behavior:**

- `DeferWindowPos` can fail and return null; subsequent calls must not assume success.
- Incorrect flags can skip intended movement or sizing.
- Child windows may still repaint if invalidation/redraw policy is not controlled.

**Example:**

```cpp
HDWP hdwp = BeginDeferWindowPos(2);
if (hdwp) {
    hdwp = DeferWindowPos(hdwp, hwndTabs, nullptr, 0, 0, width, tabHeight,
                          SWP_NOZORDER | SWP_NOACTIVATE);
}
if (hdwp) {
    hdwp = DeferWindowPos(hdwp, hwndContent, nullptr, 0, tabHeight,
                          width, height - tabHeight,
                          SWP_NOZORDER | SWP_NOACTIVATE);
}
if (hdwp) {
    EndDeferWindowPos(hdwp);
}
```

**Gotchas:**

- For Task 01, this is evidence interpretation guidance, not an instruction to refactor layout yet.
- Later UI layout tasks should prefer one layout transaction per resize over scattered `SetWindowPos` calls.
