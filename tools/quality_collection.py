# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Collect all fixed quality roles, retaining declared evidence and observed refusals."""

from __future__ import annotations

import copy
import os
import subprocess
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

from jsonschema import ValidationError

from document_quality import read_json, validator
from quality_evidence import command_options, confined, digest, evidence_error, reference
from quality_execution import execution_evidence
from quality_protocol import BUNDLE_ROLES

if TYPE_CHECKING:
    from collections.abc import Callable
    from pathlib import Path

    from quality_replay import ReplayObservation

SEMANTIC_FACTS = ("source", "request", "execution", "protection", "output", "build")
EVIDENCE_ERRORS = (
    OSError,
    ValueError,
    TypeError,
    KeyError,
    ValidationError,
    subprocess.SubprocessError,
)


@dataclass(frozen=True)
class BundleChecks:
    """One selected executable and its actual verification/replay/fidelity owners."""

    executable: Path
    build: dict[str, Any]
    read_bundle: Callable[..., dict[str, Any]]
    replay: Callable[..., ReplayObservation]
    fidelity: Callable[..., None]


def delivered_path(destination: str, name: str) -> str:
    """Preserve admitted identity bytes using the product's platform separator rule."""
    separator = "/"
    if os.name == "nt":
        index = max(destination.rfind("/"), destination.rfind("\\"))
        separator = destination[index] if index >= 0 else "\\"
    return destination + separator + name


def completed_record(
    root: Path, page: dict[str, Any], role: dict[str, Any], name: str, checks: BundleChecks
) -> tuple[dict[str, Any], dict[str, Any], Path]:
    """Bind delivered success to the scored record and immutable actual replay."""
    destination, _ = command_options(
        page["commands"][name], page["source"]["path"], checks.executable
    )
    if role["bundle"] != destination:
        message = "Completed bundle differs from frozen output destination"
        raise ValueError(message)
    directory = confined(root, destination)
    record = checks.read_bundle(checks.executable, directory, page["source"]["sha256"])
    response_record = role["execution"]["response"]["record"]
    if (
        response_record["run"] != record["record"]["run"]
        or response_record["sha256"] != digest(directory / "run.json")
        or response_record["bytes"] != (directory / "run.json").stat().st_size
        or role["execution"]["response"]["output"] != delivered_path(destination, "result.png")
    ):
        message = "Delivered success differs from the scored bundle identity"
        raise ValueError(message)
    if record["build"] != checks.build:
        message = "Bundle build differs from independently referenced build provenance"
        raise ValueError(message)
    return (
        record,
        {
            "record_sha256": digest(directory / "run.json"),
            "command": page["commands"][name].copy(),
            "role": name,
        },
        directory,
    )


def replay_record(
    root: Path,
    record: dict[str, Any],
    report: dict[str, Any],
    checks: BundleChecks,
) -> None:
    """Expose every actual replay attempt before assessing record agreement."""
    name = report.pop("role")
    observed = checks.replay(report.pop("command"), root)
    report["replay_attempt"] = observed.attempt
    report["replay_argv"] = observed.attempt["argv"]
    if observed.record is None:
        message = "Frozen replay failed; retained actual command/effect evidence is incomplete"
        raise ValueError(message)
    if observed.record["record"]["run"] == record["record"]["run"]:
        message = "Replay must be independently identified from its scored execution"
        raise ValueError(message)
    if any(observed.record[field] != record[field] for field in SEMANTIC_FACTS):
        message = f"Frozen {name} differs from actual processing by selected executable"
        raise ValueError(message)


def role_evidence(
    root: Path, page: dict[str, Any], role: dict[str, Any], name: str, checks: BundleChecks
) -> tuple[dict[str, Any], dict[str, Any] | None, Path | None]:
    """Observe one declared role; failures preserve its original effect evidence."""
    report = {
        "outcome": role["outcome"],
        "declared": copy.deepcopy(role),
        "errors": [],
        "record_sha256": None,
        "replay_argv": None,
        "replay_attempt": None,
        "observed_publication": "unknown",
    }
    record, directory = None, None
    try:
        if role["outcome"] != "missing_evidence":
            execution_evidence(role, root, report)
            if role["outcome"] == "completed":
                record, facts, directory = completed_record(root, page, role, name, checks)
                report.update(facts)
                replay_record(root, record, report, checks)
    except EVIDENCE_ERRORS as error:
        report["outcome"] = "missing_evidence"
        report["errors"].append(evidence_error(error))
    return report, record, directory


def prior_reviews(root: Path, assessment: dict[str, Any]) -> list[dict[str, Any]]:
    """Retained revisions cannot silently discard previously observed harm or uncertainty."""
    revisions = []
    for ref in assessment["review_revisions"]:
        revision = read_json(reference(root, ref).read_bytes())
        validator("review_revision").validate(revision)
        if revision["sample"] != assessment["sample"]:
            message = "Prior review revision belongs to another physical page"
            raise ValueError(message)
        revisions.append(revision)
    return revisions


def invalidate(role: dict[str, Any]) -> None:
    """Absent coherent evidence abstains; original declarations remain separately reported."""
    role.update(outcome="missing_evidence", bundle=None, execution=None)


def validate_revisions(revisions: list[dict[str, Any]], page: dict[str, Any]) -> None:
    """Retained human judgments still belong to the frozen pair and panel."""
    for revision in revisions:
        reviewers = [review["reviewer"] for review in revision["reviews"]]
        if (
            len(reviewers) != len(set(reviewers))
            or not set(reviewers).issubset(page["panel"])
            or any(review["blind_pair"] != page["blind_pair"] for review in revision["reviews"])
        ):
            message = "Prior reviews differ from frozen pair and panel"
            raise ValueError(message)


def inspect_directory(
    directory: Path, identities: set[tuple[int, int]], report: dict[str, Any]
) -> None:
    """Namespace loss or aliasing is one role's missing evidence, never an abort-first error."""
    try:
        stat = directory.stat()
        identity = stat.st_dev, stat.st_ino
        if identity in identities:
            report["outcome"] = "missing_evidence"
            report["errors"].append("Roles alias the same physical bundle directory")
        identities.add(identity)
    except EVIDENCE_ERRORS as error:
        report["outcome"] = "missing_evidence"
        report["errors"].append(evidence_error(error))


def validate_run_identities(
    records: dict[str, dict[str, Any]], roles: dict[str, dict[str, Any]]
) -> None:
    """All scored roles and actual replays require distinct execution identities."""
    runs = [record["record"]["run"] for record in records.values()]
    runs.extend(role["replay_attempt"]["response"]["record"]["run"] for role in roles.values())
    if len(set(runs)) != len(runs):
        message = "Scored roles and replays require pairwise distinct run identities"
        raise ValueError(message)


def page_evidence(
    root: Path, page: dict[str, Any], assessment: dict[str, Any], checks: BundleChecks
) -> dict[str, Any]:
    """A page retains all roles, prior reviews and independent cross-role fidelity errors."""
    records: dict[str, dict[str, Any]] = {}
    identities: set[tuple[int, int]] = set()
    roles: dict[str, dict[str, Any]] = {}
    failures, revisions = [], []
    for name in BUNDLE_ROLES:
        report, record, directory = role_evidence(root, page, assessment[name], name, checks)
        roles[name] = report
        if directory is not None:
            inspect_directory(directory, identities, report)
        if report["outcome"] == "missing_evidence":
            invalidate(assessment[name])
        elif record is not None:
            records[name] = record
    try:
        revisions = prior_reviews(root, assessment)
        validate_revisions(revisions, page)
        if len(records) == len(BUNDLE_ROLES):
            validate_run_identities(records, roles)
            paths = {name: assessment[name]["bundle"] for name in BUNDLE_ROLES}
            checks.fidelity(root, paths, records)
    except EVIDENCE_ERRORS as error:
        failures.append(evidence_error(error))
        for name in BUNDLE_ROLES:
            invalidate(assessment[name])
            roles[name]["outcome"] = "missing_evidence"
    return {
        "sample": assessment["sample"],
        "frozen_policy": {
            "source": copy.deepcopy(page["source"]),
            "commands": copy.deepcopy(page["commands"]),
            "assets": copy.deepcopy(page["assets"]),
        },
        "roles": roles,
        "fidelity_errors": failures,
        "prior_reviews": revisions,
        "reviews": copy.deepcopy(assessment["reviews"]),
        "review_revisions": copy.deepcopy(assessment["review_revisions"]),
    }


def collect(
    plan: dict[str, Any], root: Path, checks: BundleChecks
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Retain every intended page after valid protocol admission, without failure replay."""
    assessed = copy.deepcopy(plan)
    policies = {page["sample"]: page for page in plan["protocol"]["pages"]}
    samples = {sample["id"]: sample for sample in plan["protocol"]["corpus"]}
    panel = {person["id"] for person in plan["protocol"]["panel"]}
    evidence = []
    for assessment in assessed["assessments"]:
        page = policies[assessment["sample"]] | {
            "source": samples[assessment["sample"]],
            "panel": panel,
        }
        evidence.append(page_evidence(root, page, assessment, checks))
    return assessed, evidence
