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
import os
import re
import shlex
import subprocess
from pathlib import Path

from architecture import ArchitectureError, Manifest
from deps import ROOT, digest_file


class LayerRoots:
    """Canonical classification roots for one compiler trace, never cached across checks."""

    def __init__(self, root: Path | None = None) -> None:
        """Resolve the repository root once for this classification scope."""
        canonical = (ROOT if root is None else root).resolve()
        self.bases = (canonical / "include/docenhance", canonical / "src")

    def layer_of(self, manifest: Manifest, path: str) -> str | None:
        """Resolve each observed path freshly and classify it with native path comparisons."""
        resolved = Path(path).resolve()
        for base in self.bases:
            try:
                common = os.path.commonpath((resolved, base))
            except ValueError:  # Different Windows drives have no common root.
                continue
            if os.path.normcase(common) == os.path.normcase(str(base)):
                parts = resolved.parts[len(base.parts) :]
                candidate = parts[0] if parts else ""
                return candidate if candidate in manifest.layers else None
        return None


def layer_of(manifest: Manifest, path: str) -> str | None:
    """The layer a first-party path belongs to, or None if it is not first-party."""
    return LayerRoots().layer_of(manifest, path)


def compilation_database(manifest: Manifest, build: Path) -> list[dict[str, str]]:
    """Load the build's compilation database, keeping the first-party translation units."""
    return [
        entry for entry in compiler_entries(build) if layer_of(manifest, entry["file"]) is not None
    ]


def compiler_entries(build: Path) -> list[dict[str, str]]:
    """Bind every CDB driver to its exact directory's configured compiler identity once."""
    configuration = json.loads((build / "build-identity.json").read_text(encoding="utf-8"))
    kind = configuration["CMAKE_CXX_COMPILER_ID"]
    compiler = Path(configuration["CMAKE_CXX_COMPILER"]).resolve(strict=True)
    if kind not in {"GNU", "Clang", "AppleClang"} or (
        digest_file(compiler) != configuration["CXX_compiler_sha256"]
    ):
        message = "Configured compiler identity is unsupported or changed"
        raise ArchitectureError(message)
    entries: list[dict[str, str]] = json.loads(
        (build / "compile_commands.json").read_text(encoding="utf-8")
    )
    for entry in entries:
        driver = Path(entry["directory"]) / compiler_arguments(entry)[0]
        if driver.resolve(strict=True) != compiler:
            message = "Compilation database driver differs from its configured CXX owner"
            raise ArchitectureError(message)
        entry["compiler_id"] = kind
    return entries


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


def included_headers(entry: dict[str, str], *, syntax: bool = True) -> list[tuple[str, str]]:
    """Compiler-spelled and resolved identities of every transitively read header."""
    mode = ["-fsyntax-only"] if syntax else ["-E", "-o", os.devnull]
    output = run_compiler(
        [*compiler_arguments(entry), entry["file"], "-H", *mode],
        entry["directory"],
        f"Cannot read the includes of {entry['file']}",
    )
    return header_trace(output.stderr, entry["directory"], entry["file"], entry["compiler_id"])


def header_trace(
    output: str, directory: str, source: str, compiler_id: str
) -> list[tuple[str, str]]:
    """Resolve a real compiler -H trace, including literal spaces and dollar signs."""
    consumed, names = header_trace_values(output, compiler_id)
    if consumed:
        path = (Path(directory) / consumed).resolve()
        msg = f"Cannot audit consumed precompiled header for {source}: {path}"
        raise ArchitectureError(msg)
    paths = [(Path(directory) / name).resolve() for name in names]
    if any(not path.is_file() for path in paths):
        msg = f"Compiler include trace contains an unidentified file: {source}"
        raise ArchitectureError(msg)
    spellings = [name if Path(name).is_absolute() else f"{directory}/{name}" for name in names]
    return list(zip(spellings, (str(path) for path in paths), strict=True))


def header_trace_values(output: str, compiler_id: str) -> tuple[str | None, list[str]]:
    """Decode the existing -H text without opening or resolving any observed file."""
    if compiler_id not in {"GNU", "Clang", "AppleClang"}:
        message = "Unsupported compiler include-trace emitter"
        raise ArchitectureError(message)
    consumed = re.search(r"^! (.+)$", output, flags=re.MULTILINE)
    names = re.findall(r"^\.+ (.+)$", output, flags=re.MULTILINE)
    return consumed.group(1) if consumed else None, [
        _header_name(name, compiler_id) for name in names
    ]


def _header_name(name: str, compiler_id: str) -> str:
    """Invert only Clang Stringify transport once; GNU paths are already literal bytes."""
    if compiler_id == "GNU":
        return name
    try:
        decoded: str = json.loads('"' + name + '"', strict=False)
    except json.JSONDecodeError as error:
        message = "Malformed Clang include-trace filename escaping"
        raise ArchitectureError(message) from error
    # Stringify escapes backslash/quote and collapses CR/LF to \n. The latter is ambiguous.
    encoded = decoded.replace("\\", "\\\\").replace('"', '\\"')
    if "\n" in decoded or "\r" in decoded or encoded != name:
        message = "Ambiguous or noncanonical Clang include-trace filename escaping"
        raise ArchitectureError(message)
    return decoded
