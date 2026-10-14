# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Owned disk scans tolerate only the disappearance of concurrently cleaned scratch."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import patch

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator

from tools_path import TOOLS

import fuzz_storage


class FuzzStorageTests(unittest.TestCase):
    """A peer's successful cleanup must not abort another running target."""

    def test_sibling_directory_removed_between_listing_and_descent(self) -> None:
        """A disappearing AFL queue is skipped without losing the parent scan."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sibling = root / "finished-queue"
            sibling.mkdir()
            original_walk = os.walk
            errors: list[OSError] = []

            def racing_walk(
                path: Path, *, followlinks: bool, onerror: Callable[[OSError], None]
            ) -> Iterator[tuple[str, list[str], list[str]]]:
                def observe(error: OSError) -> None:
                    errors.append(error)
                    onerror(error)

                for base, directories, files in original_walk(
                    path, followlinks=followlinks, onerror=observe
                ):
                    yield base, directories, files
                    if Path(base) == root:
                        sibling.rmdir()

            with patch.object(os, "walk", racing_walk):
                self.assertEqual(Path(fuzz_storage.__file__).resolve(), TOOLS / "fuzz_storage.py")
                self.assertGreaterEqual(fuzz_storage.occupied_bytes(root), 0)
            self.assertTrue(any(isinstance(error, FileNotFoundError) for error in errors))

    def test_unrelated_walk_error_still_fails(self) -> None:
        """Permission failures are never mistaken for successful scratch cleanup."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)

            def denied_walk(
                path: Path, *, followlinks: bool, onerror: Callable[[OSError], None]
            ) -> Iterator[tuple[str, list[str], list[str]]]:
                del path, followlinks
                onerror(PermissionError("disk scan denied"))
                yield from ()

            with (
                patch.object(os, "walk", denied_walk),
                self.assertRaisesRegex(PermissionError, "disk scan denied"),
            ):
                fuzz_storage.occupied_bytes(root)
