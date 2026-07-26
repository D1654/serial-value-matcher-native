#!/usr/bin/env python3
import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


PACKAGE_ARTIFACT = "SerialValueMatcherNative-win32-native-x64"
PACKAGE_ZIP = f"{PACKAGE_ARTIFACT}.zip"
PACKAGE_HASH = f"{PACKAGE_ZIP}.sha256.txt"
PACKAGE_SUMMARY = f"{PACKAGE_ARTIFACT}.package-summary.txt"
UI_ARTIFACT = "windows-native-ui-screenshots"
EXE_NAME = "svm-native-win32.exe"
TARGET_NAME = "svm-native-win32"
VERSION_SOURCE = "cmake/svm_version.cmake"
SERIAL_PTY_SCENARIOS = "normal,reopen,timeout,cancel,stress,close,reopen-generation-isolation"
SERIAL_PTY_SEPARATOR_PATTERN = r"\s*[,/、，]\s*"
OBSOLETE_SERIAL_PTY_PATTERN = re.compile(
    rf"normal{SERIAL_PTY_SEPARATOR_PATTERN}reopen"
    rf"{SERIAL_PTY_SEPARATOR_PATTERN}timeout"
    rf"{SERIAL_PTY_SEPARATOR_PATTERN}cancel"
    rf"{SERIAL_PTY_SEPARATOR_PATTERN}stress"
    rf"(?!{SERIAL_PTY_SEPARATOR_PATTERN}close"
    rf"{SERIAL_PTY_SEPARATOR_PATTERN}reopen-generation-isolation)",
    re.IGNORECASE,
)
OBSOLETE_SERIAL_PTY_CONTROL_TERMS = (
    "SVM_SERIAL_LOOPBACK_STALE_WAIT_MS",
    "SVM_NATIVE_SERIAL_LOOPBACK_STALE_WAIT_MS",
)

PACKAGE_WORKFLOW = ".github/workflows/windows-native-package.yml"
UI_WORKFLOW = ".github/workflows/windows-native-ui-capture.yml"

ACTIVE_DOCS = [
    "README.md",
    "docs/用户指南.md",
    "docs/开发者指南.md",
    "docs/Win32原生架构.md",
    "docs/测试与验证.md",
    "docs/发布产物.md",
    "docs/故障排查.md",
    "docs/Windows发布说明.md",
    "docs/Windows原生体积说明.md",
    "docs/Windows原生UI验证.md",
    "docs/Windows串口真机验收.md",
    "docs/Windows原生本地调试.md",
]

LEGACY_OR_TRANSITION_DOCS = []

REQUIRED_FILES = [
    VERSION_SOURCE,
    PACKAGE_WORKFLOW,
    UI_WORKFLOW,
    "scripts/package-windows-native.ps1",
    "scripts/package-windows-native-mingw.sh",
    "scripts/check-transport-boundaries.py",
    "scripts/inspect-windows-package.py",
    "scripts/inspect-windows-package.ps1",
    "tests/inspect_windows_package_tests.py",
    "scripts/capture-windows-native-ui.ps1",
    "scripts/capture-windows-native-ui-wine.sh",
    *ACTIVE_DOCS,
    *LEGACY_OR_TRANSITION_DOCS,
]

PACKAGE_WORKFLOW_TERMS = [
    "PACKAGE_NAME: SerialValueMatcherNative-win32-native-x64",
    "  push:\n    branches:\n      - main\n  pull_request:",
    "timeout-minutes: 20",
    "ctest --test-dir $env:BUILD_DIR --output-on-failure -C Release --no-tests=error",
    "--self-test",
    "--ui-perf-test",
    "native-self-test.log",
    "native-ui-perf-test.log",
    "native-ctest.log",
    "phase-2-backend-regression.txt",
    "serial-pty-matrix.txt",
    "serial-pty-matrix-summary.txt",
    "Assert native package evidence completeness",
    "Gate status: passed",
    "if-no-files-found: error",
    "id: upload-native-artifact",
    "artifact-id",
    "artifact-url",
    "artifact-digest",
    "GateStatus=documented-local-only",
    "Classification=local-only-release-candidate-evidence",
    "CiExecutesPtyMatrix=no",
    f"ExpectedScenarios={SERIAL_PTY_SCENARIOS}",
    f"LocalCommand=SVM_SERIAL_LOOPBACK_SCENARIOS={SERIAL_PTY_SCENARIOS}",
    "TransportV2Coverage=queue,exactly-once,typed-errors,generation,synthetic-faults",
    "serial_write_queue_tests",
    "serial_session_contract_tests",
    "native_modbus_transport_adapter_tests",
    "native_serial_io_state_tests",
    "native_reconnect_state_tests",
    "native_win32_serial_tests",
    "native_win32_serial_loopback_tests",
    "Zip sha256 file matches: yes",
    "Native exe present: yes",
    "Max zip bytes: 5242880",
    "Max extracted bytes: 8388608",
    "${{ env.PACKAGE_NAME }}.zip",
    "${{ env.PACKAGE_NAME }}.zip.sha256.txt",
    "${{ env.PACKAGE_NAME }}.package-summary.txt",
]

UI_WORKFLOW_TERMS = [
    "name: windows-native-ui-screenshots",
    "  push:\n    branches:\n      - main\n  pull_request:",
    "EVIDENCE_GITHUB_SHA: ${{ github.sha }}",
    "EVIDENCE_HEAD_SHA: ${{ github.event.pull_request.head.sha || github.sha }}",
    "timeout-minutes: 20",
    "ctest --test-dir $env:BUILD_DIR --output-on-failure -C Release --no-tests=error",
    ".\\scripts\\capture-windows-native-ui.ps1",
    "--ui-perf-test",
    "capture-status.txt",
    "self-test.log",
    "ui-perf-test.log",
    "ui-evidence-summary.txt",
    "window-info.txt",
    "artifacts/windows-native-ui/*.png",
    "Assert native UI evidence completeness",
    "phase-1-ui-regression-closure",
    "id: upload-ui-screenshots",
    "artifact-id",
    "artifact-url",
    "artifact-digest",
    "GateStatus=passed",
    "GitHubSha=$env:EVIDENCE_GITHUB_SHA",
    "GitHubHeadSha=$env:EVIDENCE_HEAD_SHA",
    "CheckedOutSha=$checkedOutSha",
    "Assert-FileHasExactLine",
    'Assert-FileHasExactLine $uiSummary "CheckedOutSha=$env:EVIDENCE_GITHUB_SHA"',
    '"scripts/run-windows-native-serial-pty-loopback.py"',
    '"src/win32/**"',
    '"src/transport/**"',
    '"tests/**"',
    '"CMakeLists.txt"',
]

CMAKE_TRANSPORT_TERMS = [
    "find_package(Python3",
    "COMPONENTS Interpreter",
    "src/transport/serial_write_queue.cpp",
    "src/win32/win32_serial_session.cpp",
    "add_test(NAME serial_write_queue_tests",
    "add_test(NAME serial_session_contract_tests",
    "add_test(NAME native_modbus_transport_adapter_tests",
    "add_test(NAME native_serial_io_state_tests",
    "add_test(NAME native_reconnect_state_tests",
    "add_test(NAME native_win32_serial_tests",
    "add_test(NAME native_win32_serial_loopback_tests",
    "NAME inspect_windows_package_tests",
    "SKIP_RETURN_CODE 77",
    "get_property(svm_all_tests DIRECTORY PROPERTY TESTS)",
    "PROPERTIES TIMEOUT 30",
    "PROPERTIES TIMEOUT 60",
    "TIMEOUT 120",
    "NAME transport_boundary_tests",
    "NAME transport_boundary_self_tests",
    "scripts/check-transport-boundaries.py",
    "TIMEOUT 10",
]

TRANSPORT_BOUNDARY_GATE_TERMS = [
    "--self-test",
    "Boundary self-test: passed.",
    "Transport boundary check: passed.",
    '"win32-dependency"',
    '"ui-dependency"',
    '"codec-dependency"',
    '"upper-layer-analysis"',
    '"storage-dependency"',
    '"old-facade"',
]

MINGW_PACKAGE_GATE_TERMS = [
    "set -euo pipefail",
    "SVM_SKIP_WINE_TEST:-0",
    "SVM_STRICT_WINE_TEST:-1",
    "require_binary_env SVM_SKIP_WINE_TEST",
    "require_binary_env SVM_STRICT_WINE_TEST",
    "Wine gate status:",
    "Wine gate strict:",
    '--max-zip-bytes "$max_zip_bytes"',
    '--max-extracted-bytes "$max_extracted_bytes"',
]

REQUIRED_DOC_TERMS = {
    "README.md": [EXE_NAME, PACKAGE_ZIP],
    "docs/用户指南.md": [EXE_NAME, PACKAGE_ZIP, "artifact"],
    "docs/开发者指南.md": [
        "CMake",
        TARGET_NAME,
        EXE_NAME,
        "NativeFrameScheduler",
        "transport v2 未实现 Gray-code decoding",
    ],
    "docs/Win32原生架构.md": [
        "NativeFrameScheduler",
        "NativeLayoutModel",
        "NativeLayoutTransaction",
        "NativePaintPolicy",
        "transport v2 **未实现 Gray-code decoding**",
    ],
    "docs/测试与验证.md": [
        PACKAGE_WORKFLOW,
        UI_WORKFLOW,
        UI_ARTIFACT,
        "--self-test",
        "--ui-perf-test",
        "native-ctest.log",
        "phase-2-backend-regression.txt",
        "ui-evidence-summary.txt",
        "GitHubSha",
        "GitHubHeadSha",
        "CheckedOutSha",
        "timeout-minutes: 20",
        "serial-pty-matrix-summary.txt",
        "if-no-files-found: error",
        "artifact-digest",
        "ui-perf ok",
        "local-only release-candidate evidence",
        "Unexpected DLL files",
        "Required package files",
        "Package documentation file set",
        "package",
        "docs consistency",
        "transport v2 未实现 Gray-code decoding",
        "inspect_windows_package_tests",
        "CRLF/LF",
    ],
    "docs/架构说明.md": [
        "SerialSession",
        "Win32SerialSession",
        "SerialRtuTransport",
        "scripts/check-transport-boundaries.py",
        "scanner/matcher codec",
    ],
    "docs/发布产物.md": [
        PACKAGE_ARTIFACT,
        UI_ARTIFACT,
        PACKAGE_HASH,
        PACKAGE_SUMMARY,
        "native-ctest.log",
        "phase-2-backend-regression.txt",
        "serial-pty-matrix-summary.txt",
        "ui-evidence-summary.txt",
        "GitHubSha",
        "GitHubHeadSha",
        "CheckedOutSha",
        "if-no-files-found: error",
        "artifact-digest",
        "Unexpected DLL files",
        "Required package files",
        "Package documentation file set",
        "回滚",
        "重新发布",
        "Gate status: passed",
    ],
    "docs/Windows原生UI验证.md": [
        "GitHubSha",
        "GitHubHeadSha",
        "CheckedOutSha",
        "main",
    ],
    "docs/Windows发布说明.md": [
        UI_WORKFLOW,
        "GitHubSha",
        "GitHubHeadSha",
        "CheckedOutSha",
        "三个 SHA 都与待发布提交完全一致",
    ],
    "docs/故障排查.md": [
        PACKAGE_ARTIFACT,
        UI_ARTIFACT,
        "troubleshooting",
        "package",
        "PTY",
        "serial-pty-matrix-summary.txt",
        "回滚",
        "诊断证据包",
    ],
}

PACKAGE_SUMMARY_TERMS = [
    "Version metadata",
    "Expected version",
    "Expected release tag",
    "Native exe fixed file version",
    "Native exe fixed product version",
    "Native exe file version",
    "Native exe product version",
    "Native exe product name",
    "Native exe original filename",
    "Zip sha256",
    "Zip sha256 file matches",
    "Native exe present",
    "Native exe sha256",
    "Imported DLLs",
    "Forbidden Qt/SQLite/.NET runtime files",
    "Unexpected DLL files",
    "Required package files",
    "Unicode text probe",
    "Package documentation links",
    "Package documentation file set",
    "Gate status: passed",
]

LINE_IGNORE_MARKERS = [
    "legacy",
    "历史",
    "旧文档",
    "旧路线",
    "旧 Qt",
    "过渡",
    "不应",
    "不得",
    "不是当前",
    "不是当前 release",
    "不属于当前",
    "阻止发布",
    "应阻止",
    "未实现",
    "未来",
    "future",
    "Qt-free",
]

NEGATION_MARKERS = [
    "不需要",
    "不要求",
    "不依赖",
    "不携带",
    "不包含",
    "不导入",
    "不能",
    "不是",
    "无",
]


@dataclass
class DocText:
    path: Path
    relative: str
    text: str
    active: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check Win32 native docs/artifact consistency.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--negative-smoke",
        action="store_true",
        help="Inject a virtual bad active claim and require the checker to fail.",
    )
    return parser.parse_args()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def read_cmake_version_metadata(root: Path) -> dict[str, str]:
    metadata: dict[str, str] = {}
    pattern = re.compile(r'^\s*set\(\s*([A-Za-z0-9_]+)\s+"([^"]*)"\s*\)')
    for line in read_text(root / VERSION_SOURCE).splitlines():
        match = pattern.match(line)
        if match:
            metadata[match.group(1)] = match.group(2)
    return metadata


def line_is_contextual(line: str) -> bool:
    lowered = line.lower()
    return any(marker.lower() in lowered for marker in LINE_IGNORE_MARKERS)


def line_is_negated(line: str) -> bool:
    return any(marker in line for marker in NEGATION_MARKERS)


def add_failure(failures: list[str], relative: str, message: str, line_no: int | None = None) -> None:
    if line_no is None:
        failures.append(f"{relative}: {message}")
    else:
        failures.append(f"{relative}:{line_no}: {message}")


def check_required_files(root: Path, failures: list[str]) -> None:
    for relative in REQUIRED_FILES:
        if not (root / relative).exists():
            add_failure(failures, relative, "required file is missing")


def check_terms(text: str, terms: list[str], relative: str, failures: list[str]) -> None:
    for term in terms:
        if term not in text:
            add_failure(failures, relative, f"missing required term: {term}")


def check_workflow_terms(root: Path, failures: list[str]) -> None:
    package_text = read_text(root / PACKAGE_WORKFLOW)
    ui_text = read_text(root / UI_WORKFLOW)
    check_terms(package_text, PACKAGE_WORKFLOW_TERMS, PACKAGE_WORKFLOW, failures)
    check_terms(ui_text, UI_WORKFLOW_TERMS, UI_WORKFLOW, failures)


def check_release_paths_fail_closed(root: Path, failures: list[str]) -> None:
    mingw_packager = read_text(root / "scripts/package-windows-native-mingw.sh")
    windows_packager = read_text(root / "scripts/package-windows-native.ps1")
    package_inspector = read_text(root / "scripts/inspect-windows-package.py")
    package_inspector_tests = read_text(root / "tests/inspect_windows_package_tests.py")
    ui_capture = read_text(root / "scripts/capture-windows-native-ui.ps1")
    wine_ui_capture = read_text(root / "scripts/capture-windows-native-ui-wine.sh")
    package_workflow = read_text(root / PACKAGE_WORKFLOW)
    ui_workflow = read_text(root / UI_WORKFLOW)

    check_terms(
        mingw_packager,
        [
            'require_package_name "$package_name"',
            '^[A-Za-z0-9]([A-Za-z0-9._-]*[A-Za-z0-9])?$',
            'package_root="$(cd "$package_root" && pwd -P)"',
            'rm -rf -- "$stage_dir"',
        ],
        "scripts/package-windows-native-mingw.sh",
        failures,
    )
    check_terms(
        windows_packager,
        [
            '$cmakePath = Require-Command "cmake"',
            "$buildExitCode = $LASTEXITCODE",
            "if ($buildExitCode -ne 0)",
            "Test-Path -LiteralPath $exePath -PathType Leaf",
        ],
        "scripts/package-windows-native.ps1",
        failures,
    )
    if '(Join-Path $buildPath "svm-native-win32.exe")' in windows_packager:
        add_failure(
            failures,
            "scripts/package-windows-native.ps1",
            "standalone packager retains the stale build-root executable fallback",
        )

    check_terms(
        package_inspector,
        [
            "NORMALIZED_DOCUMENT_SUFFIXES",
            "normalized_document_text",
            'errors="backslashreplace"',
            "write_summary(summary_path, summary)",
        ],
        "scripts/inspect-windows-package.py",
        failures,
    )
    check_terms(
        package_inspector_tests,
        [
            "zipfile.ZipFile",
            "docs/设备说明.md",
            "utf8_safe_text",
            "Package documentation file set:\\n  passed",
        ],
        "tests/inspect_windows_package_tests.py",
        failures,
    )

    check_terms(
        ui_capture,
        [
            "$preservedLogNames = @()",
            'Test-LogHasExactLine -Path $selfTestLog -Expected "ok"',
            'source=current-run log=self-test.log',
        ],
        "scripts/capture-windows-native-ui.ps1",
        failures,
    )
    if "preexisting-log=self-test.log" in ui_capture:
        add_failure(
            failures,
            "scripts/capture-windows-native-ui.ps1",
            "UI capture still accepts a self-test log from an earlier executable",
        )
    check_terms(
        wine_ui_capture,
        [
            "trap finalize_ui_evidence EXIT",
            "local exit_code=$?",
            'add_capture_status "capture" "FAIL" "exit-code=$exit_code"',
            'grep -Fxq "GateStatus=passed"',
            'exit "$exit_code"',
        ],
        "scripts/capture-windows-native-ui-wine.sh",
        failures,
    )
    cleanup_index = wine_ui_capture.find("rm -f --")
    status_init_index = wine_ui_capture.find('\n: >"$output_dir/capture-status.txt"\n')
    cleanup_block = (
        wine_ui_capture[cleanup_index:status_init_index]
        if 0 <= cleanup_index < status_init_index
        else ""
    )
    if '"$output_dir"/ui-evidence-summary.txt' not in cleanup_block:
        add_failure(
            failures,
            "scripts/capture-windows-native-ui-wine.sh",
            "startup cleanup does not remove the previous UI evidence summary",
        )
    trap_index = wine_ui_capture.find("trap finalize_ui_evidence EXIT")
    prerequisite_index = wine_ui_capture.find("\nrequire_command wine\n")
    if trap_index < 0 or prerequisite_index < 0 or trap_index > prerequisite_index:
        add_failure(
            failures,
            "scripts/capture-windows-native-ui-wine.sh",
            "failure-summary trap is not installed before prerequisite checks",
        )
    if wine_ui_capture.count("write_ui_evidence_summary") != 2:
        add_failure(
            failures,
            "scripts/capture-windows-native-ui-wine.sh",
            "UI evidence summary must be written only by the EXIT finalizer",
        )

    main_push_block = "  push:\n    branches:\n      - main\n  pull_request:"
    if main_push_block not in ui_workflow:
        add_failure(
            failures,
            UI_WORKFLOW,
            "UI evidence is not generated for every main-branch push",
        )
    check_terms(
        ui_workflow,
        [
            "$checkedOutSha = (& git rev-parse HEAD).Trim()",
            "if ($checkedOutSha -cne $env:EVIDENCE_GITHUB_SHA)",
            'Assert-FileHasExactLine $uiSummary "GitHubSha=$env:EVIDENCE_GITHUB_SHA"',
            'Assert-FileHasExactLine $uiSummary "GitHubHeadSha=$env:EVIDENCE_HEAD_SHA"',
            'Assert-FileHasExactLine $uiSummary "CheckedOutSha=$env:EVIDENCE_GITHUB_SHA"',
        ],
        UI_WORKFLOW,
        failures,
    )
    if "pty-faults" in package_workflow:
        add_failure(
            failures,
            PACKAGE_WORKFLOW,
            "hosted coverage still claims PTY faults that are not executed in CI",
        )


def check_transport_release_gate_terms(root: Path, failures: list[str]) -> None:
    check_terms(read_text(root / "CMakeLists.txt"), CMAKE_TRANSPORT_TERMS, "CMakeLists.txt", failures)
    check_terms(
        read_text(root / "scripts/check-transport-boundaries.py"),
        TRANSPORT_BOUNDARY_GATE_TERMS,
        "scripts/check-transport-boundaries.py",
        failures,
    )
    check_terms(
        read_text(root / "scripts/package-windows-native-mingw.sh"),
        MINGW_PACKAGE_GATE_TERMS,
        "scripts/package-windows-native-mingw.sh",
        failures,
    )


def check_doc_required_terms(root: Path, failures: list[str]) -> None:
    for relative, terms in REQUIRED_DOC_TERMS.items():
        check_terms(read_text(root / relative), terms, relative, failures)


def check_pty_matrix_docs(root: Path, failures: list[str]) -> None:
    implementation_requirements = {
        "scripts/run-windows-native-serial-pty-loopback.py": [
            '"reopen-generation-isolation"',
            "SVM_SERIAL_LOOPBACK_GENERATION_ISOLATION_WAIT_MS",
            "SVM_NATIVE_SERIAL_LOOPBACK_GENERATION_ISOLATION_WAIT_MS",
        ],
        "tests/native_win32_serial_loopback_tests.cpp": [
            'scenario != "reopen-generation-isolation"',
            'scenario == "reopen-generation-isolation"',
            "SVM_NATIVE_SERIAL_LOOPBACK_GENERATION_ISOLATION_WAIT_MS",
        ],
    }
    sources = [
        PACKAGE_WORKFLOW,
        *implementation_requirements,
        *ACTIVE_DOCS,
    ]
    for relative in sources:
        text = read_text(root / relative)
        check_terms(text, implementation_requirements.get(relative, []), relative, failures)
        if relative in implementation_requirements and '"stale"' in text:
            add_failure(failures, relative, "retains the obsolete stale PTY scenario alias")
        for line_no, line in enumerate(text.splitlines(), start=1):
            if OBSOLETE_SERIAL_PTY_PATTERN.search(line):
                add_failure(failures, relative, "references an obsolete PTY scenario matrix", line_no)
        for term in OBSOLETE_SERIAL_PTY_CONTROL_TERMS:
            if term in text:
                add_failure(failures, relative, f"references obsolete PTY control: {term}")


def load_docs(root: Path, negative_smoke: bool) -> list[DocText]:
    docs: list[DocText] = []
    for relative in ACTIVE_DOCS:
        path = root / relative
        if path.exists():
            docs.append(DocText(path=path, relative=relative, text=read_text(path), active=True))
    for relative in LEGACY_OR_TRANSITION_DOCS:
        path = root / relative
        if path.exists():
            docs.append(DocText(path=path, relative=relative, text=read_text(path), active=False))
    if negative_smoke:
        docs.append(
            DocText(
                path=root / "__virtual_negative_doc__.md",
                relative="__virtual_negative_doc__.md",
                text="当前主程序是 svm-native.exe，当前包是 SerialValueMatcherNative-win-x64.zip。\n",
                active=True,
            )
        )
    return docs


def check_active_contradictions(docs: list[DocText], failures: list[str], warnings: list[str]) -> None:
    old_package = re.compile(r"SerialValueMatcherNative-win-x64(?:\.zip)?")
    current_old_exe = re.compile(r"(?:当前|主程序|正式|发布|release).{0,24}svm-native\.exe", re.IGNORECASE)
    qt_requirement = re.compile(r"(?:需要|要求|依赖|安装|携带).{0,12}(?:Qt|\.NET|C#|windeployqt)", re.IGNORECASE)
    current_qt_release = re.compile(r"(?:当前|正式|主).{0,16}(?:Qt Widgets|windeployqt|Qt 打包)", re.IGNORECASE)

    for doc in docs:
        for line_no, line in enumerate(doc.text.splitlines(), start=1):
            if not doc.active:
                if old_package.search(line) or "svm-native.exe" in line:
                    warnings.append(f"{doc.relative}:{line_no}: legacy/transition reference ignored")
                continue
            if line_is_contextual(line):
                continue
            if old_package.search(line):
                add_failure(failures, doc.relative, "active doc references old Qt package name", line_no)
            if current_old_exe.search(line):
                add_failure(failures, doc.relative, "active doc describes svm-native.exe as current executable", line_no)
            if qt_requirement.search(line) and not line_is_negated(line):
                add_failure(failures, doc.relative, "active doc implies Qt/.NET/windeployqt is required", line_no)
            if current_qt_release.search(line) and not line_is_negated(line):
                add_failure(failures, doc.relative, "active doc implies Qt route is current release route", line_no)


def check_package_summary_terms(root: Path, failures: list[str]) -> None:
    inspect_sources = [
        ("scripts/inspect-windows-package.py", read_text(root / "scripts/inspect-windows-package.py")),
        ("scripts/inspect-windows-package.ps1", read_text(root / "scripts/inspect-windows-package.ps1")),
    ]
    docs_text = "\n".join(read_text(root / relative) for relative in ACTIVE_DOCS if (root / relative).exists())
    for term in PACKAGE_SUMMARY_TERMS:
        if term not in docs_text:
            add_failure(failures, "docs", f"active docs do not describe package summary term: {term}")
        for relative, text in inspect_sources:
            if term not in text:
                add_failure(failures, relative, f"inspector missing package summary term: {term}")


def check_markdown_links(root: Path, docs: list[DocText], failures: list[str]) -> None:
    link_pattern = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
    for doc in docs:
        for line_no, line in enumerate(doc.text.splitlines(), start=1):
            for match in link_pattern.finditer(line):
                target = match.group(1).strip()
                if not target or target.startswith(("http://", "https://", "mailto:")):
                    continue
                target = target.split("#", 1)[0]
                if not target:
                    continue
                if target.startswith("<") and target.endswith(">"):
                    target = target[1:-1]
                resolved = (doc.path.parent / target).resolve()
                try:
                    resolved.relative_to(root.resolve())
                except ValueError:
                    add_failure(failures, doc.relative, f"markdown link leaves repository: {target}", line_no)
                    continue
                if not resolved.exists():
                    add_failure(failures, doc.relative, f"broken markdown link: {target}", line_no)


def check_cross_file_consistency(root: Path, failures: list[str]) -> None:
    package_script = read_text(root / "scripts/package-windows-native.ps1")
    mingw_package_script = read_text(root / "scripts/package-windows-native-mingw.sh")
    package_workflow = read_text(root / PACKAGE_WORKFLOW)
    release_doc = read_text(root / "docs/发布产物.md")

    for relative, text in [
        ("scripts/package-windows-native.ps1", package_script),
        ("scripts/package-windows-native-mingw.sh", mingw_package_script),
        (PACKAGE_WORKFLOW, package_workflow),
        ("docs/发布产物.md", release_doc),
    ]:
        if PACKAGE_ARTIFACT not in text and "mingw" not in relative:
            add_failure(failures, relative, f"missing package artifact name: {PACKAGE_ARTIFACT}")
        if EXE_NAME not in text:
            add_failure(failures, relative, f"missing executable name: {EXE_NAME}")


def check_version_metadata_source(root: Path, failures: list[str]) -> None:
    metadata = read_cmake_version_metadata(root)
    required_keys = [
        "SVM_VERSION",
        "SVM_VERSION_MAJOR",
        "SVM_VERSION_MINOR",
        "SVM_VERSION_PATCH",
        "SVM_VERSION_TWEAK",
        "SVM_RELEASE_TAG",
        "SVM_PACKAGE_ARTIFACT",
        "SVM_MINGW_PACKAGE_ARTIFACT",
        "SVM_WIN32_EXE_NAME",
        "SVM_RELEASE_URL",
    ]
    for key in required_keys:
        if not metadata.get(key):
            add_failure(failures, VERSION_SOURCE, f"missing version metadata key: {key}")

    if any(not metadata.get(key) for key in required_keys):
        return

    component_version = ".".join([
        metadata["SVM_VERSION_MAJOR"],
        metadata["SVM_VERSION_MINOR"],
        metadata["SVM_VERSION_PATCH"],
    ])
    if metadata["SVM_VERSION"] != component_version:
        add_failure(failures, VERSION_SOURCE, "SVM_VERSION does not match major/minor/patch components")
    if metadata["SVM_VERSION_TWEAK"] != "0":
        add_failure(failures, VERSION_SOURCE, "SVM_VERSION_TWEAK should be 0 for the current release line")
    if metadata["SVM_RELEASE_TAG"] != f"v{metadata['SVM_VERSION']}":
        add_failure(failures, VERSION_SOURCE, "SVM_RELEASE_TAG must be v + SVM_VERSION")
    if metadata["SVM_PACKAGE_ARTIFACT"] != PACKAGE_ARTIFACT:
        add_failure(failures, VERSION_SOURCE, f"SVM_PACKAGE_ARTIFACT drifted from docs constant {PACKAGE_ARTIFACT}")
    if metadata["SVM_WIN32_EXE_NAME"] != EXE_NAME:
        add_failure(failures, VERSION_SOURCE, f"SVM_WIN32_EXE_NAME drifted from docs constant {EXE_NAME}")

    cmake = read_text(root / "CMakeLists.txt")
    if VERSION_SOURCE not in cmake or 'VERSION "${SVM_VERSION}"' not in cmake:
        add_failure(failures, "CMakeLists.txt", "top-level project version must consume cmake/svm_version.cmake")
    app_rc = read_text(root / "src/win32/app.rc")
    if "svm_version_resource.h" not in app_rc or "SVM_VERSION_FILE_VERSION" not in app_rc:
        add_failure(failures, "src/win32/app.rc", "VERSIONINFO must consume generated version resource header")
    readme = read_text(root / "README.md")
    if metadata["SVM_RELEASE_TAG"] not in readme or metadata["SVM_RELEASE_URL"] not in readme:
        add_failure(failures, "README.md", "README release tag/url must match version metadata source")


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    failures: list[str] = []
    warnings: list[str] = []

    check_required_files(root, failures)
    if failures:
        for failure in failures:
            print(f"docs consistency failure: {failure}", file=sys.stderr)
        return 1

    docs = load_docs(root, args.negative_smoke)
    check_workflow_terms(root, failures)
    check_release_paths_fail_closed(root, failures)
    check_transport_release_gate_terms(root, failures)
    check_doc_required_terms(root, failures)
    check_pty_matrix_docs(root, failures)
    check_active_contradictions(docs, failures, warnings)
    check_package_summary_terms(root, failures)
    check_markdown_links(root, docs, failures)
    check_cross_file_consistency(root, failures)
    check_version_metadata_source(root, failures)

    if args.negative_smoke and not failures:
        add_failure(failures, "__virtual_negative_doc__.md", "negative smoke did not fail")

    if failures:
        for failure in failures:
            print(f"docs consistency failure: {failure}", file=sys.stderr)
        return 1

    print("docs consistency ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
