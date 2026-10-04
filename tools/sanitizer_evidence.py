# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Check effective compiler instrumentation against the requested sanitizer modes."""

from __future__ import annotations

import shlex
from typing import Any

from test_evidence import EvidenceError

ENABLED = {"ON", "TRUE", "YES", "1"}
MODES = {"DE_ENABLE_ASAN": "address", "DE_ENABLE_UBSAN": "undefined", "DE_ENABLE_TSAN": "thread"}


def required(cache: dict[str, str]) -> set[str]:
    """Requested build modes need their actual compiler instrumentation, not only cache flags."""
    return {mode for option, mode in MODES.items() if cache.get(option, "").upper() in ENABLED}


def check_compilation(cache: dict[str, str], entries: list[dict[str, Any]]) -> None:
    """Every compiled application TU must retain required instrumentation and fatal diagnostics."""
    wanted = required(cache)
    if not wanted:
        return
    if not entries:
        msg = "Sanitized compilation evidence is empty"
        raise EvidenceError(msg)
    for entry in entries:
        args = entry.get("arguments") or shlex.split(entry["command"])
        if not fatal_instrumentation(args, wanted):
            msg = f"Required fatal sanitizer instrumentation is absent: {entry['file']}"
            raise EvidenceError(msg)


def fatal_instrumentation(args: list[str], wanted: set[str]) -> bool:
    """No source opt-out is supported; evaluate recovery controls in command order."""
    active: set[str] = set()
    recovery: set[str] = set()
    fatal = False
    for arg in args:
        if arg.startswith("-fsanitize="):
            active.update(arg.partition("=")[2].split(","))
        elif arg.startswith(
            ("-fno-sanitize=", "-fsanitize-ignorelist=", "-fsanitize-blacklist=")
        ) or arg in {"-fsanitize-ignorelist", "-fsanitize-blacklist"}:
            return False
        elif arg.startswith("-fno-sanitize-recover="):
            removed = set(arg.partition("=")[2].split(","))
            if "all" in removed:
                fatal = True
                recovery.clear()
            else:
                recovery -= removed
        elif arg.startswith("-fsanitize-recover="):
            recovery.update(arg.partition("=")[2].split(","))
    return wanted <= active and fatal and not recovery
