# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Verify sanitizer and coverage instrumentation in the actual imported PNG/zlib/LCMS archives."""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from fuzz_manifest import FuzzError


def instrumented_symbols(text: str) -> bool:
    """Both memory/UB diagnostics and a coverage engine must reach the upstream C code."""
    coverage = "__sanitizer_cov_" in text or "__afl_area_ptr" in text
    return "__asan_" in text and "__ubsan_handle_" in text and coverage


def inspect_archives(build: Path) -> dict[str, str]:
    """Check resolved imported-target files, retaining their exact SHA-256 identities."""
    archives = json.loads((build / "fuzz-codecs.json").read_text(encoding="utf-8"))
    if not isinstance(archives, dict) or set(archives) != {"png", "zlib", "lcms"}:
        msg = "The fuzz build must identify exactly its PNG, zlib and Little CMS archives"
        raise FuzzError(msg)
    nm = shutil.which("nm")
    if nm is None:
        msg = "nm is required to verify codec instrumentation"
        raise FuzzError(msg)
    identities = {}
    for name, filename in archives.items():
        path = Path(filename)
        result = subprocess.run(
            [nm, "-u", str(path)], capture_output=True, text=True, check=True, timeout=30
        )
        if not instrumented_symbols(result.stdout):
            msg = f"Codec archive lacks ASan, UBSan or coverage instrumentation: {path}"
            raise FuzzError(msg)
        identities[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return identities
