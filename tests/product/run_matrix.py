#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Replay the retained product matrix against a real binary and preserve all evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import time
from collections import Counter
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING, Any

from audit_paths import ROOT
from continuous_fixtures import Fixture, decode_output
from jsonschema import Draft202012Validator, ValidationError
from matrix_assertions import (
    compare_outputs,
    discovery_check,
    quality,
    require,
    sample_check,
    stage_check,
)
from matrix_cases import active_coverage, catalog, matrix
from matrix_fixtures import write_sources

if TYPE_CHECKING:
    from matrix_types import Case

MATRIX_PATH = Path(__file__).with_name("capability-matrix.json")
COMMAND_TIMEOUT = 90
STRESS_TIMEOUT = 300


def encoded_catalog() -> str:
    """Serialize the sole scenario source consistently for review and drift detection."""
    return (
        json.dumps(
            {
                "matrix_version": 1,
                "active_method_cases": active_coverage(matrix()),
                "cases": catalog(),
            },
            indent=2,
        )
        + "\n"
    )


def digest(path: Path) -> str:
    """Bind execution evidence to exact delivered bytes, without claiming review quality."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Run:
    """One owned evidence directory and immutable command inputs for this execution."""

    def __init__(self, binary: Path, directory: Path) -> None:
        """Refuse to overwrite prior evidence and retain fixture bytes before processing."""
        self.binary = binary
        self.binary_sha256 = digest(binary)
        self.platform = platform.platform()
        self.python = platform.python_version()
        self.expected_count = len(matrix())
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=False)
        self.sources = write_sources(directory / "fixtures")
        self.outputs: dict[str, Fixture] = {}
        self.results: list[dict[str, Any]] = []
        self.validator = self.schema("command-response")
        self.record_validator = self.schema("run-record")
        self.logs = directory / "commands"
        self.logs.mkdir()
        self.bundles = directory / "bundles"
        self.bundles.mkdir()
        (directory / "matrix.json").write_text(encoded_catalog(), encoding="utf-8")

    @staticmethod
    def schema(name: str) -> Draft202012Validator:
        """Use the pinned complete Draft 2020-12 validator, never a homemade subset."""
        schema = json.loads((ROOT / "schemas" / f"{name}.schema.json").read_text())
        Draft202012Validator.check_schema(schema)
        return Draft202012Validator(schema)

    def arguments(self, case: Case) -> list[str]:
        """Resolve owned source placeholders only, preserving portable matrix arguments."""
        options = [
            str(self.sources[value[1:-1]]) if value.startswith("{") else value
            for value in case.options
        ]
        if case.command == "process" and case.source:
            return [
                "process",
                str(self.sources[case.source]),
                "--out-dir",
                str(self.bundles / case.id),
                *options,
                "--json",
            ]
        return [*([] if case.command == "root" else [case.command]), *options, "--json"]

    def command(self, name: str, args: list[str], expected: int, timeout: int) -> dict[str, Any]:
        """Retain exact arguments and both streams, including failures and watchdog expiry."""
        command = [str(self.binary), *args]
        (self.logs / f"{name}.command.json").write_text(
            json.dumps(command, indent=2) + "\n", encoding="utf-8"
        )
        try:
            result = subprocess.run(command, capture_output=True, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as error:
            (self.logs / f"{name}.stdout").write_bytes(error.output or b"")
            (self.logs / f"{name}.stderr").write_bytes(error.stderr or b"")
            raise
        (self.logs / f"{name}.stdout").write_bytes(result.stdout)
        (self.logs / f"{name}.stderr").write_bytes(result.stderr)
        excerpt = result.stdout.decode("utf-8", errors="replace")[:500]
        require(
            result.returncode == expected,
            f"exit {result.returncode}; expected {expected}: {excerpt}",
        )
        require(not result.stderr, "JSON command emitted diagnostic stderr")
        response: dict[str, Any] = json.loads(result.stdout.decode("utf-8"))
        self.validator.validate(response)
        require(response["exit_code"] == result.returncode, "response/process exit mismatch")
        return response

    def successful_process(self, case: Case, response: dict[str, Any]) -> dict[str, Any]:
        """Independently inspect delivered samples, record, source identity and real verify."""
        directory = self.bundles / case.id
        image, manifest = directory / "result.png", directory / "run.json"
        require(response["publication"] == "completed", "success publication not completed")
        require(response["output"] == str(image), "reported output spelling differs")
        record = json.loads(manifest.read_bytes())
        self.record_validator.validate(record)
        for stage, observation in record["execution"].items():
            if stage in response:
                require(
                    response[stage] == observation, f"{stage} response/record observations differ"
                )
        require(
            record["source"]["sha256"] == digest(self.sources[case.source]),
            "source byte identity differs",
        )
        require(response["record"]["sha256"] == digest(manifest), "record byte identity differs")
        raw = image.read_bytes()
        output = decode_output(raw)
        stage_check(case, response)
        sample_check(case, output, raw)
        if case.reference:
            compare_outputs(case, output, self.outputs[case.reference])
        self.command(case.id + "-verify", ["verify", str(directory), "--json"], 0, COMMAND_TIMEOUT)
        self.outputs[case.id] = output
        return {
            "width": output.width,
            "height": output.height,
            "depth": output.depth,
            "channels": len(output.pixels[0]),
            "image_sha256": digest(image),
            "quality": quality(case, output),
        }

    def execute(self, case: Case) -> None:
        """A bad case remains recorded while independent cases continue to expose other defects."""
        started = time.monotonic()
        item: dict[str, Any] = {"id": case.id, "group": case.group, "contract": "FAIL"}
        source_digest = digest(self.sources[case.source]) if case.source else ""
        before = set(self.bundles.iterdir())
        try:
            timeout = STRESS_TIMEOUT if case.group == "stress" else COMMAND_TIMEOUT
            response = self.command(case.id, self.arguments(case), case.exit_code, timeout)
            if case.error:
                require(response["error"]["code"] == case.error, "error classification differs")
                require(response["error"]["message"].strip(), "error message is empty")
            if case.command == "process" and case.source:
                if case.exit_code:
                    require(
                        response["publication"] == case.publication,
                        "refused request publication differs",
                    )
                    require(
                        set(self.bundles.iterdir()) == before, "refused request left output/staging"
                    )
                else:
                    item.update(self.successful_process(case, response))
                require(
                    digest(self.sources[case.source]) == source_digest,
                    "processing changed source bytes",
                )
            else:
                discovery_check(case, response)
            item["contract"] = "PASS"
        except (
            AssertionError,
            OSError,
            ValueError,
            KeyError,
            ValidationError,
            subprocess.TimeoutExpired,
        ) as error:
            item["failure"] = str(error)
        item["elapsed_seconds"] = time.monotonic() - started
        self.results.append(item)
        self.save()
        if item["contract"] != "PASS":
            print(f"FAIL {case.id}: {item['failure']}")
        elif len(self.results) % 25 == 0:
            print(f"Completed {len(self.results)} retained scenarios")

    def save(self) -> dict[str, Any]:
        """Write partial progress too, so an interrupted audit cannot become an empty success."""
        counts = Counter(result["contract"] for result in self.results)
        quality_checks = [result["quality"] for result in self.results if result.get("quality")]
        report = {
            "started_from_binary": str(self.binary),
            "binary_sha256": self.binary_sha256,
            "platform": self.platform,
            "python": self.python,
            "recorded_utc": datetime.now(UTC).isoformat(),
            "scenario_count": self.expected_count,
            "executed_count": len(self.results),
            "contract_counts": dict(counts),
            "quality_assessed": len(quality_checks),
            "quality_accepted": sum(bool(item["accepted"]) for item in quality_checks),
            "quality_gaps": sum(not item["accepted"] for item in quality_checks),
            "active_method_coverage": {
                method: [
                    item["id"]
                    for item in self.results
                    if item["id"] in identifiers and item["contract"] == "PASS"
                ]
                for method, identifiers in active_coverage(matrix()).items()
            },
            "cases": self.results,
        }
        (self.directory / "results.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8"
        )
        return report


def main() -> int:
    """Generate/replay the reviewable matrix without any runtime product component."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", nargs="?", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--write-matrix", action="store_true")
    args = parser.parse_args()
    if args.write_matrix:
        MATRIX_PATH.write_text(encoded_catalog(), encoding="utf-8")
        print(f"Wrote {len(matrix())} scenarios to {MATRIX_PATH}")
        return 0
    if args.binary is None or args.output_dir is None:
        parser.error("binary and --output-dir are required for execution")
    if not args.binary.is_file():
        parser.error("binary must be an existing freshly built executable")
    if MATRIX_PATH.read_text(encoding="utf-8") != encoded_catalog():
        parser.error("reviewed matrix is stale; regenerate with --write-matrix and review it")
    run = Run(args.binary.resolve(), args.output_dir.resolve())
    for case in matrix():
        run.execute(case)
    report = run.save()
    require(digest(run.binary) == run.binary_sha256, "binary changed during the audit")
    require(all(report["active_method_coverage"].values()), "an implemented active method failed")
    print(
        json.dumps(
            {
                key: report[key]
                for key in (
                    "executed_count",
                    "contract_counts",
                    "quality_assessed",
                    "quality_accepted",
                    "quality_gaps",
                )
            }
        )
    )
    return int(report["contract_counts"].get("FAIL", 0) > 0 or report["quality_gaps"] > 0)


if __name__ == "__main__":
    raise SystemExit(main())
