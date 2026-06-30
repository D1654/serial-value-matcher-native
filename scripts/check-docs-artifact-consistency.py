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

PACKAGE_WORKFLOW = ".github/workflows/windows-native-package.yml"
UI_WORKFLOW = ".github/workflows/windows-native-ui-capture.yml"

ACTIVE_DOCS = [
    "README.md",
    "docs/user-guide.md",
    "docs/developer-guide.md",
    "docs/architecture-win32-native.md",
    "docs/testing-validation.md",
    "docs/release-artifacts.md",
    "docs/troubleshooting.md",
    "docs/windows-deployment.md",
    "docs/windows-native-slimming.md",
    "docs/windows-native-ui-validation.md",
    "docs/windows-serial-validation.md",
    "docs/windows-native-local-debug.md",
]

LEGACY_OR_TRANSITION_DOCS = [
    "docs/legacy-qt-notes.md",
    "docs/architecture.md",
    "docs/windows-native-parity.md",
]

REQUIRED_FILES = [
    PACKAGE_WORKFLOW,
    UI_WORKFLOW,
    "scripts/package-windows-native.ps1",
    "scripts/package-windows-native-mingw.sh",
    "scripts/inspect-windows-package.py",
    "scripts/inspect-windows-package.ps1",
    "scripts/capture-windows-native-ui.ps1",
    "scripts/capture-windows-native-ui-wine.sh",
    *ACTIVE_DOCS,
    *LEGACY_OR_TRANSITION_DOCS,
]

PACKAGE_WORKFLOW_TERMS = [
    "PACKAGE_NAME: SerialValueMatcherNative-win32-native-x64",
    "--self-test",
    "--ui-perf-test",
    "native-self-test.log",
    "native-ui-perf-test.log",
    "serial-pty-matrix.txt",
    "Gate status: passed",
    "${{ env.PACKAGE_NAME }}.zip",
    "${{ env.PACKAGE_NAME }}.zip.sha256.txt",
    "${{ env.PACKAGE_NAME }}.package-summary.txt",
]

UI_WORKFLOW_TERMS = [
    "name: windows-native-ui-screenshots",
    "--self-test",
    "--ui-perf-test",
    "capture-status.txt",
    "ui-perf-test.log",
    "window-info.txt",
    "artifacts/windows-native-ui/*.png",
]

REQUIRED_DOC_TERMS = {
    "README.md": [EXE_NAME, PACKAGE_ZIP],
    "docs/user-guide.md": [EXE_NAME, PACKAGE_ZIP, "artifact"],
    "docs/developer-guide.md": ["CMake", TARGET_NAME, EXE_NAME, "NativeFrameScheduler"],
    "docs/architecture-win32-native.md": [
        "NativeFrameScheduler",
        "NativeLayoutModel",
        "NativeLayoutTransaction",
        "NativePaintPolicy",
    ],
    "docs/testing-validation.md": [
        PACKAGE_WORKFLOW,
        UI_WORKFLOW,
        UI_ARTIFACT,
        "--self-test",
        "--ui-perf-test",
        "package",
        "docs consistency",
    ],
    "docs/release-artifacts.md": [
        PACKAGE_ARTIFACT,
        UI_ARTIFACT,
        PACKAGE_HASH,
        PACKAGE_SUMMARY,
        "Gate status: passed",
    ],
    "docs/troubleshooting.md": [
        PACKAGE_ARTIFACT,
        UI_ARTIFACT,
        "troubleshooting",
        "package",
        "PTY",
    ],
}

PACKAGE_SUMMARY_TERMS = [
    "Zip sha256",
    "Zip sha256 file matches",
    "Native exe present",
    "Native exe sha256",
    "Imported DLLs",
    "Forbidden Qt/SQLite/.NET runtime files",
    "Unicode text probe",
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


def check_doc_required_terms(root: Path, failures: list[str]) -> None:
    for relative, terms in REQUIRED_DOC_TERMS.items():
        check_terms(read_text(root / relative), terms, relative, failures)


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
    release_doc = read_text(root / "docs/release-artifacts.md")

    for relative, text in [
        ("scripts/package-windows-native.ps1", package_script),
        ("scripts/package-windows-native-mingw.sh", mingw_package_script),
        (PACKAGE_WORKFLOW, package_workflow),
        ("docs/release-artifacts.md", release_doc),
    ]:
        if PACKAGE_ARTIFACT not in text and "mingw" not in relative:
            add_failure(failures, relative, f"missing package artifact name: {PACKAGE_ARTIFACT}")
        if EXE_NAME not in text:
            add_failure(failures, relative, f"missing executable name: {EXE_NAME}")


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
    check_doc_required_terms(root, failures)
    check_active_contradictions(docs, failures, warnings)
    check_package_summary_terms(root, failures)
    check_markdown_links(root, docs, failures)
    check_cross_file_consistency(root, failures)

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
