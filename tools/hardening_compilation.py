# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Check actual C/C++ compiler commands against the owning reviewed hardening policy."""

from __future__ import annotations

import json
import re
import shlex
from pathlib import Path


def command_errors(command: str, required: list[str]) -> list[str]:
    """A flag appearing only as part of another argument does not establish propagation."""
    arguments = shlex.split(command)
    errors = [f"Missing compiler protection {flag}" for flag in required if flag not in arguments]
    weakening = re.compile(
        r"^(?:-fno-stack-protector|-fstack-protector(?:-explicit)?|/GS-|/guard:cf-|"
        r"-U_FORTIFY_SOURCE)$",
        re.IGNORECASE,
    )
    errors.extend(
        f"Compiler protection disabled: {flag}" for flag in arguments if weakening.fullmatch(flag)
    )
    if "-D_FORTIFY_SOURCE=2" in required:
        errors.extend(
            f"Compiler fortification differs from policy: {flag}"
            for flag in arguments
            if flag.startswith("-D_FORTIFY_SOURCE=") and flag != "-D_FORTIFY_SOURCE=2"
        )
    return errors


def errors(build: Path, policy: Path) -> list[str]:
    """Header-only packages have no database; every selected compiled package must have one."""
    expected = json.loads(policy.read_text(encoding="utf-8"))["compile"]
    if (
        not isinstance(expected, list)
        or not expected
        or not all(isinstance(f, str) for f in expected)
    ):
        message = "Invalid or empty native compiler protection policy"
        raise ValueError(message)
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    compiled = [
        entry
        for entry in entries
        if Path(entry["file"]).suffix.lower() in {".c", ".cc", ".cpp", ".cxx"}
    ]
    if not compiled:
        return [f"No C/C++ compiler work discovered: {build}"]
    return [
        f"{entry['file']}: {error}"
        for entry in compiled
        for error in command_errors(entry["command"], expected)
    ]
