# BS-3: Technology Selection

Completed: 2026-07-12T07:45:00+08:00

## Research Findings

- Direct Win32 APIs remain necessary for serial configuration and I/O.
- WIL improves generic handle RAII and error plumbing, but it does not solve
  serial semantics and would add a dependency to the release path.
- Boost.Asio supplies async serial I/O and cancellation, but its executor/IOCP
  model is broader than this small tool needs and would increase build and
  package complexity.
- WebSearch again returned a decode error; official-source fallback and
  DeepWiki checks were used.

## Multi-Perspective Evaluation

- User/Product: keeping the current native stack reduces surprise and keeps
  effort on serial reliability rather than dependency integration.
- Developer: C++20 standard-library threads, condition variables, containers,
  and RAII are enough for the first session owner.
- Architect: direct Win32 keeps the serial-specific boundary explicit and leaves
  a clean future point for overlapped I/O without adopting a framework now.
- Security: fewer runtime components mean fewer package and supply-chain gates.
- Ops/SRE: the existing CMake/CTest/MinGW/Wine pipeline remains valid.
- Maintainer: no framework-specific executor or error vocabulary has to be
  taught to future contributors.

## Self-Interrogation

Initial recommendation: C++20 standard library plus direct Win32, with a small
RAII handle owner inside `SerialSession`; no Boost.Asio or WIL.

Challenge 1: Hand-written Win32 ownership can leak or double-close a handle.
Response: constrain ownership to one class, use move-only standard C++ RAII,
and add close/reopen tests; WIL would not remove the serial lifecycle design.

Challenge 2: A later move to overlapped I/O might make the first design costly.
Response: keep operation requests/results independent from the blocking call;
only the session backend changes later.

Challenge 3: MinGW support could expose API or library differences.
Response: use APIs already exercised by the current native build and keep the
existing Wine/package gates mandatory.

## Decision

Stay with C++20, direct Win32 serial APIs, the standard library, CMake/CTest,
and the existing MinGW/Wine pipeline. Do not add Boost.Asio, WIL, or another
runtime dependency for this transport pass.

Confidence: High.
