#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Read cleared external corpus/reviews and verify real candidate, comparator and repeat bundles."""

from __future__ import annotations

import argparse
import importlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, ValidationError

from document_quality import (
    ROOT,
    adjudicate,
    frozen_digest,
    read_json,
    validate_plan,
    validator,
)
from quality_collection import SEMANTIC_FACTS, BundleChecks, collect
from quality_evidence import admit, command_options, confined, digest, evidence_error
from quality_protocol import BUNDLE_ROLES
from quality_replay import ReplayObservation, execute_replay

# Independent first-party PNG decoder already used by the numerical/product references.
sys.path.insert(0, str(ROOT / "tests/cli"))
MIN_PROCESS_ARGV = 6
decode_output = importlib.import_module("continuous_fixtures").decode_output


def read_bundle(executable: Path, directory: Path, source: str) -> dict[str, Any]:
    """Run the production read-only verifier; record provenance alone cannot assert verification."""
    result = subprocess.run(
        [str(executable), "verify", str(directory), "--json"],
        capture_output=True,
        check=True,
        timeout=120,
    )
    response = read_json(result.stdout)
    record = read_json((directory / "run.json").read_bytes())
    for name, value in (("command-response", response), ("run-record", record)):
        Draft202012Validator(
            read_json((ROOT / f"schemas/{name}.schema.json").read_bytes())
        ).validate(value)
    if (
        result.stderr
        or response.get("command") != "verify"
        or response.get("run") != record["record"]["run"]
        or response.get("recorded") != record["record"]["recorded"]
        or response.get("establishes") != "artifacts_agree_with_record"
        or record["source"]["sha256"] != source
    ):
        message = "Verification or candidate source identity differs"
        raise ValueError(message)
    return record


def replay_options(options: list[str], root: Path) -> None:
    """Admit declared processing options without letting evidence redirect evaluation outputs."""
    contract = json.loads((ROOT / "spec/cli-contract.json").read_bytes())
    allowed = {
        option["name"]
        for option in contract["options"]
        if option["metavar"] and option["name"] != "--out-dir"
    }
    if len(options) % 2 or any(options[i] not in allowed for i in range(0, len(options), 2)):
        message = "Replay requires exact processing option/value pairs with no output override"
        raise ValueError(message)
    for i in range(0, len(options), 2):
        if options[i] in {"--protect-mask", "--psf-file"}:
            confined(root, options[i + 1])


def replay(command: list[str], root: Path) -> ReplayObservation:
    """Copy admitted full argv and substitute only its owned output-destination value."""
    if len(command) < MIN_PROCESS_ARGV:
        message = "Replay requires a complete frozen process command"
        raise ValueError(message)
    _, options = command_options(command, command[2], Path(command[0]))
    replay_options(options, root)
    return execute_replay(command, root, read_bundle)


def check_comparator(candidate: dict[str, Any], baseline: dict[str, Any]) -> None:
    """Prevent changed representation policy from masquerading as enhancement benefit."""
    operation = candidate["request"]["operation"]
    comparator = baseline["request"]["operation"]
    if operation["kind"] == "continuous" and operation != comparator:
        message = "Representation policies differ; enhancement cannot claim conversion benefit"
        raise ValueError(message)
    if operation["kind"] == "binarization" and (
        comparator["kind"] != "continuous"
        or comparator["parameters"]
        != {"output_mode": "preserve", "depth": "8", "alpha": "white", "profile": "srgb"}
        or baseline["execution"]["conversion"]["source_orientation"] != 1
    ):
        message = (
            "Binary comparator requires normal-oriented stored-gray identity under explicit sRGB"
        )
        raise ValueError(message)


def check_repetition(candidate: dict[str, Any], replicate: dict[str, Any]) -> None:
    """Require separate execution identities and identical build-bound repeat facts."""
    if candidate["record"]["run"] == replicate["record"]["run"]:
        message = "Repeatability requires independently identified executions"
        raise ValueError(message)
    if any(candidate[name] != replicate[name] for name in SEMANTIC_FACTS):
        message = "Repeated processing differs in source, protection or processing/build facts"
        raise ValueError(message)


def fidelity(root: Path, assessment: dict[str, Any], records: dict[str, Any]) -> None:
    """Compare repeated output, geometry and protected integers with the independent decoder."""
    candidate, baseline, replicate = (records[name] for name in BUNDLE_ROLES)
    check_repetition(candidate, replicate)
    image = decode_output((confined(root, assessment["candidate"]) / "result.png").read_bytes())
    reference = decode_output((confined(root, assessment["baseline"]) / "result.png").read_bytes())
    if (image.width, image.height) != (reference.width, reference.height) or candidate["request"][
        "rotation_degrees"
    ] != baseline["request"]["rotation_degrees"]:
        message = "Candidate and comparator geometry differ"
        raise ValueError(message)
    check_comparator(candidate, baseline)
    for name in ("illumination", "denoising", "restoration", "contrast", "sharpening"):
        report = baseline["execution"][name]
        if report.get("requested") is not None or report.get("method") is not None:
            message = "Comparator must contain no photometric enhancement"
            raise ValueError(message)
    protection = candidate["protection"]
    if protection != baseline["protection"]:
        message = "Candidate and comparator protection masks differ"
        raise ValueError(message)
    if protection is not None:
        mask = decode_output(
            (confined(root, assessment["candidate"]) / protection["stored"]["path"]).read_bytes()
        )
        turns = candidate["request"]["rotation_degrees"] // 90
        width, height = mask.width, mask.height
        for v in range(height):
            for u in range(width):
                if mask.pixels[v * width + u][0] == 0:
                    continue
                x, y = (
                    (u, v),
                    (height - 1 - v, u),
                    (width - 1 - u, height - 1 - v),
                    (v, width - 1 - u),
                )[turns]
                index = y * image.width + x
                if image.pixels[index] != reference.pixels[index]:
                    message = "Protected destination differs from the unenhanced comparator"
                    raise ValueError(message)


def evaluate(plan: dict[str, Any], root: Path, executable: Path) -> dict[str, Any]:
    """Validate all provenance and real bundle boundaries before scoring external human evidence."""
    validate_plan(plan)
    build = admit(plan, root, executable, replay_options)
    assessed, evidence = collect(
        plan, root, BundleChecks(executable, build, read_bundle, replay, fidelity)
    )
    prior = {
        page["sample"]: [
            review for revision in page["prior_reviews"] for review in revision["reviews"]
        ]
        for page in evidence
    }
    report = adjudicate(assessed, prior)
    report.update(
        {
            "executable_sha256": digest(executable),
            "frozen_corpus_sha256": plan["protocol"]["frozen_corpus_sha256"],
            "clearance": "curator assertions; requires independent rights and reviewer review",
            "build_commit_binding": (
                "independently referenced build provenance; byte identity verified locally"
            ),
            "evidence": evidence,
        }
    )
    report["report_sha256"] = frozen_digest(report)
    validator("report").validate(report)
    return report


def main() -> int:
    """Missing or invalid evidence refuses an acceptance report; no successful empty evaluation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = evaluate(
            read_json(args.manifest.read_bytes()),
            args.manifest.parent.resolve(),
            args.executable.resolve(),
        )
        with args.out.open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError, TypeError, ValidationError, subprocess.SubprocessError) as error:
        print(f"Document-quality evidence refused: {evidence_error(error)}", file=sys.stderr)
        return 1
    return 0 if report["decision"] == "accepted" else 1


if __name__ == "__main__":
    raise SystemExit(main())
