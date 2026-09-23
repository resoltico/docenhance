#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Run one bounded harness and retain corpus provenance, statistics, logs and findings.

Every invocation creates a new owned directory. No old evidence is removed and no inputs are
silently promoted into the repository. Review and deliberately copy minimized regressions instead.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
from pathlib import Path
from typing import TYPE_CHECKING

from fuzz_execution import Run, execute
from fuzz_manifest import DEFAULT_SECONDS, FuzzError, duration, targets

if TYPE_CHECKING:
    from types import FrameType


def interrupted(_signum: int, _frame: FrameType | None) -> None:
    """Unwind on CTest cancellation so the engine group is killed and evidence is retained."""
    msg = "Fuzz execution interrupted"
    raise InterruptedError(msg)


def main() -> int:
    """Run one declared target; zero engine work or incomplete evidence is failure."""
    declared = {target.name: target for target in targets()}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=sorted(declared))
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument(
        "--work", type=Path, required=True, help="Parent of new evidence directories"
    )
    parser.add_argument("--seconds", type=duration, default=DEFAULT_SECONDS)
    parser.add_argument("--engine", choices=["libfuzzer", "afl"], default="libfuzzer")
    args = parser.parse_args()
    if os.name == "posix":
        signal.signal(signal.SIGTERM, interrupted)
    # The campaign owns a fresh parent; independently invoked targets use their explicit --work.
    work = Path(os.environ.get("DE_FUZZ_RUN_ROOT", str(args.work))).resolve()
    try:
        run = Run(declared[args.target], args.binary.resolve(), args.engine, args.seconds)
        directory = execute(run, work)
        report = json.loads((directory / "result.json").read_text(encoding="utf-8"))
    except (OSError, ValueError, FuzzError) as exc:
        print(f"Fuzzing {args.target} failed: {exc}", file=sys.stderr)
        return 1
    passed = report["passed"] is True
    print(f"{'PASS' if passed else 'FAIL'}: {args.target}; evidence: {directory}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
