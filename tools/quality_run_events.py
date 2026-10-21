# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Fixed original/PREPARED/FINISHED event order and immutable transitions."""

from __future__ import annotations

from typing import Any

from quality_observations import original_ready, require
from quality_run_observations import attempt_relationships, original_relationships


def transition(before: dict[str, Any], after: dict[str, Any]) -> None:
    """Permit historical preparation to advance without rewriting established facts."""
    states = ("not_started", "may_have_started", "child_returned", "joined")
    require(
        states.index(before["launch_state"]) <= states.index(after["launch_state"]),
        "Prepared launch state cannot move backward",
    )
    for key in (
        "argv",
        "artifact_directory",
        "pid",
        "exit_code",
        "response",
        "response_origin",
        "primary_error",
    ):
        require(
            before[key] is None or before[key] == after[key], "Prepared observation was rewritten"
        )
    require(not before["timed_out"] or after["timed_out"], "Timeout cannot be erased")
    if before["publication"] == "completed":
        require(after["publication"] == "completed", "Confirmed commit cannot be erased")
    for group in ("errors", "cleanup_errors"):
        for key, cause in before[group].items():
            require(cause is None or cause == after[group][key], "First fixed cause was replaced")
    for name in ("stdout", "stderr"):
        for key in ("path", "sha256", "closed"):
            require(
                before[name][key] is None or before[name][key] == after[name][key],
                "Prepared stream identity was rewritten",
            )
        for key, cause in before[name]["errors"].items():
            require(
                cause is None or cause == after[name]["errors"][key], "Stream cause was replaced"
            )


def dependencies(
    value: dict[str, Any],
    slot: dict[str, Any],
    finished: dict[int, dict[str, Any]],
    original: dict[int, dict[str, Any]],
) -> None:
    """Require coherent original or predecessor evidence before preparation or launch."""
    if value["launch_state"] == "not_started" and value["artifact_directory"] is None:
        return
    index = slot["index"]
    if slot["kind"] == "scored_verify":
        require(
            original_ready(original[slot["role_index"]]),
            "Verifier cannot cure missing original delivery",
        )
    else:
        require(
            index - 1 in finished and finished[index - 1]["outcome"] == "completed",
            "Dependent invocation preceded coherent predecessor completion",
        )


def events(
    value: dict[str, Any], slots: list[dict[str, Any]]
) -> tuple[dict[int, dict[str, Any]], dict[int, dict[str, Any]]]:
    """Account each intended original and unique prepared/finished invocation once."""
    original: dict[int, dict[str, Any]] = {}
    prepared: dict[int, dict[str, Any]] = {}
    finished: dict[int, dict[str, Any]] = {}
    require(
        len(value["events"]) == 21 * len(value["intended"]["plan"]["assessments"]),
        "Finished accounting needs actual 21N events",
    )
    for event in value["events"]:
        observation = event["observation"]
        if event["event"] == "original_observed":
            index = event["role_index"]
            require(
                index < len(slots) // 3
                and index not in original
                and observation["role_index"] == index,
                "Original event index is duplicate or invalid",
            )
            original_relationships(observation)
            original[index] = observation
            continue
        index = event["invocation_index"]
        require(index < len(slots), "Invocation event index exceeds the actual work index")
        slot = slots[index]
        require(slot["role_index"] in original, "Invocation precedes its original observation")
        attempt_relationships(observation, slot)
        dependencies(observation, slot, finished, original)
        if event["event"] == "invocation_prepared":
            require(index not in prepared and index not in finished, "Duplicate prepared event")
            require(
                observation["launch_state"] in ("not_started", "may_have_started")
                and observation["pid"] is None
                and observation["exit_code"] is None
                and observation["response"] is None,
                "Prepared intent invents returned child facts",
            )
            prepared[index] = observation
        else:
            require(
                index in prepared and index not in finished,
                "Finished event lacks unique preparation",
            )
            transition(prepared[index], observation)
            finished[index] = observation
    require(
        len(original) == len(slots) // 3 and len(finished) == len(slots),
        "Finished container omits an intended original or invocation",
    )
    return original, finished
