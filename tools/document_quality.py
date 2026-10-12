# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Conservative paired, per-class no-harm adjudication; no runtime image enhancement rules."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "spec/document-quality.schema.json"
CONTROL_CLASSES = {"already_good", "preservation_control"}
FAMILY_ERROR_RATE = 0.05
BUNDLE_ROLES = ("candidate", "baseline", "replicate")


def frozen_digest(corpus: list[dict[str, Any]]) -> str:
    """Bind the original split, permissions, classes and file identities without a self-hash."""
    return hashlib.sha256(
        json.dumps(corpus, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def validate_plan(plan: dict[str, Any]) -> None:
    """Refuse missing clearance, corpus drift, tuning contamination and invented reviewers."""
    Draft202012Validator(json.loads(SCHEMA.read_bytes())).validate(plan)
    if frozen_digest(plan["corpus"]) != plan["frozen_corpus_sha256"]:
        message = "Frozen corpus identity differs; tuning or relabeling invalidates evaluation"
        raise ValueError(message)
    panel = [person["id"] for person in plan["panel"]]
    samples = [sample["id"] for sample in plan["corpus"]]
    identities = [sample["sha256"] for sample in plan["corpus"]]
    assessed = [item["sample"] for item in plan["assessments"]]
    if any(len(items) != len(set(items)) for items in (panel, samples, identities, assessed)):
        message = "Duplicate reviewer, sample, source bytes or assessment"
        raise ValueError(message)
    groups: dict[str, list[str]] = {}
    for sample in plan["corpus"]:
        groups.setdefault(sample["document_id"], []).append(sample["split"])
    if any(
        len(set(splits)) != 1 or ("heldout" in splits and len(splits) != 1)
        for splits in groups.values()
    ):
        message = "A physical document cannot cross splits or count as independent held-out pages"
        raise ValueError(message)
    heldout = {sample["id"] for sample in plan["corpus"] if sample["split"] == "heldout"}
    if set(assessed) != heldout:
        message = "Every frozen held-out input must be assessed exactly once, never tuning inputs"
        raise ValueError(message)
    for assessment in plan["assessments"]:
        paths = {Path(assessment[name]) for name in BUNDLE_ROLES}
        if len(paths) != len(BUNDLE_ROLES):
            message = "Candidate, baseline and replicate require distinct bundle directories"
            raise ValueError(message)
        reviewers = [review["reviewer"] for review in assessment["reviews"]]
        pairs = {review["blind_pair"] for review in assessment["reviews"]}
        if len(set(reviewers)) != len(reviewers) or set(reviewers) != set(panel) or len(pairs) != 1:
            message = "Each blind pair needs the whole independent panel exactly once"
            raise ValueError(message)


def adjudicate(plan: dict[str, Any]) -> dict[str, Any]:
    """Any material harm blocks acceptance; disagreement abstains rather than hiding in a mean."""
    validate_plan(plan)
    classes = json.loads(SCHEMA.read_bytes())["$defs"]["classes"]["items"]["enum"]
    samples = {sample["id"]: sample for sample in plan["corpus"]}
    results: dict[str, list[dict[str, Any]]] = {name: [] for name in classes}
    for assessment in plan["assessments"]:
        reviews = assessment["reviews"]
        harms = sorted({harm for review in reviews for harm in review["harms"]})
        deltas = {review["delta"] for review in reviews}
        uncertain = any(review["uncertain"] for review in reviews) or len(deltas) != 1
        # Disagreement needs another frozen review; unanimous scoring is conservative.
        item = {
            "sample": assessment["sample"],
            "harms": harms,
            "uncertain": uncertain,
            "delta": min(deltas),
        }
        for name in samples[assessment["sample"]]["classes"]:
            results[name].append(item)
    reports = {}
    for name, items in results.items():
        harmed = [item["sample"] for item in items if item["harms"] or item["delta"] < 0]
        uncertain_samples = [item["sample"] for item in items if item["uncertain"]]
        n = len(items)
        positive = sum(item["delta"] > 0 and not item["uncertain"] for item in items)
        # One-sided exact paired sign tail, ties treated as nonpositive; Bonferroni family control.
        tail = sum(math.comb(n, k) for k in range(positive, n + 1)) / (2**n)
        accepted = bool(items) and not harmed and not uncertain_samples
        if name not in CONTROL_CLASSES:
            accepted &= positive == n and tail <= FAMILY_ERROR_RATE / len(classes)
        reports[name] = {
            "count": n,
            "positive": positive,
            "sign_tail": tail,
            "harmed": harmed,
            "uncertain": uncertain_samples,
            "accepted": accepted,
        }
    return {
        "protocol_version": 1,
        "scope": "this frozen corpus and explicit settings only",
        "source_commit": plan["source_commit"],
        "classes": reports,
        "decision": "accepted" if all(r["accepted"] for r in reports.values()) else "abstain",
    }
