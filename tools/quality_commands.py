# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Frozen process argv syntax and output-option ownership."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def command_options(command: list[str], source: str, executable: Path) -> tuple[str, list[str]]:
    """A role freezes full argv; replay substitutes only the output destination."""
    if command[:3] != [str(executable), "process", source] or command[-1] != "--json":
        message = "Frozen command must bind selected executable, process source and JSON delivery"
        raise ValueError(message)
    options = command[3:-1]
    names = options[::2]
    if len(options) % 2 or len(set(names)) != len(names) or names.count("--out-dir") != 1:
        message = "Frozen command requires unique option/value pairs and one output directory"
        raise ValueError(message)
    index = names.index("--out-dir") * 2
    return options[index + 1], options[:index] + options[index + 2 :]
