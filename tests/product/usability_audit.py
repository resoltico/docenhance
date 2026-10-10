#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Replay retained CLI usability probes against BINARY, preserving raw streams and file effects."""

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
from continuous_fixtures import RGBA, Fixture
from jsonschema import Draft202012Validator
from usability_cases import probes

if TYPE_CHECKING:
    from usability_cases import Probe

TIMEOUT_SECONDS = 30


def digest(path: Path) -> str:
    """Bind observations to exact local bytes, without treating identity as review quality."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(directory: Path) -> dict[str, str]:
    """Observe all regular file identities and directory names in the owned fixture/bundle tree."""
    return {
        str(path.relative_to(directory)): digest(path) if path.is_file() else "directory"
        for path in directory.rglob("*")
    }


def text_errors(case: Probe, stdout: bytes, stderr: bytes) -> list[str]:
    """Check selected human text separately from JSON structure and publication fields."""
    errors = []
    selected, other = (stderr, stdout) if case.exit_code else (stdout, stderr)
    if not selected or other:
        errors.append("text stream routing mismatch")
    try:
        text = selected.decode("utf-8")
    except UnicodeDecodeError:
        errors.append("non-UTF-8 text response")
        text = ""
    prefix = case.error + ": "
    if case.error and (
        not text.startswith(prefix) or not text.partition("\n")[0][len(prefix) :].strip()
    ):
        errors.append("text response omits expected error identifier/message")
    return errors


def fixtures(directory: Path) -> dict[str, Path]:
    """Encode deliberately explicit first-party gray, RGBA16 and nonimage sources."""
    directory.mkdir()
    paths = {
        name: directory / filename
        for name, filename in (
            ("gray", "gray.png"),
            ("rgba", "rgba16.png"),
            ("corrupt", "not-image.png"),
        )
    }
    paths["gray"].write_bytes(
        Fixture(
            32, 32, tuple((80 + (5 * x + 3 * y) % 175,) for y in range(32) for x in range(32))
        ).encoded()
    )
    paths["rgba"].write_bytes(
        Fixture(
            2,
            2,
            (
                (65535, 0, 0, 32768),
                (0, 65535, 0, 65535),
                (0, 0, 65535, 0),
                (30000, 30000, 30000, 65535),
            ),
            color=RGBA,
            depth=16,
        ).encoded()
    )
    paths["corrupt"].write_bytes(b"this is deliberately not an image\n")
    return paths


class Audit:
    """An explicitly selected binary and a fresh, exclusively owned evidence directory."""

    def __init__(self, binary: Path, directory: Path) -> None:
        """Refuse evidence overwrite and prepare fixed sources before invoking processing."""
        self.binary = binary.resolve(strict=True)
        self.binary_sha256 = digest(self.binary)
        self.directory = directory.resolve()
        self.directory.mkdir(parents=True, exist_ok=False)
        self.logs = self.directory / "commands"
        self.logs.mkdir()
        self.sources = fixtures(self.directory / "fixtures")
        self.source_identities = {name: digest(path) for name, path in self.sources.items()}
        self.bundles = self.directory / "bundles"
        self.bundles.mkdir()
        for name in {case.working_directory for case in probes() if case.working_directory}:
            (self.bundles / name).mkdir()
        self.occupied = self.bundles / "occupied"
        self.occupied.mkdir()
        (self.occupied / "keep.txt").write_bytes(b"preserve this result\n")
        self.occupied_inventory = inventory(self.occupied)
        self.empty = self.bundles / "empty"
        self.empty.mkdir()
        schema = json.loads((ROOT / "schemas/command-response.schema.json").read_bytes())
        Draft202012Validator.check_schema(schema)
        self.validator = Draft202012Validator(schema)
        self.results: list[dict[str, Any]] = []

    def path(self, value: str, case_id: str) -> Path:
        """Resolve only declared placeholders; never normalize an argument's identity spelling."""
        if value.startswith("{bundle:"):
            return self.bundles / value[len("{bundle:") : -1]
        paths = {
            **self.sources,
            "output": self.bundles / case_id,
            "occupied": self.occupied,
            "empty": self.empty,
            "missing": self.directory / "missing.png",
            "absent-parent": self.bundles / "absent-parent" / case_id,
            "option-looking-output": self.bundles / "--json",
            "explicit-option-looking-output": self.bundles / "explicit-value" / "--json",
        }
        return paths[value[1:-1]]

    def invoke(self, case: Probe) -> None:
        """Retain exact argv, raw streams, status, parsed response and file effects."""
        arguments = [
            str(self.path(value, case.id)) if value.startswith("{") else value
            for value in case.arguments
        ]
        command = [str(self.binary), *arguments]
        working_directory = self.bundles / case.working_directory
        (self.logs / (case.id + ".command.json")).write_text(
            json.dumps(command, indent=2) + "\n", encoding="utf-8"
        )
        before = inventory(self.bundles)
        started = time.monotonic()
        timed_out = False
        try:
            completed = subprocess.run(
                command,
                capture_output=True,
                timeout=TIMEOUT_SECONDS,
                check=False,
                cwd=working_directory,
            )
            stdout, stderr, status = completed.stdout, completed.stderr, completed.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr, status = error.stdout or b"", error.stderr or b"", None
            timed_out = True
        (self.logs / (case.id + ".stdout")).write_bytes(stdout)
        (self.logs / (case.id + ".stderr")).write_bytes(stderr)
        response, errors = self.response(case, stdout, stderr, status)
        output = self.path(case.output, case.id) if case.output else None
        after = inventory(self.bundles)
        source_observations = {
            name: digest(path) if path.is_file() else "missing"
            for name, path in self.sources.items()
        }
        errors.extend(self.effects(case, output, before, after, source_observations))
        self.results.append(
            {
                "id": case.id,
                "arguments": arguments,
                "working_directory": str(working_directory),
                "expected_exit": case.exit_code,
                "exit": status,
                "timed_out": timed_out,
                "elapsed_seconds": round(time.monotonic() - started, 6),
                "response": response,
                "output_exists": output.exists() if output else None,
                "effects_before": before,
                "effects_after": after,
                "source_sha256_after": source_observations,
                "passed": not errors,
                "failures": errors,
            }
        )

    def response(
        self, case: Probe, stdout: bytes, stderr: bytes, status: int | None
    ) -> tuple[dict[str, Any] | None, list[str]]:
        """Check the current transport/error contract without implying human UX acceptance."""
        errors = []
        if status != case.exit_code:
            errors.append(f"expected exit {case.exit_code}; observed {status}")
        if not (
            case.json_response if case.json_response is not None else "--json" in case.arguments
        ):
            return None, [*errors, *text_errors(case, stdout, stderr)]
        response: dict[str, Any] | None = None
        try:
            parsed = json.loads(stdout)
            if isinstance(parsed, dict):
                response = parsed
        except (ValueError, UnicodeDecodeError):
            errors.append("missing, malformed or non-UTF-8 JSON response")
        if response is None:
            return None, [*errors, "JSON response is not an object"]
        if stderr or response.get("exit_code") != status:
            errors.append("JSON stream or response/process status mismatch")
        errors.extend(error.message for error in self.validator.iter_errors(response))
        error = response.get("error")
        if case.error and (not isinstance(error, dict) or error.get("code") != case.error):
            errors.append("unexpected error identifier")
        if case.publication and response.get("publication") != case.publication:
            errors.append("unexpected publication state")
        return response, errors

    def effects(
        self,
        case: Probe,
        output: Path | None,
        before: dict[str, str],
        after: dict[str, str],
        source_observations: dict[str, str],
    ) -> list[str]:
        """Require preserved sources/results and unchanged refused or readonly effects."""
        errors = []
        if source_observations != self.source_identities:
            errors.append("source fixture changed")
        if inventory(self.occupied) != self.occupied_inventory:
            errors.append("occupied result changed")
        if output is None or case.exit_code:
            if before != after:
                errors.append("readonly or refused command changed bundle/staging entries")
            if output is not None and output != self.occupied and output.exists():
                errors.append("refused output exists")
            return errors
        prefix = str(output.relative_to(self.bundles))
        existing = {
            name: identity
            for name, identity in after.items()
            if name != prefix and not name.startswith(prefix + "/")
        }
        if existing != before:
            errors.append("publication changed another result or left unexpected staging")
        if not output.is_dir() or not (output / "result.png").is_file():
            errors.append("successful command has no output image")
        if not (output / "run.json").is_file():
            errors.append("successful command has no complete record")
        return errors

    def save(self, summary: Path | None) -> bool:
        """Keep current probe checks distinct from manually reviewed open UX acceptance."""
        cases = probes()
        failures = [result for result in self.results if not result["passed"]]
        expected_ids = [case.id for case in cases]
        inventory_matches = expected_ids == [result["id"] for result in self.results]
        binary_unchanged = self.binary.is_file() and digest(self.binary) == self.binary_sha256
        document = {
            "date_utc": datetime.now(UTC).isoformat(),
            "binary": str(self.binary),
            "binary_sha256": self.binary_sha256,
            "binary_unchanged": binary_unchanged,
            "platform": platform.platform(),
            "machine": platform.machine(),
            "evidence_directory": str(self.directory),
            "fixture_sha256": self.source_identities,
            "expected_cases": len(cases),
            "executed_cases": len(self.results),
            "case_inventory_matches": inventory_matches,
            "passed": len(self.results) - len(failures),
            "failed": len(failures),
            "exit_counts": dict(Counter(str(result["exit"]) for result in self.results)),
            "probe_scope": "current exit/stream/schema/error/publication and file effects",
            "ux_acceptance": "Manual review; passing probes do not resolve UX-01 through UX-06",
            "cases": self.results,
        }
        (self.directory / "results.json").write_text(
            json.dumps(document, indent=2) + "\n", encoding="utf-8"
        )
        if summary:
            compact = {key: value for key, value in document.items() if key != "cases"}
            compact["cases"] = [
                {key: result[key] for key in ("id", "expected_exit", "exit", "passed", "failures")}
                for result in self.results
            ]
            summary.write_text(json.dumps(compact, indent=2) + "\n", encoding="utf-8")
        print(f"Usability audit: {len(self.results)} cases, {len(failures)} failed")
        for result in failures:
            print(result["id"], result["failures"])
        return not failures and binary_unchanged and inventory_matches


def main() -> int:
    """Run every retained probe on a selected binary and preserve complete failure evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    cases = probes()
    if len({case.id for case in cases}) != len(cases):
        parser.error("Probe identifiers must be unique to preserve command evidence")
    audit = Audit(args.binary, args.evidence)
    for case in cases:
        audit.invoke(case)
    return 0 if audit.save(args.summary) else 1


if __name__ == "__main__":
    raise SystemExit(main())
