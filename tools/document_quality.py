# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Frozen quality protocols and conservative class adjudication, separate from execution."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from quality_protocol import BUNDLE_ROLES, validate_relationships

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "spec/document-quality.schema.json"
CONTROL_MINIMUM_PAGES = 2
CONTROL_CLASSES = {"already_good", "preservation_control"}
FAMILY_ERROR_RATE = 0.05
MAX_JSON_DEPTH = 64


def canonical(value: object) -> bytes:
    """Strict UTF-8 compact sorted-key JSON; preserve arrays and reject nonfinite values."""
    return json.dumps(
        value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def frozen_digest(value: object) -> str:
    """Corpus, protocol and report identities hash their separate closed objects."""
    return hashlib.sha256(canonical(value)).hexdigest()


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    """Refuse duplicate keys instead of discarding evidence while decoding."""
    result = {}
    for name, value in pairs:
        if name in result:
            message = "Duplicate evidence JSON key"
            raise ValueError(message)
        result[name] = value
    return result


def validate_depth(value: object) -> None:
    """Bound malformed evidence nesting before schema diagnostics or canonical serialization."""
    pending: list[tuple[object, int]] = [(value, 0)]
    while pending:
        node, depth = pending.pop()
        if depth > MAX_JSON_DEPTH:
            message = "Evidence JSON exceeds its nesting depth"
            raise ValueError(message)
        if isinstance(node, dict):
            pending.extend((child, depth + 1) for child in node.values())
        elif isinstance(node, list):
            pending.extend((child, depth + 1) for child in node)


def read_json(data: bytes) -> dict[str, Any]:
    """Validate strict Unicode, finite numbers and duplicate-free objects before use."""
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=unique_object)
        validate_depth(value)
        canonical(value)
    except RecursionError as error:
        message = "Evidence JSON exceeds parser or serialization depth"
        raise ValueError(message) from error
    if not isinstance(value, dict):
        message = "Evidence JSON must be an object"
        raise TypeError(message)
    return value


def validator(kind: str) -> Draft202012Validator:
    """Use the reviewed closed plan or report shape and common definitions."""
    schema = read_json(SCHEMA.read_bytes())
    return Draft202012Validator({"$defs": schema["$defs"], "$ref": f"#/$defs/{kind}"})


def validate_plan(plan: dict[str, Any]) -> None:
    """Admit the schema and immutable protocol identity before evidence collection."""
    validator("plan").validate(plan)
    canonical(plan)
    protocol = plan["protocol"]
    if (
        frozen_digest(protocol) != plan["protocol_sha256"]
        or frozen_digest(protocol["corpus"]) != protocol["frozen_corpus_sha256"]
    ):
        message = "Frozen protocol or corpus identity differs"
        raise ValueError(message)
    validate_relationships(plan)


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
        "protocol_version": 2,
        "scope": "this frozen corpus and per-page policies only",
        "source_commit": protocol["source_commit"],
        "protocol_sha256": plan["protocol_sha256"],
        "classes": reports,
        "decision": "accepted" if all(r["accepted"] for r in reports.values()) else "abstain",
    }
