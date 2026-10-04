# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reading a configured build: its compilation database, and what the compiler does with it.

The architecture rules in tools/architecture_build.py are only as honest as their source of truth,
which is the build itself rather than the source text. This module is that source: which
translation units are first-party, what the compiler is invoked with, which headers it really
reads, and where the third-party headers of this build live.
"""

from __future__ import annotations

import json
import re
import shlex
import subprocess
from pathlib import Path

from architecture import ArchitectureError, Manifest
from deps import ROOT


def layer_of(manifest: Manifest, path: str) -> str | None:
    """The layer a first-party path belongs to, or None if it is not first-party."""
    resolved = Path(path).resolve()
    for base in (ROOT.resolve() / "include/docenhance", ROOT.resolve() / "src"):
        if resolved.is_relative_to(base):
            parts = resolved.relative_to(base).parts
            candidate = parts[0] if parts else ""
            return candidate if candidate in manifest.layers else None
    return None


def compilation_database(manifest: Manifest, build: Path) -> list[dict[str, str]]:
    """Load the build's compilation database, keeping the first-party translation units."""
    path = build / "compile_commands.json"
    if not path.is_file():
        msg = f"No compilation database at {path}; configure a build first (docs/build.md)"
        raise ArchitectureError(msg)
    entries: list[dict[str, str]] = json.loads(path.read_text(encoding="utf-8"))
    return [entry for entry in entries if layer_of(manifest, entry["file"]) is not None]


def compiler_arguments(entry: dict[str, str]) -> list[str]:
    """The compile command without its output, its compile flag and its source file."""
    arguments, skip = [], False
    for argument in shlex.split(entry["command"]):
        if skip:
            skip = False
            continue
        if argument in {"-o", "-c"} or argument == entry["file"]:
            skip = argument == "-o"
            continue
        arguments.append(argument)
    return arguments


def run_compiler(
    arguments: list[str], directory: str, what: str
) -> subprocess.CompletedProcess[str]:
    """Run a compiler invocation that must succeed, and return its captured streams."""
    result = subprocess.run(arguments, cwd=directory, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        msg = f"{what}: {(result.stderr or result.stdout).strip()[-400:]}"
        raise ArchitectureError(msg)
    return result


def package_roots(entry: dict[str, str], build: Path) -> list[str]:
    """The dependency include directories this build set up, where third-party headers live."""
    # The superbuild installs every dependency into a private prefix beside the build tree, so
    # those are the directories a third-party header can come from. A toolchain's own include
    # directories are somewhere else entirely and are not searched for package membership.
    bases = (ROOT.resolve(), *build.resolve().parents[:2])
    arguments = compiler_arguments(entry)
    candidates = [
        (Path(entry["directory"]) / arguments[index + 1]).resolve()
        for index, argument in enumerate(arguments[:-1])
        if argument == "-isystem"
    ]
    return [str(path) for path in candidates if any(path.is_relative_to(base) for base in bases)]


def included_headers(entry: dict[str, str]) -> list[str]:
    """Every header the compiler reads for this translation unit, transitively."""
    output = run_compiler(
        [*compiler_arguments(entry), entry["file"], "-H", "-fsyntax-only"],
        entry["directory"],
        f"Cannot read the includes of {entry['file']}",
    )
    # Unlike Make dependency output, the compiler's trace retains spaces and dollar signs.
    names = re.findall(r"^\.+ (.+)$", output.stderr, flags=re.MULTILINE)
    paths = [(Path(entry["directory"]) / name).resolve() for name in names]
    if any(not path.is_file() for path in paths):
        msg = f"Compiler include trace contains an unidentified file: {entry['file']}"
        raise ArchitectureError(msg)
    return [str(path) for path in paths]
