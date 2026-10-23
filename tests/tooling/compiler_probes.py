# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real C++ fixture commands sharing the declared compiler/SDK role and pinned AST engine."""

from __future__ import annotations

import hashlib
import json
import platform
import re
import shutil
import subprocess
from pathlib import Path

from tools_path import ROOT

import architecture_api
from architecture import ArchitectureError


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
    kind = observed_compiler_id(compiler)
    (root / "build-identity.json").write_text(
        json.dumps(
            {
                "CMAKE_CXX_COMPILER": str(compiler.resolve()),
                "CMAKE_CXX_COMPILER_ID": kind,
                "CXX_compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
            }
        ),
        encoding="utf-8",
    )
    source_name = source.resolve().as_posix()
    entry = {
        "directory": root.resolve().as_posix(),
        "compiler_id": kind,
        "file": source_name,
        "command": f'"{compiler.as_posix()}" -std=c++23{sdk_flags} -c "{source_name}"',
    }
    (root / "compile_commands.json").write_text(
        json.dumps([{key: value for key, value in entry.items() if key != "compiler_id"}]),
        encoding="utf-8",
    )
    return entry


def observed_compiler_id(compiler: str | Path) -> str:
    """Use one actual driver observation for this compiler-backed fixture's trace kind."""
    result = subprocess.run(
        [str(compiler), "--version"], capture_output=True, text=True, check=True, timeout=15
    )
    if re.search(r"\bApple clang version\b", result.stdout):
        return "AppleClang"
    if re.search(r"\bclang version\b", result.stdout):
        return "Clang"
    if "Free Software Foundation" in result.stdout:
        return "GNU"
    message = "Compiler fixture cannot identify its actual driver emitter"
    raise ArchitectureError(message)


def available_compiler() -> tuple[str, str]:
    """Select an available native GNU-style driver and observe its actual trace format."""
    major = json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"].split(".")[
        0
    ]
    compiler = shutil.which(f"clang++-{major}") or shutil.which("clang++") or shutil.which("g++")
    if compiler is None:
        message = "Compiler-backed controls require a native C++ driver"
        raise ArchitectureError(message)
    return compiler, observed_compiler_id(compiler)
