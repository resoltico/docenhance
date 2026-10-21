# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual selected MSVC/static-CRT producer admission, retaining acquired partial facts."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

from audit_build import read_cache
from command_arguments import command_arguments
from windows_resource_artifacts import TARGETS, identity


def dependency_crt(build: Path, runtime: str, result: dict[str, Any]) -> None:
    """Bind actual compiled dependency configurations to this same private static CRT."""
    for name in ("zlib", "jpeg", "png", "tiff", "lcms", "catch2"):
        path = build / "deps" / name / "CMakeCache.txt"
        values = read_cache(path)
        if values.get("CMAKE_MSVC_RUNTIME_LIBRARY") != runtime:
            message = f"Actual private dependency CRT differs: {name}"
            raise ValueError(message)
        result[name] = {"cache": identity(path), "CRT": values["CMAKE_MSVC_RUNTIME_LIBRARY"]}


def macro_options(args: list[str]) -> list[tuple[str, str, str]]:
    """Decode only actual CL define/undefine options, including separated operands."""
    result = []
    iterator = iter(args)
    for arg in iterator:
        if not arg.startswith(("/D", "-D", "/U", "-U")):
            continue
        text = arg[2:] or next(iterator, "")
        if not text:
            message = "Native macro option lacks its operand"
            raise ValueError(message)
        name, _, value = text.partition("=")
        result.append((arg[1], name, value))
    return result


def static_crt_options(args: list[str], *, debug: bool) -> None:
    """MTd intrinsically defines _DEBUG; reject actual contradictory D/U overrides."""
    crt = {
        arg.casefold()
        for arg in args
        if arg.casefold() in {"/mt", "-mt", "/mtd", "-mtd", "/md", "-md", "/mdd", "-mdd"}
    }
    allowed = ({"/mtd"}, {"-mtd"}) if debug else ({"/mt"}, {"-mt"})
    if crt not in allowed:
        message = "Direct observer does not compile with exact static CRT"
        raise ValueError(message)
    for operation, name, value in macro_options(args):
        contradicts = (
            (operation == "D" and name == "_DLL")
            or (
                name == "_DEBUG"
                and ((operation == "D" and not debug) or (operation == "U" and debug))
            )
            or (name == "_MT" and (operation == "U" or value not in {"", "1"}))
        )
        if contradicts:
            message = "Direct observer CRT macro override contradicts selected native flags"
            raise ValueError(message)


def compilation(build: Path, record: dict[str, Any], *, debug: bool) -> None:
    """Correlate actual MSVC compile inputs, static CRT flags and produced direct objects."""
    app = build / "app"
    config = json.loads((app / "build-identity.json").read_text())
    record["configuration"] = config
    record["config_identity"] = identity(app / "build-identity.json")
    runtime = "MultiThreadedDebug" if debug else "MultiThreaded"
    if (
        config["CMAKE_SYSTEM_NAME"] != "Windows"
        or config["CMAKE_CXX_COMPILER_ID"] != "MSVC"
        or config["CMAKE_MSVC_RUNTIME_LIBRARY"] != runtime
        or config["CMAKE_BUILD_TYPE"] != ("Debug" if debug else "Release")
    ):
        message = "Actual Windows producer configuration differs"
        raise ValueError(message)
    if (build / "build-identity.json").read_bytes() != (app / "build-identity.json").read_bytes():
        message = "Outer/inner configuration identities differ"
        raise ValueError(message)
    prefix = str((build / "prefix").resolve())
    if (
        str(config.get(prefix.replace("\\", "/"), "")).replace("\\", "/").casefold()
        != prefix.replace("\\", "/").casefold()
    ):
        message = "Actual dependency prefix is not owned by this configuration"
        raise ValueError(message)
    entries = json.loads((app / "compile_commands.json").read_text())
    records: list[dict[str, Any]] = []
    record["targets"] = records
    for target in TARGETS:
        selected = [
            entry
            for entry in entries
            if Path(entry["file"]).name == "allocation_interpose.cpp"
            and f"{target}.dir" in entry["command"]
        ]
        if len(selected) != 1:
            message = "Missing unique direct EXE allocation observer command"
            raise ValueError(message)
        entry = selected[0]
        args = command_arguments(entry["command"], windows=True)
        static_crt_options(args, debug=debug)
        output = (
            app / "tests/CMakeFiles" / (target + ".dir") / "support/allocation_interpose.cpp.obj"
        )
        acquired = {"target": target, "actual_compile": entry}
        records.append(acquired)
        acquired["object"] = identity(output)
        acquired["executable"] = identity(app / "tests" / (target + ".exe"))
    cache = read_cache(app / "CMakeCache.txt")
    record["compiler"] = identity(Path(config["CMAKE_CXX_COMPILER"]))
    record["linker"] = identity(Path(cache["CMAKE_LINKER"]))
    record["dependency_CRT"] = {}
    dependency_crt(build, runtime, record["dependency_CRT"])


def native_context(*, debug: bool) -> dict[str, Any]:
    """Hash the actual first LIB-search candidates; do not upload proprietary archives."""
    suffix = "d" if debug else ""
    directories = [Path(item) for item in os.environ["LIB"].split(";") if item]
    archives = {}
    for stem in ("libcmt", "libucrt", "libvcruntime", "libcpmt"):
        name = stem + suffix + ".lib"
        found = next(
            (directory / name for directory in directories if (directory / name).is_file()), None
        )
        if found is None:
            message = f"Selected LIB search lacks {name}"
            raise ValueError(message)
        archives[name] = identity(found)
    return {
        "ordered_LIB": [str(path) for path in directories],
        "first_resolved_candidates": archives,
        "limit": (
            "Actual search candidates and map module names; "
            "not independent archive-to-installed-source equivalence"
        ),
    }
