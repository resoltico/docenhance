#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Compile and run the dependency-free reference suites with any compiler.

Which sources those are is not a list kept here: it is every layer that spec/architecture.json
says uses no third-party package and whose sources need nothing the build generates. The suite
therefore grows with the project instead of drifting from it, and proves that the layers below the
adapter really are free of third-party code.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

from architecture import GENERATED_INCLUDES, load_manifest
from deps import ROOT

# Mirrors DE_STRICT_WARNINGS in cmake/ProjectOptions.cmake; check_project.py keeps them identical.
STRICT_WARNINGS = [
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wshadow",
    "-Wformat=2",
    "-Wold-style-cast",
    "-Wcast-align",
    "-Wcast-qual",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    "-Wdouble-promotion",
    "-Wimplicit-fallthrough",
    "-Wmissing-declarations",
    "-Wundef",
    "-Wextra-semi",
]


def reference_sources() -> list[str]:
    """Every first-party source the reference suite can compile on its own."""
    manifest = load_manifest()
    free = {name for name in manifest.layers if not manifest.package_reach(name)}
    sources = []
    for layer in sorted(free):
        for path in sorted((ROOT / "src" / layer).glob("*.cpp")):
            text = path.read_text(encoding="utf-8")
            if any(generated in text for generated in GENERATED_INCLUDES):
                continue
            sources.append(path.relative_to(ROOT).as_posix())
    return sources


def main() -> int:
    """Build and run the reference suites with the strict warning set."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("Compiler not found")
    sources = [*reference_sources(), "tests/support/standalone_main.cpp"]
    flags = ["-std=c++23", "-Werror", *STRICT_WARNINGS, "-fno-fast-math", "-ffp-contract=off"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    with tempfile.TemporaryDirectory(prefix="docenhance-reference-") as temp:
        exe = Path(temp) / "reference-tests"
        cmd = [
            compiler,
            *flags,
            "-I",
            str(ROOT / "include"),
            "-I",
            str(ROOT / "tests/support"),
            *[str(ROOT / s) for s in sources],
            "-o",
            str(exe),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
