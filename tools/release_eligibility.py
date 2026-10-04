# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Refuse publication without complete direct-checkout CI for the exact source commit."""

from __future__ import annotations

from datetime import datetime, timedelta
from typing import TYPE_CHECKING, cast

from changelog import ReleaseError, require
from ci_contract import WORKFLOW_PATH, job_names
from release_publication import Record, record

if TYPE_CHECKING:
    from collections.abc import Callable

EVENTS = ("push", "workflow_dispatch")


def collection(value: object) -> list[Record]:
    """Validate one bounded GitHub collection without discarding malformed records."""
    require("Invalid CI record collection", condition=isinstance(value, list))
    return [record(item) for item in cast("list[object]", value)]


def created(run: Record) -> datetime:
    """Compare server run creation times without treating identifiers as clocks."""
    stamp = run.get("created_at")
    require("Invalid CI creation timestamp", condition=isinstance(stamp, str))
    try:
        value = datetime.fromisoformat(cast("str", stamp))
    except ValueError as error:
        message = "Invalid CI creation timestamp"
        raise ReleaseError(message) from error
    require("CI creation timestamp must be UTC", condition=value.utcoffset() == timedelta(0))
    return value


def identity(run: Record) -> tuple[int, int]:
    """Bind one positive run identity and latest attempt."""
    run_id, attempt = run.get("id"), run.get("run_attempt")
    require("Invalid CI run identity", condition=type(run_id) is int and run_id > 0)
    require("Invalid CI attempt identity", condition=type(attempt) is int and attempt > 0)
    return cast("int", run_id), cast("int", attempt)


def require_quality(request: Callable[[str, str], object], base: str, commit: str) -> None:
    """Read the latest direct run, verify its complete attempt, and refuse changed evidence."""
    candidates: list[Record] = []
    for event in EVENTS:
        path = f"{base}/actions/workflows/ci.yml/runs?head_sha={commit}&event={event}&per_page=1"
        response = record(request("GET", path))
        runs = collection(response.get("workflow_runs"))
        count = response.get("total_count")
        require("Invalid CI run count", condition=type(count) is int and count >= 0)
        require(
            "Incomplete latest CI collection", condition=len(runs) == min(cast("int", count), 1)
        )
        for run in runs:
            require(
                "CI query returned another event or commit",
                condition=run.get("event") == event and run.get("head_sha") == commit,
            )
        candidates.extend(runs)
    require("No direct full CI run exists for the release commit", condition=bool(candidates))
    latest = max(created(candidate) for candidate in candidates)
    matches = [candidate for candidate in candidates if created(candidate) == latest]
    require("Latest direct CI ordering is ambiguous", condition=len(matches) == 1)
    selected = matches[0]
    run_id, attempt = identity(selected)
    current = record(request("GET", f"{base}/actions/runs/{run_id}"))
    require("CI run or attempt changed", condition=identity(current) == (run_id, attempt))
    workflow_path = current.get("path")
    require(
        "CI run belongs to another checkout",
        condition=current.get("head_sha") == commit
        and current.get("event") in EVENTS
        and isinstance(workflow_path, str)
        and workflow_path.partition("@")[0] == WORKFLOW_PATH
        and ("@" not in workflow_path or bool(workflow_path.partition("@")[2])),
    )
    repository = base.removeprefix("repos/")
    require(
        "CI repository identity mismatch",
        condition=record(current.get("repository")).get("full_name") == repository
        and record(current.get("head_repository")).get("full_name") == repository,
    )
    require(
        "Latest direct full CI is not successful",
        condition=current.get("status") == "completed" and current.get("conclusion") == "success",
    )
    response = record(
        request("GET", f"{base}/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100")
    )
    jobs = collection(response.get("jobs"))
    expected = job_names()
    require(
        "CI attempt is missing or duplicates required jobs",
        condition=response.get("total_count") == len(expected)
        and len(jobs) == len(expected)
        and {job.get("name") for job in jobs} == expected,
    )
    require(
        "Required CI work did not pass for the exact attempt",
        condition=all(
            job.get("head_sha") == commit
            and job.get("run_id") == run_id
            and job.get("run_attempt") == attempt
            and job.get("status") == "completed"
            and job.get("conclusion") == "success"
            for job in jobs
        ),
    )
    observed = record(request("GET", f"{base}/actions/runs/{run_id}"))
    require(
        "CI changed during admission",
        condition=identity(observed) == (run_id, attempt)
        and observed.get("status") == "completed"
        and observed.get("conclusion") == "success"
        and observed.get("head_sha") == commit,
    )
