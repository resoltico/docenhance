# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Synthetic quality controls; scores here never establish natural-document benefit."""

from __future__ import annotations

import hashlib
import json
from typing import Any

from tools_path import ROOT

from quality_json import frozen_digest

SCHEMA = ROOT / "spec/document-quality.schema.json"


def healthy_plan(count: int = 10, *, natural: bool = True) -> dict[str, Any]:
    """Simulate natural-page attestations and scores; these units prove no empirical benefit."""
    classes = json.loads(SCHEMA.read_bytes())["$defs"]["classes"]["items"]["enum"]
    corpus: list[dict[str, Any]] = []
    assessments: list[dict[str, Any]] = []
    for number in range(count):
        name = str(number)
        corpus.append(
            {
                "id": name,
                "document_id": name,
                "path": f"{name}.png",
                "sha256": hashlib.sha256(name.encode()).hexdigest(),
                "permission": {
                    "source": "synthetic control",
                    "basis": "first-party",
                    "evidence": "fixture source",
                    "curator": "test",
                },
                "split": "heldout",
                "source_kind": "natural_document" if natural else "synthetic_control",
                "classes": classes,
            }
        )
        assessments.append(
            {
                "sample": name,
                "options": [],
                "baseline_options": [],
                "baseline": f"b-{name}",
                "candidate": f"c-{name}",
                "replicate": f"r-{name}",
                "reviews": [
                    {
                        "reviewer": reviewer,
                        "blind_pair": name,
                        "delta": 1,
                        "harms": [],
                        "uncertain": False,
                        "rationale": "synthetic score",
                    }
                    for reviewer in ("a", "b", "c")
                ],
            }
        )
    pages: list[dict[str, Any]] = []
    for item in assessments:
        pages.append(
            {
                "sample": item["sample"],
                "blind_pair": item["sample"],
                "assets": [],
                "commands": {
                    role: [
                        "binary",
                        "process",
                        f"{item['sample']}.png",
                        "--out-dir",
                        item[role],
                        "--json",
                    ]
                    for role in ("candidate", "baseline", "replicate")
                },
            }
        )
        for role in ("candidate", "baseline", "replicate"):
            item[role] = {
                "outcome": "completed",
                "bundle": item[role],
                "execution": {
                    "argv": pages[-1]["commands"][role],
                    "stdout": {"path": "stdout", "sha256": "a" * 64},
                    "stderr": {"path": "stderr", "sha256": "a" * 64},
                    "exit_code": 0,
                    "timed_out": False,
                    "response": {},
                    "publication": "completed",
                },
            }
        del item["options"], item["baseline_options"]
        item["review_revisions"] = []
    schema = json.loads(SCHEMA.read_bytes())
    reference = {"path": "evidence", "sha256": "a" * 64}
    protocol = {
        "evidence_contract": "docenhance-quality-v3",
        "source_commit": "a" * 40,
        "executable_sha256": "b" * 64,
        "frozen_corpus_sha256": frozen_digest(corpus),
        "panel": [{"id": reviewer, "independent": True} for reviewer in ("a", "b", "c")],
        "corpus": corpus,
        "pages": pages,
        "build_provenance": reference,
        "review_protocol": reference,
        "assignment": reference,
        "tuning": {"performed": False, "receipt": None},
        "claim_policy": schema["$defs"]["plan"]["properties"]["protocol"]["properties"][
            "claim_policy"
        ]["const"],
    }
    return {
        "protocol_version": 3,
        "protocol": protocol,
        "protocol_sha256": frozen_digest(protocol),
        "preregistration": reference,
        "assessments": assessments,
    }


def freeze(plan: dict[str, Any]) -> None:
    """Test pre-registration only; production never repairs a drifted protocol."""
    protocol = plan["protocol"]
    protocol["frozen_corpus_sha256"] = frozen_digest(protocol["corpus"])
    plan["protocol_sha256"] = frozen_digest(protocol)
