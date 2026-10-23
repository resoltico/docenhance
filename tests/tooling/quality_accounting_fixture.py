# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Concrete closed abstention accounting and exact planned invocation fixtures."""

from __future__ import annotations

import copy
import hashlib
from pathlib import Path
from typing import TYPE_CHECKING, Any, NoReturn

from tools_path import prepare_tool_imports

from quality_fixtures import freeze, healthy_plan

from quality_attempt import RUN_ERRORS, VALIDATION_ERRORS, issue, new_attempt, new_original
from quality_commands import command_options
from quality_json import canonical
from quality_report import page_report, report, role_report

if TYPE_CHECKING:
    from quality_collection import BundleChecks

prepare_tool_imports()


def factual_run(count: int = 2, revisions: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    """A completed abstention container with no producer, PID, raw success or natural claim."""
    plan = healthy_plan(count, natural=False)
    protocol = plan["protocol"]
    values = [
        {
            "source_commit": protocol["source_commit"],
            "executable_sha256": protocol["executable_sha256"],
            "build": {},
        },
        {
            "pairs": [
                {
                    "sample": page["sample"],
                    "blind_pair": page["blind_pair"],
                    "reviewers": [person["id"] for person in protocol["panel"]],
                    "candidate_side": "left",
                }
                for page in protocol["pages"]
            ]
        },
    ]
    entries = []
    for name, value in zip(("build_provenance", "assignment"), values, strict=True):
        reference = {"path": name + ".json", "sha256": hashlib.sha256(canonical(value)).hexdigest()}
        protocol[name] = reference
        entries.append({"reference": reference, "value": value})
    for index, value in enumerate(revisions or []):
        reference = {
            "path": f"prior-{index}.json",
            "sha256": hashlib.sha256(canonical(value)).hexdigest(),
        }
        plan["assessments"][0]["review_revisions"].append(reference)
        entries.append({"reference": reference, "value": value})
    freeze(plan)
    receipt = {"protocol_sha256": plan["protocol_sha256"]}
    plan["preregistration"] = {
        "path": "receipt.json",
        "sha256": hashlib.sha256(canonical(receipt)).hexdigest(),
    }
    entries.insert(0, {"reference": plan["preregistration"], "value": receipt})
    slots, events, pages = [], [], []
    sources = {sample["id"]: sample for sample in protocol["corpus"]}
    prior = {}
    for page_index, (assessment, policy) in enumerate(
        zip(plan["assessments"], protocol["pages"], strict=True)
    ):
        roles = {}
        for role_offset, name in enumerate(("candidate", "baseline", "replicate")):
            role_index = 3 * page_index + role_offset
            assessment[name] = {"outcome": "missing_evidence", "bundle": None, "execution": None}
            original = new_original(role_index, assessment[name])
            events.append(
                {
                    "event": "original_observed",
                    "role_index": role_index,
                    "observation": copy.deepcopy(original),
                }
            )
            observations = {"original": original}
            for slot in factual_slots(role_index, policy["commands"][name]):
                if slot["kind"] == "scored_verify":
                    slot.update(planned_target=None, requested_argv=None, argv=None)
                slots.append(slot)
                attempt = new_attempt(slot)
                events.append(
                    {
                        "event": "invocation_prepared",
                        "invocation_index": slot["index"],
                        "observation": copy.deepcopy(attempt),
                    }
                )
                attempt["errors"]["launch"] = issue("not_eligible", slot["index"])
                attempt["primary_error"] = "errors.launch"
                events.append(
                    {
                        "event": "invocation_finished",
                        "invocation_index": slot["index"],
                        "observation": copy.deepcopy(attempt),
                    }
                )
                observations[slot["kind"]] = attempt
            roles[name] = role_report(
                assessment[name],
                observations,
                dict.fromkeys(VALIDATION_ERRORS),
                None,
                "missing_evidence",
            )
        retained = (revisions or []) if page_index == 0 else []
        prior[assessment["sample"]] = [
            review for revision in retained for review in revision["reviews"]
        ]
        page_policy = {**policy, "source": sources[assessment["sample"]]}
        pages.append(
            page_report(page_policy, assessment, roles, dict.fromkeys(VALIDATION_ERRORS), retained)
        )
    errors = dict.fromkeys(RUN_ERRORS)
    # This pure unavailable-evidence fixture does not manufacture reviewed native/runtime proof.
    errors["referenced_evidence"] = issue("referenced_evidence", 1)
    result = report(plan, protocol["executable_sha256"], pages, errors, prior)
    return {
        "kind": "quality_run",
        "protocol_version": 3,
        "evidence_contract": "docenhance-quality-v3",
        "protocol_sha256": plan["protocol_sha256"],
        "intended": {"plan": plan, "referenced_evidence": entries, "invocations": slots},
        "events": events,
        "report": result,
        "terminal": {
            "state": "finished",
            "accounted_roles": 3 * count,
            "accounted_invocations": 9 * count,
            "event_count": 21 * count,
            "launch_admission": "closed",
            "closure_reason": errors["referenced_evidence"],
        },
    }


def factual_slots(role_index: int, frozen: list[str]) -> list[dict[str, Any]]:
    """One fixed relative work index, preserving option-looking values in their actual roles."""
    target, _ = command_options(frozen, frozen[2], Path(frozen[0]))
    result = []
    process_target = str(Path(f"capture-{3 * role_index + 1}") / "candidate")
    for offset, kind in enumerate(("scored_verify", "replay_process", "replay_verify")):
        index = 3 * role_index + offset
        directory = f"capture-{index}"
        planned_target = target if offset == 0 else process_target
        requested = (
            frozen.copy() if offset == 1 else [frozen[0], "verify", planned_target, "--json"]
        )
        argv = requested.copy()
        if offset == 1:
            argv[4 + 2 * frozen[3:-1:2].index("--out-dir")] = planned_target
        result.append(
            {
                "index": index,
                "role_index": role_index,
                "kind": kind,
                "source_slot": "scored" if offset == 0 else "replay",
                "requested_argv": requested,
                "argv": argv,
                "planned_directory": directory,
                "planned_target": planned_target,
                "planned_stdout": directory + "/stdout",
                "planned_stderr": directory + "/stderr",
            }
        )
    return result


def unreached_read(
    _checks: BundleChecks,
    _index: int,
    _source_sha256: str,
    _published: dict[str, Any],
) -> NoReturn:
    """Fail if a control unexpectedly enters the real nonnullable record hook."""
    message = "The record hook must not be entered in this control"
    raise AssertionError(message)


def unreached_replay(
    _checks: BundleChecks, _index: int, _source_sha256: str, _record: dict[str, Any]
) -> NoReturn:
    """Fail if a control unexpectedly enters replay, without inventing a successful record."""
    message = "The replay hook must not be entered in this control"
    raise AssertionError(message)


def unreached_fidelity(
    _root: Path, _assessment: dict[str, str], _records: dict[str, dict[str, Any]]
) -> NoReturn:
    """Fail if a control unexpectedly requests independent fidelity analysis."""
    message = "The fidelity hook must not be entered in this control"
    raise AssertionError(message)
