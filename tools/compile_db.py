# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Reading a configured build: its compilation database, and what the compiler does with it.

The architecture rules in tools/architecture_build.py are only as honest as their source of truth,
which is the build itself rather than the source text. This module is that source: which
translation units are first-party, what the compiler is invoked with, which headers it really
reads, and where the third-party headers of this build live.
"""

from __future__ import annotations

import json
import shlex
import subprocess
from typing import TYPE_CHECKING

from architecture import ArchitectureError, Manifest
from deps import ROOT

if TYPE_CHECKING:
    from pathlib import Path


def layer_of(manifest: Manifest, path: str) -> str | None:
    """The layer a first-party path belongs to, or None if it is not first-party."""
    # Only inside this checkout: a directory named src anywhere else is somebody else's code.
    if not path.startswith(f"{ROOT}/"):
        return None
    for marker in (f"{ROOT}/include/docenhance/", f"{ROOT}/src/"):
        if path.startswith(marker):
            candidate = path[len(marker) :].split("/", 1)[0]
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


def run_compiler(arguments: list[str], directory: str, what: str) -> str:
    """Run a compiler invocation that must succeed, and return its output."""
    result = subprocess.run(arguments, cwd=directory, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        msg = f"{what}: {(result.stderr or result.stdout).strip()[:400]}"
        raise ArchitectureError(msg)
    return result.stdout


def package_roots(entry: dict[str, str], build: Path) -> list[str]:
    """The dependency include directories this build set up, where third-party headers live."""
    # The superbuild installs every dependency into a private prefix beside the build tree, so
    # those are the directories a third-party header can come from. A toolchain's own include
    # directories are somewhere else entirely and are not searched for package membership.
    bases = tuple(f"{base}/" for base in (ROOT, *build.parents[:2]))
    arguments = compiler_arguments(entry)
    return [
        arguments[index + 1].rstrip("/")
        for index, argument in enumerate(arguments[:-1])
        if argument == "-isystem" and arguments[index + 1].startswith(bases)
    ]


def included_headers(entry: dict[str, str]) -> list[str]:
    """Every header the compiler reads for this translation unit, transitively."""
    output = run_compiler(
        [*compiler_arguments(entry), entry["file"], "-M", "-MG"],
        entry["directory"],
        f"Cannot read the dependencies of {entry['file']}",
    )
    _, _, dependencies = output.replace("\\\n", " ").partition(":")
    return dependencies.split()
