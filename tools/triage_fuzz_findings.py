#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bounded offline replay of saved fuzz findings, without promoting or excusing them."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from fuzz_manifest import settings, targets
from process_execution import CapturePolicy, bounded_capture

MAX_FINDINGS = 32
MAX_CAPTURE_BYTES = 32768
REPLAY_SECONDS = 10
FINDING_PATH = re.compile(r"(?:afl/default/(?:crashes|hangs)/id[^/]+|artifacts/[^/]+)")


def replay(binary: Path, candidate: Path, seconds: int = REPLAY_SECONDS) -> dict[str, Any]:
    """Re-execute one exact input via libFuzzer's direct-file interface, time bounded."""
    code: int | None = None
    timed_out = False
    # Use the strict sanitizer configuration without inherited AFL skip/bypass options.
    environment = {k: v for k, v in os.environ.items() if not k.startswith("AFL_")}
    environment.update(settings()["sanitizer_options"])
    with tempfile.TemporaryDirectory(prefix="docenhance-fuzz-replay-") as temporary:
        log = Path(temporary) / "replay.log"
        try:
            code = bounded_capture(
                [str(binary), "-runs=1", str(candidate)],
                environment,
                log,
                seconds,
                CapturePolicy(MAX_CAPTURE_BYTES, lambda: None),
            )
        except subprocess.TimeoutExpired as error:
            if getattr(error, "__notes__", None):
                raise
            timed_out = True
        captured = log.read_bytes()
    return {
        "exit_code": code,
        "timed_out": timed_out,
        "reproduced": code is not None and code != 0 and not timed_out,
        "output": captured.decode("utf-8", errors="replace"),
        "truncated": len(captured) > MAX_CAPTURE_BYTES,
    }


def owned_path(path: Path, *, directory: bool) -> None:
    """Refuse links and reparse points before inspecting owned replay evidence."""
    observed = path.lstat()
    admitted = stat.S_ISDIR(observed.st_mode) if directory else stat.S_ISREG(observed.st_mode)
    if (
        not admitted
        or getattr(observed, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    ):
        message = f"Unsafe fuzz replay evidence owner: {path}"
        raise ValueError(message)


def verify_target(
    build: Path, directory: Path, engine: str, declared: dict[str, int]
) -> tuple[str, Path, list[str]]:
    """Require source-declared target identity, matching engine and original binary."""
    owned_path(directory, directory=True)
    owned_path(directory / "result.json", directory=False)
    report = json.loads((directory / "result.json").read_text(encoding="utf-8"))
    target = report.get("target")
    if target not in declared or not directory.name.startswith(f"{target}-"):
        message = f"Unrecognized target result: {directory.name}"
        raise ValueError(message)
    if report.get("engine") != engine:
        message = f"Wrong engine in finding report: {target}"
        raise ValueError(message)
    findings = report.get("findings", [])
    if not isinstance(findings, list):
        message = "Malformed finding list"
        raise TypeError(message)
    marker = "afl/default/fuzzer_stats"
    if engine == "afl" and marker in findings:
        owned_finding(directory, marker)
        findings = [value for value in findings if value != marker]
    if any(
        not isinstance(value, str) or "\\" in value or not FINDING_PATH.fullmatch(value)
        for value in findings
    ):
        message = "Malformed saved finding path"
        raise ValueError(message)
    candidates = sorted(findings)
    binary = build / "fuzz" / f"de_fuzz_{target}"
    if candidates:
        if not binary.is_file() or binary.is_symlink():
            message = f"Missing or unsafe reproduction binary: {target}"
            raise ValueError(message)
        if hashlib.sha256(binary.read_bytes()).hexdigest() != report.get("binary_sha256"):
            message = f"Finding executable identity changed: {target}"
            raise ValueError(message)
    return target, binary, candidates


def owned_finding(directory: Path, relative: str) -> Path:
    """Validate every evidence ancestor, including an AFL failure-only statistics marker."""
    candidate = directory / relative
    for ancestor in reversed(candidate.relative_to(directory).parents):
        owned_path(directory / ancestor, directory=True)
    owned_path(candidate, directory=False)
    return candidate


def finding_record(directory: Path, target: str, relative: str, max_len: int) -> dict[str, Any]:
    """Validate original finding bytes before admitting one replay per content identity."""
    candidate = owned_finding(directory, relative)
    if candidate.stat().st_size > max_len:
        message = f"Oversized saved finding: {target}"
        raise ValueError(message)
    digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
    return {
        "target": target,
        "input": relative,
        "sha256": digest,
        "bytes": candidate.stat().st_size,
    }


def triage(build: Path, engine: str) -> dict[str, Any]:
    """Identify saved crash bytes from precisely one campaign and replay them separately."""
    workspace = build / "fuzz-work"
    record: dict[str, Any] = {"schema_version": 1, "engine": engine, "cases": []}
    if not workspace.is_dir():
        record["status"] = "no_campaign_evidence"
        return record
    owned_path(workspace, directory=True)
    declared = {target.name: target.max_len for target in targets()}
    occurrences: set[tuple[str, str]] = set()
    campaigns = sorted(path for path in workspace.glob("campaign-*") if path.is_dir())
    if len(campaigns) != 1:
        record["status"] = "ambiguous_campaign" if campaigns else "no_campaign_evidence"
        return record
    owned_path(campaigns[0], directory=True)
    for directory in sorted(campaigns[0].iterdir()):
        if not directory.is_dir() or not (directory / "result.json").is_file():
            continue
        target, binary, candidates = verify_target(build, directory, engine, declared)
        for relative in candidates:
            entry = finding_record(directory, target, relative, declared[target])
            identity = (target, entry["sha256"])
            if identity in occurrences:
                continue
            if len(occurrences) >= MAX_FINDINGS:
                record["status"] = "findings_limit"
                return record
            entry.update(replay(binary.resolve(), (directory / relative).resolve()))
            occurrences.add(identity)
            record["cases"].append(entry)
    record["status"] = "complete"
    return record


def main() -> int:
    """Write independent bounded replay diagnostics without excusing original findings."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--engine", choices=("afl", "libfuzzer"), required=True)
    args = parser.parse_args()
    root = args.build.resolve()
    try:
        observation = triage(root, args.engine)
        if (root / "fuzz-work").is_dir():
            (root / "fuzz-work" / "ci-triage.json").write_text(
                json.dumps(observation, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
        print(f"Fuzz triage: {observation['status']}; {len(observation['cases'])} unique findings")
        for case in observation["cases"]:
            print(
                f"  {case['target']}: {case['sha256']}; "
                f"reproduced={case['reproduced']}; timed_out={case['timed_out']}"
            )
        if summary := os.getenv("GITHUB_STEP_SUMMARY"):
            with Path(summary).open("a", encoding="utf-8") as stream:
                stream.write(
                    f"### {args.engine} finding triage\n\n"
                    f"Status: {observation['status']}; "
                    f"{len(observation['cases'])} unique inputs. "
                    "Saved fuzz findings remain failures regardless of replay outcome.\n"
                )
        return 0 if observation["status"] in {"complete", "no_campaign_evidence"} else 1
    except (OSError, ValueError, TypeError, subprocess.SubprocessError) as error:
        print(f"Fuzz finding triage failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
