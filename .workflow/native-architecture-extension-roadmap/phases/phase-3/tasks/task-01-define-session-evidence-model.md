# Task 01: Define Session Evidence Model

> Phase: 3 — Extension Capability & Production Hardening
> Status: pending

---

## Objective

Define a structured local session evidence model for TX/RX, user actions, scan parameters, match results, and version metadata.

## Files

**Create:**
- `src/capture/session_evidence.h`
- `src/capture/session_evidence.cpp`
- `tests/session_evidence_tests.cpp`

**Modify:**
- `src/capture/capture_bus.h`
- `src/capture/capture_bus.cpp`
- `src/session/console_model.h`
- `src/session/console_model.cpp`
- `src/storage/session_store.h`

**Test:**
- `tests/session_evidence_tests.cpp`
- Existing capture/session tests

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Internal evidence model only. |

## Steps

### Step 1: Define Evidence Event Types

Add event types for raw TX, raw RX, user command, Modbus scan settings, match result, report metadata, and app version.

### Step 2: Preserve Raw Data First

Ensure raw TX/RX evidence is captured before formatting or UI filtering.

### Step 3: Add Session Association

Associate evidence with session id, monotonic order, wall-clock timestamp, and source subsystem.

### Step 4: Add Unit Tests

Cover ordering, serialization-ready fields, optional metadata, and privacy-sensitive fields.

### Step 5: Run Focused Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "session_evidence|console_model"
```

## Verification

- [ ] Evidence model is local and structured.
- [ ] Raw TX/RX can be preserved independent of UI refresh.
- [ ] Focused tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "session_evidence|console_model"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
feat: define local session evidence model (Phase 3, Task 01)
```
