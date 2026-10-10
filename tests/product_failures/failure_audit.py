#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Retain actual CLI responses and independent exit/publication/effect observations."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import time
from pathlib import Path
from threading import Lock
from typing import Any

from failure_boundaries import run_boundaries
from failure_fixtures import container_refusals, gray_png, png_refusals

INPUT_FAILURE = (3, "E_INPUT", "not_started")
RESOURCE_FAILURE = (4, "E_RESOURCE", "not_started")


def digest(path: Path) -> str:
    """Hash an artifact in bounded transfers, independent of product identities."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def decoded_response(stdout: bytes) -> dict[str, Any]:
    """Malformed/missing response is a failed observation, never evidence of no effect."""
    try:
        value = json.loads(stdout)
    except ValueError:
        return {}
    return value if isinstance(value, dict) else {}


class Audit:
    """An explicitly selected binary and an exclusively owned evidence directory."""

    def __init__(self, executable: Path, evidence: Path) -> None:
        """Create fresh evidence; never overwrite a previous audit."""
        self.executable = executable.resolve(strict=True)
        self.executable_sha256 = digest(self.executable)
        self.evidence = evidence.resolve()
        self.evidence.mkdir(parents=True, exist_ok=False)
        self.workspace = self.evidence / "workspace"
        self.workspace.mkdir()
        self.results: list[dict[str, Any]] = []
        self.lock = Lock()

    def invoke(
        self, name: str, arguments: list[str | bytes], expected: tuple[int, str, str]
    ) -> dict[str, Any]:
        """Record the complete response and compare independently specified expected facts."""
        started = time.monotonic()
        command = [os.fsencode(self.executable), *map(os.fsencode, arguments)]
        result = subprocess.run(command, capture_output=True, check=False, timeout=30)
        self.save_output(name, result.stdout, result.stderr)
        response = decoded_response(result.stdout)
        code = response.get("error", {}).get("code", "")
        publication = response.get("publication", "")
        observed = (result.returncode, code, publication)
        record = {
            "case": name,
            "arguments": [os.fsdecode(value) for value in arguments],
            "expected": list(expected),
            "observed": list(observed),
            "message": response.get("error", {}).get("message", ""),
            "elapsed_seconds": round(time.monotonic() - started, 6),
            "passed": observed == expected and not result.stderr,
        }
        record["passed"] &= response.get("exit_code") == result.returncode
        if expected[1]:
            record["passed"] &= bool(record["message"])
        with self.lock:
            self.results.append(record)
        return response

    def save_output(self, name: str, stdout: bytes, stderr: bytes) -> None:
        """Keep full stream bytes, including deliberate delivery failures."""
        (self.evidence / f"{name}.stdout").write_bytes(stdout)
        (self.evidence / f"{name}.stderr").write_bytes(stderr)

    def check(self, name: str, condition: object, detail: str) -> None:
        """Retain an artifact/property observation separately from the CLI's own claims."""
        with self.lock:
            self.results.append({"case": name, "passed": bool(condition), "detail": detail})

    def process(self, source: Path | bytes, target: Path | bytes) -> list[str | bytes]:
        """Build the public default conversion command, without shell interpretation."""
        return ["process", os.fsencode(source), "--out-dir", os.fsencode(target), "--json"]

    def refuse_source(
        self,
        name: str,
        data: bytes,
        expected: tuple[int, str, str],
        *,
        options: tuple[str, ...] = (),
    ) -> None:
        """A refusal must preserve source bytes and leave no output or owned stage."""
        directory = self.workspace / name
        directory.mkdir()
        source, target = directory / "input", directory / "output"
        source.write_bytes(data)
        self.invoke(name, [*self.process(source, target), *options], expected)
        self.check(
            f"{name}-effects",
            source.read_bytes() == data and set(directory.iterdir()) == {source},
            "Source preserved; no destination or staging effects",
        )

    def write_results(self) -> bool:
        """Publish measured evidence with binary identity and explicit coverage limits."""
        self.results.sort(key=lambda result: result["case"])
        binary_unchanged = (
            self.executable.is_file() and digest(self.executable) == self.executable_sha256
        )
        result = {
            "executable": str(self.executable),
            "executable_sha256": self.executable_sha256,
            "executable_unchanged": binary_unchanged,
            "platform": platform.platform(),
            "machine": platform.machine(),
            "response_schema_validation": (
                "Not performed by this stdlib runner; native CLI suites own it"
            ),
            "cases": self.results,
        }
        (self.evidence / "results.json").write_text(json.dumps(result, indent=2) + "\n")
        failures = [item for item in self.results if not item["passed"]]
        print(f"Failure audit: {len(self.results)} observations, {len(failures)} failed")
        if not binary_unchanged:
            print("Failure audit executable identity changed or is unavailable")
        for failure in failures:
            print(json.dumps(failure, ensure_ascii=True))
        return not failures and binary_unchanged


def resources(audit: Audit) -> None:
    """Oversized headers and encoded files must refuse rather than decode approximately."""
    audit.refuse_source("png-pixel-ceiling", gray_png(8000, 5001), RESOURCE_FAILURE)
    directory = audit.workspace / "encoded-ceiling"
    directory.mkdir()
    source = directory / "sparse.png"
    with source.open("wb") as stream:
        stream.write(gray_png())
        stream.truncate(128 * 1024 * 1024 + 1)
    before = source.stat()
    audit.invoke("encoded-ceiling", audit.process(source, directory / "output"), RESOURCE_FAILURE)
    after = source.stat()
    audit.check(
        "encoded-ceiling-effects",
        (before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns)
        and set(directory.iterdir()) == {source},
        "Oversized sparse source unchanged; no staging or output",
    )


def malformed_binary_dimensions(audit: Audit) -> None:
    """The stored-sample binary branch must also distinguish malformed shapes from resources."""
    for name, width, height in (("binary-zero-width", 0, 1), ("binary-zero-height", 2, 0)):
        audit.refuse_source(
            name,
            gray_png(width, height),
            INPUT_FAILURE,
            options=("--output-mode", "bw", "--binarize", "fixed"),
        )


def main() -> int:
    """Replay retained adversarial definitions against an explicitly named real executable."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--interrupt-driver", type=Path)
    args = parser.parse_args()
    audit = Audit(args.executable, args.evidence)
    for name, data in {**png_refusals(), **container_refusals()}.items():
        audit.refuse_source(name, data, INPUT_FAILURE)
    malformed_binary_dimensions(audit)
    resources(audit)
    run_boundaries(audit, args.interrupt_driver)
    return 0 if audit.write_results() else 1


if __name__ == "__main__":
    raise SystemExit(main())
