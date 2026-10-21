# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Collect every fixed v3 role while preserving original and finished attempt observations."""

from __future__ import annotations

import stat
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Protocol

from jsonschema import ValidationError

from quality_acquisition import confined
from quality_attempt import ATTEMPT_KINDS, issue
from quality_commands import command_options
from quality_execution import execution_evidence
from quality_observations import original_ready
from quality_protocol import BUNDLE_ROLES

if TYPE_CHECKING:
    from collections.abc import Callable
    from pathlib import Path

    from quality_attempt import CaptureAdmission


SEMANTIC_FACTS = ("source", "request", "execution", "assets", "output", "build")
EVIDENCE_ERRORS = (OSError, ValueError, TypeError, KeyError, MemoryError, ValidationError)


class BundleRun(Protocol):
    """The actual run state consumed by collection and record/replay receiving boundaries."""

    root: Path
    executable: Path
    slots: list[dict[str, Any]]
    pages: list[dict[str, Any]]
    admission: CaptureAdmission

    def original(self, observation: dict[str, Any]) -> None:
        """Persist this original observation exactly once."""
        ...

    def invoke(self, index: int) -> dict[str, Any]:
        """Consume one planned invocation and return its actual observation."""
        ...

    def skip(self, index: int, causecode: str = "not_eligible") -> dict[str, Any]:
        """Account one invocation without preparing or launching a producer."""
        ...


@dataclass(frozen=True)
class BundleChecks:
    """The admitted run and its concrete record/replay/independent fidelity owners."""

    run: BundleRun
    build: dict[str, Any]
    read_bundle: Callable[[BundleChecks, int, str, dict[str, Any]], dict[str, Any]]
    replay: Callable[[BundleChecks, int, str, dict[str, Any]], dict[str, Any] | None]
    fidelity: Callable[[Path, dict[str, str], dict[str, dict[str, Any]]], None]


def frozen_processing_assets(
    record: dict[str, Any], options: list[str], references: list[dict[str, Any]]
) -> None:
    """Bind actual supplied bytes to preregistered argv assets before any replay."""
    selected = dict(zip(options[::2], options[1::2], strict=True))
    frozen = {reference["path"]: reference["sha256"] for reference in references}
    protection = record["request"]["protection"]
    mask = protection["supplied"] if protection is not None else None
    kernels = [asset for asset in record["assets"] if asset["role"] == "psf_kernel"]
    if len(kernels) > 1:
        message = "Scored record contains duplicate supplied PSF identities"
        raise ValueError(message)
    for option, actual in (
        ("--protect-mask", mask),
        ("--psf-file", kernels[0] if kernels else None),
    ):
        path = selected.get(option)
        if path is None:
            if actual is not None:
                message = "Scored supplied asset is absent from its frozen command"
                raise ValueError(message)
        elif actual is None or frozen.get(path) != actual["sha256"]:
            message = "Scored supplied asset differs from its frozen identity"
            raise ValueError(message)


def _reject(
    checks: BundleChecks, report: dict[str, Any], phase: str, error: Exception | None = None
) -> None:
    """Retain a bounded role cause without rewriting declared facts or finished attempts."""
    cause = issue(phase, report["original"]["role_index"], error)
    if report["validation"][phase] is None:
        report["validation"][phase] = cause
    report["outcome"] = "missing_evidence"
    checks.run.admission.close(cause)


def _skip_role(checks: BundleChecks, index: int) -> None:
    """Even expected ineligible roles retain all three default invocation observations."""
    for offset in range(len(ATTEMPT_KINDS)):
        checks.run.skip(3 * index + offset)


def _directory(
    checks: BundleChecks, report: dict[str, Any], identities: set[tuple[int, int]]
) -> None:
    path = confined(checks.run.root, report["declared"]["bundle"])
    observed = path.stat(follow_symlinks=False)
    if not stat.S_ISDIR(observed.st_mode):
        message = "Scored bundle directory is unavailable"
        raise ValueError(message)
    identity = observed.st_dev, observed.st_ino
    if identity in identities:
        message = "Scored roles alias the same physical bundle directory"
        raise ValueError(message)
    identities.add(identity)


def _completed(
    checks: BundleChecks,
    policy: dict[str, Any],
    report: dict[str, Any],
    identities: set[tuple[int, int]],
) -> tuple[dict[str, Any], dict[str, Any]] | None:
    index = report["original"]["role_index"]
    name = BUNDLE_ROLES[index % 3]
    source = policy["source"]
    phase = "record"
    replay_consumed = False
    try:
        response = report["original"]["response"]
        # Always consumes scored_verify before any artifact inspection, even on failure.
        record = checks.read_bundle(checks, 3 * index, source["sha256"], response)
        phase = "role_identity"
        destination, options = command_options(
            policy["commands"][name], source["path"], checks.run.executable
        )
        phase = "role_identity"
        if report["declared"]["bundle"] != destination:
            message = "Completed bundle differs from its frozen command destination"
            raise ValueError(message)
        _directory(checks, report, identities)
        phase = "source"
        if record["source"]["sha256"] != source["sha256"]:
            message = "Scored record source differs from frozen bytes"
            raise ValueError(message)
        phase = "build"
        if record["build"] != checks.build:
            message = "Bundle build differs from frozen provenance"
            raise ValueError(message)
        phase = "record"
        frozen_processing_assets(record, options, policy["assets"])
        report["record_sha256"] = response["record_sha256"]
        replay_consumed = True
        repeated = checks.replay(checks, index, source["sha256"], record)
    except EVIDENCE_ERRORS as error:
        _reject(checks, report, phase, error)
        if not replay_consumed:
            checks.run.skip(3 * index + 1)
            checks.run.skip(3 * index + 2)
        return None

    else:
        return None if repeated is None else (record, repeated)


def _original(checks: BundleChecks, report: dict[str, Any]) -> bool:
    original = report["original"]
    declared = report["declared"]
    try:
        if declared["execution"] is not None:
            execution_evidence(original, checks.run.root, checks.run.admission)
    except EVIDENCE_ERRORS as error:
        _reject(checks, report, "delivery", error)
    finally:
        # All 3N original observations are persisted once, including expected unavailable ones.
        checks.run.original(original)
    if any(original["errors"].values()) or any(
        any(original[name]["errors"].values()) for name in ("stdout", "stderr")
    ):
        _reject(checks, report, "delivery")
        return False
    if declared["outcome"] != "completed":
        return False
    if original["stderr"]["retained_bytes"] != 0:
        _reject(checks, report, "raw_stderr")
        return False
    if not original_ready(original):
        _reject(checks, report, "delivery")
        return False
    return checks.run.admission.is_open


def _runs(
    checks: BundleChecks, report: dict[str, Any], pair: tuple[dict[str, Any], ...], seen: set[str]
) -> bool:
    runs = [record["record"]["run"] for record in pair]
    if len(set(runs)) != len(runs) or any(run in seen for run in runs):
        _reject(checks, report, "role_identity")
        return False
    seen.update(runs)
    return True


def _page(
    checks: BundleChecks,
    policy: dict[str, Any],
    page: dict[str, Any],
    identities: set[tuple[int, int]],
    runs: set[str],
) -> None:
    records = {}
    if any(page["validation"].values()):
        checks.run.admission.close(next(cause for cause in page["validation"].values() if cause))
    for name in BUNDLE_ROLES:
        report = page["roles"][name]
        index = report["original"]["role_index"]
        if report["declared"]["outcome"] == "completed":
            report["outcome"] = "missing_evidence"
        if not _original(checks, report):
            if report["declared"]["outcome"] == "completed":
                report["outcome"] = "missing_evidence"
            _skip_role(checks, index)
            continue
        pair = _completed(checks, policy, report, identities)
        if pair is not None and _runs(checks, report, pair, runs):
            report["outcome"] = "completed"
            records[name] = pair[0]
    _page_fidelity(checks, page, records)


def _page_fidelity(
    checks: BundleChecks, page: dict[str, Any], records: dict[str, dict[str, Any]]
) -> None:
    """Apply one actual cross-role fidelity refusal to all engineering denominators."""
    if len(records) != len(BUNDLE_ROLES):
        return
    try:
        paths = {name: page["roles"][name]["declared"]["bundle"] for name in BUNDLE_ROLES}
        checks.fidelity(checks.run.root, paths, records)
    except EVIDENCE_ERRORS as error:
        # Cross-role refusal applies to every engineering denominator, not human judgments.
        cause = issue("fidelity", page["roles"][BUNDLE_ROLES[0]]["original"]["role_index"], error)
        if page["validation"]["fidelity"] is None:
            page["validation"]["fidelity"] = cause
        for report in page["roles"].values():
            _reject(checks, report, "fidelity", error)


def collect(plan: dict[str, Any], checks: BundleChecks) -> list[dict[str, Any]]:
    """Use preattached report projections; every 9N slot and 3N original is accounted once."""
    policies = {page["sample"]: page for page in plan["protocol"]["pages"]}
    samples = {sample["id"]: sample for sample in plan["protocol"]["corpus"]}
    identities: set[tuple[int, int]] = set()
    runs: set[str] = set()
    for page in checks.run.pages:
        policy = policies[page["sample"]] | {"source": samples[page["sample"]]}
        _page(checks, policy, page, identities, runs)
    return checks.run.pages
