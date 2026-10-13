# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Raw command delivery and confirmed publication remain distinct from artifact agreement."""

from __future__ import annotations

from contextlib import suppress
from typing import TYPE_CHECKING, Any

from jsonschema import Draft202012Validator

from document_quality import ROOT, read_json
from quality_evidence import reference

if TYPE_CHECKING:
    from pathlib import Path


MAX_RESPONSE_BYTES = 1024 * 1024
COHERENT_DELIVERY_EXITS = frozenset({0, 2, 3, 4, 130})


def ambiguous_exit(code: int | None) -> bool:
    """Actual signal/abnormal statuses retain uncertainty; never normalize subprocess integers."""
    return code not in COHERENT_DELIVERY_EXITS


def bounded_response(stdout: Path) -> dict[str, Any] | None:
    """Interpret at most one bounded response, retaining full raw bytes through its reference."""
    if stdout.stat().st_size > MAX_RESPONSE_BYTES:
        return None
    with stdout.open("rb") as stream:
        raw = stream.read(MAX_RESPONSE_BYTES + 1)
    if len(raw) <= MAX_RESPONSE_BYTES:
        with suppress(ValueError, UnicodeError, TypeError):
            value = read_json(raw)
            if isinstance(value, dict):
                return value
    return None


def execution_evidence(role: dict[str, Any], root: Path, observed: dict[str, Any]) -> None:
    """Raw command delivery determines effect truth; ambiguous failures are never replayed."""
    evidence = role["execution"]
    response = bounded_response(reference(root, evidence["stdout"]))
    reference(root, evidence["stderr"])
    if response is not None:
        Draft202012Validator(
            read_json((ROOT / "schemas/command-response.schema.json").read_bytes())
        ).validate(response)
    confirmed = response.get("publication") if isinstance(response, dict) else None
    # Exit 5/7 or missing delivery cannot establish absence, but a confirmed commit survives.
    ambiguous = evidence["timed_out"] or ambiguous_exit(evidence["exit_code"]) or confirmed is None
    effect = "completed" if confirmed == "completed" else "unknown" if ambiguous else confirmed
    observed["observed_publication"] = effect
    if response != evidence["response"]:
        message = "Declared response differs from retained bounded raw stdout"
        raise ValueError(message)
    if response is not None and (
        response.get("command") != "process"
        or (
            not ambiguous_exit(evidence["exit_code"])
            and evidence["exit_code"] != response["exit_code"]
        )
    ):
        message = "Retained process command or actual exit disagrees with its response"
        raise ValueError(message)
    if evidence["publication"] != effect:
        message = "Effect evidence cannot infer safe retry or erase a confirmed publication"
        raise ValueError(message)
    if role["outcome"] == "completed":
        if evidence["exit_code"] != 0 or evidence["timed_out"] or confirmed != "completed":
            message = "Completed role lacks successful processing/delivery"
            raise ValueError(message)
    elif evidence["exit_code"] == 0 and not evidence["timed_out"]:
        message = "Failure/refusal cannot carry successful process exit"
        raise ValueError(message)
    refusal_codes = {
        "E_ARGUMENT",
        "E_INPUT",
        "E_RESOURCE",
        "E_METHOD_INAPPLICABLE",
        "E_NOT_IMPLEMENTED",
    }
    known_refusal = response is not None and response.get("error", {}).get("code") in refusal_codes
    if role["outcome"] == "failed" and known_refusal and not ambiguous:
        message = "Known admission refusal must be classified as refused"
        raise ValueError(message)
    if role["outcome"] == "refused" and (
        ambiguous or evidence["exit_code"] not in {2, 3, 4} or not known_refusal
    ):
        message = "Refusal requires a complete known-effect refusal response"
        raise ValueError(message)
