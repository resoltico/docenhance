# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Small journal fixtures protect lossless diagnostics independently from test qualification."""

from __future__ import annotations

import gzip
import hashlib
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import Literal, cast, override
from unittest import mock

from tools_path import prepare_tool_imports

from native_output_fixture import journal_bytes

import native_journal as journal
import native_output as output

prepare_tool_imports()

POST_READ_IDENTITY_CHECK = 2


class NativeOutputTests(unittest.TestCase):
    """Real tiny file artifacts exercise bounds, provenance, exclusivity and supervisor cleanup."""

    @override
    def setUp(self) -> None:
        """Each control owns tiny fresh journal and evidence directories."""
        self.temporary = tempfile.TemporaryDirectory(prefix="native-output-control-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.journals = self.build / "Testing/Temporary"
        self.journals.mkdir(parents=True)
        self.evidence = self.root / "evidence"
        self.evidence.mkdir()
        self.raw = journal_bytes(b"fixed full diagnostic\n" * 8)

    def populate(self, *, partial: bool = False, failure: bool = False) -> None:
        """Write one concrete journal epoch and an independently owned raw XML fixture."""
        name = "LastTest.log.tmp" if partial else "LastTest.log"
        (self.journals / name).write_bytes(
            self.raw.replace(b"Test Passed.", b"Test Failed.") if failure else self.raw
        )
        body = '<failure message="retained actual failure"/>' if failure else ""
        (self.evidence / "ctest.xml").write_text(
            f'<testsuite><testcase name="fixed" status="run">{body}</testcase></testsuite>'
        )

    def archive(self, before: journal.Snapshot) -> dict[str, output.OutputReceipt]:
        """Use the real bounded compressor and verifier on the tiny owned files."""
        return output.archive_ctest_output(self.build, self.evidence, before, None, 1)

    def test_two_real_lossless_archives_are_diagnostic_only(self) -> None:
        """Both raw artifacts survive with exact decoded bytes and bound provenance."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        junit = (self.evidence / "ctest.xml").read_bytes()
        result = self.archive(before)
        for name, expected in (("journal", self.raw), ("junit", junit)):
            with self.subTest(name=name):
                artifact = Path(result[name]["archive"])
                self.assertEqual(gzip.decompress(artifact.read_bytes()), expected)
                self.assertEqual(result[name]["raw_sha256"], hashlib.sha256(expected).hexdigest())
                self.assertLessEqual(result[name]["archive_bytes"], output.ARCHIVE_LIMIT)
        self.assertEqual((self.journals / "LastTest.log").read_bytes(), self.raw)
        self.assertEqual((self.evidence / "ctest.xml").read_bytes(), junit)
        metadata = json.loads((self.evidence / "native-output.json").read_text())
        self.assertEqual(metadata["status"], "diagnostic_only")
        self.assertEqual(metadata["errors"], [])

    def test_long_non_utf8_body_remains_lossless_across_stream_chunks(self) -> None:
        """Framing keeps bounded metadata fragments, never a whole arbitrary output line."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        raw = journal_bytes(
            b"Test Passed.\nStart testing: payload\nEnd testing: payload\nOutput:\n"
            + b"\xff" * (output.BUFFER_BYTES + 31)
        )
        (self.journals / "LastTest.log").write_bytes(raw)
        result = self.archive(before)
        self.assertEqual(result["journal"]["status"], "diagnostic_only_complete")
        self.assertEqual(gzip.decompress(Path(result["journal"]["archive"]).read_bytes()), raw)

    def test_gzip_is_deterministic_without_path_or_clock_metadata(self) -> None:
        """Identical fresh bytes produce identical gzip bytes in distinct evidence owners."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        first = Path(self.archive(before)["journal"]["archive"]).read_bytes()
        other = self.root / "other"
        other.mkdir()
        (other / "ctest.xml").write_bytes((self.evidence / "ctest.xml").read_bytes())
        second = output.archive_ctest_output(self.build, other, before, None, 1)
        self.assertEqual(first, Path(second["journal"]["archive"]).read_bytes())

    def test_failed_partial_output_is_retained_without_passing_label(self) -> None:
        """A real failed JUnit and interrupted journal are useful diagnostics, never passes."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate(partial=True, failure=True)
        result = self.archive(before)
        self.assertEqual(result["journal"]["status"], "diagnostic_only_partial")
        self.assertIn(
            "failure", gzip.decompress(Path(result["junit"]["archive"]).read_bytes()).decode()
        )
        self.assertTrue((self.evidence / "ctest-output-partial.gz").exists())

    def test_unchanged_old_or_absent_journal_is_refused(self) -> None:
        """An old journal cannot certify the new execution, even when JUnit is fresh."""
        self.populate()
        before = journal.snapshot_ctest_output(self.build)
        with self.assertRaisesRegex(journal.OutputError, "fresh"):
            self.archive(before)
        self.assertFalse((self.evidence / "ctest-output.gz").exists())
        self.assertTrue((self.evidence / "native-output-error.json").exists())

    def test_missing_journal_cannot_use_fresh_junit_as_replacement(self) -> None:
        """A fresh XML result does not replace the required complete raw CTest journal."""
        before = journal.snapshot_ctest_output(self.build)
        (self.evidence / "ctest.xml").write_text("<testsuite/>")
        with self.assertRaisesRegex(journal.OutputError, "fresh"):
            self.archive(before)
        self.assertFalse((self.evidence / "native-output.json").exists())

    def test_two_changed_journals_are_ambiguous(self) -> None:
        """Never guess which concurrent or interrupted writer owns complete evidence."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        (self.journals / "LastTest.log.tmp").write_bytes(b"different epoch")
        with self.assertRaisesRegex(journal.OutputError, "ambiguous"):
            self.archive(before)

    def test_symlink_journal_is_refused_before_reading(self) -> None:
        """A linked regular target is not the owned regular journal identity."""
        target = self.root / "target"
        target.write_bytes(self.raw)
        (self.journals / "LastTest.log").symlink_to(target)
        with self.assertRaisesRegex(journal.OutputError, "regular file"):
            journal.snapshot_ctest_output(self.build)

    def test_raw_bound_is_admission_not_silent_truncation(self) -> None:
        """Both outputs exceeding a tiny bound fail rather than produce shortened evidence."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        with mock.patch.object(output, "RAW_LIMIT", 16), self.assertRaises(journal.OutputError):
            self.archive(before)
        self.assertFalse((self.evidence / "ctest-output.gz").exists())
        self.assertFalse((self.evidence / "native-output.json").exists())
        self.assertEqual((self.journals / "LastTest.log").read_bytes(), self.raw)

    def test_compressed_writer_bound_keeps_partial_and_refuses_success(self) -> None:
        """Gzip header, body and trailer all consume the same hard writer allowance."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        (self.journals / "LastTest.log").write_bytes(bytes(range(256)))
        with mock.patch.object(output, "ARCHIVE_LIMIT", 32), self.assertRaises(journal.OutputError):
            self.archive(before)
        partial = self.evidence / "ctest-output.gz.partial"
        self.assertTrue(partial.exists())
        self.assertLessEqual(partial.stat().st_size, 32)
        self.assertFalse((self.evidence / "ctest-output.gz").exists())
        self.assertFalse((self.evidence / "native-output.json").exists())

    def test_existing_final_artifact_is_never_replaced(self) -> None:
        """Exclusive final publication preserves existing bytes and the new partial evidence."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        final = self.evidence / "ctest-output.gz"
        final.write_bytes(b"old artifact")
        with self.assertRaises(journal.OutputError):
            self.archive(before)
        self.assertEqual(final.read_bytes(), b"old artifact")
        self.assertTrue((self.evidence / "ctest-output.gz.partial").exists())

    def test_roundtrip_corruption_is_refused_before_final_publication(self) -> None:
        """Read the actual corrupted gzip through the real verifier, not a mocked verdict."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        actual_open = gzip.open

        def corrupt(path: Path, mode: Literal["rb"]) -> gzip.GzipFile:
            self.assertEqual(mode, "rb")
            path.write_bytes(gzip.compress(b"different bytes", mtime=0))
            return actual_open(path, mode)

        with (
            mock.patch.object(gzip, "open", side_effect=corrupt),
            self.assertRaisesRegex(journal.OutputError, "roundtrip"),
        ):
            self.archive(before)
        self.assertFalse((self.evidence / "ctest-output.gz").exists())

    def test_mutation_during_read_is_not_accepted_as_stable(self) -> None:
        """The opened source and final pathname identity must still describe the same bytes."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        actual_fstat = os.fstat
        observations = 0
        mutations = 0

        def mutate(descriptor: int) -> os.stat_result:
            nonlocal observations, mutations
            observations += 1
            if observations == POST_READ_IDENTITY_CHECK:
                mutations += 1
                with (self.journals / "LastTest.log").open("ab") as stream:
                    stream.write(b"later mutation")
            return actual_fstat(descriptor)

        with (
            mock.patch.object(os, "fstat", side_effect=mutate),
            self.assertRaisesRegex(journal.OutputError, "changed during"),
        ):
            self.archive(before)
        self.assertEqual(mutations, 1)
        self.assertFalse((self.evidence / "native-output.json").exists())

    def test_deadline_is_not_reset_for_archival(self) -> None:
        """Expired owner time refuses work; no independent archive clock is invented."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        with self.assertRaisesRegex(journal.OutputError, "deadline"):
            output.archive_ctest_output(self.build, self.evidence, before, time.monotonic() - 1, 1)

    def test_source_open_control_survives_owned_close_error_once(self) -> None:
        """Cleanup of the actually owned descriptor cannot replace an opening interruption."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        path = self.journals / "LastTest.log"
        identity = journal.identify_journal(path)
        if identity is None:
            self.fail("The fixed source fixture must exist")
        error = KeyboardInterrupt("source identity interrupted")
        actual_close = os.close

        def close_then_report(descriptor: int) -> None:
            actual_close(descriptor)
            msg = "owned close reported failure"
            raise OSError(msg)

        with (
            mock.patch.object(os, "fstat", side_effect=error),
            mock.patch.object(os, "close", side_effect=close_then_report) as closing,
            self.assertRaises(KeyboardInterrupt) as caught,
        ):
            self.archive(before)
        self.assertIs(caught.exception, error)
        self.assertEqual(closing.call_count, 1)
        self.assertTrue(any("descriptor close also failed" in note for note in error.__notes__))

    def test_windows_archive_keeps_descriptor_change_time_stability(self) -> None:
        """Canonical birth time permits the APIs to differ, never the fd epoch to change."""
        before = journal.snapshot_ctest_output(self.build)
        self.populate()
        actual_fstat = os.fstat

        def windows_stat(info: os.stat_result, changed: int) -> os.stat_result:
            values = {
                name: getattr(info, name)
                for name in ("st_mode", "st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
            }
            values.update(st_birthtime_ns=17, st_ctime_ns=changed)
            return cast("os.stat_result", SimpleNamespace(**values))

        def path_epoch(path: Path) -> journal.Journal | None:
            try:
                return journal.journal_identity(windows_stat(path.lstat(), 17))
            except FileNotFoundError:
                return None

        for changed in (False, True):
            observations = 0
            directory = self.evidence / str(changed)
            directory.mkdir()
            (directory / "ctest.xml").write_bytes((self.evidence / "ctest.xml").read_bytes())

            def fd_epoch(descriptor: int, *, mutating: bool = changed) -> os.stat_result:
                nonlocal observations
                observations += 1
                value = 30 if mutating and observations == POST_READ_IDENTITY_CHECK else 29
                return windows_stat(actual_fstat(descriptor), value)

            with (
                self.subTest(changed=changed),
                mock.patch.object(sys, "platform", "win32"),
                mock.patch.object(journal, "identify_journal", side_effect=path_epoch),
                mock.patch.object(output, "identify_journal", side_effect=path_epoch),
                mock.patch.object(os, "fstat", side_effect=fd_epoch),
            ):
                if changed:
                    with self.assertRaisesRegex(journal.OutputError, "changed during"):
                        output.archive_ctest_output(self.build, directory, before, None, 1)
                else:
                    archived = output.archive_ctest_output(self.build, directory, before, None, 1)
                    raw = gzip.decompress(Path(archived["journal"]["archive"]).read_bytes())
                    self.assertEqual(raw, self.raw)
