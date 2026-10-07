#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Run all tooling modules in bounded isolated workers and reconcile exact execution."""

from __future__ import annotations

import argparse
import io
import multiprocessing
import sys
import time
import unittest
from concurrent.futures import ProcessPoolExecutor
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Iterator

ROOT = Path(__file__).resolve().parents[1]
MAX_JOBS = 2


def cases(suite: unittest.TestSuite) -> Iterator[unittest.TestCase]:
    """Flatten discovered suites without performing or discarding their work."""
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from cases(test)
        elif test is not None:
            yield test


def module_run(item: tuple[Path, str]) -> tuple[list[str], bool, str, float]:
    """A spawned worker discovers and runs one module with independent interpreter state."""
    directory, name = item
    started = time.monotonic()
    output = io.StringIO()
    with redirect_stdout(output), redirect_stderr(output):
        loader = unittest.TestLoader()
        suite = loader.discover(str(directory), pattern=f"{name}.py")
        discovered = list(cases(suite))
        identifiers = [test.id() for test in discovered]
        actual = {test.__class__.__module__ for test in discovered}
        if loader.errors or not identifiers or actual != {name}:
            print(f"Module discovery differs for {name}: {loader.errors or sorted(actual)}")
            passed = False
        else:
            result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
            passed = (
                result.wasSuccessful()
                and result.testsRun == len(identifiers)
                and not result.skipped
                and not result.expectedFailures
            )
    return identifiers, bool(passed), output.getvalue(), time.monotonic() - started


def run(directory: Path, jobs: int = MAX_JOBS) -> bool:
    """Reconcile full discovery with isolated module results; every module still executes."""
    if not 1 <= jobs <= MAX_JOBS:
        message = f"tooling jobs must be in [1,{MAX_JOBS}]"
        raise ValueError(message)
    loader = unittest.TestLoader()
    suite = loader.discover(str(directory))
    if loader.errors:
        print("\n".join(str(error) for error in loader.errors))
        return False
    discovered = list(cases(suite))
    expected = {path.stem for path in directory.glob("test*.py")}
    actual = {test.__class__.__module__ for test in discovered}
    identifiers = [test.id() for test in discovered]
    if (
        not expected
        or not identifiers
        or actual != expected
        or len(set(identifiers)) != len(identifiers)
    ):
        print(f"Tooling discovery differs: expected {sorted(expected)}, got {sorted(actual)}")
        return False
    items = [(directory, name) for name in sorted(expected)]
    # Spawn on every platform: test patches, imports and globals must not cross worker boundaries.
    with ProcessPoolExecutor(
        max_workers=min(jobs, len(items)),
        mp_context=multiprocessing.get_context("spawn"),
        max_tasks_per_child=1,
    ) as pool:
        results = list(pool.map(module_run, items))
    executed = []
    passed = True
    for (_, name), (found, successful, output, elapsed) in zip(items, results, strict=True):
        print(output, end="")
        print(f"{'ok' if successful else 'FAIL'} tooling module {name} ({elapsed:.2f}s)")
        executed.extend(found)
        passed &= successful
    complete = sorted(executed) == sorted(identifiers)
    print(f"Tooling execution: {len(executed)} cases in {len(items)} modules; workers={jobs}")
    if not complete:
        print("Tooling execution identities differ from full discovery")
    return passed and complete


def main() -> int:
    """Run the repository suite or an explicitly supplied isolated negative control."""
    sys.path[:] = [entry for entry in sys.path if Path(entry).resolve() != ROOT / "tools"]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "tests/tooling")
    parser.add_argument(
        "--jobs", type=int, default=MAX_JOBS, help="isolated module workers (default: 2)"
    )
    args = parser.parse_args()
    if not 1 <= args.jobs <= MAX_JOBS:
        parser.error(f"tooling jobs must be in [1,{MAX_JOBS}]")
    return 0 if run(args.directory.resolve(), args.jobs) else 1


if __name__ == "__main__":
    raise SystemExit(main())
