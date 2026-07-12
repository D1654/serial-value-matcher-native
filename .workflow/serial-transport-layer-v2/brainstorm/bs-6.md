# BS-6: Task Decomposition Review

Completed: 2026-07-12T14:22:42+08:00

## Research Findings

WebSearch failed twice with a response-decode error, including a broader retry.
The following decomposition review is therefore labeled **AI inference**, based
on the approved Context Bus, current repository inventory, CMake target graph,
and the verified test/release entry points.

## Multi-Perspective Evaluation

- Developer: the original main-window migration task touched too many tightly
  coupled files, and moving RTU to the new capability before the production
  backend implemented it risked an intermediate build break. Split UI lifecycle
  from UI I/O, and move RTU migration after backend contract adoption.
- Architect: contracts, pure queue behavior, and fake session tests should finish
  before the Win32 owner changes. Facade deletion must remain the final migration
  task so all consumers have already moved.
- Delivery: MinGW task verification must build the entire cross CMake tree before
  CTest because the convenience script builds only the application target.
- Maintainer: keep all narrow capability declarations in one `serial_session.h`
  to avoid an unnecessary family of tiny abstraction files.

## Self-Interrogation

Question: Would moving RTU migration from Phase 1 to Phase 2 improve ordering?
Answer: Yes. The RTU adapter needs a production byte capability and should move
after the Win32 session adopts the new contract.

Question: Which tasks are likely bottlenecks?
Answer: Win32 generation/settlement, main-window I/O migration, and exactly-once
close/cancel behavior. Each receives its own verification checkpoint.

Question: Which implicit dependencies were missing?
Answer: UI pending-write matching needs generation before caller migration;
facade deletion needs UI, Modbus, RTU, and command migration; cross CTest needs
all MinGW test targets built before execution.

## Decision

Use 18 tasks instead of 17. Keep Phase 1 at four tasks, split Phase 2 into six
tasks, retain five production-hardening tasks, and retain three closure tasks.
Replace the early RTU migration task with a fake session contract suite, split
main-window lifecycle from main-window I/O, and migrate RTU/Modbus immediately
before final facade removal.

Confidence: High.
