# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
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

from size_policy import RUFF_CHECKS
from suppression_lex import blank_cxx_comments, blank_cxx_literals
from suppression_scopes import format_scope, warning_scope

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
        """Registry key: path, tool-qualified rule and SHA256 of the complete bound scope.

        Preserved content rather than line numbers binds same-line/next-line targets, complete
        compiler/formatter regions, or configuration scopes. Changed bound bytes require review.
        """
        digest = hashlib.sha256(self.text.encode("utf-8")).hexdigest()
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
    _marker(r"\bsafebuffers\b", "compiler", "stack protection opt-out", raw=True),
    _marker(
        r"^\s*#\s*pragma\s+warning\s*\(\s*push\s*,", "compiler", "warning-level override", raw=True
    ),
    _marker(r"\bNOLINT(?:BEGIN|END)\b", "clang-tidy", "range-wide lint suppression"),
    _marker(r"\bNOLINT(?:NEXTLINE)?\b(?:\((?P<rules>[^)]*)\))?", "clang-tidy"),
    _marker(
        r"^\s*#\s*pragma\s+(?:clang|GCC)\s+diagnostic\s+(?:ignored|warning)\s+\"(?P<rules>[^\"]+)\"",
        "compiler",
        raw=True,
    ),
    _marker(r"^\s*#\s*pragma\s+warning\s*\(\s*(?:disable|suppress)\s*:(?P<rules>[\d\s]+)", "msvc"),
    _marker(
        r"^\s*#\s*pragma\s+(?:clang|GCC)\s+system_header",
        "compiler",
        "hides every warning",
        raw=True,
    ),
    _marker(r"\bclang-format\s+(?P<rules>off)\b", "clang-format"),
    _marker(r"\b(?:_Pragma|__pragma)\s*\(", "compiler", "hidden compiler control", raw=True),
    _marker(
        r"\bdisable_sanitizer_instrumentation\b", "sanitizer", "sanitizer instrumentation opt-out"
    ),
    _marker(r"\[\[\s*gsl::suppress\s*\(\s*\"?(?P<rules>[^\")]+)", "gsl", raw=True),
    _marker(
        r"\bno_sanitize\w*\b(?:\s*\(\s*\"(?P<rules>[^\"]+)\")?",
        "sanitizer",
        forbidden="sanitizer instrumentation opt-out",
        raw=True,
    ),
)
PYTHON_MARKERS = (
    _marker(r"#\s*advisory-review\s*:\s*(?P<rules>[A-Za-z0-9._-]+)", "advisory"),
    _marker(r"#\s*(?:ruff|flake8)\s*:\s*noqa", "ruff", "file-wide lint suppression"),
    _marker(r"#\s*ruff\s*:\s*disable\b", "ruff", "range-wide lint suppression"),
    _marker(r"#\s*noqa\b(?:\s*:\s*(?P<rules>[A-Z]+\d+(?:\s*,\s*[A-Z]+\d+)*))?", "ruff"),
    _marker(r"#\s*type\s*:\s*ignore\b(?:\[(?P<rules>[^\]]*)\])?", "mypy"),
    _marker(r"#\s*mypy\s*:", "mypy", "inline mypy configuration"),
    _marker(r"#\s*(?:based)?pyright\s*:\s*ignore\b(?:\[(?P<rules>[^\]]*)\])?", "pyright"),
    _marker(r"#\s*pylint\s*:\s*disable\s*=\s*(?P<rules>[\w\-, ]+)", "pylint"),
    _marker(r"#\s*fmt\s*:\s*off\b", "ruff-format", "range-wide formatter suppression"),
    _marker(r"#\s*fmt\s*:\s*(?P<rules>skip)\b", "ruff-format"),
    _marker(
        r"#\s*isort\s*:\s*(?:off|skip_file)\b", "isort", "range/file-wide formatter suppression"
    ),
    _marker(r"#\s*isort\s*:\s*(?P<rules>skip)\b", "isort"),
    _marker(r"#\s*pragma\s*:\s*no\s*(?P<rules>cover|branch)\b", "coverage"),
)
CMAKE_MARKERS = (
    _marker(
        r"(?<![\w-])(?:-w(?![\w-])|-Wno-(?:error|everything)(?![\w=+-]))",
        "compiler",
        "blanket warning opt-out",
    ),
    _marker(
        r"(?<![\w/])/(?:w0\d+|(?:w0|wx-|w)(?![\w-]))", "msvc", "warning level or fatality opt-out"
    ),
    _marker(r"(?<![\w-])(?P<rules>-Wno-(?!(?:error|everything)(?![\w=+-]))[\w=+-]+)", "compiler"),
    _marker(r"(?<![\w/])(?P<rules>/wd\s*\d+)(?![\w])", "msvc"),
    _marker(r"target_include_directories\([^)]*\b(?P<rules>SYSTEM)\b", "cmake"),
)
NOLINT_NEXT = re.compile(r"\bNOLINTNEXTLINE\b")


def marker_active(
    marker: Marker, match: re.Match[str], text: str, code: tuple[str, str] | None
) -> bool:
    """Identify lint comments and compiler controls outside literal/comment contents."""
    if code is None:
        return True
    position = match.start()
    while position < match.end() and text[position].isspace():
        position += 1
    if marker.raw:
        return code[1][position] == text[position]
    return marker.tool not in {"clang-tidy", "clang-format"} or code[1][position] != text[position]


def forbidden_rule(tool: str, rule: str) -> str | None:
    """Blanket warnings and configured body/complexity checks cannot be waived."""
    if tool == "compiler" and rule == "-Weverything":
        return "blanket warning opt-out cannot be approved"
    if tool == "clang-tidy" and rule in {
        "readability-function-size",
        "readability-function-cognitive-complexity",
    }:
        return "function body size limits have no waivers"
    if tool == "ruff" and rule in RUFF_CHECKS:
        return "Python size and complexity limits have no waivers"
    return None


def _match_line(
    path: str,
    number: int,
    text: str,
    markers: tuple[Marker, ...],
    code: tuple[str, str] | None = None,
) -> ScanResult:
    """Match markers only at effective comment/compiler positions on one line."""
    result = ScanResult([], [])
    for marker in markers:
        subject = text if code is None or marker.raw else code[0]
        for match in marker.pattern.finditer(subject):
            if not marker_active(marker, match, text, code):
                continue
            where = f"{path}:{number}"
            if marker.forbidden:
                result.errors.append(
                    f"{where}: forbidden {marker.tool} suppression ({marker.forbidden})"
                )
                continue
            rules = [r.strip() for r in re.split(r"[,\s]+", match.groupdict().get("rules") or "")]
            rules = [r for r in rules if r]
            if any(rule.lower() == "all" for rule in rules):
                result.errors.append(f"{where}: blanket {marker.tool} suppression is forbidden")
                continue
            if not rules and marker.needs_rules:
                result.errors.append(f"{where}: blanket {marker.tool} suppression; name each rule")
                continue
            for rule in rules or ["*"]:
                if rules and any(character in rule for character in "*?[]"):
                    result.errors.append(
                        f"{where}: wildcard {marker.tool} suppression; name each exact rule"
                    )
                    continue
                if budget_error := forbidden_rule(marker.tool, rule):
                    result.errors.append(f"{where}: {budget_error}")
                    continue
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
    masked = blank_cxx_literals(text)
    literal_lines = masked.splitlines()
    code_lines = blank_cxx_comments(masked).splitlines()
    pairs = zip(lines, literal_lines, strict=True)
    results = []
    for number, (raw, blanked) in enumerate(pairs, 1):
        result = _match_line(path, number, raw, CXX_MARKERS, (blanked, code_lines[number - 1]))
        control = code_lines[number - 1]
        if "##" in control or control.rstrip().endswith("\\"):
            result.errors.append(
                f"{path}:{number}: token-pasted or continued compiler control is not admitted"
            )
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
        bounded = []
        for item in result.suppressions:
            binding = None
            bound_item = item
            if item.rule == "clang-format/off":
                binding = format_scope(lines, number, literal_lines)
            elif item.rule.startswith(("msvc/", "compiler/")):
                binding = warning_scope(lines, number, code_lines)
            if binding:
                text_bound, error = binding
                if error:
                    result.errors.append(f"{path}:{number}: {error}")
                    continue
                bound_item = replace(item, text=text_bound)
            bounded.append(bound_item)
        result.suppressions = bounded
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
