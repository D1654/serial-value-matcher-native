# BS-5: Draft Integrity Check

Completed: 2026-07-12T08:50:00+08:00

## Research Findings

- The project baseline and the completed BS-2/3/4/8 checks all point to the
  same smallest useful change: one serial owner, typed results, bounded work,
  and existing native gates.
- WebSearch returned a decode error in this environment. No external result was
  treated as evidence; the repository baseline and recorded official-source
  fallbacks were used.

## Multi-Perspective Evaluation

- User/Product: the proposal addresses disconnect and mixed-data failures
  before adding scanner features.
- Developer: direct migration removes duplicate APIs but requires one careful
  caller migration checkpoint.
- Architect: byte transport, RTU, and future codecs remain separate.
- Security: no network, telemetry, payload logging, or new runtime dependency
  was accidentally reintroduced.
- Ops/SRE: deterministic tests and package gates match the real desktop release
  model.
- Maintainer: the design is smaller than the current broad facade, but the
  session result types must be documented clearly.

## Self-Interrogation

Challenge 1: Removing the facade may create a large one-time diff.
Response: that is intentional and bounded; keeping the facade would preserve
the very duplication the user rejected. The migration is split from behavior
hardening so failures are easy to locate.

Challenge 2: Serialized access may limit future throughput.
Response: serial diagnostics prioritize correctness; the byte/result contract
can later support a different backend without changing callers.

Challenge 3: Gray code is only a future requirement, not a complete design.
Response: the draft deliberately reserves an upper-layer codec/bit-layout
boundary and does not pretend to solve that feature in transport v2.

## Decision

The draft is internally consistent, deliberately small, and ready for detailed
planning. Remaining assumptions are driver-specific cancellation behavior and
the exact future scanner codec requirements; neither blocks the transport base.

Confidence: High.
