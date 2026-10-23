#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Own current Release/Debug Windows CRT proof inside the already selected developer shell."""

from __future__ import annotations

import argparse
import json
import os
import platform
import stat
import sys
import time
from pathlib import Path
from typing import Any

from test_evidence import discovery
from windows_resource_artifacts import identity, write_json
from windows_resource_execution import debug_cases, debug_producer, directives
from windows_resource_producer import compilation
from windows_resource_records import (
    capture_maps,
    executable_paths,
    native_records,
    stable_producer,
)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/windows-resource-proof"
CAP_SECONDS = 2820


def sources() -> dict[str, str]:
    """Bind concrete source/configuration inputs without trusting an old collector label."""
    files = [
        path
        for name in ("src", "include", "tests/support", "cmake")
        for path in (ROOT / name).rglob("*")
        if path.is_file() and path.suffix in {".cpp", ".hpp", ".cmake", ".json"}
    ]
    files.extend(
        ROOT / name
        for name in (
            "CMakeLists.txt",
            "CMakePresets.json",
            "tests/CMakeLists.txt",
            "tests/fft_resources.cmake",
            "deps/lock.json",
            "deps/features.json",
            "deps/tools.json",
            "tools/ci_windows.ps1",
            ".github/workflows/ci.yml",
            "tools/windows_resource_proof.py",
            "tools/windows_resource_execution.py",
            "tools/windows_resource_records.py",
            "tools/restoration_resource_records.py",
            "tools/native_resource_capture.py",
            "tools/run_native_suite.py",
            "tools/windows_resource_producer.py",
            "tools/windows_resource_artifacts.py",
            "tools/native_output.py",
            "tools/native_journal.py",
            "tools/process_execution.py",
        )
    )
    return {str(path.relative_to(ROOT)): identity(path)["sha256"] for path in sorted(files)}


def release_directories() -> dict[str, list[int]]:
    """Record actual regular directory identities, never choose the newest stale report."""
    directories = {}
    for path in (ROOT / "out/release/app").glob("native-*"):
        info = path.lstat()
        if stat.S_ISREG(info.st_mode):
            continue
        if not stat.S_ISDIR(info.st_mode):
            message = "Native evidence owner is not a regular directory"
            raise ValueError(message)
        directories[str(path.resolve())] = [info.st_dev, info.st_ino]
    return directories


def snapshot() -> None:
    """Precede the real Release workflow in this same selected VS shell."""
    if platform.system() != "Windows":
        message = "Windows CRT proof requires actual Windows execution"
        raise ValueError(message)
    OUT.mkdir(parents=True, exist_ok=False)
    write_json(
        OUT / "snapshot.json",
        {
            "head": os.environ["GITHUB_SHA"],
            "sources": sources(),
            "release_before": release_directories(),
            "environment": {
                key: os.environ.get(key)
                for key in (
                    "VCToolsInstallDir",
                    "VCToolsVersion",
                    "WindowsSdkDir",
                    "WindowsSDKVersion",
                    "LIB",
                    "INCLUDE",
                )
            },
        },
    )


def unchanged(saved: dict[str, Any]) -> None:
    """Refuse changed sources before or after effectful producer work."""
    if saved["sources"] != sources():
        message = "Sources changed during Windows proof"
        raise ValueError(message)


def require_release_pass(exit_code: int) -> None:
    """A failed Release/package outcome never opens a Debug retry path."""
    if exit_code != 0:
        message = "Release workflow/package failed; Debug not launched"
        raise ValueError(message)


def admitted_release(saved: dict[str, Any]) -> dict[str, Any]:
    """Select exactly the new evidence owner from this actual invocation."""
    unchanged(saved)
    if platform.system() != "Windows" or saved["head"] != os.environ["GITHUB_SHA"]:
        message = "Current source or actual Windows invocation differs"
        raise ValueError(message)
    owners = release_directories()
    new = set(owners) - set(saved["release_before"])
    if len(new) != 1:
        message = "Expected exactly one new Release native evidence directory"
        raise ValueError(message)
    path = new.pop()
    return {"path": path, "identity": owners[path]}


def same_toolchain(record: dict[str, Any]) -> None:
    """Debug stays in the already selected VS owner, never a later ambient tool selection."""
    for key in ("compiler", "linker"):
        if record["release"]["producer"][key] != record["debug"]["producer"][key]:
            message = "Release and Debug selected different native toolchain identities"
            raise ValueError(message)


def retained_owner(owner: dict[str, Any]) -> None:
    """The selected evidence directory remains the same object through completion."""
    info = Path(owner["path"]).lstat()
    if not stat.S_ISDIR(info.st_mode) or [info.st_dev, info.st_ino] != owner["identity"]:
        message = "Selected evidence directory owner changed"
        raise ValueError(message)


def completed_inputs(saved: dict[str, Any], record: dict[str, Any], deadline: float) -> None:
    """Refuse changed source/products or an exhausted operational qualification deadline."""
    unchanged(saved)
    same_toolchain(record)
    stable_producer(record["release"])
    stable_producer(record["debug"])
    retained_owner(record["release"]["selected_owner"])
    retained_owner(record["debug"]["execution"]["selected_owner"])
    if time.monotonic() >= deadline:
        message = "Windows resource proof exceeded its operational deadline"
        raise ValueError(message)


def complete(release_exit: int) -> None:
    """Retain failures, and declare proof only after both real producer configurations pass."""
    start = time.monotonic()
    record: dict[str, Any] = {
        "status": "incomplete",
        "release_exit": release_exit,
        "qualification": (
            "Current Windows resource scope only; "
            "not whole source/platform or natural quality qualification"
        ),
        "cleanup_scope": (
            "Existing Windows ownership helper reaps its direct child; "
            "descendant cleanup after timeout remains the CI job owner. "
            "No retry or claim of an instantaneous process/disk quota."
        ),
    }
    try:
        saved = json.loads((OUT / "snapshot.json").read_text())
        fresh = admitted_release(saved)
        release = OUT / "release"
        release.mkdir()
        record["release"] = {"producer": {}, "maps": [], "selected_owner": fresh}
        compilation(ROOT / "out/release", record["release"]["producer"], debug=False)
        capture_maps(ROOT / "out/release", release, record["release"]["maps"], debug=False)
        record["release"]["evidence"] = native_records(
            Path(fresh["path"]),
            fresh["identity"],
            discovery("ctest", ROOT / "out/release/app"),
            executable_paths(record["release"]["producer"]),
        )
        directives(record["release"], "release", start + CAP_SECONDS)
        require_release_pass(release_exit)
        (OUT / "debug").mkdir()
        record["debug"] = {}
        deadline = start + CAP_SECONDS
        debug_producer(deadline, record["debug"])
        record["debug"]["execution"] = {}
        debug_cases(
            deadline, record["debug"]["execution"], executable_paths(record["debug"]["producer"])
        )
        completed_inputs(saved, record, deadline)
        record["status"] = "passed_current_release_and_debug_resource_proof"
    except BaseException as error:
        record["error"] = repr(error)
        raise
    finally:
        primary = sys.exception()
        record["elapsed_seconds"] = time.monotonic() - start
        try:
            record["snapshot"] = identity(OUT / "snapshot.json")
            write_json(OUT / "proof.json", record)
        except BaseException as error:
            if primary is not None:
                primary.add_note(f"Windows proof metadata also failed: {error!r}")
                raise primary from error
            raise


def main() -> int:
    """Expose only the two concrete phases enclosing this actual Release invocation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("snapshot", "complete"))
    parser.add_argument("--release-exit", type=int, default=0)
    args = parser.parse_args()
    try:
        if args.phase == "snapshot":
            snapshot()
        else:
            complete(args.release_exit)
    except (OSError, ValueError, KeyError) as error:
        print(f"Windows resource proof failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
