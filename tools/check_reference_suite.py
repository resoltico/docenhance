#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Compile and run the dependency-free reference suites with any compiler.

Which sources those are is not a list kept here: it is every layer that spec/architecture.json
says uses no third-party package. CMake generates actual build facts from the shared template.
The suite grows with the project instead of drifting from it, and proves that the layers below the
adapter really are free of third-party code.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from architecture import load_manifest
from deps import ROOT
from project_version import project_version

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
    sources: list[str] = []
    for layer in sorted(free):
        sources.extend(
            path.relative_to(ROOT).as_posix()
            for path in sorted((ROOT / "src" / layer).glob("*.cpp"))
        )
    return sources


def generate_build_facts(directory: Path, compiler: str, minimum: str) -> Path:
    """Use real CMake compiler detection and the production version template; no fake provider."""
    source = directory / "configuration"
    source.mkdir()
    generated = directory / "generated"
    (generated / "docenhance").mkdir(parents=True)
    (source / "CMakeLists.txt").write_text(
        f"cmake_minimum_required(VERSION {minimum})\n"
        "project(Reference LANGUAGES CXX)\n"
        'set(PROJECT_VERSION "${DE_REFERENCE_VERSION}")\n'
        'set(DE_LOCK_SHA256 "${DE_REFERENCE_LOCK}")\n'
        'configure_file("${DE_REFERENCE_TEMPLATE}" "${DE_REFERENCE_OUTPUT}" @ONLY)\n',
        encoding="utf-8",
    )
    cmake = shutil.which("cmake")
    if cmake is None:
        msg = "Reference build-facts generation requires CMake"
        raise RuntimeError(msg)
    lock_digest = hashlib.sha256((ROOT / "deps/lock.json").read_bytes()).hexdigest()
    subprocess.run(
        [
            cmake,
            "-S",
            str(source),
            "-B",
            str(directory / "configuration-build"),
            "-G",
            "Ninja",
            f"-DCMAKE_CXX_COMPILER:FILEPATH={compiler}",
            f"-DDE_REFERENCE_VERSION:STRING={project_version()}",
            f"-DDE_REFERENCE_LOCK:STRING={lock_digest}",
            f"-DDE_REFERENCE_TEMPLATE:FILEPATH={ROOT / 'cmake/version.hpp.in'}",
            f"-DDE_REFERENCE_OUTPUT:FILEPATH={generated / 'docenhance/version.hpp'}",
        ],
        check=True,
    )
    return generated


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
    tools = json.loads((ROOT / "deps/tools.json").read_text())
    standard = tools["cxx"]["standard"]
    flags = [
        f"-std=c++{standard}",
        "-Werror",
        *STRICT_WARNINGS,
        "-fno-fast-math",
        "-ffp-contract=off",
    ]
    if args.sanitize:
        flags += [
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer",
            "-g",
        ]
    with tempfile.TemporaryDirectory(prefix="docenhance-reference-") as temp:
        directory = Path(temp)
        generated = generate_build_facts(directory, compiler, tools["cmake"]["minimum"])
        exe = directory / "reference-tests"
        cmd = [
            compiler,
            *flags,
            "-I",
            str(ROOT / "include"),
            "-I",
            str(ROOT / "tests/support"),
            "-I",
            str(generated),
            *[str(ROOT / s) for s in sources],
            "-o",
            str(exe),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
