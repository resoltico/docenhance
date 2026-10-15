# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One owned quality replay retains raw delivery and artifacts on every effectful attempt."""

from __future__ import annotations

import os
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any

from jsonschema import Draft202012Validator, ValidationError

from document_quality import ROOT, read_json
from process_execution import owned_process
from quality_evidence import digest, evidence_error
from quality_execution import MAX_RESPONSE_BYTES, ambiguous_exit

if TYPE_CHECKING:
    from collections.abc import Callable

REPLAY_TIMEOUT_SECONDS = 600


@dataclass(frozen=True)
class ReplayObservation:
    """Actual attempt facts are retained even when no coherent record was returned."""

    record: dict[str, Any] | None
    attempt: dict[str, Any]


def retained_reference(root: Path, path: Path) -> dict[str, str]:
    """Reference complete captured bytes without loading untrusted output unboundedly."""
    return {"path": str(path.relative_to(root)), "sha256": digest(path)}


def delivery(attempt: dict[str, Any], directory: Path) -> None:
    """A bounded response parse preserves confirmed commit independently of later refusal."""
    raw = directory / "stdout"
    response = None
    attempt["response_truncated"] = raw.stat().st_size > MAX_RESPONSE_BYTES
    try:
        if not attempt["response_truncated"]:
            response = read_json(raw.read_bytes())
            schema = read_json((ROOT / "schemas/command-response.schema.json").read_bytes())
            Draft202012Validator(schema).validate(response)
    except (ValueError, TypeError, ValidationError) as error:
        attempt["errors"].append(evidence_error(error))
        response = None
    attempt["response"] = response
    confirmed = response.get("publication") if response is not None else None
    ambiguous = attempt["timed_out"] or ambiguous_exit(attempt["exit_code"]) or confirmed is None
    attempt["publication"] = (
        "completed" if confirmed == "completed" else "unknown" if ambiguous else confirmed
    )


def execute_replay(
    command: list[str],
    root: Path,
    read_bundle: Callable[..., dict[str, Any]],
) -> ReplayObservation:
    """Run once in owned retained storage; never retry or erase uncertain effects."""
    directory = Path(tempfile.mkdtemp(prefix="quality-replay-", dir=root))
    output = directory / "candidate"
    argv = command.copy()
    output_index = command[3:-1:2].index("--out-dir") * 2 + 4
    argv[output_index] = str(output)
    source = root / command[2]
    executable = Path(command[0])
    attempt: dict[str, Any] = {
        "argv": argv,
        "exit_code": None,
        "timed_out": False,
        "response": None,
        "publication": "unknown",
        "errors": [],
        "outcome": "failed",
        "artifact_directory": str(directory.relative_to(root)),
        "response_truncated": False,
    }
    with (directory / "stdout").open("wb") as stdout, (directory / "stderr").open("wb") as stderr:
        try:
            with owned_process(argv, dict(os.environ), (stdout, stderr), cwd=root) as process:
                attempt["exit_code"] = process.wait(timeout=REPLAY_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as error:
            attempt["timed_out"] = True
            attempt["errors"].append(evidence_error(error))
        except OSError as error:
            attempt["errors"].append(evidence_error(error))
    attempt["stdout"] = retained_reference(root, directory / "stdout")
    attempt["stderr"] = retained_reference(root, directory / "stderr")
    delivery(attempt, directory)
    return verify_replay(attempt, output, source, executable, read_bundle)


def verify_replay(
    attempt: dict[str, Any],
    output: Path,
    source: Path,
    executable: Path,
    read_bundle: Callable[..., dict[str, Any]],
) -> ReplayObservation:
    """A successful process and immutable verified record are separate required observations."""
    record = None
    try:
        record = replay_record(attempt, output, source, executable, read_bundle)
        attempt["outcome"] = "completed"
    except (
        OSError,
        ValueError,
        TypeError,
        KeyError,
        ValidationError,
        subprocess.SubprocessError,
    ) as error:
        attempt["errors"].append(evidence_error(error))
    return ReplayObservation(record, attempt)


def replay_record(
    attempt: dict[str, Any],
    output: Path,
    source: Path,
    executable: Path,
    read_bundle: Callable[..., dict[str, Any]],
) -> dict[str, Any]:
    """Validate processing delivery and bind the real verifier's immutable record."""
    response = attempt["response"]
    if (
        attempt["exit_code"] != 0
        or attempt["timed_out"]
        or response is None
        or response.get("command") != "process"
        or response.get("exit_code") != 0
        or response.get("output") != str(output) + ("\\" if os.name == "nt" else "/") + "result.png"
        or response.get("publication") != "completed"
        or (output.parent / "stderr").stat().st_size != 0
    ):
        message = "Frozen replay did not complete coherent processing and delivery"
        raise ValueError(message)
    record = read_bundle(executable, output, digest(source))
    identity = response["record"]
    if (
        identity["run"] != record["record"]["run"]
        or identity["sha256"] != digest(output / "run.json")
        or identity["bytes"] != (output / "run.json").stat().st_size
    ):
        message = "Replay response differs from its verified record identity"
        raise ValueError(message)
    return record
