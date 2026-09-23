#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Run the exact registered harness set with explicit child parallelism and complete evidence."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path
from typing import Any

from fuzz_execution import bounded_process, new_directory, write_json
from fuzz_instrumentation import inspect_archives
from fuzz_manifest import (
    DEFAULT_JOBS,
    DEFAULT_SECONDS,
    ROOT,
    FuzzError,
    campaign_timeout,
    concurrency,
    duration,
    target_timeout,
    targets,
)


def configured_engine(build: Path) -> str:
    """Bind commands to the configured engine rather than trusting an arbitrary runner flag."""
    prefix = "DE_FUZZ_ENGINE:STRING="
    lines = (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
    values = [line.removeprefix(prefix) for line in lines if line.startswith(prefix)]
    if len(values) != 1 or values[0] not in ("libfuzzer", "afl"):
        msg = "The configured fuzz engine is missing or invalid"
        raise FuzzError(msg)
    return values[0]


def validate_command(command: list[str], build: Path, name: str, seconds: int) -> None:
    """Require the actual target binary, matching duration and configured coverage engine."""
    expected = {
        "--target": name,
        "--seconds": str(seconds),
        "--engine": configured_engine(build),
        "--binary": str(build / "fuzz" / f"de_fuzz_{name}"),
    }
    if str(ROOT / "tools/run_fuzzers.py") not in command:
        msg = f"Unexpected harness runner: {name}"
        raise FuzzError(msg)
    for flag, value in expected.items():
        if command.count(flag) != 1 or command[command.index(flag) + 1] != value:
            msg = f"Harness {name} has a mismatched {flag}"
            raise FuzzError(msg)


def registration(ctest: str, build: Path, seconds: int) -> set[str]:
    """Reject missing, duplicate, disabled, substituted or mismatched child registrations."""
    result = subprocess.run(
        [ctest, "--test-dir", str(build), "--show-only=json-v1"],
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
    )
    tests = json.loads(result.stdout)["tests"]
    names = [test["name"] for test in tests]
    expected = {f"fuzz-{target.name}" for target in targets()}
    if len(names) != len(expected) or set(names) != expected:
        msg = f"CTest harness set differs: expected {sorted(expected)}, got {sorted(names)}"
        raise FuzzError(msg)
    for test in tests:
        command = test.get("command", [])
        properties = {item["name"]: item["value"] for item in test.get("properties", [])}
        if properties.get("DISABLED") or any(
            name in properties for name in ("SKIP_RETURN_CODE", "SKIP_REGULAR_EXPRESSION")
        ):
            msg = f"Harness may not be disabled or skippable: {test['name']}"
            raise FuzzError(msg)
        if properties.get("TIMEOUT") != target_timeout(seconds):
            msg = f"Harness timeout differs from the bounded policy: {test['name']}"
            raise FuzzError(msg)
        validate_command(command, build, test["name"].removeprefix("fuzz-"), seconds)
    return {name.removeprefix("fuzz-") for name in expected}


def complete_reports(directory: Path, expected: set[str]) -> bool:
    """A fresh campaign needs exactly one successful, nonempty report per expected harness."""
    reports = [
        json.loads(path.read_text(encoding="utf-8")) for path in directory.glob("*/result.json")
    ]
    names = [report.get("target") for report in reports]
    return (
        len(names) == len(expected)
        and set(names) == expected
        and all(
            report.get("passed") is True
            and report.get("exit_code") == 0
            and type(report.get("executions")) is int
            and report["executions"] > 0
            and report.get("findings") == []
            for report in reports
        )
    )


def campaign(args: argparse.Namespace) -> int:
    """Claim new evidence storage, validate selection, execute and reconcile all outcomes."""
    build = args.build.resolve()
    directory = new_directory(build / "fuzz-work", "campaign-")
    report: dict[str, Any] = {"passed": False, "seconds": args.seconds, "jobs": args.jobs}
    write_json(directory / "campaign.json", report)
    try:
        expected = registration(args.ctest, build, args.seconds)
        report["codec_archive_sha256"] = inspect_archives(build)
        command = [
            args.ctest,
            "--test-dir",
            str(build),
            "--parallel",
            str(args.jobs),
            "--output-on-failure",
            "--no-tests=error",
            "--output-junit",
            str(directory / "ctest.xml"),
        ]
        report.update(targets=sorted(expected), command=command)
        write_json(directory / "campaign.json", report)
        env = {**os.environ, "DE_FUZZ_RUN_ROOT": str(directory)}
        report["exit_code"] = bounded_process(
            command,
            env,
            directory / "ctest.log",
            campaign_timeout(len(expected), args.seconds, args.jobs),
            graceful=True,
        )
        report["passed"] = report["exit_code"] == 0 and complete_reports(directory, expected)
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as exc:
        report["error"] = str(exc)
    finally:
        write_json(directory / "campaign.json", report)
    print(f"{'PASS' if report['passed'] else 'FAIL'}: fuzz campaign; evidence: {directory}")
    return 0 if report["passed"] else 1


def main() -> int:
    """Plan a workload before building, or run the complete configured fuzz test tree."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--seconds", type=duration, default=DEFAULT_SECONDS)
    parser.add_argument("--jobs", type=concurrency, default=DEFAULT_JOBS)
    parser.add_argument(
        "--job-budget", type=int, help="Available campaign seconds, excluding setup/build"
    )
    parser.add_argument("--plan", action="store_true")
    args = parser.parse_args()
    timeout = campaign_timeout(len(targets()), args.seconds, args.jobs)
    if args.job_budget is not None and timeout > args.job_budget:
        parser.error(
            f"Campaign needs {timeout}s; job has only {args.job_budget}s after setup/build"
        )
    if args.plan:
        print(
            json.dumps(
                {
                    "targets": [target.name for target in targets()],
                    "timeout_seconds": timeout,
                    "jobs": args.jobs,
                    "seconds_per_target": args.seconds,
                }
            )
        )
        return 0
    if args.build is None:
        parser.error("--build is required except with --plan")
    return campaign(args)


if __name__ == "__main__":
    raise SystemExit(main())
