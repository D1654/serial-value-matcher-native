#!/usr/bin/env python3
import hashlib
import importlib.util
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INSPECTOR_PATH = ROOT / "scripts" / "inspect-windows-package.py"


def load_inspector():
    spec = importlib.util.spec_from_file_location("inspect_windows_package", INSPECTOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("unable to load package inspector")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def windows_eol(data: bytes) -> bytes:
    text = data.decode("utf-8")
    return text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n").encode("utf-8")


def create_package_zip(zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("README.md", windows_eol((ROOT / "README.md").read_bytes()))
        for source in sorted((ROOT / "docs").rglob("*")):
            if not source.is_file():
                continue
            relative = source.relative_to(ROOT).as_posix()
            data = source.read_bytes()
            if source.suffix.lower() in {".md", ".txt"}:
                data = windows_eol(data)
            archive.writestr(relative, data)


def run_inspector(stage_dir: Path, zip_path: Path, hash_path: Path, summary_path: Path):
    environment = os.environ.copy()
    environment["PYTHONUTF8"] = "1"
    return subprocess.run(
        [
            sys.executable,
            str(INSPECTOR_PATH),
            "--stage-dir",
            str(stage_dir),
            "--zip-path",
            str(zip_path),
            "--hash-path",
            str(hash_path),
            "--summary-path",
            str(summary_path),
            "--max-zip-bytes",
            str(64 * 1024 * 1024),
            "--max-extracted-bytes",
            str(64 * 1024 * 1024),
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="strict",
        env=environment,
    )


def assert_failed_summary(result, summary_path: Path) -> str:
    assert result.returncode == 1, result.stdout + result.stderr
    assert "Traceback" not in result.stderr
    summary = summary_path.read_text(encoding="utf-8", errors="strict")
    assert "Gate status: failed" in summary
    return summary


def verify_real_zip_eol_normalization(inspector, temporary_path: Path) -> None:
    repo_root = temporary_path / "fixture-repo"
    stage_dir = temporary_path / "fixture-stage"
    zip_path = temporary_path / "fixture.zip"
    repo_doc = repo_root / "docs" / "设备说明.md"
    repo_doc.parent.mkdir(parents=True)
    repo_doc.write_bytes("第一行\n第二行\n".encode("utf-8"))

    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("docs/设备说明.md", "第一行\r\n第二行\r\n".encode("utf-8"))
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(stage_dir)

    assert inspector.package_documentation_file_set_failures(repo_root, stage_dir) == []
    (stage_dir / "docs" / "设备说明.md").write_bytes("第一行\r\n内容已变化\r\n".encode("utf-8"))
    assert inspector.package_documentation_file_set_failures(repo_root, stage_dir) == [
        "package docs content mismatch: docs/设备说明.md"
    ]


def main() -> int:
    inspector = load_inspector()
    assert inspector.utf8_safe_text("bad-\udcff.md") == "bad-\\udcff.md"

    metadata = inspector.read_cmake_version_metadata(ROOT)
    package_name = metadata["SVM_PACKAGE_ARTIFACT"]
    with tempfile.TemporaryDirectory(prefix="svm-package-inspector-") as temporary:
        temporary_path = Path(temporary)
        verify_real_zip_eol_normalization(inspector, temporary_path)
        stage_dir = temporary_path / package_name
        zip_path = temporary_path / f"{package_name}.zip"
        hash_path = temporary_path / f"{package_name}.zip.sha256.txt"
        summary_path = temporary_path / f"{package_name}.package-summary.txt"

        create_package_zip(zip_path)
        with zipfile.ZipFile(zip_path) as archive:
            assert "docs/用户指南.md" in archive.namelist()
            archive.extractall(stage_dir)
        assert (stage_dir / "docs" / "用户指南.md").is_file()
        assert b"\r\n" in (stage_dir / "docs" / "用户指南.md").read_bytes()
        assert inspector.package_documentation_file_set_failures(ROOT, stage_dir) == []

        zip_hash = hashlib.sha256(zip_path.read_bytes()).hexdigest().upper()
        hash_path.write_text(f"SHA256 {zip_hash}  {zip_path.name}\n", encoding="utf-8")
        result = run_inspector(stage_dir, zip_path, hash_path, summary_path)
        summary = assert_failed_summary(result, summary_path)
        assert "Package documentation file set:\n  passed" in summary

        if os.name == "posix":
            raw_name = b"bad-\xff.md"
            decoded_name = os.fsdecode(raw_name)
            raw_path = os.fsencode(stage_dir / "docs") + b"/" + raw_name
            descriptor = os.open(raw_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            try:
                os.write(descriptor, b"invalid extractor filename\n")
            finally:
                os.close(descriptor)

            result = run_inspector(stage_dir, zip_path, hash_path, summary_path)
            summary = assert_failed_summary(result, summary_path)
            assert inspector.utf8_safe_text(decoded_name) in summary
            assert "Package documentation file set:\n  failed" in summary

    print("inspect_windows_package_tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
