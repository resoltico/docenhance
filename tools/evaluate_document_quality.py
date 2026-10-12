#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Read cleared external corpus/reviews and verify real candidate, comparator and repeat bundles."""

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from jsonschema import ValidationError

from document_quality import BUNDLE_ROLES, ROOT, adjudicate, validate_plan

# Independent first-party PNG decoder already used by the numerical/product references.
sys.path.insert(0, str(ROOT / "tests/cli"))
decode_output = importlib.import_module("continuous_fixtures").decode_output


def digest(path: Path) -> str:
    """Hash actual bytes, including the source, executable and final evidence document."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def confined(root: Path, name: str) -> Path:
    """Evaluation reads only ordinary relative inputs in its separately governed asset root."""
    path = root / name
    if (
        Path(name).is_absolute()
        or ".." in Path(name).parts
        or not path.resolve().is_relative_to(root)
    ):
        message = "Evaluation path escapes its corpus root"
        raise ValueError(message)
    if any(part.is_symlink() for part in (path, *path.parents)):
        message = "Evaluation evidence must not redirect through symbolic links"
        raise ValueError(message)
    return path


def read_bundle(executable: Path, directory: Path, source: str) -> dict[str, Any]:
    """Run the production read-only verifier; record provenance alone cannot assert verification."""
    result = subprocess.run(
        [str(executable), "verify", str(directory), "--json"],
        capture_output=True,
        text=True,
        check=True,
        timeout=120,
    )
    response = json.loads(result.stdout)
    record: dict[str, Any] = json.loads((directory / "run.json").read_bytes())
    if (
        result.stderr
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


def replay(executable: Path, source: Path, root: Path, options: list[str]) -> dict[str, Any]:
    """Execute frozen settings; agreeing records alone do not prove this binary produced them."""
    replay_options(options, root)
    with tempfile.TemporaryDirectory(prefix="docenhance-quality-replay-") as temporary:
        output = Path(temporary) / "candidate"
        result = subprocess.run(
            [
                str(executable),
                "process",
                str(source),
                "--out-dir",
                str(output),
                *options,
                "--json",
            ],
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
            timeout=600,
        )
        if result.stderr or json.loads(result.stdout).get("publication") != "completed":
            message = "Frozen candidate replay did not complete"
            raise ValueError(message)
        return read_bundle(executable, output, digest(source))


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
    if any(
        candidate[name] != replicate[name] for name in ("request", "execution", "output", "build")
    ):
        message = "Repeated processing differs in samples, parameters, observations or build"
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
    if digest(executable) != plan["executable_sha256"]:
        message = "Executable differs from the reviewed evaluation identity"
        raise ValueError(message)
    for sample in plan["corpus"]:
        if digest(confined(root, sample["path"])) != sample["sha256"]:
            message = "Source input changed after corpus freezing"
            raise ValueError(message)
    samples = {sample["id"]: sample for sample in plan["corpus"]}
    evidence = []
    for assessment in plan["assessments"]:
        directories = [confined(root, assessment[name]) for name in BUNDLE_ROLES]
        identities = [path.stat() for path in directories]
        if len({(item.st_dev, item.st_ino) for item in identities}) != len(BUNDLE_ROLES):
            message = "Evaluation bundles alias the same physical directory"
            raise ValueError(message)
        records = {
            name: read_bundle(
                executable,
                confined(root, assessment[name]),
                samples[assessment["sample"]]["sha256"],
            )
            for name in BUNDLE_ROLES
        }
        fidelity(root, assessment, records)
        source = confined(root, samples[assessment["sample"]]["path"])
        for name, option_field in (
            ("candidate", "options"),
            ("baseline", "baseline_options"),
            ("replicate", "options"),
        ):
            actual = replay(executable, source, root, assessment[option_field])
            if any(
                actual[field] != records[name][field]
                for field in ("request", "execution", "output", "build")
            ):
                message = f"Frozen {name} differs from actual processing by the selected executable"
                raise ValueError(message)
        actual = records["candidate"]
        evidence.append(
            {
                "sample": assessment["sample"],
                "options": assessment["options"],
                "baseline_options": assessment["baseline_options"],
                "source": actual["source"],
                "request": actual["request"],
                "build": actual["build"],
                "output": actual["output"],
                "records": {
                    name: digest(confined(root, assessment[name]) / "run.json") for name in records
                },
            }
        )
    report = adjudicate(plan)
    report.update(
        {
            "executable_sha256": digest(executable),
            "frozen_corpus_sha256": plan["frozen_corpus_sha256"],
            "clearance": "curator assertions; requires independent rights review",
            "build_commit_binding": "external reviewed build provenance; digest verified locally",
            "evidence": evidence,
        }
    )
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
            json.loads(args.manifest.read_bytes()),
            args.manifest.parent.resolve(),
            args.executable.resolve(),
        )
        with args.out.open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError, ValidationError, subprocess.SubprocessError) as error:
        print(f"Document-quality evidence refused: {error}", file=sys.stderr)
        return 1
    return 0 if report["decision"] == "accepted" else 1


if __name__ == "__main__":
    raise SystemExit(main())
