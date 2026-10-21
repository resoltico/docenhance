# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One immutable acquisition of every parsed frozen quality reference before write admission."""

from __future__ import annotations

import copy
from typing import TYPE_CHECKING, Any

from jsonschema import ValidationError

from quality_acquisition import acquire_reference
from quality_attempt import issue
from quality_provenance_contract import capture_references
from quality_provenance_validation import snapshot_failures
from quality_references import referenced_order
from quality_schema import validator

if TYPE_CHECKING:
    from pathlib import Path


class FrozenEvidence:
    """Parsed snapshots and valid prior judgments survive subsequent reference refusals."""

    def __init__(self, plan: dict[str, Any], root: Path) -> None:
        """Prepare this actual owner and all retained state before its effects."""
        self.entries: list[dict[str, Any]] = []
        self.failures: dict[int, dict[str, Any]] = {}
        self.prior: dict[str, list[dict[str, Any]]] = {}
        self.prior_failures: dict[str, dict[str, Any]] = {}
        self._values: dict[tuple[str, str], dict[str, Any]] = {}
        self._failed: set[tuple[str, str]] = set()
        for ref in referenced_order(plan):
            self._acquire(root, ref)
        try:
            nested = capture_references(self.value(plan["protocol"]["build_provenance"]))
        except (ValueError, TypeError) as error:
            self.failures.setdefault(1, issue("referenced_evidence", 1, error))
            nested = []
        for ref in nested:
            self._acquire(root, ref)
        for index in snapshot_failures(plan, self.entries):
            self.failures.setdefault(index, issue("referenced_evidence", index))
        self._retain_reviews(plan)

    def _acquire(self, root: Path, ref: dict[str, Any]) -> None:
        index = len(self.entries)
        value: dict[str, Any] | None
        key = ref["path"], ref["sha256"]
        try:
            if key not in self._values and key not in self._failed:
                self._values[key] = acquire_reference(root, ref)
            value = self.value(ref)
        except (OSError, ValueError, TypeError, MemoryError) as error:
            self._failed.add(key)
            value = None
            self.failures[index] = issue("referenced_evidence", index, error)
        self.entries.append({"reference": copy.deepcopy(ref), "value": value})

    def value(self, ref: dict[str, Any]) -> dict[str, Any]:
        """Return the one actually acquired object or its original refusal."""
        key = ref["path"], ref["sha256"]
        if key in self._failed or key not in self._values:
            message = "Frozen referenced value was not acquired"
            raise ValueError(message)
        return self._values[key]

    def _retain_reviews(self, plan: dict[str, Any]) -> None:
        policies = {page["sample"]: page for page in plan["protocol"]["pages"]}
        panel = {person["id"] for person in plan["protocol"]["panel"]}
        first = 3 + (plan["protocol"]["tuning"]["receipt"] is not None)
        index = first
        for assessment in plan["assessments"]:
            sample = assessment["sample"]
            retained = self.prior.setdefault(sample, [])
            for ref in assessment["review_revisions"]:
                try:
                    revision = self._review_snapshot(ref, sample, policies[sample], panel)
                    retained.append(revision)
                except (
                    OSError,
                    ValueError,
                    TypeError,
                    KeyError,
                    MemoryError,
                    ValidationError,
                ) as error:
                    cause = issue("prior_reviews", index, error)
                    self.failures.setdefault(index, cause)
                    self.prior_failures.setdefault(sample, cause)
                index += 1

    def _review_snapshot(
        self, ref: dict[str, Any], sample: str, policy: dict[str, Any], panel: set[str]
    ) -> dict[str, Any]:
        """Admit this same frozen review object before retaining its actual judgment."""
        revision = self.value(ref)
        validator("review_revision").validate(revision)
        reviews = revision["reviews"]
        reviewers = [review["reviewer"] for review in reviews]
        if (
            revision["sample"] != sample
            or len(reviewers) != len(set(reviewers))
            or not set(reviewers).issubset(panel)
            or any(review["blind_pair"] != policy["blind_pair"] for review in reviews)
        ):
            message = "Prior revision differs from its frozen page/panel"
            raise ValueError(message)
        return revision
