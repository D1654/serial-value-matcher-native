# DeepWiki Research Cache — Phase 3: Extension Capability & Production Hardening
Generated: 2026-07-09T14:20:36+08:00

## Repo Map

- CMake / CTest -> `Kitware/CMake`
- GitHub Actions artifact flow -> `actions/upload-artifact`
- Win32 resource metadata -> `microsoft/Windows-classic-samples`

## Kitware/CMake

### Structure

- Overview
- Core Architecture
- Key Components, including Qt Integration
- Testing Framework, including CTest Architecture and Test Infrastructure
- Development Infrastructure, including CI/CD Pipeline

### Research

**Q:** Core CMake and CTest practices relevant to Phase 3 production hardening for a C++20 Win32 native project: version metadata, build/package gates, focused tests, and release evidence.

**A:** Use `project(... VERSION ...)` and package/build metadata as the source for release identity. Keep focused regression commands explicit through `ctest -R`, labels, or test lists. Preserve package and test evidence as CI artifacts. CMake's own repository examples are project-internal, so project-specific release gates still need to be mapped to this repository's CMake targets, scripts, and workflows.

## actions/upload-artifact

### Structure

- Overview
- Upload Artifact Action, including Usage and Configuration and File Selection
- Merge Artifact Action
- Migration from v3 to v4
- Development

### Research

**Q:** Core upload-artifact practices for preserving Windows native release evidence: explicit paths, if-no-files-found behavior, retention, immutable artifacts, unique names, and digest outputs.

**A:** Use explicit artifact paths for release evidence and `if-no-files-found: error` for required files. `retention-days` governs artifact lifetime. v4 artifacts are immutable; matrix or repeated uploads need unique names unless `overwrite` is intentionally used. The action exposes `artifact-id`, `artifact-url`, and `artifact-digest`; the digest can support release evidence integrity checks. Hidden files are excluded by default unless explicitly enabled.

## microsoft/Windows-classic-samples

### Structure

- Windows Classic Samples Overview
- UI and Shell Integration, including DPI Awareness
- Device and Hardware APIs
- System Management and Administration
- Win32 Fundamentals

### Research

**Q:** Win32 native application practices relevant to VERSIONINFO/resource metadata and local diagnostic evidence without adding heavy runtime dependencies.

**A:** Standard Win32 native applications embed `VERSIONINFO` blocks in `.rc` files. `FILEVERSION`, `PRODUCTVERSION`, `StringFileInfo`, and `VarFileInfo` provide static binary metadata visible in Windows file properties and diagnostic tooling without runtime dependencies. The pattern supports Phase 3 version metadata hardening while preserving the lightweight native package.

## Phase 3 Implications

- Do not add runtime dependencies for production hardening tasks unless a task explicitly proves necessity and package delta.
- Keep focused tests and package evidence as stable, reproducible commands and artifact paths.
- Use existing Win32 resource and CMake mechanisms for version/release metadata before adding tooling.
