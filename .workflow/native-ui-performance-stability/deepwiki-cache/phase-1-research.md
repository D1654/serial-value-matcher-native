# DeepWiki Research Cache — Phase 1: Fact Baseline And Documentation Audit

Generated: 2026-06-30T02:40:36+08:00

## Repo Map

- CMake -> `Kitware/CMake`
- GitHub Actions runner -> `actions/runner`

## Kitware/CMake

### Structure

DeepWiki returned these relevant pages:

- Overview
- Core Architecture
  - Bootstrap and Initialization
  - Build System Architecture
  - Generators
  - Policies and Versioning
- Key Components
  - Find Package System
  - Generator Expressions
  - Compiler Detection
  - Qt Integration
  - External Projects
- Testing Framework
  - CTest Architecture
  - Test Infrastructure
- Command-line Tools
- CI/CD Pipeline

### Research

**Q:** Core APIs and best practices for inventorying CMake build targets and CTest tests from `CMakeLists.txt` for a native desktop application.

**A:** DeepWiki identified `add_executable()`, `add_library()`, and `add_custom_target()` as the main target definitions to inventory, and `add_test()` plus `enable_testing()` as the main CTest signals. It also called out `set_tests_properties()` for test metadata, `BUILD_TESTING`/CTest module behavior, and direct `ctest(1)` usage for test execution and inventory. For this project, Phase 1 inventory should therefore classify executable/library/custom targets separately from CTest registrations and should not infer runtime coverage from source files alone.

## actions/runner

### Structure

DeepWiki returned these relevant pages:

- Overview
- Architecture
  - Runner Listener and Message Processing
  - Job Execution System
  - Action Management
  - Execution Context and Step Processing
  - Action Handlers
- Command Systems
- Runner Lifecycle
- Build and Deployment

### Research

**Q:** Core GitHub Actions workflow job, step, artifact, and environment practices relevant to auditing CI gates for a Windows native desktop application.

**A:** DeepWiki identified workflow YAML jobs, steps, `runs-on`, matrix entries, `run` commands, `uses` actions, artifact upload/download, environment variables, and secret masking as the relevant audit surface. For Windows native desktop gates, Phase 1 should classify workflows by actual Windows job behavior, artifact publication, package/hash evidence, UI capture/stress gates, and forbidden runtime checks, not merely by workflow filename.

## Phase 1 Application

The source inventory must treat current source, CMake targets, CTest registrations, workflow jobs, scripts, and artifacts as the authoritative evidence base. README and docs remain historical context until separately audited.

