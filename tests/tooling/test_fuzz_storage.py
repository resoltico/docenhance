# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Owned disk scans tolerate only the disappearance of concurrently cleaned scratch."""

from __future__ import annotations

import os
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import patch

if TYPE_CHECKING:
    from collections.abc import Iterator

from tools_path import TOOLS

import fuzz_storage


class FuzzStorageTests(unittest.TestCase):
    """A peer's successful cleanup must not abort another running target."""

    def test_empty_files_still_consume_the_entry_budget(self) -> None:
        """An engine cannot evade disk supervision with zero allocated payload blocks."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index in range(3):
                (root / str(index)).touch()
            with (
                patch.object(fuzz_storage, "MAX_WORK_ENTRIES", 3),
                self.assertRaisesRegex(ValueError, "entry disk stop threshold"),
            ):
                fuzz_storage.occupied_bytes(root)
            self.assertEqual(len(list(root.iterdir())), 3)

    def test_invalid_reservation_cannot_bypass_accumulation_guard(self) -> None:
        """Negative reservations never manufacture free capacity."""
        with tempfile.TemporaryDirectory() as temporary:
            for reserve in (0, -1, True, fuzz_storage.MAX_WORK_PARENT_BYTES + 1):
                with self.subTest(reserve=reserve), self.assertRaises(ValueError):
                    fuzz_storage.admit_work_parent(Path(temporary), reserve)

    def test_entry_budget_stops_enumeration_before_collecting_directory(self) -> None:
        """An enormous engine directory need not be materialized before supervision refuses it."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index in range(3):
                (root / str(index)).touch()
            original_scan = os.scandir
            observed = []
            maximum_observed = 2

            @contextmanager
            def bounded_scan(path: Path) -> Iterator[Iterator[os.DirEntry[str]]]:
                with original_scan(path) as children:

                    def bounded_children() -> Iterator[os.DirEntry[str]]:
                        for child in children:
                            observed.append(child.name)
                            if len(observed) > maximum_observed:
                                self.fail("Scanner collected entries beyond the stop threshold")
                            yield child

                    yield bounded_children()

            with (
                patch.object(os, "scandir", bounded_scan),
                patch.object(fuzz_storage, "MAX_WORK_ENTRIES", 2),
                self.assertRaisesRegex(ValueError, "entry disk stop threshold"),
            ):
                fuzz_storage.occupied_bytes(root)
            self.assertEqual(len(observed), 2)

    def test_sibling_directory_removed_between_listing_and_descent(self) -> None:
        """A disappearing AFL queue is skipped without losing the parent scan."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sibling = root / "finished-queue"
            sibling.mkdir()
            original_scan = os.scandir
            errors: list[OSError] = []

            @contextmanager
            def racing_scan(path: Path) -> Iterator[Iterator[os.DirEntry[str]]]:
                try:
                    with original_scan(path) as children:
                        yield children
                    if path == root:
                        sibling.rmdir()
                except FileNotFoundError as error:
                    errors.append(error)
                    raise

            with patch.object(os, "scandir", racing_scan):
                self.assertEqual(Path(fuzz_storage.__file__).resolve(), TOOLS / "fuzz_storage.py")
                self.assertGreaterEqual(fuzz_storage.occupied_bytes(root), 0)
            self.assertTrue(any(isinstance(error, FileNotFoundError) for error in errors))

    def test_unrelated_walk_error_still_fails(self) -> None:
        """Permission failures are never mistaken for successful scratch cleanup."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)

            with (
                patch.object(os, "scandir", side_effect=PermissionError("disk scan denied")),
                self.assertRaisesRegex(PermissionError, "disk scan denied"),
            ):
                fuzz_storage.occupied_bytes(root)
