# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Coherent original/product observations and bound completed native records."""

from __future__ import annotations

from typing import Any

from jsonschema import Draft202012Validator

from quality_attempt import MAX_RESPONSE_BYTES, delivered_path
from quality_json import ROOT, read_json


def validate_product_response(value: dict[str, Any]) -> None:
    """Completed effect/command claims require the actual native response schema, not a prefix."""
    Draft202012Validator(
        read_json((ROOT / "schemas/command-response.schema.json").read_bytes())
    ).validate(value)


def original_ready(original: dict[str, Any]) -> bool:
    """Read-only verification cannot cure missing or unattributed original delivery."""
    execution = original["declared"]["execution"]
    response = original["response"]
    if (
        original["declared"]["outcome"] != "completed"
        or execution is None
        or response is None
        or execution["exit_code"] != 0
        or execution["timed_out"]
        or response != execution["response"]
        or response.get("command") != "process"
        or response.get("exit_code") != 0
        or response.get("publication") != "completed"
        or any(original["errors"].values())
    ):
        return False
    for name in ("stdout", "stderr"):
        stream = original[name]
        if (
            stream["read_state"] != "eof"
            or stream["retention_state"] != "complete"
            or stream["closed"] is not True
            or stream["observed_extent"] != "exact"
            or stream["retained_bytes"] is None
            or stream["retained_bytes"] > MAX_RESPONSE_BYTES
            or (name == "stderr" and stream["retained_bytes"] != 0)
            or stream["observed_prefix_bytes"] != stream["retained_bytes"]
            or stream["sha256"] != execution[name]["sha256"]
            or any(stream["errors"].values())
        ):
            return False
    return True


def require(condition: object, message: str) -> None:
    """Refuse incoherent evidence without repairing or replacing its facts."""
    if not condition:
        raise ValueError(message)


def completed_record_agreement(
    role: dict[str, Any], source: dict[str, Any], build: dict[str, Any]
) -> tuple[str, str]:
    """Correlate actual completed native process/verifier snapshots without reopening artifacts."""
    original = role["original"]["response"]
    replay = role["replay_process"]["response"]
    record, repeated = original["record"], replay["record"]
    require(
        record["source"]["sha256"] == source["sha256"] and record["build"] == build["build"],
        "Completed role differs from frozen source/build provenance",
    )
    require(role["record_sha256"] == original["record_sha256"], "Scored record identity drift")
    require(
        original["output"] == delivered_path(role["declared"]["bundle"], "result.png"),
        "Original delivered target spelling differs",
    )
    require(
        all(record[key] == repeated[key] for key in record if key != "record"),
        "Replay semantic record differs from the scored execution",
    )
    argv = role["replay_process"]["argv"]
    target = argv[4 + 2 * argv[3:-1:2].index("--out-dir")]
    require(
        replay["output"] == delivered_path(target, "result.png"), "Replay delivered target differs"
    )
    identifiers = (record["record"]["run"], repeated["record"]["run"])
    require(identifiers[0] != identifiers[1], "Replay reused its scored execution identity")
    for name, response in (("scored_verify", original), ("replay_verify", replay)):
        verified = role[name]["response"]
        context = response["record"]["record"]
        confirmed = verified["confirmed"]
        artifacts = [response["record"]["output"], *response["record"]["assets"]]
        require(
            len(confirmed) == len(artifacts) + 1, "Completed verifier artifact inventory differs"
        )
        for artifact in artifacts:
            matches = [item for item in confirmed if item["path"] == artifact["path"]]
            require(
                len(matches) == 1
                and all(matches[0][key] == artifact[key] for key in ("path", "sha256", "bytes")),
                "Completed verifier artifact identity differs from its process record",
            )
        identities = [item for item in confirmed if item["path"] == "run.json"]
        require(
            verified["command"] == "verify"
            and verified["directory"] == role[name]["argv"][2]
            and verified["run"] == context["run"]
            and verified["recorded"] == context["recorded"]
            and len(identities) == 1
            and identities[0]["sha256"] == response["record_sha256"]
            and 0 < identities[0]["bytes"] <= 256 * 1024,
            "Completed verifier disagrees with its bound process record",
        )
    return identifiers
