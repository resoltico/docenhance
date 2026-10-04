# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Parallel checks report what serial ones would, in the same order, and stay within their bound."""

from __future__ import annotations

import threading
import unittest
from concurrent.futures import ThreadPoolExecutor
from unittest import mock

from tools_path import TOOLS

import parallel

# A wait that only expires when a test is wrong: it fails a broken run instead of hanging it.
DEADLINE_SECONDS = 30


LATE_FAILURE = 3
EARLY_FAILURE = 1


class OrderedMapTests(unittest.TestCase):
    """The result depends on the items, never on how the work was scheduled."""

    def test_the_module_under_test_is_the_tool(self) -> None:
        """The checks import this module by name, so this is the file they run."""
        self.assertEqual(parallel.__file__, str(TOOLS / "parallel.py"))

    def test_results_follow_item_order_whatever_finishes_first(self) -> None:
        """The first item is held until every other has finished, so it completes last."""
        items = list(range(6))
        others_done = threading.Semaphore(0)

        def work(item: int) -> int:
            if item == 0:
                for _ in range(len(items) - 1):
                    self.assertTrue(others_done.acquire(timeout=DEADLINE_SECONDS))
            else:
                others_done.release()
            return item * item

        self.assertEqual(parallel.ordered_map(work, items, 6), [item * item for item in items])

    def test_parallel_and_serial_agree(self) -> None:
        """Any worker count returns the serial answer."""
        items = list(range(40))
        serial = parallel.ordered_map(lambda item: (item, item % 7), items, 1)
        for jobs in (2, 3, 8, parallel.MAX_JOBS):
            self.assertEqual(
                parallel.ordered_map(lambda item: (item, item % 7), items, jobs), serial
            )

    def test_work_really_overlaps(self) -> None:
        """Every worker must be inside the function at once, or the barrier never releases."""
        jobs = 4
        meeting = threading.Barrier(jobs)

        def work(item: int) -> int:
            meeting.wait(timeout=DEADLINE_SECONDS)
            return item

        self.assertEqual(parallel.ordered_map(work, range(jobs), jobs), list(range(jobs)))

    def test_the_pool_is_never_larger_than_the_bound_or_the_work(self) -> None:
        """Work that finishes instantly cannot show a peak, so the pool's own size is checked."""
        sizes: list[int] = []
        real = ThreadPoolExecutor

        def recording(max_workers: int) -> ThreadPoolExecutor:
            sizes.append(max_workers)
            return real(max_workers=max_workers)

        with mock.patch.object(parallel, "ThreadPoolExecutor", recording):
            parallel.ordered_map(str, range(50), 3)
            parallel.ordered_map(str, range(2), 8)
        self.assertEqual(sizes, [3, 2])

    def test_the_earliest_failing_item_is_the_one_reported(self) -> None:
        """Item 1 fails after item 3 has already failed; item 1's error is the one raised."""
        third_failed = threading.Event()

        def work(item: int) -> int:
            if item == LATE_FAILURE:
                third_failed.set()
                message = "third"
                raise RuntimeError(message)
            if item == EARLY_FAILURE:
                self.assertTrue(third_failed.wait(timeout=DEADLINE_SECONDS))
                message = "first"
                raise RuntimeError(message)
            return item

        with self.assertRaisesRegex(RuntimeError, "first"):
            parallel.ordered_map(work, range(4), 4)

    def test_work_that_has_not_started_is_cancelled_after_a_failure(self) -> None:
        """A failure stops the work that has not started, so fewer items run than were given."""
        ran: list[int] = []

        def work(item: int) -> int:
            ran.append(item)
            message = "stop"
            raise RuntimeError(message)

        with self.assertRaises(RuntimeError):
            parallel.ordered_map(work, range(20), 2)
        self.assertLess(len(ran), 20)

    def test_empty_and_single_inputs(self) -> None:
        """Nothing to overlap is still answered, without a pool."""
        self.assertEqual(parallel.ordered_map(str, [], 8), [])
        self.assertEqual(parallel.ordered_map(str, [5], 8), ["5"])

    def test_bounds_on_the_worker_count(self) -> None:
        """Zero, negative and absurd counts are refused rather than clamped."""
        for jobs in (0, -1, parallel.MAX_JOBS + 1):
            with self.assertRaises(ValueError):
                parallel.ordered_map(str, [1, 2], jobs)


if __name__ == "__main__":
    unittest.main()
