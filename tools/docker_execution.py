# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bound one Docker CLI and reconcile only its uniquely labelled daemon container."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from typing import TYPE_CHECKING

from cache_lock import WriterStateUnknownError
from process_execution import CapturePolicy, bounded_capture, termination_unwind

if TYPE_CHECKING:
    from pathlib import Path

LABEL = "org.docenhance.gate"
RPC_SECONDS = 30
# Four daemon operations plus a slot for CLI-group and capture-reader cleanup.
CLEANUP_SECONDS = 5 * RPC_SECONDS
LOG_LIMIT_BYTES = 2 * 1024 * 1024


class ContainerStateUnknownError(WriterStateUnknownError):
    """Retain both failures while keeping the shared writer claim inspection-required."""

    def __init__(
        self,
        identity: str,
        primary: BaseException | None,
        cleanup: BaseException | None,
        status: int | None,
    ) -> None:
        """Keep the actual primary and cleanup objects, plus a nonzero CLI status if available."""
        super().__init__(
            f"Container cleanup or creation remains unconfirmed: {identity}; "
            f"primary={primary!r}, exit={status}; cleanup={cleanup!r}; inspect writer claim"
        )
        self.primary = primary
        self.cleanup = cleanup
        self.status = status


def remaining(deadline: float, reserve: float = 0) -> float:
    """Consume one original deadline, reserving daemon reconciliation before CLI launch."""
    result = deadline - time.monotonic() - reserve
    if result <= 0:
        msg = "Original Linux gate deadline exhausted"
        raise TimeoutError(msg)
    return result


def rpc_timeout(deadline: float) -> float:
    """A control query gets neither more than thirty seconds nor a reset deadline."""
    return min(RPC_SECONDS, remaining(deadline))


def query(docker: str, name: str, deadline: float) -> list[str]:
    """A successful empty query establishes absence; daemon errors do not."""
    result = subprocess.run(
        [docker, "ps", "--all", "--no-trunc", "--filter", f"name=^/{name}$", "--format", "{{.ID}}"],
        capture_output=True,
        text=True,
        check=True,
        timeout=rpc_timeout(deadline),
    )
    return result.stdout.splitlines()


def remove_owned(docker: str, name: str, token: str, deadline: float) -> bool:
    """Never remove a name collision or mistake CLI death for daemon cleanup."""
    identities = query(docker, name, deadline)
    if not identities:
        return False
    if len(identities) != 1:
        msg = "Linux gate container identity is ambiguous"
        raise RuntimeError(msg)
    result = subprocess.run(
        [docker, "inspect", identities[0]],
        capture_output=True,
        text=True,
        check=True,
        timeout=rpc_timeout(deadline),
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
        [docker, "rm", "--force", identities[0]],
        check=True,
        timeout=rpc_timeout(deadline),
        stdout=subprocess.DEVNULL,
    )
    if query(docker, name, deadline):
        msg = "Linux gate container removal is unconfirmed"
        raise RuntimeError(msg)
    return True


def run_container(
    arguments: list[str],
    log: Path,
    name: str,
    token: str,
    deadline: float,
) -> bool:
    """Bound CLI output/work, then reconcile daemon ownership even after interruption."""
    identity = f"{name}, {LABEL}={token}"
    status: int | None = None
    finished = False

    def monitor() -> None:
        remaining(deadline, CLEANUP_SECONDS)

    with termination_unwind():
        try:
            status = bounded_capture(
                arguments,
                dict(os.environ),
                log,
                remaining(deadline, CLEANUP_SECONDS),
                CapturePolicy(LOG_LIMIT_BYTES - 1024, monitor, interval=1),
            )
            if b"[engine output omitted:" in log.read_bytes():
                msg = "Linux gate diagnostics exceeded their bounded capture; no passing result"
                raise RuntimeError(msg)
            finished = True
        finally:
            primary = None if finished else sys.exception()
            reconciled = False
            try:
                removed = remove_owned(arguments[0], name, token, deadline)
                reconciled = True
            finally:
                if not reconciled:
                    cleanup = sys.exception()
                    raise ContainerStateUnknownError(
                        identity, primary, cleanup, status
                    ) from cleanup
            if status != 0 and not removed:
                raise ContainerStateUnknownError(identity, primary, None, status) from primary
    remaining(deadline)
    return status == 0
