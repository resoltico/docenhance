# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reconcile only the uniquely labelled container created by one Linux gate."""

from __future__ import annotations

import json
import os
import subprocess
from typing import TYPE_CHECKING

from cache_lock import WriterStateUnknownError
from process_execution import owned_process, termination_unwind

if TYPE_CHECKING:
    from pathlib import Path

LABEL = "org.docenhance.gate"


def query(docker: str, name: str) -> list[str]:
    """A successful empty query establishes absence; daemon errors do not."""
    result = subprocess.run(
        [docker, "ps", "--all", "--no-trunc", "--filter", f"name=^/{name}$", "--format", "{{.ID}}"],
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
    )
    return result.stdout.splitlines()


def remove_owned(docker: str, name: str, token: str) -> bool:
    """Never remove a name collision or rely on CLI death to stop daemon-owned work."""
    identities = query(docker, name)
    if not identities:
        return False
    if len(identities) != 1:
        msg = "Linux gate container identity is ambiguous"
        raise RuntimeError(msg)
    result = subprocess.run(
        [docker, "inspect", identities[0]], capture_output=True, text=True, check=True, timeout=30
    )
    records = json.loads(result.stdout)
    if (
        len(records) != 1
        or records[0]["Id"] != identities[0]
        or records[0]["Config"].get("Labels", {}).get(LABEL) != token
    ):
        msg = "Linux gate container ownership differs; preserved for inspection"
        raise RuntimeError(msg)
    subprocess.run(
        [docker, "rm", "--force", identities[0]], check=True, timeout=30, stdout=subprocess.DEVNULL
    )
    if query(docker, name):
        msg = "Linux gate container removal is unconfirmed"
        raise RuntimeError(msg)
    return True


def run_container(arguments: list[str], log: Path, root: Path, name: str, token: str) -> bool:
    """Join the CLI group and reconcile its labelled daemon container on every exit."""
    with termination_unwind():
        cli_completed = False
        try:
            with log.open("a", encoding="utf-8") as stream:
                stream.write("\n" + " ".join(arguments) + "\n")
                stream.flush()
                with owned_process(
                    arguments, dict(os.environ), stream, cwd=root, graceful=True
                ) as process:
                    status = process.wait()
                    cli_completed = status == 0
        finally:
            removed = False
            try:
                removed = remove_owned(arguments[0], name, token)
            except BaseException as error:
                # Any interrupted or malformed cleanup leaves daemon ownership unconfirmed.
                msg = (
                    f"Container cleanup unconfirmed: {name}, {LABEL}={token}; inspect writer claim"
                )
                raise WriterStateUnknownError(msg) from error
            if not cli_completed and not removed:
                msg = (
                    f"Container creation remains unconfirmed: {name}, {LABEL}={token}; "
                    "empty query establishes absence only at that observation; inspect writer claim"
                )
                raise WriterStateUnknownError(msg)
    return status == 0
