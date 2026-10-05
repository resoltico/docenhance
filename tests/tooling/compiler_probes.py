# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real C++ fixture commands sharing the declared compiler/SDK role and pinned AST engine."""

from __future__ import annotations

import json
import platform
import subprocess
from pathlib import Path

import architecture_api


def fixture_compiler(system: str, query: Path) -> Path:
    """Match the supported native compiler role; macOS analysis tools need no LLVM compiler."""
    if system == "Darwin":
        return Path("/usr/bin/clang++")
    return query.resolve().with_name("clang++.exe" if system == "Windows" else "clang++")


def compiler_probe(root: Path, source: Path, text: str) -> dict[str, str]:
    """Use a supported real compiler with the pinned AST engine, without executing a probe."""
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text(text, encoding="utf-8")
    query = Path(architecture_api.find_clang_query()).resolve()
    compiler = fixture_compiler(platform.system(), query)
    sdk_flags = ""
    if platform.system() == "Darwin":
        sdk = subprocess.check_output(
            ["/usr/bin/xcrun", "--sdk", "macosx", "--show-sdk-path"],
            text=True,
        ).strip()
        sdk_flags = f' -isysroot "{sdk}"'
    source_name = source.resolve().as_posix()
    entry = {
        "directory": root.resolve().as_posix(),
        "file": source_name,
        "command": f'"{compiler.as_posix()}" -std=c++23{sdk_flags} -c "{source_name}"',
    }
    (root / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    return entry
