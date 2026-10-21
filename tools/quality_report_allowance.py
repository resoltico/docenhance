# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Finite per-plan quality report byte allowances from the actual factual projections."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from quality_adjudication import adjudicate
from quality_allowance import attempt_allowance, issue_allowance, original_allowance
from quality_attempt import ATTEMPT_KINDS, RUN_ERRORS, VALIDATION_ERRORS
from quality_json import canonical
from quality_protocol import BUNDLE_ROLES
from quality_report import _identity, _projection, _slots, page_report, role_report
from quality_schema import harm_ids, outcome_ids


@dataclass(frozen=True)
class _Bytes:
    """An encoded-value allowance, never a JSON value or a purported successful observation."""

    count: int


def _count(value: object) -> int:
    """Count the same canonical JSON framing and every occurrence, without serializing domains."""
    if isinstance(value, _Bytes):
        if type(value.count) is not int or value.count < 0:
            message = "Invalid quality encoded-value allowance"
            raise ValueError(message)
        return value.count
    if isinstance(value, dict):
        return (
            2
            + max(0, len(value) - 1)
            + sum(len(canonical(key)) + 1 + _count(child) for key, child in value.items())
        )
    if isinstance(value, (list, tuple)):
        return 2 + max(0, len(value) - 1) + sum(_count(child) for child in value)
    return len(canonical(value))


def _choice(values: tuple[object, ...]) -> _Bytes:
    return _Bytes(max(len(canonical(value)) for value in values))


def _cause_bounds(keys: tuple[str, ...], subject: int) -> dict[str, _Bytes]:
    bound = _Bytes(max(len(canonical(None)), issue_allowance(subject, canonical)))
    return dict.fromkeys(keys, bound)


def _item_bounds(item: dict[str, Any], domains: dict[str, tuple[str, ...]]) -> dict[str, Any]:
    """Derive every class-page member from the actual adjudication projection and its domain."""
    result: dict[str, Any] = {}
    for key, value in item.items():
        if key == "sample":
            result[key] = value
        elif key in {"engineering_complete", "natural_document", "review_complete", "uncertain"}:
            result[key] = _choice((False, True))
        elif key == "harms":
            result[key] = list(domains["harms"])
        elif key == "delta":
            result[key] = _choice((None, -2, -1, 0, 1, 2))
        elif key == "outcomes":
            if set(value) != set(BUNDLE_ROLES):
                message = "Class adjudication outcomes lost an intended role"
                raise ValueError(message)
            result[key] = dict.fromkeys(value, _choice(domains["outcomes"]))
        else:
            message = "Unhandled quality class-page member in serialization allowance"
            raise ValueError(message)
    return result


def _class_bounds(value: dict[str, Any], domains: dict[str, tuple[str, ...]]) -> dict[str, Any]:
    """Each subset may conservatively contain every actual member; no invented page is counted."""
    members = [item["sample"] for item in value["pages"]]
    arrays = {
        "harmed",
        "uncertain",
        "incomplete",
        "engineering_failed",
        "engineering_refused",
        "missing_evidence",
        "review_incomplete",
    }
    result: dict[str, Any] = {}
    for key, actual in value.items():
        if key == "pages":
            result[key] = [_item_bounds(item, domains) for item in actual]
        elif key in {"count", "positive"}:
            result[key] = _choice((0, len(members)))
        elif key == "sign_tail":
            result[key] = _Bytes(32)
        elif key == "accepted":
            result[key] = _choice((False, True))
        elif key in arrays:
            result[key] = members
        else:
            message = "Unhandled quality class member in serialization allowance"
            raise ValueError(message)
    return result


def _adjudication_bounds(
    base: dict[str, Any], domains: dict[str, tuple[str, ...]]
) -> dict[str, Any]:
    """New adjudication fields require their own concrete domain before any launch admission."""
    result: dict[str, Any] = {}
    for key, value in base.items():
        if key in {"protocol_version", "scope", "source_commit", "protocol_sha256"}:
            result[key] = value
        elif key == "classes":
            result[key] = {name: _class_bounds(item, domains) for name, item in value.items()}
        elif key == "decision":
            result[key] = _choice(("accepted", "abstain"))
        else:
            message = "Unhandled quality adjudication member in serialization allowance"
            raise ValueError(message)
    return result


def _role_bounds(
    role: dict[str, Any],
    slots: dict[tuple[int, str], dict[str, Any]],
    context: dict[str, Any],
    outcomes: tuple[str, ...],
) -> dict[str, Any]:
    original = role["original"]
    index = original["role_index"]
    observations: dict[str, Any] = {"original": _Bytes(original_allowance(original, canonical))}
    observations.update(
        {name: _Bytes(attempt_allowance(slots[index, name], canonical)) for name in ATTEMPT_KINDS}
    )
    return role_report(
        role["declared"],
        observations,
        _cause_bounds(VALIDATION_ERRORS, context["subject_max"]),
        _choice((None, "f" * 64)),
        _choice(outcomes),
    )


def report_allowance(
    plan: dict[str, Any],
    evidence: list[dict[str, Any]],
    run_errors: dict[str, Any],
    prior: dict[str, list[dict[str, Any]]],
    allowance_context: dict[str, Any],
) -> int:
    """Finite U(R) from the same projections, actual frozen values and owned member domains."""
    subject = allowance_context["subject_max"]
    if type(subject) is not int or subject < 0:
        message = "Quality allowance needs the admitted inventory's maximum subject index"
        raise ValueError(message)
    invocations = allowance_context["invocations"]
    slots = {(slot["role_index"], slot["kind"]): slot for slot in invocations}
    if len(slots) != len(invocations) or len(invocations) != 9 * len(evidence):
        message = "Quality report allowance requires every unique planned invocation"
        raise ValueError(message)
    _slots(run_errors, RUN_ERRORS)
    base = adjudicate(plan, prior)
    domains = {"harms": harm_ids(), "outcomes": outcome_ids()}
    base = _adjudication_bounds(base, domains)
    pages = []
    for position, page in enumerate(evidence):
        roles = {}
        for offset, name in enumerate(BUNDLE_ROLES):
            role = page["roles"][name]
            if role["original"]["role_index"] != 3 * position + offset:
                message = "Quality allowance role order differs from frozen assessments"
                raise ValueError(message)
            roles[name] = _role_bounds(role, slots, allowance_context, domains["outcomes"])
        pages.append(
            page_report(
                page["frozen_policy"],
                page,
                roles,
                _cause_bounds(VALIDATION_ERRORS, subject),
                page["prior_reviews"],
            )
        )
    value = _projection(plan, base, _choice(("f" * 64,)), pages, _cause_bounds(RUN_ERRORS, subject))
    return _count(_identity(value, _choice(("f" * 64,))))
