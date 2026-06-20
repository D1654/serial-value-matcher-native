#!/usr/bin/env python3
import argparse
import hashlib
import sys
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
    "日志：HEX+文本",
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


FORBIDDEN_NAMES = {"qsqlite.dll"}
FORBIDDEN_DLL_PREFIXES = ("qt6",)
FORBIDDEN_IMPORTS = {
    "qt6core.dll",
    "qt6gui.dll",
    "qt6widgets.dll",
    "qt6serialport.dll",
    "qt6sql.dll",
    "qsqlite.dll",
    "mscoree.dll",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inspect SerialValueMatcher Win32 native package.")
    parser.add_argument("--stage-dir", required=True, type=Path)
    parser.add_argument("--zip-path", required=True, type=Path)
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


def main() -> int:
    args = parse_args()
    stage_dir = args.stage_dir.resolve()
    zip_path = args.zip_path.resolve()
    summary_path = args.summary_path.resolve()

    failures: list[str] = []
    summary: list[str] = []

    if not stage_dir.is_dir():
        failures.append(f"包检查失败：目录不存在：{stage_dir}")
    if not zip_path.is_file():
        failures.append(f"包检查失败：zip 不存在：{zip_path}")
    if failures:
        print(" ".join(failures), file=sys.stderr)
        return 2

    package_files = sorted([path for path in stage_dir.rglob("*") if path.is_file()])
    package_bytes = sum(path.stat().st_size for path in package_files)
    zip_bytes = zip_path.stat().st_size
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
    else:
        exe_bytes = native_exe.read_bytes()
        for term in UNICODE_PROBE_TERMS:
            if term.encode("utf-16le") not in exe_bytes:
                missing_unicode_terms.append(term)
        imported = imported_dlls(native_exe)
        exe_hash = file_sha256(native_exe)

    forbidden_imports = [
        dll for dll in imported if dll.lower() in FORBIDDEN_IMPORTS
    ]

    summary.append("SerialValueMatcher Native Windows package summary")
    summary.append("Package kind: Win32 native")
    summary.append("Inspector: Python local")
    summary.append(f"Zip path: {zip_path}")
    summary.append(f"Zip bytes: {zip_bytes}")
    summary.append(f"Extracted bytes: {package_bytes}")
    summary.append(f"File count: {len(package_files)}")
    summary.append(f"Max zip bytes: {args.max_zip_bytes}")
    summary.append(f"Max extracted bytes: {args.max_extracted_bytes}")
    if native_exe is not None:
        summary.append(f"Native exe sha256: {exe_hash}")
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
    summary.append("Unicode text probe:")
    if missing_unicode_terms:
        summary.append("  failed")
        for term in missing_unicode_terms:
            summary.append(f"  missing: {term}")
    else:
        summary.append("  passed")
        for term in UNICODE_PROBE_TERMS:
            summary.append(f"  required: {term}")

    if forbidden_files:
        failures.append("native 包中出现 Qt 或 qsqlite 运行时文件。")
    if native_exe is None:
        failures.append("native 包缺少 svm-native-win32.exe。")
    if missing_unicode_terms:
        failures.append("native exe 缺少关键中文 UTF-16LE 文本。")
    if forbidden_imports:
        failures.append("native exe 导入了禁止运行时：" + ", ".join(forbidden_imports))
    if zip_bytes > args.max_zip_bytes:
        failures.append(f"zip 体积超过门禁：{zip_bytes} > {args.max_zip_bytes}。")
    if package_bytes > args.max_extracted_bytes:
        failures.append(f"解压后体积超过门禁：{package_bytes} > {args.max_extracted_bytes}。")

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
