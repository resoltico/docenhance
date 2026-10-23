# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Exact CTest generated names and bounded stale-epoch selection, without native execution."""

from __future__ import annotations

import stat
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import TYPE_CHECKING, BinaryIO, Literal, override
from unittest import mock

from tools_path import prepare_tool_imports

import native_history as history
import native_journal as journal

prepare_tool_imports()

if TYPE_CHECKING:
    import os


class NativeJournalTests(unittest.TestCase):
    """Only actual CTest journal spellings and one changed owned epoch can be selected."""

    @override
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        self.directory = self.build / "Testing/Temporary"
        self.directory.mkdir(parents=True)

    def test_exact_generated_name_grammar(self) -> None:
        """Five lowercase hexadecimal digits match the pinned generated stream implementation."""
        self.assertEqual(journal.journal_status("LastTest.log"), "complete")
        for suffix in ("00000", "bb9f0", "fffff"):
            self.assertEqual(journal.journal_status("LastTest.log.tmp" + suffix), "partial")
        for name in (
            "LastTest.log.tmp",
            "LastTest.log.tmpabcd",
            "LastTest.log.tmpabcdef",
            "LastTest.log.tmpABCDe",
            "LastTest.log.tmpggggg",
            "LastTest.log.tmpabcde.gz",
            "prefixLastTest.log.tmpabcde",
            "LastTest.log.tmpabcde/child",
            "ctest.xml.tmp",
        ):
            with self.subTest(name=name):
                self.assertIsNone(journal.journal_status(name))

    def test_stale_dynamic_then_fresh_temporary_and_final_rename(self) -> None:
        """Stale retained output is inventoried; only a new epoch wins, never the newest guess."""
        stale = self.directory / "LastTest.log.tmp00001"
        stale.write_bytes(b"old")
        before = journal.snapshot_ctest_output(self.build)
        self.assertIn(stale.name, before)
        with self.assertRaisesRegex(journal.OutputError, "fresh"):
            journal.select_journal(self.build, before)
        fresh = self.directory / "LastTest.log.tmpbb9f0"
        fresh.write_bytes(b"new")
        self.assertEqual(journal.select_journal(self.build, before)[::2], (fresh, "partial"))
        final = self.directory / "LastTest.log"
        fresh.rename(final)
        self.assertEqual(journal.select_journal(self.build, before)[::2], (final, "complete"))
        self.assertEqual(stale.read_bytes(), b"old")

    def test_changed_existing_temporary_epoch_is_fresh_but_partial(self) -> None:
        """A retained name rewritten by the actual child is still never a completed journal."""
        path = self.directory / "LastTest.log.tmp00000"
        path.write_bytes(b"old")
        before = journal.snapshot_ctest_output(self.build)
        path.write_bytes(b"different new extent")
        self.assertEqual(journal.select_journal(self.build, before)[::2], (path, "partial"))

    def test_multiple_fresh_random_candidates_refuse(self) -> None:
        """Two changed generated streams cannot be resolved through timestamp ordering."""
        before = journal.snapshot_ctest_output(self.build)
        for name in ("LastTest.log.tmp00000", "LastTest.log.tmpfffff"):
            (self.directory / name).write_bytes(b"different writer")
        with self.assertRaisesRegex(journal.OutputError, "ambiguous"):
            journal.select_journal(self.build, before)

    def test_incomplete_or_unknown_before_names_refuse(self) -> None:
        """The canonical baseline and only valid generated names must precede the child."""
        variants: tuple[journal.Snapshot, ...] = (
            {},
            {"LastTest.log": None, "LastTest.log.tmp": None},
            {"LastTest.log": None, "ctest.xml": None},
        )
        for before in variants:
            with (
                self.subTest(before=before),
                self.assertRaisesRegex(journal.OutputError, "incomplete"),
            ):
                journal.select_journal(self.build, before)

    def test_enumeration_counts_unknown_siblings_and_has_a_finite_bound(self) -> None:
        """Unrecognized metadata cannot create an unbounded namespace scan or a raw candidate."""
        before = journal.snapshot_ctest_output(self.build)
        for name in ("metadata-one", "metadata-two", "LastTest.log.tmp"):
            (self.directory / name).write_bytes(b"kept")
        with (
            mock.patch.object(journal, "JOURNAL_DIRECTORY_ENTRIES", 2),
            self.assertRaisesRegex(journal.OutputError, "entry bound"),
        ):
            journal.snapshot_ctest_output(self.build)
        with self.assertRaisesRegex(journal.OutputError, "fresh"):
            journal.select_journal(self.build, before)

    def test_generated_candidate_directory_or_symlink_refuses(self) -> None:
        """Recognized names never follow a redirect or treat a directory as raw output."""
        path = self.directory / "LastTest.log.tmpabcde"
        path.mkdir()
        with self.assertRaisesRegex(journal.OutputError, "regular file"):
            journal.snapshot_ctest_output(self.build)
        path.rmdir()
        target = self.root / "foreign"
        target.write_bytes(b"do not acquire")
        path.symlink_to(target)
        with self.assertRaisesRegex(journal.OutputError, "regular file"):
            journal.snapshot_ctest_output(self.build)
        self.assertEqual(target.read_bytes(), b"do not acquire")

    def test_windows_reparse_file_and_parent_refuse(self) -> None:
        """Windows ordinary mode bits do not make a reparse owner safe."""
        info = SimpleNamespace(
            st_mode=stat.S_IFREG, st_file_attributes=stat.FILE_ATTRIBUTE_REPARSE_POINT
        )
        with (
            mock.patch.object(Path, "lstat", return_value=info),
            self.assertRaisesRegex(journal.OutputError, "regular file"),
        ):
            journal.identify_journal(self.directory / "LastTest.log.tmpabcde")
        info.st_mode = stat.S_IFDIR
        with (
            mock.patch.object(Path, "lstat", return_value=info),
            self.assertRaisesRegex(journal.OutputError, "parent"),
        ):
            journal.snapshot_ctest_output(self.build)


class RawJUnitRetirementTests(unittest.TestCase):
    """Only the qualified current byte epoch can lose its redundant raw copy."""

    def exercise(self, mutation: str) -> None:
        """Keep earlier evidence and both compressed bytes untouched on every refusal."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            owner = root / "native-current"
            owner.mkdir()
            raw = owner / "ctest.xml"
            body = b"qualified current raw XML"
            raw.write_bytes(body)
            archive = owner / "ctest.xml.gz"
            archive.write_bytes(b"verified archive is caller-owned")
            previous = root / "native-previous"
            previous.mkdir()
            (previous / "ctest.xml").write_bytes(b"older retained evidence")
            identity, directory = journal.identify_journal(raw), owner.lstat()
            deadline = float("inf")
            archive_identity = journal.identify_journal(archive)
            if mutation == "content":
                raw.write_bytes(b"different epoch")
            elif mutation == "replacement":
                raw.rename(owner / "original")
                raw.write_bytes(body)
            elif mutation == "owner":
                owner.rename(root / "original-owner")
                owner.mkdir()
                raw.write_bytes(body)
            elif mutation == "deadline":
                deadline = -1
            elif mutation == "retained":
                body = b"different retained bytes"
            self.change_gzip(archive, mutation)
            current = history.NativeHistory(root, deadline, owner)
            if mutation == "healthy":
                self.remove_closed(current, body, identity, archive_identity, directory)
                self.assertFalse(raw.exists())
            elif mutation == "unlink":
                error = PermissionError("Windows open handle refuses deletion")
                with (
                    mock.patch.object(Path, "unlink", side_effect=error),
                    self.assertRaises(PermissionError) as caught,
                ):
                    current.retire_junit(body, identity, archive_identity, directory)
                self.assertIs(caught.exception, error)
                self.assertTrue(raw.exists())
            else:
                with self.assertRaises(journal.OutputError):
                    current.retire_junit(body, identity, archive_identity, directory)
                self.assertTrue(raw.exists())
            if mutation not in {"owner", "gzip_missing", "gzip_changed"}:
                self.assertEqual(archive.read_bytes(), b"verified archive is caller-owned")
            self.assertEqual((previous / "ctest.xml").read_bytes(), b"older retained evidence")

    @staticmethod
    def change_gzip(archive: Path, mutation: str) -> None:
        """Exercise each observable archive loss/replacement without changing the raw epoch."""
        if mutation == "gzip_missing":
            archive.unlink()
        elif mutation == "gzip_changed":
            archive.write_bytes(b"changed gzip")
        elif mutation == "gzip_replaced":
            archive.rename(archive.parent / "original.gz")
            archive.write_bytes(b"verified archive is caller-owned")

    def remove_closed(
        self,
        current: history.NativeHistory,
        body: bytes,
        identity: journal.Journal | None,
        archive_identity: journal.Journal | None,
        directory: os.stat_result,
    ) -> None:
        """Assert the Windows-compatible close-before-unlink ordering with real open readers."""
        opened: list[BinaryIO] = []
        original_open, original_unlink = Path.open, Path.unlink

        def tracked_open(path: Path, mode: Literal["rb"]) -> BinaryIO:
            stream = original_open(path, mode)
            opened.append(stream)
            return stream

        def closed_unlink(path: Path) -> None:
            self.assertTrue(opened)
            self.assertTrue(all(stream.closed for stream in opened))
            original_unlink(path)

        with (
            mock.patch.object(Path, "open", tracked_open),
            mock.patch.object(Path, "unlink", closed_unlink),
        ):
            current.retire_junit(body, identity, archive_identity, directory)

    def test_only_current_duplicate_is_removed_with_all_readers_closed(self) -> None:
        """Current bytes disappear after readers close; all prior evidence remains."""
        self.exercise("healthy")

    def test_changed_content_or_identical_replacement_is_retained(self) -> None:
        """Changed bytes/epochs, deadline and Windows deletion refusal keep raw evidence."""
        for mutation in ("content", "replacement", "owner", "retained", "deadline", "unlink"):
            with self.subTest(mutation=mutation):
                self.exercise(mutation)

    def test_missing_changed_or_identically_replaced_gzip_keeps_raw(self) -> None:
        """In-memory verified bytes never substitute for the retained gzip pathname epoch."""
        for mutation in ("gzip_missing", "gzip_changed", "gzip_replaced"):
            with self.subTest(mutation=mutation):
                self.exercise(mutation)
