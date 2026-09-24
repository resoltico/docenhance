#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Deliver real process interrupts after a readiness handshake; validate the normal response."""

from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[2]
CANCELLED = 130


def interrupt(driver: Path, child: subprocess.Popen[str], event: int) -> None:
    """Deliver a supported native event; the child acknowledges its latch before dispatch."""
    if os.name == "nt":
        subprocess.run([str(driver), "--send", str(child.pid)], check=True, timeout=10)
    else:
        os.kill(child.pid, event)


def exercise(driver: Path, root: Path, event: int) -> None:
    """No input or output may be opened when the admitted invocation is already cancelled."""
    flags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) if os.name == "nt" else 0
    with subprocess.Popen(
        [str(driver), str(root / "absent.png"), str(root / "uncreated")],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        creationflags=flags,
    ) as child:
        try:
            if child.stdout is None or child.stdout.readline() != "READY\n":
                msg = "Interrupt driver did not establish readiness"
                raise AssertionError(msg)
            interrupt(driver, child, event)
            stdout, stderr = child.communicate("\n", timeout=15)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=5)
    if child.returncode != CANCELLED or stderr or any(root.iterdir()):
        msg = f"Interrupt outcome or effect mismatch: {child.returncode}, {stderr!r}"
        raise AssertionError(msg)
    response = json.loads(stdout)
    schema = json.loads((ROOT / "schemas/command-response.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator(schema).validate(response)
    if response["error"]["code"] != "E_CANCELLED" or response["publication"] != "not_started":
        msg = f"Invalid cancellation response: {response!r}"
        raise AssertionError(msg)


def main() -> int:
    """Exercise SIGINT/SIGTERM on POSIX and a targeted real CTRL_BREAK on Windows."""
    driver = Path(sys.argv[1]).resolve()
    if os.name != "nt":
        subprocess.run([str(driver), "--dispositions", "unused"], check=True, timeout=10)
    events = (0,) if os.name == "nt" else (signal.SIGINT, signal.SIGTERM)
    with tempfile.TemporaryDirectory() as directory:
        for event in events:
            exercise(driver, Path(directory), event)
    print(f"PASS: {len(events)} native interrupt cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
