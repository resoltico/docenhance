#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Evaluate frozen external quality evidence through one bounded append-only v3 run."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, ValidationError

from quality_acquisition import acquire_json_file, acquire_payload
from quality_attempt import delivered_path
from quality_collection import BundleChecks, collect
from quality_evidence import admit_evaluation, evidence_error
from quality_fidelity import fidelity
from quality_journal import JournalError
from quality_json import ROOT, read_json
from quality_replay import replay
from quality_report import report
from quality_run import QualityRun
from quality_schema import validator


def read_bundle(
    checks: BundleChecks,
    invocation_index: int,
    source_sha256: str,
    published_response: dict[str, Any],
) -> dict[str, Any]:
    """Consume this fixed verifier first; acquire/hash/parse exactly its observed record bytes."""
    run = checks.run
    attempt = run.invoke(invocation_index)
    if attempt["outcome"] != "completed":
        message = "Read-only verifier lacks coherent complete delivery"
        raise ValueError(message)
    response = attempt["response"]
    confirmed = response.get("confirmed", [])
    records = [entry for entry in confirmed if entry["path"] == "run.json"]
    if len(records) != 1 or not 0 < records[0]["bytes"] <= 256 * 1024:
        message = "Verifier did not confirm the bounded record-role identity"
        raise ValueError(message)
    target = run.slots[invocation_index]["planned_target"]
    identity = records[0]
    payload = acquire_payload(run.root, {**identity, "path": target + "/run.json"})
    record = read_json(payload)
    Draft202012Validator(acquire_json_file(ROOT / "schemas/run-record.schema.json")).validate(
        record
    )
    expected = [record["output"], *record["assets"], identity]
    if len(confirmed) != len(expected):
        message = "Verifier confirmations differ from the closed acquired inventory"
        raise ValueError(message)
    for entry in expected:
        observed = [item for item in confirmed if item["path"] == entry["path"]]
        if len(observed) != 1 or any(
            observed[0][key] != entry[key] for key in ("path", "sha256", "bytes")
        ):
            message = "Verifier file identity differs from acquired record"
            raise ValueError(message)
    if (
        response.get("command") != "verify"
        or response.get("directory") != target
        or response.get("run") != record["record"]["run"]
        or response.get("recorded") != record["record"]["recorded"]
        or response.get("establishes") != "artifacts_agree_with_record"
        or record["source"]["sha256"] != source_sha256
    ):
        message = "Verification or frozen source identity differs"
        raise ValueError(message)
    if (
        published_response.get("command") != "process"
        or published_response.get("publication") != "completed"
        or published_response.get("record_sha256") != identity["sha256"]
        or published_response.get("record") != record
        or published_response.get("output") != delivered_path(target, "result.png")
    ):
        message = "Process delivery differs from the same-snapshot verified record"
        raise ValueError(message)
    return record


def evaluate(plan: dict[str, Any], root: Path, executable: Path, output: Path) -> dict[str, Any]:
    """Own and flush intended work before any process or invocation-directory preparation."""
    inputs = admit_evaluation(plan, root, executable)
    plan = inputs.plan
    run = QualityRun(inputs, output)
    try:
        checks = BundleChecks(run, inputs.build, read_bundle, replay, fidelity)
        evidence = collect(plan, checks)
        prior = {
            page["sample"]: [
                review for revision in page["prior_reviews"] for review in revision["reviews"]
            ]
            for page in evidence
        }
        result = report(plan, plan["protocol"]["executable_sha256"], evidence, run.errors, prior)
        validator("report").validate(result)
        return run.finish(result)
    finally:
        run.close()


def main() -> int:
    """A prefix or unsuccessful final close never earns successful report delivery."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        value = evaluate(
            acquire_json_file(args.manifest),
            args.manifest.parent.resolve(),
            args.executable.resolve(),
            args.out,
        )
    except (
        OSError,
        ValueError,
        TypeError,
        KeyError,
        MemoryError,
        ValidationError,
        JournalError,
    ) as error:
        print(f"Document-quality evidence refused: {evidence_error(error)}", file=sys.stderr)
        return 1
    return 0 if value["report"]["decision"] == "accepted" else 1


if __name__ == "__main__":
    raise SystemExit(main())
