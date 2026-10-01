# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Bounded, order-preserving parallel work for the checks that drive one subprocess per item."""

from __future__ import annotations

import json
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable, Iterable

MAX_JOBS: int = json.loads((Path(__file__).resolve().parents[1] / "deps/tools.json").read_text())[
    "build"
]["max_jobs"]


def available_jobs() -> int:
    """Bound an observed CPU count by the project execution policy."""
    return min(MAX_JOBS, max(1, os.cpu_count() or 1))


# One item is nothing to overlap, so it never pays for a pool.
MINIMUM_TO_OVERLAP = 2


def ordered_map[T, R](function: Callable[[T], R], items: Iterable[T], jobs: int) -> list[R]:
    """Apply function to every item on at most `jobs` threads and return results in item order.

    The order is the order of `items`, whatever order the work finishes in, so a report built from
    the results does not depend on scheduling. When an item raises, the error of the earliest
    failing item is the one that propagates, and work that has not started is cancelled. The work
    is subprocess-bound, so threads run it concurrently.
    """
    if not 1 <= jobs <= MAX_JOBS:
        msg = f"jobs must be in [1, {MAX_JOBS}], not {jobs}"
        raise ValueError(msg)
    values = list(items)
    if jobs == 1 or len(values) < MINIMUM_TO_OVERLAP:
        return [function(value) for value in values]
    with ThreadPoolExecutor(max_workers=min(jobs, len(values))) as pool:
        futures = [pool.submit(function, value) for value in values]
        try:
            return [future.result() for future in futures]
        except BaseException:
            for future in futures:
                future.cancel()
            raise
