# Phase 4: Boundary and Release Closure

> Parent: [Project Plan](../../project-plan.md)
> Status: Completed

---

## Objective

Close the architecture with enforceable documentation and a complete release-evidence pass while keeping Gray-code implementation out of transport v2.

## Prerequisites

- [x] Phase 3 production hardening and PTY/CI gates pass.
- [x] No old facade symbols or files remain.

## Libraries & Dependencies

| Library | GitHub Repo | Used For |
|---------|-------------|----------|
| CMake/CTest | `Kitware/CMake` | Architecture-check registration and full test execution. |
| Wine | `wine-mirror/wine` | Final native self-test, UI performance, PTY, and package verification. |
| GitHub Actions | `actions/runner` | Final Windows package/UI workflow evidence. |

## Task List

| # | Task | Description | Files | Est. Steps |
|---|------|-------------|-------|------------|
| 1 | Document codec boundary | Explain that variable bit layouts and encodings such as Gray code belong above byte transport and are not implemented in this project. | `docs/Win32原生架构.md`; `docs/开发者指南.md`; `docs/测试与验证.md` | 7 |
| 2 | Enforce transport boundaries | Add a repository check that rejects UI, matcher/codec, Gray-code, old-facade, or concrete Win32 dependencies in neutral transport contracts. | Create `scripts/check-transport-boundaries.py`; modify `CMakeLists.txt`, `scripts/check-docs-artifact-consistency.py`, `docs/架构说明.md` | 8 |
| 3 | Run final release verification | Execute the complete host/MinGW/Wine/PTY/package/docs matrix and record an evidence-based completion report. | Create `.workflow/serial-transport-layer-v2/completion-report.md`; update `.workflow/serial-transport-layer-v2/state.json` | 10 |

## Deliverables

- [x] User/developer/architecture documentation states the future codec boundary accurately.
- [x] Automated architecture checks protect the neutral transport layer.
- [x] Completion report records every required command, result, artifact, and known hardware limitation.

## Verification Checklist

- [x] Run the new transport-boundary check through CTest.
- [x] Run fresh host CTest and full MinGW CTest builds.
- [x] Run PTY, Wine self-test/UI performance, package audit/hash, docs consistency, and diff checks.
- [x] Verify documentation never claims Gray code is implemented.

## Phase-Specific Risks

| Risk | Mitigation |
|------|------------|
| A boundary checker becomes brittle or over-broad. | Check explicit forbidden dependency directions and symbols, and cover the script with representative fixtures or self-checks. |
| Final evidence is mistaken for real-hardware certification. | Record PTY/Wine scope and optional hardware smoke limitations in the report. |

---

> Detailed task instructions are in the `tasks/` subdirectory.
