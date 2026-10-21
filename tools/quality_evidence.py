# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Admit frozen assets, provenance and the fixed per-page processing commands."""

from __future__ import annotations

import copy
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

from jsonschema import ValidationError

import quality_acquisition as acquisition
from parameter_contract import merged_options
from quality_commands import command_options
from quality_inputs import FrozenEvidence
from quality_json import ROOT, frozen_digest
from quality_plan import validate_plan
from quality_protocol import BUNDLE_ROLES
from quality_provenance import QualityProvenance, admit_provenance
from quality_provenance_contract import PROVENANCE_KEYS, provenance_shape

if TYPE_CHECKING:
    from collections.abc import Callable
    from pathlib import Path


@dataclass(frozen=True)
class QualityInputs:
    """One owned plan/reference acquisition and its selected pre-effect execution premises."""

    plan: dict[str, Any]
    root: Path
    executable: Path
    snapshots: FrozenEvidence
    build: dict[str, Any]
    provenance: QualityProvenance | None


def admit_evaluation(plan: dict[str, Any], root: Path, executable: Path) -> QualityInputs:
    """No journal, directory or child exists before the same acquired premises admit."""
    plan = copy.deepcopy(plan)
    validate_plan(plan)
    snapshots = FrozenEvidence(plan, root)
    build = admit(plan, root, executable, replay_options, snapshots)
    provenance = admit_provenance(plan, root, executable, snapshots)
    return QualityInputs(plan, root, executable, snapshots, build, provenance)


def evidence_error(error: Exception) -> str:
    """Derived schema diagnostics retain path/rule without duplicating evidence or schema."""
    if isinstance(error, ValidationError):
        path = "/" + "/".join(
            str(part).replace("~", "~0").replace("/", "~1") for part in error.absolute_path
        )
        return f"JSON schema refusal at {path} (rule {error.validator})"
    return str(error)


def digest(path: Path) -> str:
    """Fingerprint one regular initial-extent snapshot through its actual owned descriptor."""
    return acquisition.fingerprint(path)["sha256"]


def reference(root: Path, ref: dict[str, Any]) -> Path:
    """Admission checks bytes independently of the declared receipt or asset contents."""
    path = acquisition.confined(root, ref["path"])
    if digest(path) != ref["sha256"]:
        message = "Frozen referenced bytes differ: " + ref["path"]
        raise ValueError(message)
    return path


def replay_options(options: list[str], root: Path) -> None:
    """Admit current processing option pairs without allowing evidence to redirect outputs."""
    contract = acquisition.acquire_json_file(ROOT / "spec/cli-contract.json")
    methods = acquisition.acquire_json_file(ROOT / "spec/method-contract.json")
    allowed = {
        option["name"]
        for option in merged_options(contract, methods)
        if option["metavar"] and "process" in option["scope"] and option["name"] != "--out-dir"
    }
    if len(options) % 2 or any(
        options[index] not in allowed for index in range(0, len(options), 2)
    ):
        message = "Replay requires processing option/value pairs without output override"
        raise ValueError(message)
    for index in range(0, len(options), 2):
        if options[index] in {"--protect-mask", "--psf-file"}:
            acquisition.confined(root, options[index + 1])


def admit(
    plan: dict[str, Any],
    root: Path,
    executable: Path,
    check_options: Callable[[list[str], Path], None],
    snapshots: FrozenEvidence,
) -> dict[str, Any]:
    """Refuse drift before assessment; receipt identity stays outside the protocol self-hash."""
    protocol = plan["protocol"]
    if digest(executable) != protocol["executable_sha256"]:
        message = "Executable differs from the reviewed evaluation identity"
        raise ValueError(message)
    receipt = snapshots.value(plan["preregistration"])
    if (
        set(receipt) != {"protocol_sha256"}
        or receipt.get("protocol_sha256") != plan["protocol_sha256"]
    ):
        message = "Independent preregistration receipt references another protocol"
        raise ValueError(message)
    build = snapshots.value(protocol["build_provenance"])
    if (
        set(build) != PROVENANCE_KEYS
        or build.get("source_commit") != protocol["source_commit"]
        or build.get("executable_sha256") != protocol["executable_sha256"]
        or not isinstance(build.get("build"), dict)
    ):
        message = "Reviewed build provenance differs from frozen source/executable"
        raise ValueError(message)
    provenance_shape(build)
    reference(root, protocol["review_protocol"])
    admit_reviews(protocol, snapshots)
    samples = {sample["id"]: sample for sample in protocol["corpus"]}
    for sample in samples.values():
        if digest(acquisition.confined(root, sample["path"])) != sample["sha256"]:
            message = "Source input changed after corpus freezing"
            raise ValueError(message)
    admit_pages(protocol, root, executable, check_options)
    return dict(build["build"])


def admit_pages(
    protocol: dict[str, Any],
    root: Path,
    executable: Path,
    check_options: Callable[[list[str], Path], None],
) -> None:
    """Frozen per-page commands use exactly their byte-identified assets and distinct outputs."""
    samples = {sample["id"]: sample for sample in protocol["corpus"]}
    for page in protocol["pages"]:
        assets = {ref["path"]: ref for ref in page["assets"]}
        if len(assets) != len(page["assets"]):
            message = "Duplicate frozen asset path"
            raise ValueError(message)
        for ref in assets.values():
            reference(root, ref)
        used = set()
        destinations = []
        for command in page["commands"].values():
            destination, options = command_options(
                command, samples[page["sample"]]["path"], executable
            )
            destinations.append(acquisition.confined(root, destination))
            check_options(options, root)
            for i in range(0, len(options), 2):
                if options[i] in {"--protect-mask", "--psf-file"}:
                    used.add(options[i + 1])
        if used != set(assets) or len(set(destinations)) != len(BUNDLE_ROLES):
            message = "Frozen assets or distinct role destinations disagree with commands"
            raise ValueError(message)


def admit_reviews(protocol: dict[str, Any], snapshots: FrozenEvidence) -> None:
    """Retained assignment and tuning receipts bind reviewed identities, not just file existence."""
    assignment = snapshots.value(protocol["assignment"])
    expected = [
        {
            "sample": page["sample"],
            "blind_pair": page["blind_pair"],
            "reviewers": [person["id"] for person in protocol["panel"]],
        }
        for page in protocol["pages"]
    ]
    pairs = assignment.get("pairs")
    if (
        set(assignment) != {"pairs"}
        or not isinstance(pairs, list)
        or any(
            not isinstance(pair, dict)
            or set(pair) != {"sample", "blind_pair", "reviewers", "candidate_side"}
            or pair["candidate_side"] not in {"left", "right"}
            for pair in pairs
        )
        or [
            {key: value for key, value in pair.items() if key != "candidate_side"} for pair in pairs
        ]
        != expected
    ):
        message = "Blinded assignment differs from frozen pages and panel"
        raise ValueError(message)
    if protocol["tuning"]["receipt"] is not None:
        tuning = snapshots.value(protocol["tuning"]["receipt"])
        inputs = [sample for sample in protocol["corpus"] if sample["split"] == "tuning"]
        if set(tuning) != {"tuning_corpus_sha256"} or tuning[
            "tuning_corpus_sha256"
        ] != frozen_digest(inputs):
            message = "Tuning receipt differs from cleared tuning inputs"
            raise ValueError(message)
