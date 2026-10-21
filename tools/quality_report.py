# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual quality report projections and their finite per-plan serialization allowance."""

from __future__ import annotations

import copy
from typing import TYPE_CHECKING, Any

from quality_adjudication import adjudicate
from quality_attempt import ATTEMPT_KINDS, RUN_ERRORS, VALIDATION_ERRORS
from quality_json import frozen_digest
from quality_protocol import BUNDLE_ROLES
from quality_schema import outcome_ids

if TYPE_CHECKING:
    from quality_report_allowance import _Bytes


OBSERVATIONS = ("original", *ATTEMPT_KINDS)
CLEARANCE = "curator assertions; requires independent rights and reviewer review"
BUILD_BINDING = "independently referenced build provenance; byte identity verified locally"


def _slots(value: dict[str, Any], keys: tuple[str, ...]) -> dict[str, Any]:
    """Unknown or missing causes cannot escape the domains counted before effects."""
    if set(value) != set(keys):
        message = "Quality report cause slots differ from their source owner"
        raise ValueError(message)
    return value


def role_report(
    declared: dict[str, Any],
    observations: dict[str, Any],
    validation: dict[str, Any],
    record_sha256: str | _Bytes | None,
    outcome: str | _Bytes,
) -> dict[str, Any]:
    """Attach the same real observations before preparation; preserve declarations separately."""
    if set(observations) != set(OBSERVATIONS):
        message = "Quality role must retain every original and invocation observation"
        raise ValueError(message)
    return {
        "outcome": outcome,
        "declared": copy.deepcopy(declared),
        **{key: observations[key] for key in OBSERVATIONS},
        "validation": _slots(validation, VALIDATION_ERRORS),
        "record_sha256": record_sha256,
    }


def page_report(
    policy: dict[str, Any],
    assessment: dict[str, Any],
    roles: dict[str, Any],
    validation: dict[str, Any],
    prior_reviews: list[Any],
) -> dict[str, Any]:
    """Retain every frozen policy and actual current/prior review, without changing judgments."""
    if set(roles) != set(BUNDLE_ROLES):
        message = "Quality page must retain all three intended roles"
        raise ValueError(message)
    return {
        "sample": assessment["sample"],
        "frozen_policy": {
            key: copy.deepcopy(policy[key]) for key in ("source", "commands", "assets")
        },
        "roles": roles,
        "validation": _slots(validation, VALIDATION_ERRORS),
        "prior_reviews": copy.deepcopy(prior_reviews),
        "reviews": copy.deepcopy(assessment["reviews"]),
        "review_revisions": copy.deepcopy(assessment["review_revisions"]),
    }


def _projection(
    plan: dict[str, Any],
    adjudication: dict[str, Any],
    executable_sha256: str | _Bytes,
    evidence: list[Any],
    run_errors: dict[str, Any],
) -> dict[str, Any]:
    """One final report field owner serves actual facts and count-only domain projections."""
    return {
        **adjudication,
        "executable_sha256": executable_sha256,
        "frozen_corpus_sha256": plan["protocol"]["frozen_corpus_sha256"],
        "clearance": CLEARANCE,
        "build_commit_binding": BUILD_BINDING,
        "evidence": evidence,
        "errors": _slots(run_errors, RUN_ERRORS),
    }


def _identity(value: dict[str, Any], digest: str | _Bytes) -> dict[str, Any]:
    return {**value, "report_sha256": digest}


def _assessed(plan: dict[str, Any], evidence: list[dict[str, Any]]) -> dict[str, Any]:
    """Only observed missing evidence alters engineering admission; original declarations stay."""
    if [page["sample"] for page in evidence] != [item["sample"] for item in plan["assessments"]]:
        message = "Report evidence must retain original intended assessment order"
        raise ValueError(message)
    assessed = copy.deepcopy(plan)
    outcomes = outcome_ids()
    for assessment, page in zip(assessed["assessments"], evidence, strict=True):
        for name in BUNDLE_ROLES:
            outcome = page["roles"][name]["outcome"]
            if outcome not in outcomes:
                message = "Unknown quality role outcome"
                raise ValueError(message)
            if outcome != assessment[name]["outcome"] and outcome != "missing_evidence":
                message = "Observed evidence cannot relabel the frozen original outcome"
                raise ValueError(message)
            if outcome == "missing_evidence":
                assessment[name].update(outcome=outcome, bundle=None, execution=None)
    return assessed


def report(
    plan: dict[str, Any],
    executable_sha256: str,
    evidence: list[dict[str, Any]],
    run_errors: dict[str, Any],
    prior: dict[str, list[dict[str, Any]]],
) -> dict[str, Any]:
    """Keep existing adjudication mathematics; hash the report before its identity is inserted."""
    result = _projection(
        plan, adjudicate(_assessed(plan, evidence), prior), executable_sha256, evidence, run_errors
    )
    failed = any(run_errors.values()) or any(
        any(page["validation"].values())
        or any(any(role["validation"].values()) for role in page["roles"].values())
        for page in evidence
    )
    if failed:
        result["decision"] = "abstain"
    return _identity(result, frozen_digest(result))
