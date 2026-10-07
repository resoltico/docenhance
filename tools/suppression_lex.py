# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Position-preserving C++ literal/comment masks for suppression syntax and scope binding."""

from __future__ import annotations

import re

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


def blank_cxx_comments(text: str) -> str:
    """Blank comments after masking literal contents, preserving every coordinate and newline."""
    chars = list(text)
    for match in re.finditer(r"//[^\n]*|/\*.*?(?:\*/|$)", text, re.DOTALL):
        _blank(chars, match.start(), match.end())
    return "".join(chars)
