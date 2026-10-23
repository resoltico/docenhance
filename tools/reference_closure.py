# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Observed header, closed command and executed-suite admission for standalone references."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from architecture import ArchitectureError, load_manifest
from compile_db import header_trace
from deps import ROOT

# Mirrors DE_STRICT_WARNINGS in cmake/ProjectOptions.cmake.
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
REFERENCE_SUITES = (
    "parser_cases",
    "decimal_cases",
    "numerical_cases",
    "capabilities_cases",
    "memory_cases",
    "architecture_cases",
    "schedule_cases",
    "box_mean_cases",
    "sharpening_cases",
    "otsu_cases",
)


def validate_flags(flags: list[str]) -> None:
    """No library, archive, object, search, discard or injected compiler option is admitted."""
    admitted = {
        *STRICT_WARNINGS,
        "-std=c++23",
        "-Werror",
        "-fno-fast-math",
        "-ffp-contract=off",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer",
        "-g",
    }
    if rejected := set(flags) - admitted:
        msg = f"Unsupported reference flags: {sorted(rejected)}"
        raise ArchitectureError(msg)


def windows_system_roots() -> list[Path]:
    """Bind active VC/SDK include owners to the actual selected developer-shell compiler."""
    if sys.platform == "win32":
        toolset = os.environ.get("VCTOOLSINSTALLDIR", "")
        sdk = os.environ.get("WINDOWSSDKDIR", "")
        version = os.environ.get("WINDOWSSDKVERSION", "").rstrip("/\\")
        compiler = shutil.which("cl.exe")
        if (
            not toolset
            or not sdk
            or not compiler
            or not re.fullmatch(r"[0-9]+(?:\.[0-9]+){3}", version)
        ):
            message = "Reference Windows compiler/SDK provenance is unavailable"
            raise ArchitectureError(message)
        vc, sdk_root = Path(toolset).resolve(), Path(sdk).resolve()
        binary = Path(compiler).resolve()
        include = (vc / "include").resolve()
        if (
            not binary.is_file()
            or not binary.is_relative_to(vc / "bin")
            or not include.is_dir()
            or not include.is_relative_to(vc)
        ):
            message = "Reference Windows compiler does not own the selected toolset"
            raise ArchitectureError(message)
        roots = [include]
        for family in ("ucrt", "shared", "um", "winrt", "cppwinrt"):
            include = (sdk_root / "Include" / version / family).resolve()
            if include.is_dir():
                if not include.is_relative_to(sdk_root):
                    message = "Reference Windows SDK include owner escapes its selected SDK"
                    raise ArchitectureError(message)
                roots.append(include)
        return roots
    return []


def system_roots(paths: list[str], *, simulate_id: str = "") -> list[Path]:
    """Narrow real CMake implicit paths to compiler, C++ library, OS and selected SDK roots."""
    windows = windows_system_roots()
    roots = list(windows) if sys.platform == "win32" and simulate_id == "MSVC" else []
    for name in paths:
        path = Path(name).resolve()
        spelling = path.as_posix()
        sdk = next((parent for parent in (path, *path.parents) if parent.suffix == ".sdk"), None)
        compiler = re.search(
            r"/(?:lib/clang/[^/]+/include|include/c\+\+/[^/]+|"
            r"gcc/[^/]+/[^/]+/include(?:-fixed)?)$",
            spelling,
        )
        windows_owner = next((base for base in windows if path.is_relative_to(base)), None)
        operating_system = spelling == "/usr/include" or spelling.startswith("/usr/include/")
        if windows_owner is not None:
            roots.append(windows_owner)
        elif sdk is not None:
            roots.append(sdk)
        elif re.search(r"/lib/clang/[^/]+/include$", spelling):
            # Sanitized Clang also traces resource/share/asan_ignorelist.txt as an input.
            roots.append(path.parent)
        elif compiler or operating_system:
            roots.append(path)
    return sorted(set(roots))


def observe_headers(
    trace: str, directory: Path, source: Path, allowed_roots: list[Path], compiler_id: str
) -> None:
    """Audit the same object compilation's canonical headers; package rejection comes first."""
    manifest = load_manifest()
    roots = [path.resolve() for path in allowed_roots]
    private = [(ROOT / name).resolve() for name in (".cache", "out")]
    for spelled, canonical in header_trace(trace, str(directory), str(source), compiler_id):
        path = Path(canonical)
        names = (Path(spelled).as_posix(), path.as_posix())
        package = any(manifest.package_of_header(name) for name in names)
        # pocketfft is refused even before the native package is admitted to the manifest.
        pocketfft = any("pocketfft" in Path(name).name for name in names)
        if package or pocketfft or any(path.is_relative_to(base) for base in private):
            msg = f"Reference reaches native/package header: {source}: {path}"
            raise ArchitectureError(msg)
        if not any(path.is_relative_to(base) for base in roots):
            msg = f"Reference reaches unowned header: {source}: {path}"
            raise ArchitectureError(msg)


def reconcile_suites(output: str) -> dict[str, int]:
    """Require exactly the ten ordered callbacks with positive actual assertion work."""
    records = [line.split() for line in output.splitlines() if line.startswith("REFERENCE ")]
    if any(
        len(record) != len(("REFERENCE", "name", "count"))
        or not record[2].isascii()
        or not record[2].isdigit()
        for record in records
    ):
        msg = "Malformed reference assertion receipt"
        raise ArchitectureError(msg)
    if tuple(record[1] for record in records) != REFERENCE_SUITES:
        msg = "Reference suite census/order differs from the ten required callbacks"
        raise ArchitectureError(msg)
    counts = {record[1]: int(record[2]) for record in records}
    if any(count == 0 for count in counts.values()):
        msg = "Reference callback executed no assertions"
        raise ArchitectureError(msg)
    return counts


def verify_gnu_pch(compiler: str, flags: list[str], directory: Path) -> None:
    """Exercise GNU's real automatic PCH path, with healthy and package-only controls."""
    validate_flags(flags)
    control = directory / "pch-control"
    control.mkdir()
    native = control / "nlohmann/json.hpp"
    native.parent.mkdir()
    native.write_text("inline constexpr int observed_value = 17;\n", encoding="utf-8")
    source = control / "control.cpp"
    source.write_text(
        '#include "bridge.h"\nint main() { return observed_value != 17; }\n', encoding="utf-8"
    )
    header = control / "bridge.h"
    header.write_text("inline constexpr int observed_value = 17;\n", encoding="utf-8")
    arguments = [compiler, *flags, "-I", str(control)]
    target = control / "control.o"

    def compile_object() -> str:
        result = subprocess.run(
            [*arguments, "-H", "-c", str(source), "-o", str(target)],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stderr

    observe_headers(compile_object(), control, source, [control], "GNU")
    executable = control / "control"
    subprocess.run([compiler, *flags, str(target), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
    header.write_text('#include "nlohmann/json.hpp"\n', encoding="utf-8")
    refuse_trace(compile_object(), control, source, "native/package header")
    subprocess.run(
        [*arguments, "-x", "c++-header", str(header), "-o", str(header) + ".gch"],
        capture_output=True,
        text=True,
        check=True,
    )
    trace = compile_object()
    if re.search(r"^! (.+)$", trace, flags=re.MULTILINE) is None:
        msg = "GNU automatic PCH control did not actually consume its precompiled header"
        raise ArchitectureError(msg)
    refuse_trace(trace, control, source, "consumed precompiled header")
    print("PASS: real GNU owned-header, package-header and consumed-PCH controls")


def refuse_trace(trace: str, directory: Path, source: Path, diagnostic: str) -> None:
    """Require the intended actual observer refusal, never an unrelated compiler failure."""
    try:
        observe_headers(trace, directory, source, [directory], "GNU")
    except ArchitectureError as error:
        if diagnostic in str(error):
            return
        raise
    msg = f"Reference observer accepted the {diagnostic} control"
    raise ArchitectureError(msg)
