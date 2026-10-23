#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Compile and run the dependency-free reference suites with any compiler.

The complete pure-owner source partition stays selected when library edges change. Actual compiler
headers and a closed fresh-object link prove independence. CMake generates real build facts and
toolchain search roots; execution reconciles all ten independent suite callbacks.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from architecture import ArchitectureError
from deps import ROOT
from parallel import MAX_JOBS, ordered_map
from project_version import project_version
from reference_closure import (
    STRICT_WARNINGS,
    observe_headers,
    reconcile_suites,
    system_roots,
    validate_flags,
    verify_gnu_pch,
)

REFERENCE_LAYERS = ("app", "core", "exec", "image", "methods", "record")
REFERENCE_EXCLUSIONS = {
    "src/methods/restoration_working.cpp",
    "src/record/json.cpp",
    "src/record/inventory.cpp",
}


def reference_sources() -> list[str]:
    """Complete pure owners; native orchestration and JSON I/O stay separately exercised."""
    sources: list[str] = []
    for layer in REFERENCE_LAYERS:
        sources.extend(
            path.relative_to(ROOT).as_posix()
            for path in sorted((ROOT / "src" / layer).glob("*.cpp"))
            if path.relative_to(ROOT).as_posix() not in REFERENCE_EXCLUSIONS
        )
    return sources


def validate_source_inventory(sources: list[Path]) -> None:
    """A production reference link cannot omit an owner, duplicate it or add a foreign object."""
    production = (ROOT / "src").resolve()
    canonical = [source.resolve() for source in sources]
    if any(source.is_relative_to(production) for source in canonical):
        expected = {ROOT / source for source in reference_sources()}
        expected.add(ROOT / "tests/support/standalone_main.cpp")
        if set(canonical) != expected or len(canonical) != len(expected):
            msg = "Reference source inventory differs from the complete owner partition"
            raise ArchitectureError(msg)


def check_environment() -> None:
    """Reuse the production compiler/search/link injection refusal before each build."""
    cmake = shutil.which("cmake")
    if cmake is None:
        msg = "Reference environment verification requires CMake"
        raise RuntimeError(msg)
    subprocess.run([cmake, "-P", str(ROOT / "cmake/BuildEnvironment.cmake")], check=True)


def generate_build_facts(directory: Path, compiler: str, minimum: str) -> Path:
    """Use real CMake compiler detection and the production version template; no fake provider."""
    source = directory / "configuration"
    source.mkdir()
    generated = directory / "generated"
    (generated / "docenhance").mkdir(parents=True)
    (source / "CMakeLists.txt").write_text(
        f"cmake_minimum_required(VERSION {minimum})\n"
        'include("${DE_REFERENCE_ENVIRONMENT}")\n'
        "project(Reference LANGUAGES CXX)\n"
        'set(PROJECT_VERSION "${DE_REFERENCE_VERSION}")\n'
        'set(DE_LOCK_SHA256 "${DE_REFERENCE_LOCK}")\n'
        'configure_file("${DE_REFERENCE_TEMPLATE}" "${DE_REFERENCE_OUTPUT}" @ONLY)\n'
        'file(WRITE "${DE_REFERENCE_ROOTS}" "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES};'
        '${CMAKE_OSX_SYSROOT};${CMAKE_SYSROOT}")\n'
        'file(WRITE "${DE_REFERENCE_COMPILER_ID}" "${CMAKE_CXX_COMPILER_ID}")\n'
        'file(WRITE "${DE_REFERENCE_SIMULATE_ID}" "${CMAKE_CXX_SIMULATE_ID}")\n',
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
            f"-DDE_REFERENCE_ENVIRONMENT:FILEPATH={ROOT / 'cmake/BuildEnvironment.cmake'}",
            f"-DDE_REFERENCE_ROOTS:FILEPATH={directory / 'toolchain-roots.txt'}",
            f"-DDE_REFERENCE_COMPILER_ID:FILEPATH={directory / 'compiler-id.txt'}",
            f"-DDE_REFERENCE_SIMULATE_ID:FILEPATH={directory / 'compiler-simulate-id.txt'}",
        ],
        check=True,
    )
    identity = (directory / "compiler-id.txt").read_text(encoding="utf-8")
    if identity in {"Clang", "AppleClang"}:
        observed = subprocess.run(
            [compiler, "-print-resource-dir"],
            capture_output=True,
            text=True,
            check=True,
            timeout=15,
        )
        lines = observed.stdout.splitlines()
        if len(lines) != 1 or ";" in lines[0] or not Path(lines[0]).is_absolute():
            message = "Reference selected Clang resource directory is not one absolute path"
            raise RuntimeError(message)
        resource = Path(lines[0]).resolve()
        include = resource / "include"
        if not include.is_dir() or system_roots([str(include)]) != [resource]:
            message = "Reference selected Clang resource include owner is unavailable or unowned"
            raise RuntimeError(message)
        with (directory / "toolchain-roots.txt").open("a", encoding="utf-8") as stream:
            stream.write(";" + str(include))
    return generated


def build_reference(
    compiler: str,
    flags: list[str],
    sources: list[Path],
    directory: Path,
    jobs: int,
) -> Path:
    """Compile every admitted source once into owned objects, then link the complete set."""
    validate_flags(flags)
    validate_source_inventory(sources)
    check_environment()
    includes = [
        "-I",
        str(ROOT / "include"),
        "-I",
        str(ROOT / "tests/support"),
        "-I",
        str(ROOT / "src/methods"),
        "-I",
        str(directory / "generated"),
    ]
    compiler_id = (directory / "compiler-id.txt").read_text(encoding="utf-8")
    roots_file = directory / "toolchain-roots.txt"
    simulate_file = directory / "compiler-simulate-id.txt"
    simulate_id = simulate_file.read_text(encoding="utf-8") if simulate_file.is_file() else ""
    roots = (
        system_roots(roots_file.read_text().split(";"), simulate_id=simulate_id)
        if roots_file.is_file()
        else []
    )
    roots += [ROOT / "include", ROOT / "tests/support", directory / "generated"]
    roots += [ROOT / "src" / layer for layer in REFERENCE_LAYERS]

    def compile_source(item: tuple[int, Path]) -> Path:
        index, source = item
        target = directory / f"reference-{index}.o"
        result = subprocess.run(
            [compiler, *flags, *includes, "-H", "-c", str(source), "-o", str(target)],
            capture_output=True,
            text=True,
            check=True,
        )
        observe_headers(result.stderr, Path.cwd(), source, roots, compiler_id)
        return target

    objects = ordered_map(compile_source, enumerate(sources), jobs)
    executable = directory / (
        "reference-tests.exe" if sys.platform == "win32" else "reference-tests"
    )
    subprocess.run(
        [compiler, *flags, *(str(path) for path in objects), "-o", str(executable)], check=True
    )
    return executable


def main() -> int:
    """Build and run the reference suites with the strict warning set."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument(
        "--jobs", type=int, default=2, help="bounded compilation workers (default: 2)"
    )
    args = parser.parse_args()
    if not 1 <= args.jobs <= MAX_JOBS:
        parser.error(f"reference jobs must be in [1,{MAX_JOBS}]")
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
        generate_build_facts(directory, compiler, tools["cmake"]["minimum"])
        identity = (directory / "compiler-id.txt").read_text()
        print(f"Reference compiler family: {identity}", flush=True)
        if identity == "GNU":
            check_environment()
            verify_gnu_pch(compiler, flags, directory)
        exe = build_reference(
            compiler, flags, [ROOT / source for source in sources], directory, args.jobs
        )
        result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
        counts = reconcile_suites(result.stdout)
        print(result.stdout, end="")
        print(
            f"PASS: {len(counts)} live independent suites; "
            f"{len(sources)} compiled translation units"
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        if error.stdout:
            print(error.stdout, end="")
        if error.stderr:
            print(error.stderr, end="", file=sys.stderr)
        raise SystemExit(error.returncode) from error
