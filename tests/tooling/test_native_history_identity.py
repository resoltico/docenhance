# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Authoritative nonfollowing metadata owns native-history identity on every platform."""

from __future__ import annotations

import os
import stat
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools_path import prepare_tool_imports

import native_history

prepare_tool_imports()


class NativeHistoryIdentityTests(unittest.TestCase):
    """Windows directory-entry cache fields never substitute for actual file identity."""

    def test_windows_cached_zero_fields_use_fresh_hardlink_identity(self) -> None:
        """Documented zero DirEntry fields cannot reject or collapse actual distinct files."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            owner = root / "native-prior"
            owner.mkdir()
            first = owner / "first"
            first.write_bytes(b"123")
            second = owner / "second"
            second.write_bytes(b"12345")
            alias = owner / "alias"
            os.link(first, alias)
            history = native_history.NativeHistory(root, time.monotonic() + 5)
            for path in (first, second, alias):
                entry = mock.Mock(spec=os.DirEntry, path=str(path))
                entry.stat.return_value = os.stat_result([stat.S_IFREG, 0, 0, 0, 0, 0, 0, 0, 0, 0])
                expected = path.lstat()
                with mock.patch.object(
                    Path, "lstat", autospec=True, side_effect=Path.lstat
                ) as observe:
                    info = native_history.regular_entry(entry)
                observe.assert_called_once_with(path)
                entry.stat.assert_not_called()
                self.assertIsNotNone(info)
                self.assertEqual(info, expected)
            history.admit()
            self.assertEqual(history.peak_bytes, 8)
            self.assertGreaterEqual(history.peak_entries, 5)

    def test_authoritative_zero_inode_still_refuses(self) -> None:
        """A genuine missing file identity retains the original refusal boundary."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            owner = root / "native-prior"
            owner.mkdir()
            report = owner / "report"
            report.write_bytes(b"payload")
            observed = report.lstat()
            zero = SimpleNamespace(
                **{
                    name: getattr(observed, name)
                    for name in dir(observed)
                    if name.startswith("st_")
                }
            )
            zero.st_ino = 0
            actual_lstat = Path.lstat

            def observe(path: Path) -> os.stat_result | SimpleNamespace:
                return zero if path == report else actual_lstat(path)

            with (
                mock.patch.object(Path, "lstat", autospec=True, side_effect=observe),
                self.assertRaisesRegex(ValueError, "no usable inode identity"),
            ):
                native_history.NativeHistory(root, time.monotonic() + 5).admit()

    def test_rename_disappearance_only_is_tolerated(self) -> None:
        """A disappeared CTest writer is omitted; denied metadata remains an actual error."""
        entry = mock.Mock(spec=os.DirEntry, path="owned-report")
        with mock.patch.object(Path, "lstat", side_effect=FileNotFoundError):
            self.assertIsNone(native_history.regular_entry(entry))
        primary = PermissionError("actual metadata refusal")
        with (
            mock.patch.object(Path, "lstat", side_effect=primary),
            self.assertRaises(PermissionError) as caught,
        ):
            native_history.regular_entry(entry)
        self.assertIs(caught.exception, primary)

    def test_fresh_redirected_or_special_metadata_refuses(self) -> None:
        """Cached regular metadata cannot hide current symlink, reparse or special identity."""
        entry = mock.Mock(spec=os.DirEntry, path="owned-report")
        entry.stat.return_value = os.stat_result([stat.S_IFREG, 1, 1, 1, 0, 0, 7, 0, 0, 0])
        for mode, attributes in (
            (stat.S_IFLNK, 0),
            (stat.S_IFIFO, 0),
            (stat.S_IFREG, stat.FILE_ATTRIBUTE_REPARSE_POINT),
            (stat.S_IFDIR, stat.FILE_ATTRIBUTE_REPARSE_POINT),
        ):
            with self.subTest(mode=mode, attributes=attributes):
                info = mock.Mock(st_mode=mode, st_file_attributes=attributes)
                with (
                    mock.patch.object(Path, "lstat", return_value=info),
                    self.assertRaisesRegex(ValueError, "redirected or special"),
                ):
                    native_history.regular_entry(entry)
        entry.stat.assert_not_called()
