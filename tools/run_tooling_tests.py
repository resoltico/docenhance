#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Run all tooling modules and refuse empty discovery, skipped work and unexpected success."""

from __future__ import annotations

import argparse
import unittest
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Iterator

ROOT = Path(__file__).resolve().parents[1]


def cases(suite: unittest.TestSuite) -> Iterator[unittest.TestCase]:
    """Flatten discovered suites without performing or discarding their work."""
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from cases(test)
        elif test is not None:
            yield test


def run(directory: Path) -> bool:
    """Require every test module to contribute cases and every case to execute successfully."""
    loader = unittest.TestLoader()
    suite = loader.discover(str(directory))
    if loader.errors:
        print("\n".join(str(error) for error in loader.errors))
        return False
    discovered = list(cases(suite))
    expected = {path.stem for path in directory.glob("test*.py")}
    actual = {test.__class__.__module__ for test in discovered}
    if not expected or not discovered or actual != expected:
        print(f"Tooling discovery differs: expected {sorted(expected)}, got {sorted(actual)}")
        return False
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return result.wasSuccessful() and result.testsRun == len(discovered) and not result.skipped


def main() -> int:
    """Run the repository suite or an explicitly supplied isolated negative control."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "tests/tooling")
    return 0 if run(parser.parse_args().directory.resolve()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
