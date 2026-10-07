# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Decode generated compiler commands using POSIX or Windows argument quoting."""

from __future__ import annotations

import shlex


def backslashes(command: str, index: int, *, quoted: bool) -> tuple[str, int, bool]:
    """Consume a backslash run and its optional quote under Windows rules."""
    end = index
    while end < len(command) and command[end] == "\\":
        end += 1
    count = end - index
    if end == len(command) or command[end] != '"':
        return "\\" * count, end, quoted
    literal = "\\" * (count // 2)
    return (literal + '"', end + 1, quoted) if count % 2 else (literal, end + 1, not quoted)


def windows_arguments(command: str) -> list[str]:
    """Decode Windows backslash/quote pairs without treating path backslashes as escapes."""
    arguments: list[str] = []
    word: list[str] = []
    quoted = started = False
    index = 0
    while index < len(command):
        char = command[index]
        if char in " \t" and not quoted:
            if started:
                arguments.append("".join(word))
                word, started = [], False
            index += 1
            continue
        started = True
        if char == "\\":
            literal, index, quoted = backslashes(command, index, quoted=quoted)
            word.extend(literal)
        elif char == '"':
            quoted = not quoted
            index += 1
        else:
            word.append(char)
            index += 1
    if quoted:
        message = "Generated Windows command has an unmatched quote"
        raise ValueError(message)
    if started:
        arguments.append("".join(word))
    return arguments


def command_arguments(command: str, *, windows: bool) -> list[str]:
    """Use the generator host's shell argument syntax."""
    return windows_arguments(command) if windows else shlex.split(command)
