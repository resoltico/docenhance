# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bind supported suppression regions to their complete bounded source text."""

from __future__ import annotations

import re

FORMAT_SWITCH = re.compile(r"\bclang-format\s+(off|on)\b")
WARNING_SWITCH = re.compile(
    r"^\s*#\s*pragma\s+(?P<family>warning|(?:clang|GCC)\s+diagnostic)"
    r"\s*(?:\(\s*)?(?P<action>push|pop|disable|suppress|ignored|warning)\b"
)


def format_scope(lines: list[str], number: int, masked: list[str]) -> tuple[str, str | None]:
    """A format-off region requires a single matching on; all excluded bytes are bound."""
    for index in range(number, len(lines)):
        match = FORMAT_SWITCH.search(masked[index])
        if match:
            if match[1] == "on":
                return "\n".join(lines[number - 1 : index + 1]), None
            return "", "nested format-off suppression is forbidden"
    return "", "format-off suppression has no matching on"


def warning_scope(lines: list[str], number: int, masked: list[str]) -> tuple[str, str | None]:
    """Compiler warning disables require a preceding push and a bounded matching pop."""
    current = WARNING_SWITCH.match(masked[number - 1])
    if current and current["action"] == "suppress":
        if number < len(lines):
            return "\n".join(lines[number - 1 : number + 1]), None
        return "", "next-line compiler suppression has no target"
    push_index = next((index for index in range(number - 2, -1, -1) if masked[index].strip()), -1)
    pushed = WARNING_SWITCH.match(masked[push_index]) if push_index >= 0 else None
    if (
        not pushed
        or pushed["action"] != "push"
        or not current
        or pushed["family"] != current["family"]
    ):
        return "", "warning disable must immediately follow a scoped push"
    depth = 1
    for index in range(number, len(lines)):
        match = WARNING_SWITCH.match(masked[index])
        if match and match["family"] == current["family"]:
            depth += int(match["action"] == "push") - int(match["action"] == "pop")
            if depth == 0:
                return "\n".join(lines[push_index : index + 1]), None
    return "", "warning disable has no matching pop"
