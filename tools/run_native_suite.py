#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Execute the complete configured native suite and reconcile discovery, compilation and results."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

from architecture import load_manifest
from audit_build import read_cache
from fuzz_manifest import targets
from native_journal import snapshot_ctest_output
from native_output import archive_ctest_output
from native_resource_capture import retained_bodies
from process_execution import bounded_process
from restoration_resource_records import (
    reconcile_restoration_records,
    resource_cases,
)
from sanitizer_evidence import check_compilation
from test_evidence import EvidenceError, complete_junit, discovery

ROOT = Path(__file__).resolve().parents[1]
# Test cases contain their own CPU-heavy references, compilers and processing workers.
MAX_TEST_PROCESSES = 2
# Leave a minute for group cleanup and evidence reconciliation inside CMake's 1800-second limit.
NATIVE_EXECUTION_SECONDS = 1740
# Operational archive allowance within the same deadline; cleanup can consume this reserve.
NATIVE_ARCHIVE_SECONDS = 30


def layer_records_errors(layers: dict[str, Any], records: dict[str, Any]) -> list[str]:
    """Every native target must retain exactly its reviewed layer identity."""
    expected = {value["target"]: name for name, value in layers.items()}
    actual = {target: value.get("layer") for target, value in records.items()}
    return [] if actual == expected else ["Native target registration differs from reviewed layers"]


def compilation(build: Path) -> None:
    """Observe actual compiled translation units, not filename mentions in build scripts."""
    records = json.loads((build / "architecture-targets.json").read_text())
    errors = layer_records_errors(load_manifest().layers, records)
    if errors:
        msg = "; ".join(errors)
        raise EvidenceError(msg)
    cache = read_cache(build / "CMakeCache.txt")
    entries = json.loads((build / "compile_commands.json").read_text())
    check_compilation(cache, entries)
    compiled = {Path(item["file"]).resolve() for item in entries}
    roots = ["src", "tests", "fuzz", "tools"]
    missing = [
        str(path.relative_to(ROOT))
        for root in roots
        for path in (ROOT / root).rglob("*.cpp")
        if path.resolve() not in compiled
    ]
    if missing:
        msg = f"Native sources are absent from actual compilation: {sorted(missing)}"
        raise EvidenceError(msg)


def registrations(build: Path, tests: list[dict[str, Any]]) -> set[str]:
    """Compare Catch discovery, static registrations, CLI scripts and manifest corpus replay."""
    manifest = json.loads((build / "native-tests.json").read_text())
    binary = manifest["unit_binary"]
    result = subprocess.run(
        [binary, "*", "--list-tests", "--reporter", "json"],
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
    )
    listed = json.loads(result.stdout)["listings"]["tests"]
    units = {item["name"] for item in listed}
    if (
        not units
        or len(units) != len(listed)
        or any(
            tag.startswith((".", "!mayfail", "!shouldfail"))
            for item in listed
            for tag in item["tags"]
        )
    ):
        msg = "Catch cases must be nonempty, unique and unconditionally required"
        raise EvidenceError(msg)
    expected = units | set(manifest["static_tests"])
    if {test["name"] for test in tests} != expected:
        msg = "CTest differs from executable unit discovery and configured static tests"
        raise EvidenceError(msg)
    commands = [test["command"] for test in tests]
    scripts = set((ROOT / "tests/cli").glob("test_*.py"))
    for script in scripts:
        if (
            sum(len(command) > 1 and Path(command[1]).resolve() == script for command in commands)
            != 1
        ):
            msg = f"CLI script must be registered exactly once: {script}"
            raise EvidenceError(msg)
    names = {test["name"] for test in tests}
    if {name for name in names if name.startswith("fuzz-replay-")} != {
        f"fuzz-replay-{target.name}" for target in targets()
    }:
        msg = "Native corpus replay differs from the reviewed fuzz manifest"
        raise EvidenceError(msg)
    for test in tests:
        if test["name"] in units and (
            Path(test["command"][0]).resolve() != Path(binary).resolve()
            or test["command"][-4:] != ["--reporter", "xml", "--abort", "--invisibles"]
        ):
            msg = (
                "Unit case must execute its actual binary with fail-fast XML evidence: "
                f"{test['name']}"
            )
            raise EvidenceError(msg)
    resource_cases(tests, manifest["resource_binaries"])
    return units


def _combined_error(
    directory: Path, test_error: str, archive_error: BaseException, primary: BaseException | None
) -> None:
    """Retain both outcomes; diagnostic archival never converts a failed child to success."""
    metadata = {
        "status": "diagnostic_only_error",
        "test_error": test_error,
        "archive_error": repr(archive_error),
    }
    control = archive_error if not isinstance(archive_error, Exception) else None
    written = False
    try:
        with (directory / "native-run-error.json").open("x", encoding="utf-8") as stream:
            json.dump(metadata, stream, indent=2)
            stream.write("\n")
        written = True
    finally:
        if not written:
            error = sys.exception()
            archive_error.add_note(f"Failure metadata could not be written: {error}")
            if control is None and error is not None and not isinstance(error, Exception):
                control = error
        if primary is not None:
            primary.add_note(f"Diagnostic archive also failed: {archive_error!r}")
            raise primary from archive_error
        if control is not None:
            raise control
        msg = f"{test_error}; diagnostic archive: {archive_error}"
        raise EvidenceError(msg) from archive_error


def _execute_owned(
    command: list[str], build: Path, directory: Path, deadline: float, expected_count: int
) -> int:
    """Archive both raw outputs after bounded_process has reaped its group, even on failure."""
    remaining = deadline - time.monotonic() - NATIVE_ARCHIVE_SECONDS
    if remaining <= 0:
        msg = "Native preflight exhausted the child and diagnostic archive deadline"
        raise EvidenceError(msg)
    junit = directory / "ctest.xml"
    if junit.exists() or junit.is_symlink():
        msg = "Native evidence directory already contains stale JUnit"
        raise EvidenceError(msg)
    before = snapshot_ctest_output(build)
    failure: BaseException | None = None
    exit_code: int | None = None
    journal_status: str | None = None
    journal_cases: int | None = None
    child_finished = False
    try:
        exit_code = bounded_process(command, dict(os.environ), None, remaining, graceful=True)
        child_finished = True
    finally:
        failure = None if child_finished else sys.exception()
        archived = False
        try:
            retained = archive_ctest_output(build, directory, before, deadline, expected_count)
            journal_status = retained["journal"]["status"]
            journal_cases = retained["journal"]["completed_raw_case_count"]
            archived = True
        finally:
            if not archived:
                error = sys.exception()
                if error is not None:
                    state = repr(failure) if failure is not None else f"CTest exit {exit_code}"
                    _combined_error(directory, state, error, failure)
    if exit_code == 0 and journal_status != "diagnostic_only_complete":
        msg = "A partial CTest journal cannot qualify successful native execution"
        raise EvidenceError(msg)
    if exit_code == 0 and journal_cases != expected_count:
        msg = "Complete CTest journal case count differs from actual discovery"
        raise EvidenceError(msg)
    return exit_code


def resource_results(directory: Path, tests: list[dict[str, Any]], build: Path) -> None:
    """Successful native execution must cover the complete actual resource shard union."""
    bodies = retained_bodies(directory)
    manifest = json.loads((build / "native-tests.json").read_text())
    result = reconcile_restoration_records(tests, bodies["journal"], manifest["resource_binaries"])
    (directory / "native-restoration-resources.json").write_text(
        json.dumps(result, indent=2) + "\n"
    )


def run(args: argparse.Namespace) -> int:
    """Create fresh evidence, execute every test, and refuse incomplete or skipped results."""
    deadline = time.monotonic() + NATIVE_EXECUTION_SECONDS
    build = args.build.resolve()
    directory = Path(tempfile.mkdtemp(prefix="native-", dir=build))
    print(f"Native suite evidence: {directory}", flush=True)
    tests = discovery(args.ctest, build)
    compilation(build)
    units = registrations(build, tests)
    command = [
        args.ctest,
        "--test-dir",
        str(build),
        "--parallel",
        str(args.jobs),
        "--test-output-size-passed",
        str(1024 * 1024),
        "--no-tests=error",
        "--output-junit",
        str(directory / "ctest.xml"),
    ]
    exit_code = _execute_owned(command, build, directory, deadline, len(tests))
    complete_junit(directory / "ctest.xml", {test["name"] for test in tests}, units)
    if exit_code == 0:
        resource_results(directory, tests, build)
        if time.monotonic() >= deadline:
            msg = "Native resource reconciliation exceeded the execution deadline"
            raise EvidenceError(msg)
    return exit_code


def main() -> int:
    """Run the actual child CTest tree with bounded explicit parallelism."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument(
        "--jobs",
        type=int,
        default=os.environ.get("DE_NATIVE_TEST_JOBS", str(MAX_TEST_PROCESSES)),
    )
    args = parser.parse_args()
    if not 1 <= args.jobs <= MAX_TEST_PROCESSES:
        parser.error(f"native test jobs must be in [1,{MAX_TEST_PROCESSES}]")
    try:
        return run(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"FAIL: native suite: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
