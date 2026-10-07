# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Evaluate all manifest AST predicates in two traversals, retaining overlapping rule matches."""

from __future__ import annotations

import re

from architecture import ArchitectureError

SUMMARY = re.compile(r"^(\d+) match(?:es)?\.$", re.MULTILINE)
MATCH_START = re.compile(r"^Match #\d+:\s*$", re.MULTILINE)
RULE_NOTE = re.compile(r'^.*:\d+:\d+: note: "rule_(\d+)" binds here$', re.MULTILINE)
STATEMENT_ROOTS = frozenset(
    {"cxxThrowExpr", "declRefExpr", "cxxConstructExpr", "expr", "cxxCatchStmt"}
)


def commands(rules: list[tuple[str, str]]) -> list[str]:
    """Combine compatible roots with eachOf, which preserves every matching alternative."""
    groups: dict[str, list[str]] = {"stmt": [], "decl": []}
    for index, (_, matcher) in enumerate(rules):
        root = matcher.partition("(")[0]
        if root == "namespaceDecl":
            kind = "decl"
        elif root in STATEMENT_ROOTS:
            kind = "stmt"
        else:
            message = f"Unsupported architecture matcher root: {root}"
            raise ArchitectureError(message)
        groups[kind].append(f'{matcher}.bind("rule_{index}")')
    result: list[str] = []
    for kind, parts in groups.items():
        if parts:
            inner = parts[0] if len(parts) == 1 else f"eachOf({','.join(parts)})"
            result.extend(("-c", f"match {kind}({inner})"))
    return result


def violations(source: str, rules: list[tuple[str, str]], output: str) -> list[str]:
    """Reconcile every query result and binding before reporting rule-specific diagnostics."""
    expected = len(commands(rules)) // 2
    counts = SUMMARY.findall(output)
    if len(counts) != expected:
        message = f"clang-query answered {len(counts)} of {expected} rule groups on {source}"
        raise ArchitectureError(message)
    reports: dict[int, list[str]] = {}
    matched = 0
    for block in SUMMARY.split(output)[:-1:2]:
        for report in MATCH_START.split(block)[1:]:
            bindings = RULE_NOTE.findall(report)
            if len(bindings) != 1 or int(bindings[0]) >= len(rules):
                message = f"clang-query returned an unattributed architecture match on {source}"
                raise ArchitectureError(message)
            matched += 1
            reports.setdefault(int(bindings[0]), []).append(report.strip())
    if matched != sum(int(count) for count in counts):
        message = f"clang-query match counts and rule bindings differ on {source}"
        raise ArchitectureError(message)
    return [
        f"{source}: {rule}\n" + "\n\n".join(reports[index])
        for index, (rule, _) in enumerate(rules)
        if index in reports
    ]
