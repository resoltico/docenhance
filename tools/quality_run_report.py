# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed completed report agreement and global actual execution identity accounting."""

from __future__ import annotations

from typing import Any

from quality_attempt import ATTEMPT_KINDS
from quality_observations import completed_record_agreement, original_ready, require
from quality_protocol import BUNDLE_ROLES, admitted_prior_snapshots
from quality_report import report
from quality_run_observations import complete_stream


def bounded_causes(values: list[Any], maximum: int) -> None:
    """Keep global reference subjects within the actual admitted inventory."""
    for cause in values:
        if cause is not None and cause["subject"] is not None:
            require(cause["subject"] <= maximum, "Issue subject exceeds its actual work inventory")


def admit_run_identities(identifiers: list[str], seen: set[str]) -> None:
    """Keep scored and replay identifiers distinct across the whole intended run."""
    require(
        len(identifiers) == len(set(identifiers)) and not seen.intersection(identifiers),
        "Scored/replay role identities collide across the intended run",
    )
    seen.update(identifiers)


def report_relationships(
    value: dict[str, Any],
    original: dict[int, dict[str, Any]],
    finished: dict[int, dict[str, Any]],
    maximum: int,
) -> None:
    """Bind final roles to actual finished events, frozen policy and unchanged adjudication."""
    plan = value["intended"]["plan"]
    evidence = value["report"]["evidence"]
    snapshots = value["intended"]["referenced_evidence"]
    policies = {page["sample"]: page for page in plan["protocol"]["pages"]}
    sources = {sample["id"]: sample for sample in plan["protocol"]["corpus"]}
    offset = 3 + int(plan["protocol"]["tuning"]["receipt"] is not None)
    prior = {}
    seen_identifiers: set[str] = set()
    require(len(evidence) == len(plan["assessments"]), "Report dropped an intended page")
    for page_index, (assessment, page) in enumerate(
        zip(plan["assessments"], evidence, strict=True)
    ):
        policy = policies[assessment["sample"]]
        frozen = {
            "source": sources[assessment["sample"]],
            "commands": policy["commands"],
            "assets": policy["assets"],
        }
        require(
            page["sample"] == assessment["sample"]
            and page["frozen_policy"] == frozen
            and page["reviews"] == assessment["reviews"]
            and page["review_revisions"] == assessment["review_revisions"],
            "Frozen page/review identity drift",
        )
        count = len(assessment["review_revisions"])
        admitted = admitted_prior_snapshots(
            assessment,
            policy | {"panel": [p["id"] for p in plan["protocol"]["panel"]]},
            snapshots[offset : offset + count],
        )
        offset += count
        require(page["prior_reviews"] == admitted, "Admitted human revisions were lost or invented")
        prior[assessment["sample"]] = [
            review for revision in admitted for review in revision["reviews"]
        ]
        if len(admitted) != count:
            require(
                page["validation"]["prior_reviews"] is not None,
                "Unavailable revision lost its finite cause",
            )
        bounded_causes(list(page["validation"].values()), maximum)
        identifiers: list[str] = []
        for role_offset, name in enumerate(BUNDLE_ROLES):
            index = 3 * page_index + role_offset
            role = page["roles"][name]
            require(
                role["declared"] == assessment[name]
                and role["original"] == original[index]
                and original[index]["declared"] == assessment[name],
                "Original role snapshot drift",
            )
            for step, kind in enumerate(ATTEMPT_KINDS):
                require(
                    role[kind] == finished[3 * index + step],
                    "Final role must equal FINISHED, not PREPARED",
                )
            bounded_causes(list(role["validation"].values()), maximum)
            require(
                role["outcome"] in (assessment[name]["outcome"], "missing_evidence"),
                "Role outcome was upgraded",
            )
            if role["outcome"] == "completed":
                require(
                    not any(page["validation"].values())
                    and not any(role["validation"].values())
                    and original[index]["observed_publication"] == "completed"
                    and original_ready(original[index])
                    and all(
                        complete_stream(original[index][stream]) for stream in ("stdout", "stderr")
                    )
                    and all(
                        role[kind]["outcome"] == "completed"
                        and role[kind]["descendant_cleanup"] != "unconfirmed"
                        for kind in ATTEMPT_KINDS
                    ),
                    "Completed role lacks its full original/verification/replay obligations",
                )
                build = snapshots[1]["value"]
                require(build is not None, "Completed role lacks acquired build provenance")
                identifiers.extend(
                    completed_record_agreement(role, sources[assessment["sample"]], build)
                )
        admit_run_identities(identifiers, seen_identifiers)
    current = value["report"]
    require(
        current == report(plan, current["executable_sha256"], evidence, current["errors"], prior),
        "Factual report differs from actual adjudication/projection/digest",
    )
