# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Conservative per-class quality adjudication retaining harms and uncertainty."""

from __future__ import annotations

import math
from typing import Any

from quality_plan import validate_plan
from quality_protocol import BUNDLE_ROLES

CONTROL_MINIMUM_PAGES = 2
CONTROL_CLASSES = {"already_good", "preservation_control"}
FAMILY_ERROR_RATE = 0.05


def adjudicate(
    plan: dict[str, Any], prior: dict[str, list[dict[str, Any]]] | None = None
) -> dict[str, Any]:
    """Incomplete engineering or human evidence abstains while retaining every intended page."""
    validate_plan(plan)
    protocol = plan["protocol"]
    classes = protocol["claim_policy"]["classes"]
    samples = {sample["id"]: sample for sample in protocol["corpus"]}
    panel = {person["id"] for person in protocol["panel"]}
    results: dict[str, list[dict[str, Any]]] = {name: [] for name in classes}
    for assessment in plan["assessments"]:
        reviews = assessment["reviews"]
        retained = reviews + (prior or {}).get(assessment["sample"], [])
        outcomes = {name: assessment[name]["outcome"] for name in BUNDLE_ROLES}
        complete = all(value == "completed" for value in outcomes.values())
        reviewed = {review["reviewer"] for review in reviews} == panel
        harms = sorted({harm for review in retained for harm in review["harms"]})
        deltas = {review["delta"] for review in retained}
        page_uncertain = (
            not reviewed
            or any(review["uncertain"] or review["delta"] < 0 for review in retained)
            or len(deltas) != 1
        )
        item = {
            "sample": assessment["sample"],
            "engineering_complete": complete,
            "natural_document": samples[assessment["sample"]]["source_kind"] == "natural_document",
            "review_complete": reviewed,
            "harms": harms,
            "uncertain": page_uncertain,
            "delta": min(deltas) if deltas else None,
            "outcomes": outcomes,
        }
        for name in samples[assessment["sample"]]["classes"]:
            results[name].append(item)
    reports = {}
    for name, items in results.items():
        harmed = [
            item["sample"]
            for item in items
            if item["harms"] or (item["delta"] is not None and item["delta"] < 0)
        ]
        uncertain = [item["sample"] for item in items if item["uncertain"]]
        incomplete = [
            item["sample"]
            for item in items
            if not item["engineering_complete"] or not item["review_complete"]
        ]
        n = len(items)
        positive = sum(
            item["delta"] is not None
            and item["delta"] > 0
            and not item["uncertain"]
            and item["engineering_complete"]
            for item in items
        )
        tail = sum(math.comb(n, k) for k in range(positive, n + 1)) / (2**n)
        accepted = (
            bool(items)
            and not harmed
            and not uncertain
            and not incomplete
            and all(item["natural_document"] for item in items)
        )
        if name in CONTROL_CLASSES:
            accepted &= n >= CONTROL_MINIMUM_PAGES
        else:
            accepted &= positive == n and tail <= FAMILY_ERROR_RATE / len(classes)
        reports[name] = {
            "count": n,
            "positive": positive,
            "sign_tail": tail,
            "harmed": harmed,
            "uncertain": uncertain,
            "incomplete": incomplete,
            "engineering_failed": [
                item["sample"] for item in items if "failed" in item["outcomes"].values()
            ],
            "engineering_refused": [
                item["sample"] for item in items if "refused" in item["outcomes"].values()
            ],
            "missing_evidence": [
                item["sample"] for item in items if "missing_evidence" in item["outcomes"].values()
            ],
            "review_incomplete": [item["sample"] for item in items if not item["review_complete"]],
            "accepted": accepted,
            "pages": items,
        }
    return {
        "protocol_version": 3,
        "scope": "this frozen corpus and per-page policies only",
        "source_commit": protocol["source_commit"],
        "protocol_sha256": plan["protocol_sha256"],
        "classes": reports,
        "decision": "accepted" if all(r["accepted"] for r in reports.values()) else "abstain",
    }
