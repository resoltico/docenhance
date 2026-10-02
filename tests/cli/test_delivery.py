#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Challenge the real process with response pipes whose readers are already gone."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import Fixture

OUTPUT_FAILURE = 5


def closed_pipe(executable: Path, arguments: list[str], *, diagnostic: bool = False) -> None:
    """Delivery must return exit 5, even when default native signals terminate pipe writers."""
    reader, writer = os.pipe()
    os.close(reader)
    try:
        result = subprocess.run(
            [str(executable), *arguments],
            stdout=subprocess.PIPE if diagnostic else writer,
            stderr=writer if diagnostic else subprocess.PIPE,
            check=False,
            timeout=30,
            restore_signals=True,
        )
    finally:
        os.close(writer)
    if result.returncode != OUTPUT_FAILURE or (result.stdout if diagnostic else result.stderr):
        msg = f"Broken pipe did not remain one-shot delivery failure: {result!r}"
        raise AssertionError(msg)


def main() -> int:
    """Check both selected streams, unselected streams and publication before delivery fails."""
    executable = Path(sys.argv[1]).resolve()
    closed_pipe(executable, ["version", "--json"])
    closed_pipe(executable, ["unknown-command"], diagnostic=True)
    reader, writer = os.pipe()
    os.close(reader)
    try:
        subprocess.run(
            [str(executable), "version", "--json"],
            stdout=subprocess.PIPE,
            stderr=writer,
            check=True,
            timeout=10,
        )
    finally:
        os.close(writer)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "input.png"
        source.write_bytes(Fixture(2, 1, ((0,), (255,))).encoded())
        output = root / "result"
        closed_pipe(
            executable,
            ["process", str(source), "--out-dir", str(output), "--output-mode", "bw", "--json"],
        )
        subprocess.run(
            [str(executable), "verify", str(output), "--json"],
            capture_output=True,
            check=True,
            timeout=10,
        )
        if any(".staging-" in path.name for path in root.iterdir()):
            msg = "Completed publication left staging behind"
            raise AssertionError(msg)
    print("PASS: real closed-pipe delivery and retained verified publication")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
