# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Inspect native executable protection markers independently of build configuration."""

from __future__ import annotations

import os
import platform
import re
import shutil
import subprocess
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

DLL_HIGH_ENTROPY = 0x0020
DLL_DYNAMIC_BASE = 0x0040
DLL_NX_COMPAT = 0x0100
DLL_GUARD_CF = 0x4000
GUARD_CF_INSTRUMENTED = 0x0100
GUARD_CF_TABLE_PRESENT = 0x0400


def hex_field(headers: str, label: str) -> int:
    """Read actual PE field values; repeated identical tool sections are harmless."""
    values = {
        int(value, 16)
        for value in re.findall(
            rf"^\s*([0-9a-f]+)\s+{re.escape(label)}\s*$",
            headers,
            flags=re.IGNORECASE | re.MULTILINE,
        )
    }
    return values.pop() if len(values) == 1 else 0


def inspect(tool: str, arguments: list[str], executable: Path) -> str:
    """Missing inspection tools or failed inspection cannot establish protection."""
    resolved = shutil.which(tool)
    if resolved is None:
        message = f"{tool} is required to inspect executable hardening"
        raise ValueError(message)
    result = subprocess.run(
        [resolved, *arguments, str(executable)],
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
        env={**os.environ, "LC_ALL": "C", "VSLANG": "1033"},
    )
    return result.stdout


def markers(system: str, headers: str, symbols: str) -> list[str]:
    """Require observable protection, without treating one symbol as per-function coverage."""
    if system == "Linux":
        requirements = {
            "PIE": re.search(r"Type:\s+DYN\b", headers) is not None
            and re.search(r"FLAGS_1[^\n]*\bPIE\b", headers) is not None,
            "nonexecuting stack": re.search(r"GNU_STACK[^\n]*\sRW\s", headers) is not None,
            "RELRO": "GNU_RELRO" in headers,
            "immediate binding": "BIND_NOW" in headers or re.search(r"FLAGS[^\n]*\bNOW\b", headers),
            "stack canary": "__stack_chk_fail" in symbols,
        }
    elif system == "Darwin":
        requirements = {
            "PIE": re.search(r"\bEXECUTE\b[^\n]*\bPIE\b", headers) is not None,
            "nonexecuting stack": "ALLOW_STACK_EXECUTION" not in headers,
            "stack canary": "___stack_chk_fail" in symbols,
        }
    elif system == "Windows":
        characteristics = hex_field(headers, "DLL characteristics")
        guard = hex_field(headers, "Guard Flags")
        guard_required = GUARD_CF_INSTRUMENTED | GUARD_CF_TABLE_PRESENT
        requirements = {
            "ASLR": bool(characteristics & DLL_DYNAMIC_BASE),
            "high entropy ASLR": bool(characteristics & DLL_HIGH_ENTROPY),
            "nonexecuting data": bool(characteristics & DLL_NX_COMPAT),
            "control-flow guard": bool(characteristics & DLL_GUARD_CF)
            and guard & guard_required == guard_required
            and hex_field(headers, "Guard CF function table") != 0
            and hex_field(headers, "Guard CF function count") != 0,
            "stack cookie": hex_field(headers, "Security Cookie") != 0,
        }
    else:
        message = f"Executable hardening inspection is undefined for {system}"
        raise ValueError(message)
    return [
        f"Executable lacks required {name}" for name, present in requirements.items() if not present
    ]


def errors(executable: Path) -> list[str]:
    """Inspect the delivered executable on its native host before running it."""
    system = platform.system()
    if system == "Linux":
        headers = inspect("readelf", ["-h", "-lW", "-dW"], executable)
        symbols = inspect("readelf", ["-sW"], executable)
    elif system == "Darwin":
        headers = inspect("otool", ["-hv"], executable)
        symbols = inspect("nm", ["-u"], executable)
    elif system == "Windows":
        headers = inspect("dumpbin", ["/HEADERS", "/LOADCONFIG"], executable)
        symbols = ""
    else:
        message = f"Executable hardening inspection is undefined for {system}"
        raise ValueError(message)
    return markers(system, headers, symbols)
