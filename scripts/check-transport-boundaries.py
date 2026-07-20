#!/usr/bin/env python3
import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


TRANSPORT_SOURCE_DIR = Path("src/transport")
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp",
}
RAW_STRING_START = re.compile(r'(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(')
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"](?P<header>[^>"]+)[>"]')


@dataclass(frozen=True)
class Rule:
    rule_id: str
    pattern: re.Pattern[str] | None
    message: str
    remediation: str


@dataclass(frozen=True)
class Violation:
    path: Path
    line_no: int
    rule: Rule


PATH_RULES_BY_ID = {
    "win32-dependency": Rule(
        "win32-dependency",
        None,
        "transport source or include path references Win32 implementation",
        "move the concrete implementation to src/win32 and depend on a neutral serial contract",
    ),
    "ui-dependency": Rule(
        "ui-dependency",
        None,
        "transport source or include path references UI or main-window code",
        "move UI coordination outside src/transport and consume typed serial results",
    ),
    "codec-dependency": Rule(
        "codec-dependency",
        None,
        "transport source or include path references matcher, codec, Gray, or bit-layout behavior",
        "place device interpretation in an upper-layer core codec",
    ),
    "upper-layer-analysis": Rule(
        "upper-layer-analysis",
        None,
        "transport source or include path references analysis or report-layer behavior",
        "keep analysis dependencies above protocol and byte transport",
    ),
    "storage-dependency": Rule(
        "storage-dependency",
        None,
        "transport source or include path references native persistence",
        "persist completed typed results outside src/transport",
    ),
    "old-facade": Rule(
        "old-facade",
        None,
        "transport source or include path reintroduces a removed broad serial facade",
        "use the narrow session contracts instead of a compatibility facade",
    ),
}

WINDOWS_SDK_HEADERS = {
    "commapi.h",
    "fileapi.h",
    "handleapi.h",
    "processthreadsapi.h",
    "setupapi.h",
    "synchapi.h",
    "winbase.h",
    "windows.h",
    "winnt.h",
}

TEXT_RULES = (
    Rule(
        "win32-dependency",
        re.compile(r'(?:\b(?:src/)?win32/|\bsvm::win32\b|\b[A-Za-z0-9_]*Win32[A-Za-z0-9_]*\b)', re.IGNORECASE),
        "neutral transport references a Win32 path, namespace, or concrete type",
        "move the concrete implementation to src/win32 and depend on a neutral serial contract",
    ),
    Rule(
        "win32-api",
        re.compile(
            r"\b(?:HANDLE|HWND|HINSTANCE|DWORD|BOOL|OVERLAPPED|CRITICAL_SECTION|DCB|COMMTIMEOUTS|COMSTAT|"
            r"CreateFileW|ReadFile|WriteFile|GetLastError|CloseHandle|ClearCommError|SetCommState|GetCommState|"
            r"SetCommTimeouts|SetCommMask|EscapeCommFunction|CreateEventW|SetEvent|ResetEvent|WaitForSingleObject|"
            r"CreateThread|CancelSynchronousIo|CancelIoEx|PurgeComm|SetupComm|GetCommConfig|SetCommConfig|"
            r"GetCommProperties|GetCommModemStatus|WaitCommEvent|TransmitCommChar)\b"
        ),
        "neutral transport references a concrete Win32 type or API",
        "keep native handles and Windows API calls inside Win32SerialSession",
    ),
    Rule(
        "ui-dependency",
        re.compile(
            r"(?:\b(?:NativeMainWindow|MainWindow|Native[A-Za-z0-9_]*Ui[A-Za-z0-9_]*)\b|main_window)",
            re.IGNORECASE,
        ),
        "neutral transport references a UI or main-window type",
        "pass typed serial results through a narrow contract instead of depending on UI state",
    ),
    Rule(
        "codec-dependency",
        re.compile(
            r"\b(?:ValueMatchCandidate|MatchTolerance|CandidateGenerationOptions|"
            r"ProtocolFieldRule[A-Za-z0-9_]*|[A-Za-z0-9_]*(?:Codec|Matcher|MatchCandidate|MatchRule|"
            r"Gray(?:Code|16)?|BitLayout)[A-Za-z0-9_]*)\b",
            re.IGNORECASE,
        ),
        "neutral transport references matcher, codec, Gray, or bit-layout behavior",
        "place device interpretation in an upper-layer core codec that consumes byte or register results",
    ),
    Rule(
        "upper-layer-analysis",
        re.compile(
            r"\b(?:NumericCandidateType|WordOrder|ByteOrder|MatchTolerance|ScaleTransform|TargetValue|"
            r"RegisterSample|CandidateGenerationOptions|ValueMatchCandidate|CandidateGenerationResult|"
            r"CandidateObservation|StabilityAnalysisOptions|StableCandidate|StabilityAnalysisResult|"
            r"BitFlagInterpretationDefinition|EnumMapInterpretationDefinition|"
            r"InterpretationMapValidationResult|ProtocolFieldRule[A-Za-z0-9_]*)\b"
        ),
        "neutral transport references analysis or report-layer behavior",
        "keep analysis dependencies above protocol and byte transport",
    ),
    Rule(
        "storage-dependency",
        re.compile(r"(?:\bnative_storage::|\bNativeSessionStore\b)", re.IGNORECASE),
        "neutral transport references native persistence",
        "persist completed typed results outside src/transport",
    ),
    Rule(
        "old-facade",
        re.compile(r"\b(?:SerialTransport|Win32SerialPort)\b"),
        "removed broad serial facade is referenced",
        "use SerialSession, SerialByteStream, SerialWriteScheduler, or Win32SerialSession directly",
    ),
)

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check neutral serial transport dependency boundaries.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def masked_cpp_views(text: str) -> tuple[str, str]:
    comment_free: list[str] = []
    code_only: list[str] = []
    state = "code"
    raw_terminator = ""
    index = 0

    def append(value: str, keep_comment_free: bool, keep_code: bool) -> None:
        for character in value:
            preserved = character if character in "\r\n" else " "
            comment_free.append(character if keep_comment_free else preserved)
            code_only.append(character if keep_code else preserved)

    while index < len(text):
        if state == "line-comment":
            character = text[index]
            append(character, False, False)
            index += 1
            if character == "\n":
                state = "code"
            continue
        if state == "block-comment":
            if text.startswith("*/", index):
                append("*/", False, False)
                index += 2
                state = "code"
            else:
                append(text[index], False, False)
                index += 1
            continue
        if state == "raw-string":
            if text.startswith(raw_terminator, index):
                append(raw_terminator, True, False)
                index += len(raw_terminator)
                state = "code"
            else:
                append(text[index], True, False)
                index += 1
            continue
        if state in {"string", "character"}:
            quote = '"' if state == "string" else "'"
            character = text[index]
            append(character, True, False)
            index += 1
            if character == "\\" and index < len(text):
                append(text[index], True, False)
                index += 1
            elif character == quote:
                state = "code"
            continue

        if text.startswith("//", index):
            append("//", False, False)
            index += 2
            state = "line-comment"
            continue
        if text.startswith("/*", index):
            append("/*", False, False)
            index += 2
            state = "block-comment"
            continue
        raw_match = RAW_STRING_START.match(text, index)
        if raw_match:
            token = raw_match.group(0)
            raw_terminator = ")" + raw_match.group("delimiter") + '"'
            append(token, True, False)
            index = raw_match.end()
            state = "raw-string"
            continue
        if text[index] == '"':
            append('"', True, False)
            index += 1
            state = "string"
            continue
        if text[index] == "'":
            append("'", True, False)
            index += 1
            state = "character"
            continue
        append(text[index], True, True)
        index += 1

    if state in {"block-comment", "raw-string", "string", "character"}:
        raise ValueError(f"unterminated C/C++ lexical state: {state}")
    return "".join(comment_free), "".join(code_only)


def path_tokens(path_text: str) -> list[str]:
    tokens: list[str] = []
    for segment in path_text.replace("\\", "/").split("/"):
        if segment in {"", ".", ".."}:
            continue
        stem = segment.rsplit(".", 1)[0]
        separated = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", stem)
        tokens.extend(token.lower() for token in re.findall(r"[A-Za-z0-9]+", separated))
    return tokens


def contains_token_sequence(tokens: list[str], sequence: tuple[str, ...]) -> bool:
    width = len(sequence)
    return any(tuple(tokens[index : index + width]) == sequence for index in range(len(tokens) - width + 1))


def dependency_path_rule_ids(path_text: str) -> tuple[str, ...]:
    tokens = path_tokens(path_text)
    token_set = set(tokens)
    rule_ids: list[str] = []

    def add(rule_id: str) -> None:
        if rule_id not in rule_ids:
            rule_ids.append(rule_id)

    basename = path_text.replace("\\", "/").rsplit("/", 1)[-1].lower()
    if "win32" in token_set or basename in WINDOWS_SDK_HEADERS:
        add("win32-dependency")
    if "ui" in token_set or contains_token_sequence(tokens, ("main", "window")):
        add("ui-dependency")
    if (
        token_set.intersection({"codec", "matcher", "matching", "gray", "gray16", "graycode"})
        or contains_token_sequence(tokens, ("gray", "code"))
        or contains_token_sequence(tokens, ("bit", "layout"))
        or contains_token_sequence(tokens, ("match", "candidate"))
        or contains_token_sequence(tokens, ("match", "rule"))
    ):
        add("codec-dependency")
    if token_set.intersection({"analysis", "report"}):
        add("upper-layer-analysis")
    if (
        "storage" in token_set
        or contains_token_sequence(tokens, ("native", "store"))
        or contains_token_sequence(tokens, ("native", "storage"))
    ):
        add("storage-dependency")
    if (
        contains_token_sequence(tokens, ("serial", "transport"))
        or contains_token_sequence(tokens, ("win32", "serial", "port"))
    ):
        add("old-facade")
    return tuple(rule_ids)


def violations_for_text(relative: Path, text: str) -> list[Violation]:
    violations = [
        Violation(relative, 1, PATH_RULES_BY_ID[rule_id])
        for rule_id in dependency_path_rule_ids(relative.as_posix())
    ]
    comment_free, code_only = masked_cpp_views(text)
    comment_lines = comment_free.splitlines()
    code_lines = code_only.splitlines()
    for line_no, (comment_line, code_line) in enumerate(zip(comment_lines, code_lines), start=1):
        include_match = INCLUDE_PATTERN.match(comment_line) if re.match(r"^\s*#\s*include\b", code_line) else None
        if include_match:
            header = include_match.group("header").replace("\\", "/")
            for rule_id in dependency_path_rule_ids(header):
                violations.append(Violation(relative, line_no, PATH_RULES_BY_ID[rule_id]))
        for rule in TEXT_RULES:
            assert rule.pattern is not None
            if rule.pattern.search(code_line):
                violations.append(Violation(relative, line_no, rule))
    return violations


def is_transport_source(path: Path) -> bool:
    return path.suffix.lower() in SOURCE_SUFFIXES


def transport_sources(root: Path) -> list[Path]:
    source_dir = root / TRANSPORT_SOURCE_DIR
    if not source_dir.is_dir():
        raise ValueError(f"missing transport source directory: {source_dir}")
    sources = sorted(
        path for path in source_dir.rglob("*")
        if path.is_file() and is_transport_source(path)
    )
    if not sources:
        raise ValueError(f"transport source directory contains no C/C++ sources: {source_dir}")
    return sources


def check_repository(root: Path) -> tuple[list[Violation], list[str]]:
    violations: list[Violation] = []
    errors: list[str] = []
    try:
        sources = transport_sources(root)
    except ValueError as error:
        return violations, [str(error)]
    for path in sources:
        relative = path.relative_to(root)
        try:
            text = path.read_text(encoding="utf-8", errors="strict")
            file_violations = violations_for_text(relative, text)
        except (OSError, UnicodeError, ValueError) as error:
            errors.append(f"cannot read {relative}: {error}")
            continue
        violations.extend(file_violations)
    return violations, errors


def run_self_test() -> int:
    allowed = """
#include "core/modbus_scan_executor_core.h"
#include "transport/serial_session.h"
class SerialRtuTransport final {};
bool generationMatching = true;
const char* description = "gray codec /* // SerialTransport";
const char* raw = R"tag(Win32SerialPort // Gray16 /*)tag";
const char* includeText = R"tag(
#include <windows.h>
)tag";
// HANDLE SerialTransport;
/* Win32SerialPort Gray16Codec; */
"""
    forbidden = [
        ("win32 project include", '#include "win32/win32_serial_session.h"', "win32-dependency"),
        ("relative Win32 include", '#include "../win32/win32_serial_session.h"', "win32-dependency"),
        ("source-root Win32 include", '#include "src/win32/win32_serial_session.h"', "win32-dependency"),
        ("Windows SDK include", "#include <fileapi.h>", "win32-dependency"),
        ("Win32 scalar/API", "DWORD code = GetLastError(); CloseHandle(handle);", "win32-api"),
        ("Win32 async API", "OVERLAPPED io{}; CancelSynchronousIo(thread);", "win32-api"),
        ("Win32 serial API", "PurgeComm(handle, 0); SetupComm(handle, 64, 64);", "win32-api"),
        ("UI type", "NativeMainWindow* owner = nullptr;", "ui-dependency"),
        ("UI include", '#include "ui/serial_panel.h"', "ui-dependency"),
        ("Gray codec", "Gray16Codec codec;", "codec-dependency"),
        ("snake-case codec", "device_codec codec; gray16ToBinary(raw);", "codec-dependency"),
        ("codec include", '#include "core/device_codec.h"', "codec-dependency"),
        ("matcher include", '#include "core/matcher.h"', "codec-dependency"),
        ("codec directory include", '#include "core/codec/device.h"', "codec-dependency"),
        ("matcher directory include", '#include "../core/matcher/types.hpp"', "codec-dependency"),
        ("Gray directory include", '#include "src/core/gray/decoder.h"', "codec-dependency"),
        ("bit-layout directory include", '#include "core/bit_layout/field.hpp"', "codec-dependency"),
        ("real match type", "ValueMatchCandidate candidate; MatchTolerance tolerance;", "codec-dependency"),
        ("real analysis options", "CandidateGenerationOptions options;", "upper-layer-analysis"),
        ("stable candidate", "StableCandidate candidate;", "upper-layer-analysis"),
        ("analysis include", '#include "core/analysis_core.h"', "upper-layer-analysis"),
        ("relative analysis include", '#include "../core/analysis_core.h"', "upper-layer-analysis"),
        ("analysis directory include", '#include "core/analysis/types.h"', "upper-layer-analysis"),
        ("report directory include", '#include "core/report/model.hpp"', "upper-layer-analysis"),
        ("storage include", '#include "native_storage/native_session_store.h"', "storage-dependency"),
        ("relative storage include", '#include "../native_storage/native_session_store.h"', "storage-dependency"),
        ("old facade", "SerialTransport transport;", "old-facade"),
        ("old Win32 port", "Win32SerialPort port;", "old-facade"),
        ("legacy facade include", '#include "legacy/serial_transport.h"', "old-facade"),
        ("string cannot hide violation", 'const char* marker = "/*"; SerialTransport hidden;', "old-facade"),
        ("URL cannot hide violation", 'const char* uri = "serial://local"; HANDLE hidden;', "win32-api"),
    ]
    failures: list[str] = []
    if violations_for_text(TRANSPORT_SOURCE_DIR / "serial_rtu_transport.h", allowed):
        failures.append("allowed RTU/neutral sample produced a violation")
    for name, sample, expected_rule in forbidden:
        found = {
            violation.rule.rule_id
            for violation in violations_for_text(TRANSPORT_SOURCE_DIR / "fixture.h", sample)
        }
        if expected_rule not in found:
            failures.append(f"{name} sample was not rejected by {expected_rule}")
    path_cases = [
        ("old-facade", TRANSPORT_SOURCE_DIR / "serial_transport.h"),
        ("codec-dependency", TRANSPORT_SOURCE_DIR / "device_codec.cpp"),
        ("ui-dependency", TRANSPORT_SOURCE_DIR / "ui/presenter.h"),
        ("win32-dependency", TRANSPORT_SOURCE_DIR / "win32/adapter.cpp"),
        ("codec-dependency", TRANSPORT_SOURCE_DIR / "codec/value.cpp"),
        ("codec-dependency", TRANSPORT_SOURCE_DIR / "gray/value.cpp"),
        ("codec-dependency", TRANSPORT_SOURCE_DIR / "matcher/value.cpp"),
        ("codec-dependency", TRANSPORT_SOURCE_DIR / "device_codec/value.cpp"),
        ("ui-dependency", TRANSPORT_SOURCE_DIR / "native_ui/state.h"),
        ("win32-dependency", TRANSPORT_SOURCE_DIR / "win32_backend/adapter.cpp"),
        ("codec-dependency", TRANSPORT_SOURCE_DIR / "value_matcher/result.h"),
        ("upper-layer-analysis", TRANSPORT_SOURCE_DIR / "analysis/types.h"),
        ("storage-dependency", TRANSPORT_SOURCE_DIR / "native_storage/record.h"),
    ]
    for expected_rule, path in path_cases:
        found = {violation.rule.rule_id for violation in violations_for_text(path, "#pragma once")}
        if expected_rule not in found:
            failures.append(f"forbidden path was not rejected by {expected_rule}")
    if violations_for_text(TRANSPORT_SOURCE_DIR / "circuit_breaker.h", "#pragma once"):
        failures.append("allowed path circuit_breaker.h produced a violation")
    for suffix in (".inl", ".ipp", ".tpp"):
        if not is_transport_source(Path("fixture" + suffix)):
            failures.append(f"source extension is not scanned: {suffix}")
    try:
        violations_for_text(TRANSPORT_SOURCE_DIR / "fixture.h", "/*")
    except ValueError:
        pass
    else:
        failures.append("unterminated block comment did not fail closed")
    if failures:
        for failure in failures:
            print(f"Boundary self-test failure: {failure}", file=sys.stderr)
        return 1
    print("Boundary self-test: passed.")
    return 0


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    root = args.root.resolve()
    violations, errors = check_repository(root)
    for error in errors:
        print(f"transport boundary failure: {error}", file=sys.stderr)
    for violation in violations:
        print(
            f"transport boundary violation: {violation.path}:{violation.line_no}: "
            f"[{violation.rule.rule_id}] {violation.rule.message}; "
            f"remediation: {violation.rule.remediation}",
            file=sys.stderr,
        )
    if errors or violations:
        return 1
    print("Transport boundary check: passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
