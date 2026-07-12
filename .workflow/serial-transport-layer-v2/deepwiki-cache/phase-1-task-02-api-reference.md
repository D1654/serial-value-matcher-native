# Phase 1 Task 02 API Reference

## Query Decision

No external library, framework, or runtime API applies to this task. The
dependency table lists only the neutral capability declared by the project in
`src/transport/serial_session.h`, so no DeepWiki or external API query was
required.

## Internal Migration Guidance

- Replace the command execution context's `SerialWritePort*` dependency with
  the narrow session write-scheduler capability. Do not retain an adapter,
  forwarding facade, or compatibility path for the old write-port boundary.
- Preserve existing serial-command validation, safety limits, timeout input,
  default timeout behavior, missing-backend handling, and assertion behavior.
- Preserve the command sequence's current completion meaning: successful queue
  admission completes the command step. This task must not change the step to
  wait for physical transmission or for the request's terminal I/O result.
- Keep the last admission result available to `LastSerialWriteAccepted`
  assertions and evidence generation, but base decisions on typed fields rather
  than localized or backend-generated message text.
- Carry structured admission evidence through the command path: request ID,
  session generation, typed admission status/category, byte count, and deadline
  information. These fields allow later evidence to identify the accepted
  request and distinguish it from stale work after reconnect.
- Treat admission rejection as a typed outcome from the scheduler. Do not infer
  queue-full, closed-session, timeout, or other categories by parsing strings.
- Update tests to use a deterministic fake narrow scheduler and cover accepted,
  missing-backend, unsafe-input, and assertion paths without depending on a
  concrete queue implementation.
- Remove all `SerialWritePort` references from the command-sequence header,
  implementation, and focused tests after migration.

## Confidence

High. The task has no external dependency ambiguity, and the approved domain
knowledge and task plan define both the new capability boundary and the
behavior that must remain unchanged.
