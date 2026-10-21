# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual bounded synthetic pipe producers and capture observations for tooling controls."""

from __future__ import annotations

import contextlib
import copy
import sys
from typing import IO, TYPE_CHECKING, Any
from unittest.mock import patch

from tools_path import prepare_tool_imports

from process_execution import owned_process
from quality_attempt import CaptureAdmission, new_attempt
from quality_capture import capture_attempt
from quality_provenance import child_environment

if TYPE_CHECKING:
    import subprocess
    from collections.abc import Iterator
    from pathlib import Path

prepare_tool_imports()

ProcessOutput = IO[bytes] | IO[str] | tuple[IO[bytes] | int, IO[bytes] | int] | int | None


@contextlib.contextmanager
def pipe_producer(code: str, calls: list[list[str]]) -> Iterator[None]:
    """Select a concrete synthetic producer while retaining actual owned pipes and cleanup."""

    @contextlib.contextmanager
    def controlled(
        argv: list[str], env: dict[str, str], streams: ProcessOutput, *, cwd: Path | None = None
    ) -> Iterator[subprocess.Popen[bytes]]:
        calls.append(copy.deepcopy(argv))
        with owned_process([sys.executable, "-c", code], env, streams, cwd=cwd) as child:
            yield child

    with patch("quality_capture.owned_process", controlled):
        yield


def capture_code(
    root: Path, slot: dict[str, Any], code: str
) -> tuple[dict[str, Any], CaptureAdmission, list[list[str]]]:
    """Substitute only the producer; retain actual pipes, readers, process owner and storage."""
    calls: list[list[str]] = []

    attempt, admission = new_attempt(slot), CaptureAdmission(environment=child_environment())
    with pipe_producer(code, calls):
        capture_attempt(attempt, slot, root, admission)
    return attempt, admission, calls
