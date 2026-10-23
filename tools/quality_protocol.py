# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Semantic relationships of the closed frozen quality protocol."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from jsonschema import ValidationError

from quality_attempt import ATTEMPT_KINDS
from quality_json import canonical, frozen_digest
from quality_observations import require
from quality_provenance_contract import provenance_shape
from quality_provenance_validation import snapshot_failures
from quality_schema import validator

BUNDLE_ROLES = ("candidate", "baseline", "replicate")


def validate_relationships(plan: dict[str, Any]) -> None:
    """Validate frozen sample, tuning and panel relationships separately from byte identity."""
    protocol = plan["protocol"]
    panel = [person["id"] for person in protocol["panel"]]
    samples = [sample["id"] for sample in protocol["corpus"]]
    identities = [sample["sha256"] for sample in protocol["corpus"]]
    pages = [page["sample"] for page in protocol["pages"]]
    pairs = [page["blind_pair"] for page in protocol["pages"]]
    assessed = [item["sample"] for item in plan["assessments"]]
    if any(
        len(items) != len(set(items))
        for items in (panel, samples, identities, pages, pairs, assessed)
    ):
        message = "Duplicate reviewer, sample, source bytes, blind pair or assessment"
        raise ValueError(message)
    groups: dict[str, list[str]] = {}
    for sample in protocol["corpus"]:
        groups.setdefault(sample["document_id"], []).append(sample["split"])
    if any(
        len(set(splits)) != 1 or ("heldout" in splits and len(splits) != 1)
        for splits in groups.values()
    ):
        message = "A physical document cannot cross splits or count as independent held-out pages"
        raise ValueError(message)
    tuning = protocol["tuning"]
    has_tuning = any(sample["split"] == "tuning" for sample in protocol["corpus"])
    if tuning["performed"] != has_tuning or tuning["performed"] != (tuning["receipt"] is not None):
        message = "Tuning requires cleared tuning inputs and its independent receipt"
        raise ValueError(message)
    heldout = {sample["id"] for sample in protocol["corpus"] if sample["split"] == "heldout"}
    if set(assessed) != heldout or set(pages) != heldout:
        message = "Every frozen held-out input must be retained exactly once"
        raise ValueError(message)
    validate_assessments(plan, panel)


def validate_assessments(plan: dict[str, Any], panel: list[str]) -> None:
    """Every mutable assessment belongs to its frozen page, command and panel."""
    protocol = plan["protocol"]
    policies = {page["sample"]: page for page in protocol["pages"]}
    for assessment in plan["assessments"]:
        paths = [
            Path(assessment[name]["bundle"])
            for name in BUNDLE_ROLES
            if assessment[name]["bundle"] is not None
        ]
        if len(paths) != len(set(paths)):
            message = "Roles require distinct bundle directories"
            raise ValueError(message)
        reviews = assessment["reviews"]
        reviewers = [review["reviewer"] for review in reviews]
        pair = policies[assessment["sample"]]["blind_pair"]
        if (
            len(set(reviewers)) != len(reviewers)
            or not set(reviewers).issubset(panel)
            or any(review["blind_pair"] != pair for review in reviews)
        ):
            message = "Reviews must reference their frozen pair and independent panel"
            raise ValueError(message)
        for name in BUNDLE_ROLES:
            execution = assessment[name]["execution"]
            if (
                execution is not None
                and execution["argv"] != policies[assessment["sample"]]["commands"][name]
            ):
                message = "Actual command differs from frozen role argv"
                raise ValueError(message)


def admitted_prior_snapshots(
    assessment: dict[str, Any], policy: dict[str, Any], snapshots: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    """Retain each independently admitted revision; one refusal cannot erase another judgment."""
    retained = []
    for entry in snapshots:
        revision = entry["value"]
        if revision is None:
            continue
        try:
            validator("review_revision").validate(revision)
        except ValidationError:
            continue
        reviewers = [review["reviewer"] for review in revision["reviews"]]
        if (
            revision["sample"] != assessment["sample"]
            or len(reviewers) != len(set(reviewers))
            or not set(reviewers).issubset(policy["panel"])
            or any(review["blind_pair"] != policy["blind_pair"] for review in revision["reviews"])
        ):
            continue
        retained.append(revision)
    return retained


def validate_work_index(plan: dict[str, Any], slots: list[dict[str, Any]]) -> None:
    """Bind all actual intended slots to exact derived commands and relative owners."""
    require(len(slots) == 9 * len(plan["assessments"]), "Work index must contain actual 9N slots")
    policies = {page["sample"]: page for page in plan["protocol"]["pages"]}
    directories = []
    for index, slot in enumerate(slots):
        role, offset = divmod(index, 3)
        assessment = plan["assessments"][role // 3]
        frozen = policies[assessment["sample"]]["commands"][BUNDLE_ROLES[role % 3]]
        require(
            slot["index"] == index
            and slot["role_index"] == role
            and slot["kind"] == ATTEMPT_KINDS[offset]
            and slot["source_slot"] == ("scored" if offset == 0 else "replay"),
            "Work index identity/order differs from the actual intended roles",
        )
        expected_target = (
            assessment[BUNDLE_ROLES[role % 3]]["bundle"]
            if offset == 0
            else slots[3 * role + 1]["planned_target"]
        )
        require(
            slot["planned_target"] == expected_target, "Verifier target differs from its bound role"
        )
        requested = (
            frozen
            if offset == 1
            else (
                None
                if expected_target is None
                else [frozen[0], "verify", expected_target, "--json"]
            )
        )
        if expected_target is None:
            expected = None
        else:
            if requested is None:
                message = "Prepared command is unavailable"
                raise ValueError(message)
            expected = requested.copy()
        if offset == 1 and expected is not None:
            position = 4 + 2 * frozen[3:-1:2].index("--out-dir")
            expected[position] = expected_target
        require(
            slot["requested_argv"] == requested and slot["argv"] == expected,
            "Work argv is not the exact frozen/derived invocation",
        )
        directory = slot["planned_directory"]
        if directory is not None:
            require(
                not Path(directory).is_absolute() and ".." not in Path(directory).parts,
                "Planned capture directory must remain root-relative",
            )
            directories.append(Path(directory))
        for stream in ("stdout", "stderr"):
            expected_path = None if directory is None else directory + "/" + stream
            require(
                slot["planned_" + stream] == expected_path,
                "Planned stream escaped its capture owner",
            )
        if offset == 1 and expected_target is not None:
            require(
                directory is not None and Path(expected_target).parent == Path(directory),
                "Replay output must belong to its planned capture directory",
            )
            require(
                expected_target not in (slot["planned_stdout"], slot["planned_stderr"]),
                "Replay output collides with a planned raw stream",
            )
    require(len(set(directories)) == len(directories), "Planned capture directories collide")


def _core_snapshots_failed(plan: dict[str, Any], entries: list[dict[str, Any]]) -> bool:
    """Bind the original preregistration, build and selected capture bodies first."""
    protocol = plan["protocol"]
    receipt, build = (entry["value"] for entry in entries[:2])
    if receipt != {"protocol_sha256": plan["protocol_sha256"]}:
        return True
    try:
        provenance_shape(build)
    except (ValueError, TypeError):
        return True
    if (
        build["source_commit"] != protocol["source_commit"]
        or build["executable_sha256"] != protocol["executable_sha256"]
    ):
        return True
    return bool(snapshot_failures(plan, entries))


def reference_snapshot_failures(plan: dict[str, Any], entries: list[dict[str, Any]]) -> bool:
    """Unavailable/incoherent pre-effect receipts remain facts but cannot admit acceptance."""
    protocol = plan["protocol"]
    assignment = entries[2]["value"]
    if _core_snapshots_failed(plan, entries):
        return True
    if (
        assignment is None
        or set(assignment) != {"pairs"}
        or not isinstance(assignment["pairs"], list)
    ):
        return True
    expected = [
        {
            "sample": page["sample"],
            "blind_pair": page["blind_pair"],
            "reviewers": [person["id"] for person in protocol["panel"]],
        }
        for page in protocol["pages"]
    ]
    pairs = assignment["pairs"]
    if any(
        not isinstance(pair, dict)
        or set(pair) != {"sample", "blind_pair", "reviewers", "candidate_side"}
        or pair["candidate_side"] not in ("left", "right")
        for pair in pairs
    ):
        return True
    if [
        {key: item for key, item in pair.items() if key != "candidate_side"} for pair in pairs
    ] != expected:
        return True
    if protocol["tuning"]["receipt"] is not None:
        inputs = [sample for sample in protocol["corpus"] if sample["split"] == "tuning"]
        actual_tuning: object = entries[3]["value"]
        return actual_tuning != {"tuning_corpus_sha256": frozen_digest(inputs)}
    return False


def reference_snapshots_coherent(entries: list[dict[str, Any]]) -> None:
    """The same identified bytes cannot acquire two different parsed immutable values."""
    seen: dict[tuple[str, str], bytes] = {}
    for entry in entries:
        reference = entry["reference"]
        identity = (reference["path"], reference["sha256"])
        value = canonical(entry["value"])
        require(identity not in seen or seen[identity] == value, "Identified JSON snapshot drift")
        seen[identity] = value
