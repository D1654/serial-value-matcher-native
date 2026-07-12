# BS-8: Production and Verification Strategy

Completed: 2026-07-12T08:30:00+08:00

## Research Findings

- The current native release already has the right production gates: CMake,
  CTest, MinGW/Wine, PTY scenarios, package audit, size checks, and hashes.
- This is a local desktop tool, not a network service. Containers, cloud
  orchestration, remote telemetry, and a database would add risk without user
  value.
- WebSearch returned a decode error in this environment; the existing CI and
  package evidence were used as the project-specific source of truth.

## Multi-Perspective Evaluation

- User/Product: a release is trustworthy when a disconnected device produces a
  clear local result and the package still runs without setup surprises.
- Developer: contract tests must run before slower Wine and package checks.
- Architect: production hardening belongs in the session/result contracts, not
  in a separate service layer.
- Security: keep no network, no telemetry, no payload logging by default, and
  preserve package provenance/hash checks.
- Ops/SRE: recovery is close, invalidate generation, and explicitly reconnect;
  there is no remote service to restart.
- Maintainer: a short local verification matrix is more useful than generic
  deployment infrastructure.

## Self-Interrogation

Initial recommendation: retain the current local CI/release pipeline and add
focused race, timeout, generation, and queue tests before packaging.

Challenge 1: Without real PLC hardware, a driver-specific fault may be missed.
Response: make fake and PTY tests mandatory, keep hardware smoke optional, and
record the hardware limitation rather than pretending CI proves it.

Challenge 2: More evidence could accidentally expose user payloads.
Response: log operation, endpoint, generation, byte count, status, and native
code only; payload remains opt-in and local.

Challenge 3: A new dependency could simplify code but break the small package
gate. Response: no dependency change in v2; any future exception needs its own
size and release decision.

## Decision

Use the existing native CI and package gates as the production boundary. Add
deterministic transport tests for close/cancel/reopen, deadlines, queue byte
limits, stale generations, short writes, and error categories. Keep real-device
smoke testing optional and local. No service, telemetry, database, or network
deployment layer is introduced.

Confidence: High.
