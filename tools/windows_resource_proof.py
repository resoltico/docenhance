#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Own current Release/Debug Windows CRT proof inside the already selected developer shell."""

from __future__ import annotations

import argparse
import json
import os
import platform
import sys
import time
from pathlib import Path
from typing import Any

import windows_resource_records as records
from test_evidence import discovery
from windows_resource_artifacts import identity, write_json
from windows_resource_execution import (
    debug_cases,
    debug_producer,
    directives,
    release_fft_preflight,
)
from windows_resource_producer import compilation

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
            "tools/test_evidence.py",
            "tools/windows_resource_records.py",
            "tools/restoration_resource_records.py",
            "tools/restoration_resource_cases.py",
            "tools/native_resource_capture.py",
            "tools/run_native_suite.py",
            "tools/native_history.py",
            "tools/cache_lock.py",
            "tools/windows_resource_producer.py",
            "tools/windows_resource_artifacts.py",
            "tools/native_output.py",
            "tools/native_journal.py",
            "tools/process_execution.py",
            "tools/capture_reader.py",
        )
    )
    return {str(path.relative_to(ROOT)): identity(path)["sha256"] for path in sorted(files)}


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
            "release_before": records.release_directories(ROOT),
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


def current_invocation(saved: dict[str, Any]) -> None:
    """Keep source and native-shell admission shared by early and complete Release checks."""
    unchanged(saved)
    if platform.system() != "Windows" or saved["head"] != os.environ["GITHUB_SHA"]:
        message = "Current source or actual Windows invocation differs"
        raise ValueError(message)


def admitted_release(saved: dict[str, Any]) -> dict[str, Any]:
    """Select exactly the new evidence owner from this actual invocation."""
    current_invocation(saved)
    owners = records.release_directories(ROOT)
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


def completed_inputs(saved: dict[str, Any], record: dict[str, Any], deadline: float) -> None:
    """Refuse changed source/products or an exhausted operational qualification deadline."""
    unchanged(saved)
    same_toolchain(record)
    records.stable_producer(record["release"])
    records.stable_producer(record["debug"])
    records.retained_owner(record["release"]["selected_owner"])
    records.retained_owner(record["debug"]["execution"]["selected_owner"])
    if time.monotonic() >= deadline:
        message = "Windows resource proof exceeded its operational deadline"
        raise ValueError(message)


def current_preflight_inputs(saved: dict[str, Any], deadline: float) -> None:
    """Source admission and its final rehash share the original early-run deadline."""
    current_invocation(saved)
    if time.monotonic() >= deadline:
        message = "Early FFT preflight exceeded its original120second deadline"
        raise ValueError(message)


def preflight() -> None:
    """One fresh actual FFT run precedes the full Release build in the selected Windows shell."""
    path = OUT / "preflight.json"
    if path.exists() or path.is_symlink():
        message = "Early FFT preflight already attempted; no retry or report adoption"
        raise ValueError(message)
    start = time.monotonic()
    record: dict[str, Any] = {
        "status": "incomplete",
        "total_seconds": 120,
        "case_seconds": 90,
        "archive_reserve_seconds": 30,
        "scope": "One early FFT resource case only; full suite/proof still required",
    }
    try:
        saved = json.loads((OUT / "snapshot.json").read_text())
        current_preflight_inputs(saved, start + 120)
        release_fft_preflight(start + 120, record)
        current_preflight_inputs(saved, start + 120)
        record["status"] = "passed_early_fft_case_only"
    except BaseException as error:
        record["error"] = repr(error)
        raise
    finally:
        primary = sys.exception()
        record["elapsed_seconds"] = time.monotonic() - start
        try:
            write_json(path, record)
        except BaseException as error:
            if primary is not None:
                primary.add_note(f"Early FFT metadata also failed: {error!r}")
                raise primary from error
            raise


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
        records.capture_maps(ROOT / "out/release", release, record["release"]["maps"], debug=False)
        record["release"]["evidence"] = records.native_records(
            Path(fresh["path"]),
            fresh["identity"],
            discovery("ctest", ROOT / "out/release/app"),
            records.executable_paths(record["release"]["producer"]),
        )
        directives(record["release"], "release", start + CAP_SECONDS)
        require_release_pass(release_exit)
        (OUT / "debug").mkdir()
        record["debug"] = {}
        deadline = start + CAP_SECONDS
        debug_producer(deadline, record["debug"])
        record["debug"]["execution"] = {}
        debug_cases(
            deadline,
            record["debug"]["execution"],
            records.executable_paths(record["debug"]["producer"]),
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
    """Expose the concrete snapshot, early FFT and complete proof phases."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("snapshot", "preflight", "complete"))
    parser.add_argument("--release-exit", type=int, default=0)
    args = parser.parse_args()
    try:
        if args.phase == "snapshot":
            snapshot()
        elif args.phase == "preflight":
            preflight()
        else:
            complete(args.release_exit)
    except (OSError, ValueError, KeyError) as error:
        print(f"Windows resource proof failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
