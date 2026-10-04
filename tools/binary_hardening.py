# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
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
        env={**os.environ, "LC_ALL": "C"},
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
        requirements = {
            "ASLR": "Dynamic base" in headers,
            "high entropy ASLR": "High Entropy Virtual Addresses" in headers,
            "nonexecuting data": "NX compatible" in headers,
            "control-flow guard": "Guard" in headers
            and "CF Instrumented" in headers
            and "FID table present" in headers,
            "stack cookie": re.search(
                r"\b[0-9A-Fa-f]*[1-9A-Fa-f][0-9A-Fa-f]* Security Cookie\b", headers
            )
            is not None,
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
