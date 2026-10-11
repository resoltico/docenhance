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
import signal
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from fuzz_manifest import targets

MAX_FINDINGS = 32
MAX_CAPTURE_BYTES = 32768
REPLAY_SECONDS = 10
FINDING_PATH = re.compile(r"(?:afl/default/(?:crashes|hangs)/id[^/]+|artifacts/[^/]+)")


def replay(binary: Path, candidate: Path, seconds: int = REPLAY_SECONDS) -> dict[str, Any]:
    """Re-execute one exact input via libFuzzer's direct-file interface, time bounded."""
    code: int | None = None
    timed_out = False
    options: dict[str, str] = {}
    # Use the strict sanitizer configuration without inherited AFL skip/bypass options.
    from fuzz_manifest import settings

    environment = {k: v for k, v in os.environ.items() if not k.startswith("AFL_")}
    environment.update(settings()["sanitizer_options"])
    with tempfile.TemporaryFile() as output:
        with subprocess.Popen(
            [str(binary), "-runs=1", str(candidate)],
            stdout=output,
            stderr=subprocess.STDOUT,
            env=environment,
            start_new_session=os.name == "posix",
        ) as child:
            try:
                code = child.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                if os.name == "posix":
                    try:
                        os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                else:
                    child.kill()
                child.wait()
        output.seek(0)
        captured = output.read(MAX_CAPTURE_BYTES + 1)
    options["output"] = captured[:MAX_CAPTURE_BYTES].decode("utf-8", errors="replace")
    options["truncated"] = len(captured) > MAX_CAPTURE_BYTES
    return {
        "exit_code": code,
        "timed_out": timed_out,
        "reproduced": code is not None and code != 0 and not timed_out,
        **options,
    }


def triage(build: Path, engine: str) -> dict[str, Any]:
    """Identify original crash bytes from the recorded campaign and report replay results."""
    workspace = build / "fuzz-work"
    record: dict[str, Any] = {"schema_version": 1, "engine": engine, "cases": []}
    if not workspace.is_dir():
        record["status"] = "no_campaign_evidence"
        return record
    declared = {target.name: target.max_len for target in targets()}
    occurrences: set[str] = set()
    campaigns = sorted(path for path in workspace.glob("campaign-*") if path.is_dir())
    if len(campaigns) != 1:
        record["status"] = "ambiguous_campaign" if campaigns else "no_campaign_evidence"
        return record
    for directory in sorted(campaigns[0].iterdir()):
        result = directory / "result.json"
        if not directory.is_dir() or not result.is_file():
            continue
        report = json.loads(result.read_text(encoding="utf-8"))
        target = report.get("target")
        if target not in declared or not directory.name.startswith(f"{target}-"):
            raise ValueError(f"Unrecognized target result: {directory.name}")
        if report.get("engine") != engine:
            raise ValueError(f"Wrong engine in finding report: {target}")
        binary = build / "fuzz" / f"de_fuzz_{target}"
        findings = report.get("findings", [])
        if not isinstance(findings, list):
            raise ValueError("Malformed finding list")
        candidate_paths = sorted(x for x in findings if isinstance(x, str) and FINDING_PATH.fullmatch(x))
        if not candidate_paths:
            continue
        if not binary.is_file() or binary.is_symlink():
            raise ValueError(f"Missing or unsafe reproduction binary: {target}")
        if hashlib.sha256(binary.read_bytes()).hexdigest() != report.get("binary_sha256"):
            raise ValueError(f"Finding executable identity changed: {target}")
        for relative in candidate_paths:
            if len(occurrences) >= MAX_FINDINGS:
                record["status"] = "findings_limit"
                return record
            candidate = directory / relative
            if candidate.is_symlink() or not candidate.is_file():
                raise ValueError(f"Nonregular or missing saved finding: {relative}")
            if candidate.stat().st_size > declared[target]:
                raise ValueError(f"Oversized saved finding: {target}")
            digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
            if digest in occurrences:
                continue
            occurrences.add(digest)
            observation = replay(binary.resolve(), candidate.resolve())
            record["cases"].append({
                "target": target,
                "input": relative,
                "sha256": digest,
                "bytes": candidate.stat().st_size,
                **observation,
            })
    record["status"] = "complete"
    return record


def main() -> int:
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
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Fuzz finding triage failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
