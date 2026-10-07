# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Observe expanded Ninja compiler rules; CMake's database omits the linter wrapper."""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path
from typing import Any

from audit_build import read_cache
from check_reference_suite import STRICT_WARNINGS
from command_arguments import command_arguments

REQUIRED_TIDY = ("--warnings-as-errors=*", "--extra-arg=-Wno-unknown-warning-option", "--use-color")
WRAPPER_MINIMUM = 6
WRAPPER_FIELDS = 2
AUTO_DRIVERS = frozenset(
    {"--extra-arg-before=--driver-mode=g++", "--extra-arg-before=--driver-mode=cl"}
)


def entry_path(entry: dict[str, str], key: str) -> Path:
    """Resolve generator-spelled sources/outputs against their compilation directory."""
    return (Path(entry["directory"]) / entry[key]).resolve()


def compiler_error(args: list[str]) -> str | None:
    """Prove fatal reviewed warnings and refuse command-wide or unbound diagnostic silencing."""
    required = {"/W4", "/WX"} if os.name == "nt" else {*STRICT_WARNINGS, "-Werror"}
    if not required.issubset(args):
        return "actual compiler lacks the reviewed fatal warning profile"
    broad = {"-w", "-Wno-error", "-Wno-everything", "/w", "/w0", "/wx-"}
    for arg in args:
        if arg in broad or arg.lower() in broad:
            return "actual compiler has a forbidden blanket warning opt-out"
        if arg.startswith("-Wno-") or arg.lower().startswith(("/wd", "/w0")):
            return "actual compiler has an unadmitted command-line warning waiver"
        if arg.lower() in {"/w1", "/w2", "/w3", "/w-", "-wno-all", "-wno-extra"}:
            return "actual compiler lowers the reviewed warning level"
    return None


def wrapper_error(entry: dict[str, str], cache: dict[str, str]) -> str | None:
    """Require the actual CMake wrapper, source identity and closed reviewed tidy command."""
    args = command_arguments(entry["command"], windows=os.name == "nt")
    source = entry_path(entry, "file")
    if len(args) < WRAPPER_MINIMUM or args[1:3] != ["-E", "__run_co_compile"]:
        return "actual compiler rule has no clang-tidy wrapper"
    if Path(args[0]).resolve() != Path(cache["CMAKE_COMMAND"]).resolve() or "--" not in args:
        return "actual compiler rule has an unreviewed launcher"
    prefix = args[3 : args.index("--")]
    tidy = [part.removeprefix("--tidy=") for part in prefix if part.startswith("--tidy=")]
    named = [part.removeprefix("--source=") for part in prefix if part.startswith("--source=")]
    if len(tidy) != 1 or len(named) != 1 or len(prefix) != WRAPPER_FIELDS:
        return "actual clang-tidy wrapper arguments differ"
    if (Path(entry["directory"]) / named[0]).resolve() != source:
        return "actual clang-tidy source differs from the compiler source"
    return tidy_error(tidy[0], cache) or compiler_error(args[args.index("--") + 1 :])


def tidy_error(tidy: str, cache: dict[str, str]) -> str | None:
    """Bind every linter invocation to the configured tool and reviewed option list."""
    flags = tidy.split(";")
    if not flags or Path(flags[0]).resolve() != Path(cache["DE_CLANG_TIDY"]).resolve():
        return "actual clang-tidy executable differs from the admitted tool"
    expected = [*REQUIRED_TIDY]
    if os.name == "nt":
        expected.append("--extra-arg-before=/EHsc")
    remaining = flags[1:]
    if remaining[: len(expected)] != expected or len(remaining) != len(expected) + 1:
        return "actual clang-tidy options differ from the reviewed command"
    if remaining[-1] not in AUTO_DRIVERS:
        return "actual clang-tidy driver mode is not admitted"
    return None


def expanded_entries(build: Path, cache: dict[str, str]) -> list[dict[str, str]]:
    """Ask the configured Ninja to expand every rule, including response files."""
    result = subprocess.run(
        [cache["CMAKE_MAKE_PROGRAM"], "-C", str(build), "-t", "compdb", "-x"],
        capture_output=True,
        text=True,
        check=True,
    )
    data: Any = json.loads(result.stdout)
    if not isinstance(data, list) or not all(
        isinstance(entry, dict)
        and all(
            isinstance(entry.get(key), str) for key in ("file", "output", "command", "directory")
        )
        for entry in data
    ):
        message = "Expanded Ninja compilation database is invalid"
        raise ValueError(message)
    return data


def lint_errors(build: Path, entries: list[dict[str, str]]) -> list[str]:
    """Every admitted compiler instance must actually invoke the complete linter."""
    try:
        cache = read_cache(build / "CMakeCache.txt")
        if cache.get("CMAKE_GENERATOR") != "Ninja":
            return ["Compiler lint coverage requires the reviewed Ninja generator"]
        required = ("CMAKE_MAKE_PROGRAM", "CMAKE_COMMAND", "DE_CLANG_TIDY")
        if any(not cache.get(key) for key in required):
            return ["Compiler lint coverage lacks admitted generator/tool identities"]
        actual = expanded_entries(build, cache)
        indexed: dict[tuple[Path, Path], list[dict[str, str]]] = {}
        for entry in actual:
            indexed.setdefault((entry_path(entry, "file"), entry_path(entry, "output")), []).append(
                entry
            )
        errors = []
        for entry in entries:
            key = entry_path(entry, "file"), entry_path(entry, "output")
            matching = indexed.get(key, [])
            if not matching:
                errors.append(f"{key[0]} ({key[1]}): no actual Ninja compiler rule")
            for rule in matching:
                error = wrapper_error(rule, cache)
                if error is not None:
                    errors.append(f"{key[0]} ({key[1]}): {error}")
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        return [f"Actual compiler lint coverage could not be observed: {exc}"]
    else:
        return errors
