#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Execute the complete configured native suite and reconcile discovery, compilation and results."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from architecture import load_manifest
from audit_build import FALSE, read_cache
from fuzz_manifest import targets
from sanitizer_evidence import check_compilation
from test_evidence import EvidenceError, complete_junit, discovery

ROOT = Path(__file__).resolve().parents[1]
# Test cases contain their own CPU-heavy references, compilers and processing workers.
MAX_TEST_PROCESSES = 2


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
        raise EvidenceError("; ".join(errors))
    cache = read_cache(build / "CMakeCache.txt")
    entries = json.loads((build / "compile_commands.json").read_text())
    check_compilation(cache, entries)
    compiled = {Path(item["file"]).resolve() for item in entries}
    roots = ["src", "tests", "fuzz"]
    if cache.get("DE_BUILD_TOOLS", "").upper() not in FALSE:
        roots.append("tools")
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
            or test["command"][-2:] != ["--reporter", "xml"]
        ):
            msg = f"Unit case must execute its actual binary with XML evidence: {test['name']}"
            raise EvidenceError(msg)
    return units


def run(args: argparse.Namespace) -> int:
    """Create fresh evidence, execute every test, and refuse incomplete or skipped results."""
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
        "--output-on-failure",
        "--test-output-size-passed",
        str(1024 * 1024),
        "--no-tests=error",
        "--output-junit",
        str(directory / "ctest.xml"),
    ]
    result = subprocess.run(command, check=False)
    complete_junit(directory / "ctest.xml", {test["name"] for test in tests}, units)
    return result.returncode


def main() -> int:
    """Run the actual child CTest tree with bounded explicit parallelism."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--jobs", type=int, default=MAX_TEST_PROCESSES)
    args = parser.parse_args()
    if not 1 <= args.jobs <= MAX_TEST_PROCESSES:
        parser.error(f"native test jobs must be in [1,{MAX_TEST_PROCESSES}]")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
