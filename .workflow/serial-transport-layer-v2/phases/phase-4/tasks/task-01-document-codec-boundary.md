# Task 01: Document Codec Boundary

> Phase: 4 — Boundary and Release Closure
> Status: Pending

---

## Objective

Document the stable byte-transport boundary and state clearly that variable bit layouts and encodings such as Gray code are future upper-layer features, not transport-v2 behavior.

## Files

**Create:**

None.

**Modify:**

- `docs/Win32原生架构.md`
- `docs/开发者指南.md`
- `docs/测试与验证.md`

**Test:**

- `scripts/check-docs-artifact-consistency.py`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

None. This task changes project documentation only.

## Steps

### Step 1: Reconcile Documentation With the Implemented Tree

Inspect the final session, capability, RTU, matcher, and test paths before editing documentation; record the actual type and target names to avoid documenting planned names that changed during implementation.

### Step 2: Describe the Session Ownership Boundary

Update `docs/Win32原生架构.md` to show the UI-owned `Win32SerialSession`, the sole HANDLE owner, session generation, typed results, bounded queue, and the non-owning write/byte capability views.

### Step 3: Describe the Protocol Boundary

State that RTU framing, CRC, stale-RX handling, retry policy, and protocol errors remain above raw byte operations and do not belong in neutral session types.

### Step 4: Describe the Future Codec Boundary

Add a concise future-extension section explaining that variable bit locations, binary/Gray/other encodings, scaling, signedness, and device-specific interpretation belong in a scanner/matcher codec layer above RTU/transport.

### Step 5: Prevent Feature Overstatement

State explicitly that Gray-code decoding is not implemented by transport v2 and that no current UI control or persistence format is added by this project.

### Step 6: Update Developer Guidance

Update `docs/开发者指南.md` with dependency-direction rules and a short checklist for future codec work: no HANDLE access, no transport changes unless byte semantics truly change, and separate codec tests.

### Step 7: Update Verification Guidance

Update `docs/测试与验证.md` to list the session/queue/PTY/boundary gates and distinguish automated PTY/Wine evidence from optional real-device smoke evidence.

## Verification

- [ ] All documented source files, targets, scripts, and commands exist.
- [ ] Documentation contains an explicit “not implemented” statement for Gray code.
- [ ] Documentation describes codec/matcher dependencies as pointing toward byte results, never the reverse.
- [ ] Documentation consistency and diff checks pass.

**Test command:**

```bash
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

**Expected output:**

```text
Documentation consistency check passes with exit code 0.
git diff --check produces no output and exits 0.
```

## Commit

```text
docs: document serial codec boundary (Phase 4, Task 01)
```
