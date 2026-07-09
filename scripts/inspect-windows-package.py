#!/usr/bin/env python3
import argparse
import hashlib
import re
import sys
import urllib.parse
from pathlib import Path

try:
    import pefile
except ImportError:  # pragma: no cover
    pefile = None


UNICODE_PROBE_TERMS = [
    "串口值匹配器",
    "文件",
    "串口",
    "工具",
    "分析",
    "帮助",
    "串口选择",
    "功能工作区",
    "单发",
    "多发",
    "发送文件",
    "发送进度",
    "当前页提示",
    "日志缓存",
    "扫描与分析工作流",
    "通信日志",
    "流程：连接设备",
    "分析生成",
    "规则验证",
    "导出报告",
    "视图",
    "默认色系",
    "柔和色系",
    "高对比色系",
    "显示日志时间",
    "复制可见日志",
    "导出可见日志",
    "查找日志",
    "跟随最新日志",
    "模式",
    "行尾",
    "历史",
    "过滤",
    "搜索",
    "界面偏好",
    "界面偏好保存失败",
    "十六进制",
    "十进制",
    "二进制",
    "十进制字节流",
    "二进制字节流",
    "HEX+文本",
    "日志时间已显示",
    "日志文本解码编码",
    "当前可见通信日志",
    "正在回看历史日志",
    "文件发送进行中",
    "停止扫描",
    "完整数据已写入 native 存储",
    "UTF-8",
    "GBK",
]


FORBIDDEN_NAMES = {
    "qsqlite.dll",
    "mscoree.dll",
    "coreclr.dll",
    "clrjit.dll",
    "hostfxr.dll",
    "hostpolicy.dll",
}
FORBIDDEN_DLL_PREFIXES = ("qt6", "system.", "microsoft.net")
FORBIDDEN_IMPORTS = {
    "qt6core.dll",
    "qt6gui.dll",
    "qt6widgets.dll",
    "qt6serialport.dll",
    "qt6sql.dll",
    "qsqlite.dll",
    "mscoree.dll",
}

REQUIRED_PACKAGE_FILES = [
    "svm-native-win32.exe",
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

MARKDOWN_LINK_PATTERN = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
PACKAGED_PATH_REFERENCE_PATTERN = re.compile(r"`((?:docs/|README\.md)[^`]+)`")
EXTERNAL_LINK_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inspect SerialValueMatcher Win32 native package.")
    parser.add_argument("--stage-dir", required=True, type=Path)
    parser.add_argument("--zip-path", required=True, type=Path)
    parser.add_argument("--hash-path", required=True, type=Path)
    parser.add_argument("--summary-path", required=True, type=Path)
    parser.add_argument("--max-zip-bytes", required=True, type=int)
    parser.add_argument("--max-extracted-bytes", required=True, type=int)
    return parser.parse_args()


def relative_path(root: Path, path: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def read_cmake_version_metadata(root: Path) -> dict[str, str]:
    version_file = root / "cmake" / "svm_version.cmake"
    metadata: dict[str, str] = {}
    if not version_file.is_file():
        return metadata
    pattern = re.compile(r'^\s*set\(\s*([A-Za-z0-9_]+)\s+"([^"]*)"\s*\)')
    for line in version_file.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            metadata[match.group(1)] = match.group(2)
    return metadata


def imported_dlls(exe_path: Path) -> list[str]:
    if pefile is None:
        return []
    pe = pefile.PE(str(exe_path), fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]]
    )
    imports = []
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        imports.append(entry.dll.decode("ascii", errors="replace"))
    return sorted(set(imports), key=str.lower)


def decode_pe_version_value(value) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace").rstrip("\x00")
    return str(value).rstrip("\x00")


def version_from_ms_ls(ms: int, ls: int) -> str:
    return f"{(ms >> 16) & 0xFFFF}.{ms & 0xFFFF}.{(ls >> 16) & 0xFFFF}.{ls & 0xFFFF}"


def native_exe_version_info(exe_path: Path) -> dict[str, str]:
    if pefile is None:
        return {}
    pe = pefile.PE(str(exe_path), fast_load=False)
    values: dict[str, str] = {}
    fixed = getattr(pe, "VS_FIXEDFILEINFO", [])
    if fixed:
        info = fixed[0]
        values["FixedFileVersion"] = version_from_ms_ls(info.FileVersionMS, info.FileVersionLS)
        values["FixedProductVersion"] = version_from_ms_ls(info.ProductVersionMS, info.ProductVersionLS)
    for file_info in getattr(pe, "FileInfo", []) or []:
        for entry in file_info:
            if decode_pe_version_value(getattr(entry, "Key", "")) != "StringFileInfo":
                continue
            for table in getattr(entry, "StringTable", []) or []:
                for key, value in table.entries.items():
                    values[decode_pe_version_value(key)] = decode_pe_version_value(value)
    return values


def local_markdown_target(raw_target: str) -> str | None:
    target = raw_target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1].strip()
    if not target or target.startswith("#"):
        return None
    if EXTERNAL_LINK_PATTERN.match(target):
        return None
    target = target.split()[0]
    target = target.split("#", 1)[0].split("?", 1)[0]
    if not target or "*" in target:
        return None
    return urllib.parse.unquote(target)


def package_documentation_link_failures(stage_dir: Path, package_files: list[Path]) -> list[str]:
    failures: list[str] = []
    stage_root = stage_dir.resolve()
    markdown_files = sorted(path for path in package_files if path.suffix.lower() == ".md")

    def check_target(
        markdown_path: Path,
        relative_markdown: str,
        line_no: int,
        raw_target: str,
        *,
        root_relative: bool,
    ) -> None:
        target = local_markdown_target(raw_target)
        if target is None:
            return
        base_dir = stage_root if root_relative else markdown_path.parent
        resolved = (base_dir / target).resolve()
        try:
            resolved.relative_to(stage_root)
        except ValueError:
            failures.append(f"{relative_markdown}:{line_no}: link leaves package: {raw_target}")
            return
        if not resolved.exists():
            failures.append(f"{relative_markdown}:{line_no}: broken package link: {raw_target}")

    for markdown_path in markdown_files:
        relative_markdown = relative_path(stage_dir, markdown_path)
        lines = markdown_path.read_text(encoding="utf-8", errors="replace").splitlines()
        for line_no, line in enumerate(lines, start=1):
            for match in MARKDOWN_LINK_PATTERN.finditer(line):
                check_target(markdown_path, relative_markdown, line_no, match.group(1), root_relative=False)
            for match in PACKAGED_PATH_REFERENCE_PATTERN.finditer(line):
                check_target(markdown_path, relative_markdown, line_no, match.group(1), root_relative=True)
    return failures


def required_package_file_failures(stage_dir: Path) -> list[str]:
    return [
        relative
        for relative in REQUIRED_PACKAGE_FILES
        if not (stage_dir / relative).is_file()
    ]


def collect_relative_files(root: Path) -> set[str]:
    if not root.is_dir():
        return set()
    return {
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file()
    }


def package_documentation_file_set_failures(repo_root: Path, stage_dir: Path) -> list[str]:
    failures: list[str] = []
    repo_docs_dir = repo_root / "docs"
    package_docs_dir = stage_dir / "docs"

    if not repo_docs_dir.is_dir():
        failures.append("repository docs directory is missing")
    if not package_docs_dir.is_dir():
        failures.append("package docs directory is missing")
    if failures:
        return failures

    repo_files = collect_relative_files(repo_docs_dir)
    package_files = collect_relative_files(package_docs_dir)
    for relative in sorted(repo_files - package_files):
        failures.append(f"missing package docs file: docs/{relative}")
    for relative in sorted(package_files - repo_files):
        failures.append(f"unexpected package docs file: docs/{relative}")
    for relative in sorted(repo_files & package_files):
        repo_path = repo_docs_dir / relative
        package_path = package_docs_dir / relative
        if file_sha256(repo_path) != file_sha256(package_path):
            failures.append(f"package docs content mismatch: docs/{relative}")
    return failures


def main() -> int:
    args = parse_args()
    stage_dir = args.stage_dir.resolve()
    zip_path = args.zip_path.resolve()
    hash_path = args.hash_path.resolve()
    summary_path = args.summary_path.resolve()
    repo_root = Path(__file__).resolve().parents[1]
    version_metadata = read_cmake_version_metadata(repo_root)

    failures: list[str] = []
    summary: list[str] = []

    if not stage_dir.is_dir():
        failures.append(f"包检查失败：目录不存在：{stage_dir}")
    if not zip_path.is_file():
        failures.append(f"包检查失败：zip 不存在：{zip_path}")
    if not hash_path.is_file():
        failures.append(f"包检查失败：SHA256 文件不存在：{hash_path}")
    if failures:
        print(" ".join(failures), file=sys.stderr)
        return 2

    package_files = sorted([path for path in stage_dir.rglob("*") if path.is_file()])
    package_bytes = sum(path.stat().st_size for path in package_files)
    zip_bytes = zip_path.stat().st_size
    zip_hash = file_sha256(zip_path)
    hash_text = hash_path.read_text(encoding="utf-8", errors="replace").strip()
    hash_matches = zip_hash in hash_text.upper()
    largest_files = sorted(package_files, key=lambda path: path.stat().st_size, reverse=True)[:12]
    native_exe = next((path for path in package_files if path.name.lower() == "svm-native-win32.exe"), None)

    forbidden_files = []
    for path in package_files:
        lower_name = path.name.lower()
        lower_relative = relative_path(stage_dir, path).lower()
        if (
            lower_name in FORBIDDEN_NAMES
            or any(lower_name.startswith(prefix) and lower_name.endswith(".dll") for prefix in FORBIDDEN_DLL_PREFIXES)
            or "sqldrivers/" in lower_relative
        ):
            forbidden_files.append(path)

    missing_unicode_terms = []
    if native_exe is None:
        missing_unicode_terms.extend(UNICODE_PROBE_TERMS)
        imported = []
        exe_hash = ""
        exe_version_info = {}
    else:
        exe_bytes = native_exe.read_bytes()
        for term in UNICODE_PROBE_TERMS:
            if term.encode("utf-16le") not in exe_bytes:
                missing_unicode_terms.append(term)
        imported = imported_dlls(native_exe)
        exe_hash = file_sha256(native_exe)
        exe_version_info = native_exe_version_info(native_exe)

    forbidden_imports = [
        dll for dll in imported if dll.lower() in FORBIDDEN_IMPORTS
    ]
    unexpected_dll_files = sorted(
        [path for path in package_files if path.suffix.lower() == ".dll"],
        key=lambda path: relative_path(stage_dir, path).lower(),
    )
    markdown_files = sorted(path for path in package_files if path.suffix.lower() == ".md")
    required_file_failures = required_package_file_failures(stage_dir)
    documentation_link_failures = package_documentation_link_failures(stage_dir, package_files)
    documentation_file_set_failures = package_documentation_file_set_failures(repo_root, stage_dir)

    summary.append("SerialValueMatcher Native Windows package summary")
    summary.append("Package kind: Win32 native")
    summary.append("Inspector: Python local")
    summary.append(f"Zip path: {zip_path}")
    summary.append(f"Hash path: {hash_path}")
    summary.append(f"Zip bytes: {zip_bytes}")
    summary.append(f"Zip sha256: {zip_hash}")
    summary.append(f"Zip sha256 file matches: {'yes' if hash_matches else 'no'}")
    summary.append(f"Extracted bytes: {package_bytes}")
    summary.append(f"File count: {len(package_files)}")
    summary.append(f"Max zip bytes: {args.max_zip_bytes}")
    summary.append(f"Max extracted bytes: {args.max_extracted_bytes}")
    if native_exe is not None:
        summary.append("Native exe present: yes")
        summary.append(f"Native exe bytes: {native_exe.stat().st_size}")
        summary.append(f"Native exe sha256: {exe_hash}")
    else:
        summary.append("Native exe present: no")
    summary.append("")
    summary.append("Version metadata:")
    summary.append("  Source: cmake/svm_version.cmake")
    summary.append(f"  Expected version: {version_metadata.get('SVM_VERSION', '')}")
    summary.append(f"  Expected release tag: {version_metadata.get('SVM_RELEASE_TAG', '')}")
    summary.append(f"  Expected package artifact: {version_metadata.get('SVM_PACKAGE_ARTIFACT', '')}")
    summary.append(f"  Expected MinGW package artifact: {version_metadata.get('SVM_MINGW_PACKAGE_ARTIFACT', '')}")
    summary.append(f"Native exe fixed file version: {exe_version_info.get('FixedFileVersion', '')}")
    summary.append(f"Native exe fixed product version: {exe_version_info.get('FixedProductVersion', '')}")
    summary.append(f"Native exe file version: {exe_version_info.get('FileVersion', '')}")
    summary.append(f"Native exe product version: {exe_version_info.get('ProductVersion', '')}")
    summary.append(f"Native exe product name: {exe_version_info.get('ProductName', '')}")
    summary.append(f"Native exe original filename: {exe_version_info.get('OriginalFilename', '')}")
    summary.append("")
    summary.append("Largest files:")
    for path in largest_files:
        summary.append(f"  {path.stat().st_size:12d}  {relative_path(stage_dir, path)}")
    summary.append("")
    summary.append("Imported DLLs:")
    if imported:
        for dll in imported:
            summary.append(f"  {dll}")
    elif native_exe is None:
        summary.append("  unavailable: missing svm-native-win32.exe")
    elif pefile is None:
        summary.append("  unavailable: python3-pefile is not installed")
    else:
        summary.append("  none")
    summary.append("")
    summary.append("Forbidden Qt/SQLite/.NET runtime files:")
    if forbidden_files:
        for path in forbidden_files:
            summary.append(f"  {path.stat().st_size:12d}  {relative_path(stage_dir, path)}")
    else:
        summary.append("  none")
    summary.append("")
    summary.append("Unexpected DLL files:")
    if unexpected_dll_files:
        for path in unexpected_dll_files:
            summary.append(f"  {path.stat().st_size:12d}  {relative_path(stage_dir, path)}")
    else:
        summary.append("  none")
    summary.append("")
    summary.append("Required package files:")
    if required_file_failures:
        summary.append("  failed")
        for failure in required_file_failures:
            summary.append(f"  missing: {failure}")
    else:
        summary.append("  passed")
        for relative in REQUIRED_PACKAGE_FILES:
            summary.append(f"  required: {relative}")
    summary.append("")
    summary.append("Unicode text probe:")
    if missing_unicode_terms:
        summary.append("  failed")
        for term in missing_unicode_terms:
            summary.append(f"  missing: {term}")
    else:
        summary.append("  passed")
        for term in UNICODE_PROBE_TERMS:
            summary.append(f"  required: {term}")
    summary.append("")
    summary.append("Package documentation links:")
    if documentation_link_failures:
        summary.append("  failed")
        for failure in documentation_link_failures:
            summary.append(f"  {failure}")
    else:
        summary.append("  passed")
        summary.append(f"  checked files: {len(markdown_files)}")
    summary.append("")
    summary.append("Package documentation file set:")
    if documentation_file_set_failures:
        summary.append("  failed")
        for failure in documentation_file_set_failures:
            summary.append(f"  {failure}")
    else:
        summary.append("  passed")
        summary.append(f"  checked files: {len(collect_relative_files(repo_root / 'docs'))}")

    if forbidden_files:
        failures.append("native 包中出现 Qt、SQLite 或 .NET 运行时文件。")
    if unexpected_dll_files:
        failures.append("native 包中出现非预期 DLL 文件：" + ", ".join(relative_path(stage_dir, path) for path in unexpected_dll_files) + "。")
    if native_exe is None:
        failures.append("native 包缺少 svm-native-win32.exe。")
    if required_file_failures:
        failures.append("native 包缺少必备文件：" + ", ".join(required_file_failures) + "。")
    if not hash_matches:
        failures.append("zip SHA256 文件缺失或内容不匹配。")
    if native_exe is not None and pefile is None:
        failures.append("缺少 python3-pefile，无法验证 native exe 导入表。")
    if missing_unicode_terms:
        failures.append("native exe 缺少关键中文 UTF-16LE 文本。")
    if forbidden_imports:
        failures.append("native exe 导入了禁止运行时：" + ", ".join(forbidden_imports))
    if zip_bytes > args.max_zip_bytes:
        failures.append(f"zip 体积超过门禁：{zip_bytes} > {args.max_zip_bytes}。")
    if package_bytes > args.max_extracted_bytes:
        failures.append(f"解压后体积超过门禁：{package_bytes} > {args.max_extracted_bytes}。")
    if documentation_link_failures:
        failures.append("native 包内文档链接或文档路径存在断链。")
    if documentation_file_set_failures:
        failures.append("native 包内 docs 文件集合与仓库 docs 不一致。")

    required_version_keys = [
        "SVM_VERSION",
        "SVM_RELEASE_TAG",
        "SVM_PRODUCT_DISPLAY_NAME",
        "SVM_WIN32_EXE_NAME",
        "SVM_PACKAGE_ARTIFACT",
        "SVM_MINGW_PACKAGE_ARTIFACT",
    ]
    missing_version_keys = [key for key in required_version_keys if not version_metadata.get(key)]
    if missing_version_keys:
        failures.append("版本元数据源缺少字段：" + ", ".join(missing_version_keys) + "。")
    expected_artifacts = {
        version_metadata.get("SVM_PACKAGE_ARTIFACT", ""),
        version_metadata.get("SVM_MINGW_PACKAGE_ARTIFACT", ""),
    }
    if stage_dir.name not in expected_artifacts:
        failures.append(f"包目录名与版本元数据不一致：{stage_dir.name}。")
    if zip_path.name != f"{stage_dir.name}.zip":
        failures.append(f"zip 文件名与包目录名不一致：{zip_path.name}。")
    if native_exe is not None and pefile is not None and version_metadata:
        expected_version = version_metadata.get("SVM_VERSION", "")
        expected_fixed_version = ".".join([
            version_metadata.get("SVM_VERSION_MAJOR", ""),
            version_metadata.get("SVM_VERSION_MINOR", ""),
            version_metadata.get("SVM_VERSION_PATCH", ""),
            version_metadata.get("SVM_VERSION_TWEAK", ""),
        ])
        version_expectations = {
            "FixedFileVersion": expected_fixed_version,
            "FixedProductVersion": expected_fixed_version,
            "FileVersion": expected_version,
            "ProductVersion": expected_version,
            "ProductName": version_metadata.get("SVM_PRODUCT_DISPLAY_NAME", ""),
            "OriginalFilename": version_metadata.get("SVM_WIN32_EXE_NAME", ""),
        }
        for field, expected in version_expectations.items():
            actual = exe_version_info.get(field, "")
            if actual != expected:
                failures.append(f"native exe VERSIONINFO {field} 不一致：{actual} != {expected}。")

    summary.append("")
    if failures:
        summary.append("Gate status: failed")
        for failure in failures:
            summary.append(f"  - {failure}")
    else:
        summary.append("Gate status: passed")

    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text("\n".join(summary) + "\n", encoding="utf-8")

    if failures:
        print(" ".join(failures), file=sys.stderr)
        return 1

    print(f"Native 包检查通过：zip={zip_bytes} extracted={package_bytes} files={len(package_files)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
