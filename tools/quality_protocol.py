# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Semantic relationships of the closed frozen quality protocol."""

from __future__ import annotations

from pathlib import Path
from typing import Any

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
