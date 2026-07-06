# BS-8 — Production Readiness Architecture

Generated: 2026-07-06T10:09:39+08:00
Mode: Layer 1 — Context-Enriched Self-Reflection

## Research Findings

Search/Open: GitHub Actions artifacts and release evidence

- GitHub Actions supports storing and sharing workflow data as artifacts; current docs redirect the older artifact page to the newer "store and share data" tutorial path.
- Artifact preservation is relevant for Windows exe/zip, self-test logs, UI screenshots, package audits, and release evidence, but artifact retention is not a substitute for tagged release assets.
- Source: https://docs.github.com/en/actions/tutorials/store-and-share-data

Search/Open: CMake / CPack / install and package metadata

- CMake / CPack can package installed targets and metadata; install rules and runtime dependency filtering help define exactly what enters a release package.
- CMake itself does not replace project-specific size, dependency, version, and forbidden-runtime checks; those gates must be scripted and enforced in CI.
- Source: https://cmake.org/cmake/help/latest/module/CPack.html

Search/Open: Windows version metadata, signing, and recovery docs

- Windows `VERSIONINFO` resources provide file/product version metadata for native executables.
- SignTool is Microsoft's signing and verification tool for Windows files; signing can become a later release gate when certificate logistics are available.
- `RegisterApplicationRecoveryCallback` is available for crash/hang recovery scenarios, supporting a future path for saving volatile state before process termination.
- Sources: https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource, https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool, https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-registerapplicationrecoverycallback

DeepWiki: `Kitware/CMake`

- CPack supports Windows packaging metadata, install rules, runtime dependency filtering, and CTest as a release-governance step before packaging.
- DeepWiki also notes experimental SBOM capabilities in newer CMake, but for this project a script-level dependency manifest/size audit is safer and simpler first.
- Result: https://deepwiki.com/search/what-production-packaging-and_a4ab6fd0-eef6-43db-81f4-ebd72a7f068d

DeepWiki: `actions/upload-artifact`

- `actions/upload-artifact` preserves files/directories as immutable zip artifacts with retention policy, compression controls, missing-file behavior, and artifact naming constraints.
- Limitations matter for release evidence: artifacts are zip archives, not mutable stores; matrix jobs need unique names; retention must be planned.
- Result: https://deepwiki.com/search/what-capabilities-and-limitati_ad3fa8dc-1d83-42db-aa74-e723497c099a

DeepWiki: `microsoft/Windows-classic-samples`

- Windows samples show WER-related crash/error reporting, local log registration, application recovery callbacks, and `VERSIONINFO` resource metadata patterns.
- These patterns are useful as optional future hardening, but the immediate production baseline should stay simple: local logs, recovery-safe session writes, version metadata, and CI evidence.
- Result: https://deepwiki.com/search/what-operational-or-diagnostic_4711eb14-4196-4366-b0e7-f8f64975bd50

## Multi-Perspective Evaluation

Evaluating: production readiness for a small local Windows native executable, centered on deterministic build/release gates, local diagnostics, recovery, and security boundaries.

👤 User/Product: The user judges the GitHub Actions exe as the final target, so production readiness must validate that exact artifact, not only local debug builds. The release package should be easy to trust: correct docs, version, screenshots, size, and no surprise runtime dependencies.

💻 Developer: CI gates and evidence artifacts prevent "works locally" drift. The risk is making release scripts too elaborate; each gate must map to a real past failure or future feature risk.

🏗️ Architect: Desktop production architecture should not import cloud deployment patterns. Containers, orchestration, service discovery, and server autoscaling are non-goals; workflow-as-code and package governance are the correct production layer here.

🔒 Security: Minimum attack surface remains the core production decision: no telemetry, no account system, no hidden network runtime, normal-user operation, user-selected file/serial access, and explicit confirmation for dangerous writes.

⚙️ Ops/SRE: For a local tool, "observability" means deterministic local logs, self-test output, crash/recovery evidence, CI artifacts, package audit reports, and reproducible release verification. It is not centralized monitoring.

🔮 Future Maintainer: Runbooks and release checklists should be short and executable. Future maintainers should be able to reproduce a release, inspect package contents, and understand failure evidence without tribal knowledge.

## Self-Interrogation

Initial recommendation: Define production architecture as release artifact governance plus local runtime diagnostics/recovery, not as a cloud/server deployment model.

❓ Challenge 1: If we avoid heavier production tooling such as installers, signed installers, telemetry, and crash upload, then users may have less confidence in the executable.
💬 Response: The user's current workflow is GitHub Actions exe/zip. Confidence should first come from deterministic artifacts, hashes, version metadata, package audits, screenshots, tests, and release notes. Signing can be a later gate when certificate handling is settled; telemetry/upload contradicts the security boundary.
📊 Verdict: Recommendation holds.

❓ Challenge 2: If observability remains local-only, then remote debugging customer issues may be harder.
💬 Response: The security requirement explicitly rejects upload/telemetry. The correct design is exportable local diagnostic bundles with optional redaction, not automatic remote observability.
📊 Verdict: Recommendation holds with diagnostic-bundle requirement.

❓ Challenge 3: If package gates focus on size and dependencies, they may miss functional regressions in serial/Modbus/UI.
💬 Response: Package gates must be layered after functional gates: CTest, serial loopback/fake transport, UI perf/screenshot checks, self-test logs, then package/size/dependency/version/doc checks. No single gate is sufficient.
📊 Verdict: Recommendation holds with ordered gate requirement.

## Decision

✅ Decision: Use a desktop-native production architecture:

1. Deployment: GitHub Actions Windows native exe/zip remains the target artifact; no Docker/K8s/server deployment.
2. Release gates: CTest, self-test, serial simulation/loopback, UI screenshot/perf, package content audit, size gate, version metadata, docs/release consistency, dependency/forbidden-runtime audit.
3. Observability: local structured logs, exportable diagnostic bundle, self-test reports, release evidence artifacts, and optional crash/recovery evidence; no telemetry/upload.
4. Security: no hidden network runtime, no account/cloud/sync, normal-user privilege, explicit dangerous-write confirmation, report path privacy/redaction, future signing gate.
5. Data protection: storage schema version, recoverable file commits, orphan recovery, user-controlled export paths, privacy-aware report content.
6. Resilience: graceful serial disconnect/reconnect, bounded queues, backpressure, timeout/cancel paths, safe startup after interrupted sessions.
7. Operations: short runbooks for build, release, artifact verification, UI review, serial stress, rollback/re-release, and incident triage.

🎯 Confidence: High

📚 Key evidence:

- GitHub Actions/upload-artifact and CMake/CPack evidence supports artifact/evidence preservation and package governance.
- Windows samples and Microsoft docs support version metadata and optional crash/recovery diagnostics without adding cloud behavior.

⚠️ Open risks:

- Signing is valuable but certificate handling is a product/release-process decision; do not block Phase 4 on it unless a certificate exists.
- Diagnostic bundles must be carefully redacted to avoid leaking local file paths, customer data, or device identifiers.

❓ Need to verify with user: None for this phase; this aligns with the user's local-exe target and no-network security boundary.
