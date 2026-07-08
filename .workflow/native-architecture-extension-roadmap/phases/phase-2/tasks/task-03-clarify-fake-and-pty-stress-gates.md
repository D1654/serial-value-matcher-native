# Task 03: Clarify Fake and PTY Stress Gates

> Phase: 2 — Backend Consistency
> Status: completed

---

## Objective

Separate CI-blocking fake/native serial tests from current local-only PTY loopback release-candidate evidence.

## Files

**Create:**
- None

**Modify:**
- `scripts/run-windows-native-serial-pty-loopback.py`
- `.github/workflows/windows-native-package.yml`
- `docs/Windows串口真机验收.md`
- `docs/测试与验证.md`

**Test:**
- Existing docs consistency test
- Local PTY loopback script when environment supports it

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| GitHub Actions artifact flow | actions/upload-artifact | artifact upload, step summary | Preserve PTY matrix evidence and avoid false CI claims. |

## Steps

### Step 1: Preserve Current Local-Only PTY Status

Keep Windows runner PTY limitation explicit in workflow summary and docs.

### Step 2: Define CI-Blocking Serial Gates

Identify tests that can block in CI today: native serial unit tests, fake transport tests, self-test, package audit.

### Step 3: Document Release-Candidate PTY Evidence

Write the local command and expected scenarios for PTY loopback release-candidate validation.

### Step 4: Evaluate CI Substitute

If feasible, add a CI-safe fake/loopback stress target; otherwise document why local evidence remains required.

### Step 5: Run Docs Consistency

```bash
python3 scripts/check-docs-artifact-consistency.py
```

## Verification

- [x] CI-blocking and local-only evidence are clearly separated.
- [x] Workflow summary does not imply PTY is fully CI-blocking.
- [x] Docs consistency passes.

**Test command:**
```bash
python3 scripts/check-docs-artifact-consistency.py
```

**Expected output:**
```
No docs/artifact consistency failures.
```

## Commit

```
docs: clarify native serial stress gate boundaries (Phase 2, Task 03)
```
