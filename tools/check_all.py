#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Run every local check: the authoritative list used by the Git hooks.

This is the one place the local check set is defined. `.pre-commit-config.yaml` invokes this
script. The GitHub source-archive workflow deliberately runs only the checks that do not need a
native dependency build; the full local gate remains the contributor standard. The native build,
its clang-tidy pass and fuzzing are separate, because they need a configured build tree:
`cmake --workflow --preset dev` and `--preset fuzz`.

Every check runs even when an earlier one fails, so one command reports every problem.
"""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# (name, argv after the interpreter).
CHECKS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("structure and contracts", ("tools/check_project.py",)),
    ("quality gates", ("tools/check_gates.py",)),
    ("architecture", ("tools/check_architecture.py",)),
    ("clang-format", ("tools/check_format.py",)),
    ("ruff format", ("-m", "ruff", "format", "--check")),
    ("ruff lint", ("-m", "ruff", "check")),
    ("mypy", ("-m", "mypy")),
    ("tooling tests", ("-m", "unittest", "discover", "-s", "tests/tooling")),
)


def missing_tool(arguments: tuple[str, ...]) -> str | None:
    """The importable name of a linter this interpreter lacks, if any."""
    if arguments[0] != "-m":
        return None
    module = arguments[1]
    return None if importlib.util.find_spec(module) is not None else module


def run(name: str, arguments: tuple[str, ...], *, quiet: bool) -> bool:
    """Run one check and report whether it passed."""
    if (module := missing_tool(arguments)) is not None:
        print(
            f"SKIP {name}: {sys.executable} has no {module}. "
            "Activate the project environment and run: python tools/install_build_tools.py --lint",
            file=sys.stderr,
        )
        return False
    started = time.monotonic()
    output = subprocess.DEVNULL if quiet else None
    result = subprocess.run(
        [sys.executable, *arguments], cwd=ROOT, stdout=output, stderr=output, check=False
    )
    elapsed = time.monotonic() - started
    status = "ok  " if result.returncode == 0 else "FAIL"
    print(f"{status} {name} ({elapsed:.1f}s)", file=sys.stderr)
    return result.returncode == 0


def main() -> int:
    """Run every check and return non-zero if any of them failed."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="Show only the pass/fail summary")
    args = parser.parse_args()
    failed = [name for name, arguments in CHECKS if not run(name, arguments, quiet=args.quiet)]
    if failed:
        print(f"FAILED: {', '.join(failed)}", file=sys.stderr)
        return 1
    print(f"PASS: {len(CHECKS)} local checks", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
