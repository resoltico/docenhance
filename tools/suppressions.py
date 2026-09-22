# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Find every in-source lint, type-check, format and compiler-warning suppression.

A suppression is allowed only when it names the rules it silences and each rule is registered,
with an explanation, in tests/exceptions/registry.json. Blanket and file-wide forms never are.
C++ is lexed so that string literals can neither hide nor fake a marker; Python markers are read
from comment tokens only; CMake lines are scanned as written.
"""

from __future__ import annotations

import hashlib
import io
import re
import tokenize
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


@dataclass(frozen=True)
class Suppression:
    """One registered-or-not rule silenced at one line."""

    path: str
    line: int
    rule: str
    text: str = ""

    @property
    def key(self) -> str:
        """Registry key: path, tool-qualified rule and a digest of the suppressed line.

        Keyed by content rather than line number, so unrelated edits above a suppression do not
        invalidate its entry, while changing the suppressed line itself does require review.
        """
        digest = hashlib.sha256(" ".join(self.text.split()).encode()).hexdigest()[:8]
        return f"{self.path}:{self.rule}@{digest}"


@dataclass(frozen=True)
class Marker:
    """A suppression syntax. The rules group, when present, lists what is silenced."""

    pattern: re.Pattern[str]
    tool: str
    forbidden: str = ""
    needs_rules: bool = True
    # Match the unblanked line: the rules are themselves string literals, as in pragmas.
    raw: bool = False


@dataclass
class ScanResult:
    """Suppressions found in one file and the forms that are never allowed."""

    suppressions: list[Suppression]
    errors: list[str]


def _marker(
    pattern: str, tool: str, forbidden: str = "", *, needs_rules: bool = True, raw: bool = False
) -> Marker:
    return Marker(re.compile(pattern, re.IGNORECASE), tool, forbidden, needs_rules, raw)


CXX_MARKERS = (
    _marker(r"\bNOLINT(?:BEGIN|END)\b", "clang-tidy", "range-wide lint suppression"),
    _marker(r"\bNOLINT(?:NEXTLINE)?\b(?:\((?P<rules>[^)]*)\))?", "clang-tidy"),
    _marker(
        r"^\s*#\s*pragma\s+(?:clang|GCC)\s+diagnostic\s+(?:ignored|warning)\s+\"(?P<rules>[^\"]+)\"",
        "compiler",
        raw=True,
    ),
    _marker(r"^\s*#\s*pragma\s+warning\s*\(\s*(?:disable|suppress)\s*:(?P<rules>[\d\s]+)", "msvc"),
    _marker(r"^\s*#\s*pragma\s+(?:clang|GCC)\s+system_header", "compiler", "hides every warning"),
    _marker(r"\bclang-format\s+(?P<rules>off)\b", "clang-format"),
    _marker(r"\[\[\s*gsl::suppress\s*\(\s*\"?(?P<rules>[^\")]+)", "gsl", raw=True),
    _marker(
        r"\bno_sanitize\w*\b(?:\s*\(\s*\"(?P<rules>[^\"]+)\")?",
        "sanitizer",
        needs_rules=False,
        raw=True,
    ),
)
PYTHON_MARKERS = (
    _marker(r"#\s*(?:ruff|flake8)\s*:\s*noqa", "ruff", "file-wide lint suppression"),
    _marker(r"#\s*ruff\s*:\s*disable\b", "ruff", "range-wide lint suppression"),
    _marker(r"#\s*noqa\b(?:\s*:\s*(?P<rules>[A-Z]+\d+(?:\s*,\s*[A-Z]+\d+)*))?", "ruff"),
    _marker(r"#\s*type\s*:\s*ignore\b(?:\[(?P<rules>[^\]]*)\])?", "mypy"),
    _marker(r"#\s*mypy\s*:", "mypy", "inline mypy configuration"),
    _marker(r"#\s*(?:based)?pyright\s*:\s*ignore\b(?:\[(?P<rules>[^\]]*)\])?", "pyright"),
    _marker(r"#\s*pylint\s*:\s*disable\s*=\s*(?P<rules>[\w\-, ]+)", "pylint"),
    _marker(r"#\s*fmt\s*:\s*(?P<rules>off|skip)\b", "ruff-format"),
    _marker(r"#\s*isort\s*:\s*(?P<rules>skip|off)\b", "isort"),
    _marker(r"#\s*pragma\s*:\s*no\s*(?P<rules>cover|branch)\b", "coverage"),
)
CMAKE_MARKERS = (
    _marker(r"(?<![\w-])(?P<rules>-Wno-[\w=+-]+)", "compiler"),
    _marker(r"(?<![\w-])(?P<rules>-w)(?![\w-])", "compiler"),
    _marker(r"(?<![\w/])(?P<rules>/wd\s*\d+|/w)(?![\w])", "msvc"),
    _marker(r"\b(?P<rules>SKIP_LINTING)\b", "cmake"),
    _marker(r"target_include_directories\([^)]*\b(?P<rules>SYSTEM)\b", "cmake"),
)
NOLINT_NEXT = re.compile(r"\bNOLINTNEXTLINE\b")
RAW_STRING_OPEN = re.compile(r'(?<![\w])(?:u8|[uUL])?R"(?P<delimiter>[^()\\\s]{0,16})\(')


def _blank(chars: list[str], start: int, end: int) -> None:
    for index in range(start, end):
        if chars[index] != "\n":
            chars[index] = " "


def _quoted_end(text: str, start: int) -> int:
    """Index just past the literal opened at start (or the end of its line when unterminated)."""
    quote, index = text[start], start + 1
    while index < len(text) and text[index] not in (quote, "\n"):
        index += 2 if text[index] == "\\" else 1
    return min(index + 1, len(text))


def blank_cxx_literals(text: str) -> str:
    """Return text with string and character literal contents blanked, keeping comments."""
    chars, index = list(text), 0
    while index < len(text):
        if text.startswith("//", index):
            newline = text.find("\n", index)
            index = len(text) if newline < 0 else newline
        elif text.startswith("/*", index):
            close = text.find("*/", index + 2)
            index = len(text) if close < 0 else close + 2
        elif match := RAW_STRING_OPEN.match(text, index):
            terminator = ")" + match["delimiter"] + '"'
            close = text.find(terminator, match.end())
            end = len(text) if close < 0 else close + len(terminator)
            _blank(chars, match.end(), end)
            index = end
        elif text[index] == '"' or (text[index] == "'" and not text[index - 1 : index].isalnum()):
            end = _quoted_end(text, index)
            _blank(chars, index + 1, end - 1)
            index = end
        else:
            index += 1
    return "".join(chars)


def _match_line(
    path: str, number: int, text: str, markers: tuple[Marker, ...], code: str | None = None
) -> ScanResult:
    """Match markers on one line.

    With code (the literal-blanked line), non-raw markers match the code form, and raw markers
    must start outside any literal.
    """
    result = ScanResult([], [])
    for marker in markers:
        subject = text if code is None or marker.raw else code
        for match in marker.pattern.finditer(subject):
            if code is not None and marker.raw and code[match.start()] != text[match.start()]:
                continue
            where = f"{path}:{number}"
            if marker.forbidden:
                result.errors.append(
                    f"{where}: forbidden {marker.tool} suppression ({marker.forbidden})"
                )
                continue
            rules = [r.strip() for r in re.split(r"[,\s]+", match.groupdict().get("rules") or "")]
            rules = [r for r in rules if r]
            if not rules and marker.needs_rules:
                result.errors.append(f"{where}: blanket {marker.tool} suppression; name each rule")
                continue
            for rule in rules or ["*"]:
                rule_name = f"{marker.tool}/{rule}"
                result.suppressions.append(Suppression(path, number, rule_name, text))
    return result


def _merge(results: list[ScanResult]) -> ScanResult:
    merged = ScanResult([], [])
    for result in results:
        merged.suppressions.extend(result.suppressions)
        merged.errors.extend(result.errors)
    return merged


def scan_cxx(path: str, text: str) -> ScanResult:
    """Scan C++ markers and bind next-line exceptions to the code they actually suppress."""
    lines = text.splitlines()
    pairs = zip(lines, blank_cxx_literals(text).splitlines(), strict=True)
    results = []
    for number, (raw, blanked) in enumerate(pairs, 1):
        result = _match_line(path, number, raw, CXX_MARKERS, blanked)
        if NOLINT_NEXT.search(blanked) and result.suppressions:
            if number == len(lines):
                result.errors.append(f"{path}:{number}: next-line suppression has no target")
            else:
                result.suppressions = [
                    replace(item, text=f"{raw}\n{lines[number]}")
                    if item.rule.startswith("clang-tidy/")
                    else item
                    for item in result.suppressions
                ]
        results.append(result)
    return _merge(results)


def scan_python(path: str, text: str) -> ScanResult:
    """Scan Python comment tokens only, so strings and docstrings are never misread."""
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (tokenize.TokenError, SyntaxError) as exc:
        return ScanResult([], [f"{path}: cannot tokenize for suppression scan: {exc}"])
    comments = [t for t in tokens if t.type == tokenize.COMMENT]
    lines = text.splitlines()
    results = []
    for token in comments:
        result = _match_line(path, token.start[0], token.string, PYTHON_MARKERS)
        result.suppressions = [
            replace(item, text=lines[token.start[0] - 1]) for item in result.suppressions
        ]
        results.append(result)
    return _merge(results)


def scan_cmake(path: str, text: str) -> ScanResult:
    """Scan CMake lines for flags and properties that disable warnings or linting."""
    lines = text.splitlines()
    return _merge([_match_line(path, n, line, CMAKE_MARKERS) for n, line in enumerate(lines, 1)])


def scan(path: Path, rel: str, kind: str) -> ScanResult:
    """Scan one file of the given kind ("cxx", "python" or "build")."""
    text = path.read_text(encoding="utf-8")
    if kind == "cxx":
        return scan_cxx(rel, text)
    if kind == "python":
        return scan_python(rel, text)
    if path.name == "CMakeLists.txt" or path.suffix == ".cmake":
        return scan_cmake(rel, text)
    return ScanResult([], [])
