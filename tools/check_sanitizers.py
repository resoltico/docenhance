#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Require actual benign/error detection from the configured native sanitizer runtime."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from audit_build import read_cache
from fuzz_manifest import settings
from sanitizer_evidence import required


def check(binary: Path, modes: set[str], directory: Path) -> list[str]:
    """Exit zero and arbitrary crashes are insufficient; require the intended diagnostic."""
    env = {**os.environ, **settings()["sanitizer_options"]}
    failures = []
    controls = {
        "address": ("memory", "8", "AddressSanitizer: heap-buffer-overflow"),
        "undefined": ("arithmetic", "2147483647", "runtime error: signed integer overflow"),
        "thread": ("race", "0", "ThreadSanitizer: data race"),
    }
    for mode in sorted(modes):
        action, value, diagnostic = controls[mode]
        benign = subprocess.run(
            [str(binary), "memory", "0"], capture_output=True, env=env, check=False, timeout=30
        )
        (directory / f"{mode}-benign.log").write_bytes(benign.stdout + benign.stderr)
        bad = subprocess.run(
            [str(binary), action, value], capture_output=True, env=env, check=False, timeout=30
        )
        (directory / f"{mode}-fault.log").write_bytes(bad.stdout + bad.stderr)
        if benign.returncode != 0 or bad.returncode == 0 or diagnostic.encode() not in bad.stderr:
            failures.append(
                f"{mode} did not establish benign execution and intended fault detection"
            )
    return failures


def main() -> int:
    """Retain fresh process diagnostics for every requested instrumentation mode."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    modes = required(read_cache(args.build / "CMakeCache.txt"))
    directory = Path(tempfile.mkdtemp(prefix="sanitizer-evidence-", dir=args.build))
    failures = (
        check(args.binary.resolve(), modes, directory) if modes else ["No sanitizer requested"]
    )
    print(
        "\n".join(failures)
        if failures
        else f"PASS: actual sanitizer detection; evidence: {directory}"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
