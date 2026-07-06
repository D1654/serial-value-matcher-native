# BS-3 — Tech Stack Selection

Generated: 2026-07-06T08:28:19+08:00
Mode: Layer 1 — Context-Enriched Self-Reflection

## Research Findings

Search/Open: Microsoft Win32 communications and asynchronous I/O docs

- Windows serial communication is still grounded in Win32 handles, communication resources, `DCB`, `COMMTIMEOUTS`, `ReadFile` / `WriteFile`, and overlapped I/O.
- Microsoft documents that synchronous I/O blocks the calling thread until completion, which supports keeping serial work off the UI thread and reserving the UI thread for message dispatch and painting.
- Sources: https://learn.microsoft.com/en-us/windows/win32/devio/communications-resources, https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o

Search/Open: CMake / CTest / GoogleTest official docs

- CMake's `add_test` / CTest model keeps verification as first-class build graph behavior.
- GoogleTest's CMake quickstart supports separate test executables and `gtest_discover_tests`, which can remain CI/development-only instead of entering release artifacts.
- Sources: https://cmake.org/cmake/help/latest/command/add_test.html, https://google.github.io/googletest/quickstart-cmake.html

Search/Open: vcpkg and SQLite official docs

- vcpkg manifest mode improves dependency reproducibility but adds a dependency-management surface that is not justified without real third-party runtime dependencies.
- SQLite is appropriate when structured query, indexing, and durable local data relationships are needed, and it has a small footprint; however, adding any database should still be justified by actual product need and migration cost.
- Sources: https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode, https://learn.microsoft.com/en-us/vcpkg/users/versioning, https://www.sqlite.org/whentouse.html, https://www.sqlite.org/footprint.html

DeepWiki: `qt/qtserialport`

- Qt SerialPort provides useful behavioral reference points for enumeration, serial configuration, error modeling, blocking waits, and Windows mapping to `CreateFile`, `DCB`, `COMMTIMEOUTS`, and overlapped I/O.
- It depends on Qt's event loop and abstraction model, so it should remain a legacy/reference baseline rather than a native release runtime dependency.
- Result: https://deepwiki.com/search/what-are-the-main-capabilities_251d1c0a-e098-47c4-afe5-1ea6c91a61e1

DeepWiki: `google/googletest`

- GoogleTest supports modern C++ unit testing, CMake integration, fixtures, parameterized tests, and separate test executables.
- Risks are dependency pinning, Windows CRT mismatch, and test dependency governance; it is valuable as a development/CI tool, not as a release artifact dependency.
- Result: https://deepwiki.com/search/what-are-the-main-capabilities_66a48755-6991-4d97-a1b7-614d2a147431

DeepWiki: `microsoft/vcpkg`

- vcpkg manifest mode gives direct/transitive dependency declaration, version baselines, optional features, and reproducible dependency governance.
- It also adds manifest/triplet/configuration complexity and automatic dependency-copy behavior; for this project it should be deferred until real third-party dependencies justify it.
- Result: https://deepwiki.com/search/what-are-the-main-capabilities_2ab521ee-1225-4da6-8536-cc1d35fdfe1e

DeepWiki: `Kitware/CMake`

- CMake / CTest can keep test tools and optional dependencies out of release artifacts through target/install boundaries and runtime dependency filtering.
- CMake can support package metadata, generated version gates, and size/dependency governance through target-centric build scripts and CI checks.
- Result: https://deepwiki.com/search/what-cmake-and-ctest-capabilit_f0891087-2ee2-499f-8b2d-4ef8dc6a16a4

DeepWiki: `microsoft/Windows-classic-samples`

- Windows native samples reinforce explicit message handling, resize/paint/DPI separation, and background work reporting back to the UI through posted messages.
- This supports staying with a small Win32 native app rather than introducing a heavier desktop framework.
- Result: https://deepwiki.com/search/what-windows-native-sample-pat_c219ca10-0085-4c21-be58-dff1c48589cd

## Multi-Perspective Evaluation

Evaluating: conservative native stack — C++20 + Win32 API + CMake/CTest + GitHub Actions/Wine, with Qt/GoogleTest/vcpkg/SQLite behind explicit gates.

👤 User/Product: The stack preserves the user's experienced release quality: small, fast-opening Windows exe with Chinese-first local workflows. It avoids converting the tool into a heavy dashboard or framework application before new features prove that need.

💻 Developer: C++20 + Win32 keeps the existing codebase coherent, while CMake/CTest and optional GoogleTest give a path to stronger tests for pure logic, layout model, parser, storage transaction, and fake transport code. The main developer risk is that raw Win32 can become unreadable unless controller/service boundaries are enforced.

🏗️ Architect: The stack aligns with BS-2 ports/adapters: serial transport now, TCP transport later by decision gate; file storage now, SQLite later by decision gate. vcpkg is useful only after dependency count grows enough to require governance.

🔒 Security: No cloud, no telemetry, no account system, and no network runtime in this phase keep the attack surface narrow. TCP-capable abstractions must remain compile-time architecture boundaries only, with no TCP UI or listener/client behavior shipped in this phase.

⚙️ Ops/SRE: GitHub Actions + CMake/CTest + Wine/Xvfb + package audits provide a realistic local/CI acceptance loop for a Windows native executable. Release gates should measure exe/zip size, dependency list, version metadata, tests, UI perf, and serial simulation.

🔮 Future Maintainer: Deferring Qt runtime, vcpkg, SQLite, and broad plugin systems keeps the mental model small. The maintainability risk moves to discipline: if new code bypasses tests and controllers, the small stack alone will not prevent another god-object.

## Self-Interrogation

Initial recommendation: Keep C++20 + Win32 native as the production stack; use CMake/CTest as mandatory verification infrastructure; keep GoogleTest, vcpkg, SQLite, and TCP runtime behind explicit decision gates.

❓ Challenge 1: If raw Win32 remains the UI foundation, then future UI work may keep producing flicker/layout bugs because Win32 gives low-level primitives instead of modern retained-mode layout.
💬 Response: This is exactly why Section 2 makes `NativeLayoutModel` production-facing and pushes feature state into controllers. Replacing the UI framework would be a larger risk to package size, release stability, and existing user-validated behavior.
📊 Verdict: Recommendation holds with a strict layout-model productionization requirement.

❓ Challenge 2: If GoogleTest is deferred too much, then deeper storage/parser/transport tests may remain weak, because ad hoc asserts scale poorly.
💬 Response: GoogleTest should not be a runtime dependency, but it can be introduced as a CI/development-only test framework when Phase 3 tasks need richer tests. The gate is not "never use it"; it is "do not put it in release or add it without a test-value reason."
📊 Verdict: Recommendation holds, with GoogleTest allowed as dev/CI-only if justified by test scope.

❓ Challenge 3: If future multi-protocol and evidence queries grow, then file storage may become too limited and SQLite should have been adopted earlier.
💬 Response: The storage interface should be narrowed and schema-versioned now, with transaction/orphan recovery. SQLite should be introduced only after indexed cross-session queries or relational evidence models become real requirements; adding it now would solve a hypothetical problem while increasing migration and package governance work.
📊 Verdict: Recommendation holds with SQLite decision gate preserved.

## Decision

✅ Decision: Use the following tech-stack policy for the roadmap:

1. Production language/runtime: C++20 + Win32 API.
2. Build/verification: CMake + CTest + GitHub Actions + Wine/Xvfb verification scripts.
3. Serial implementation: Win32 communications APIs directly; Qt SerialPort remains behavioral/reference material only.
4. Testing: current CTest/assert tests remain; GoogleTest may be introduced only as development/CI test infrastructure, never as release runtime.
5. Dependency governance: no vcpkg release chain in this phase; add it only if real third-party dependencies make reproducibility harder than the governance overhead.
6. Storage: compatible native file storage now; narrow `SessionStore`, transaction/recovery, schema version; SQLite remains a future backend decision gate.
7. Transport expansion: no TCP runtime/UI in this phase; reserve transport/protocol interfaces for future TCP or multi-protocol work.
8. Size governance: 10MB is an absolute red line, not a target. Current small-size baseline remains the standard; each new dependency must prove necessity, size delta, and architecture benefit.

🎯 Confidence: High

📚 Key evidence:

- Win32/QtSerialPort evidence confirms direct Win32 serial APIs are the correct native implementation layer while Qt remains useful only as behavioral reference.
- CMake/CTest/GoogleTest evidence supports stronger testing without pulling test infrastructure into the release package.

⚠️ Open risks:

- Raw Win32 remains easy to misuse unless layout/model/controller boundaries become mandatory in Phase 3 tasks.
- Deferring SQLite is correct now, but storage interface work must be done before evidence packages and command sequences grow.

❓ Need to verify with user: None at this gate; this matches Q7/Q9 and the user's 10MB-as-red-line clarification.
