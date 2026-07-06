# BS-6 — Task Decomposition Review

Generated: 2026-07-06T16:27:22+08:00
Mode: Layer 1 — Reduced

## Research Findings

Search/Open: Incremental modernization and strangler-style replacement

- The strangler pattern supports gradual replacement by creating seams around existing behavior and migrating incrementally instead of rewriting the whole system.
- This matches the plan's "no broad directory move, no big rewrite" constraint for `NativeMainWindow`, storage, and Modbus paths.
- Source: https://learn.microsoft.com/en-us/azure/architecture/patterns/strangler-fig

Search: "software architecture task decomposition implementation plan best practices incremental refactoring tests"

- The useful common guidance is to slice work by independently verifiable increments, preserve existing behavior with tests, and avoid large cross-cutting tasks without acceptance gates.
- This supports keeping 3 phases with 8 tasks each, but requires Level 3 task details to state verification commands and expected evidence.

Search: "CMake CTest test driven refactoring legacy code best practices"

- For native C++ projects, build-system/test gates should be part of the task inventory, not a final afterthought.
- This supports dedicated tasks for UI capture, native package workflow, docs/package consistency, and release evidence hardening.

## Multi-Perspective Evaluation

Evaluating: 3 phases, 24 tasks, ordered as UI/architecture foundation → backend consistency → extension capability and production hardening.

Developer: The task count is high but appropriate because the codebase already has many specific seams and tests. The main risk is broad tasks such as "extract controllers"; Level 3 must define small file-level steps and behavior-preserving checkpoints.

Architect: The dependency order is correct. UI/layout seams come before backend unification because otherwise backend work would continue wiring directly through `NativeMainWindow`. Backend consistency comes before evidence/command sequence because those features depend on stable serial, Modbus, and storage semantics.

Ops/SRE: The plan correctly treats existing workflows/scripts as assets to harden. Level 3 must not invent new release infrastructure where current workflows already cover CTest, self-test, UI perf, package audit, and docs consistency.

Security: The no-TCP-runtime and no-SQLite-by-default constraints remain visible. Level 3 should repeat these constraints in task dependencies/verification where relevant so Phase 4 cannot accidentally expand runtime behavior.

Maintainer: The plan is maintainable if every task has a prewritten commit message and touches a small file set. If any task grows beyond 1-5 files of meaningful code change, Phase 4 should split it before implementation.

## Self-Interrogation

Initial recommendation: Keep the 3-phase / 24-task decomposition and proceed to Level 3 with stricter acceptance details.

❓ Challenge 1: Would swapping Phase 1 and Phase 2 be more logical because async serial and Modbus are more core than UI?
💬 Response: No. Current user pain and code risk include UI flicker, tab blanks, and `NativeMainWindow` coupling. Backend changes wired through an unstable shell would amplify risk. Phase 1 first is correct.
📊 Verdict: Recommendation holds.

❓ Challenge 2: Which task is most likely to become a bottleneck?
💬 Response: Phase 1 Task 6 and Phase 2 Task 2 are bottlenecks: UI boundary extraction and async write queue integration both touch live workflow paths. Level 3 must make these conservative and behavior-protected.
📊 Verdict: Recommendation holds with heightened verification.

❓ Challenge 3: Are there implicit dependencies not reflected in Level 2?
💬 Response: Yes: existing validation assets are prerequisites for many gates, and PTY loopback is not fully CI-blocking. Level 3 must explicitly reference existing tests/workflows and distinguish CI-blocking gates from local release-candidate evidence.
📊 Verdict: Recommendation holds with explicit task acceptance criteria.

## Decision

✅ Decision: Keep the Level 1/Level 2 decomposition unchanged: 3 phases, 24 tasks. Write Level 3 details with these mandatory refinements:

1. Every task must use existing tests/workflows/scripts where possible before adding new infrastructure.
2. Phase 1 Task 6 and Phase 2 Task 2 must be conservative and split during Phase 4 if they exceed safe file/step scope.
3. PTY loopback must remain local pre-release evidence unless a CI-capable substitute is implemented.
4. UI performance thresholds must be baseline-derived from current `--ui-perf-test` output.
5. No task may implement TCP UI/runtime, default SQLite, or a broad plugin/script engine.

🎯 Confidence: High

⚠️ Open risks:

- Some task file lists are estimates; Phase 4 may split a task before coding if local code reading shows a safer cut.
- Consistency verification must check task count and production readiness coverage before approval.
