# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Descriptor-initial extents, same-byte parsing and historical raw ownership controls."""

from __future__ import annotations

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import TYPE_CHECKING, cast
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_evidence_fixture import (
    binary_record,
    component_inputs,
    delivered_candidate,
    refusal,
    successful_response,
    write_reference,
)

from evaluate_document_quality import read_bundle
from quality_acquisition import acquire_file, acquire_original_stream, acquire_reference
from quality_attempt import CHUNK_BYTES, MAX_RESPONSE_BYTES, CaptureAdmission, new_original
from quality_collection import BundleChecks, collect
from quality_fidelity import fidelity
from quality_file_snapshot import AcquiredFile, ReadSource, read_extent
from quality_replay import replay
from quality_report import report
from quality_run import QualityRun

if TYPE_CHECKING:
    import os

prepare_tool_imports()


EXPECTED_PAGE_COUNT = 2


class QualityAcquisitionTests(unittest.TestCase):
    """Keep descriptor extents and same-byte acquisition truthful."""

    def test_windows_path_birth_time_and_descriptor_change_time_keep_distinct_guards(self) -> None:
        """Cross-API birth identity matches; descriptor metadata mutation still refuses."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            path = root / "value"
            path.write_bytes(b"abc")
            info = path.stat()
            reference = {"path": "value", "bytes": 3, "sha256": hashlib.sha256(b"abc").hexdigest()}

            def stamp(changed: int, birth: int = 11) -> os.stat_result:
                return cast(
                    "os.stat_result",
                    SimpleNamespace(
                        st_dev=info.st_dev,
                        st_ino=info.st_ino,
                        st_mode=info.st_mode,
                        st_size=3,
                        st_mtime_ns=info.st_mtime_ns,
                        st_ctime_ns=changed,
                        st_birthtime_ns=birth,
                    ),
                )

            path_info, descriptor = stamp(11), stamp(23)
            with (
                patch("native_journal.sys.platform", "win32"),
                patch.object(Path, "stat", return_value=path_info),
                patch("quality_file_snapshot.os.fstat", return_value=descriptor),
            ):
                acquired = acquire_file(root, reference)
                self.assertTrue(acquired.identity_bound)
                self.assertEqual(acquired.payload, b"abc")
            for changed, birth in ((24, 11), (23, 12)):
                final = stamp(changed, birth)
                with (
                    self.subTest(changed=changed, birth=birth),
                    patch("native_journal.sys.platform", "win32"),
                    patch.object(Path, "stat", return_value=path_info),
                    patch("quality_file_snapshot.os.fstat", side_effect=[descriptor, final]),
                ):
                    acquired = acquire_file(root, reference)
                    self.assertFalse(acquired.identity_bound)
                    self.assertIsNone(acquired.payload)
                    self.assertIn("read", acquired.failures)

    def test_metadata_larger_than_response_limit_uses_its_actual_extent(self) -> None:
        """Metadata larger than response limit uses its actual extent."""
        value = {"independent_review_metadata": "é" * MAX_RESPONSE_BYTES}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            reference = write_reference(root, "review.json", value)
            self.assertGreater((root / reference["path"]).stat().st_size, MAX_RESPONSE_BYTES)
            self.assertEqual(acquire_reference(root, reference), value)
            wrong = reference | {"sha256": "0" * 64}
            with self.assertRaisesRegex(ValueError, "declared identity"):
                acquire_reference(root, wrong)

    def test_original_oversize_hashes_same_full_bytes_without_retaining_or_rewriting_them(
        self,
    ) -> None:
        """Original oversize hashes same full bytes without retaining or rewriting them."""
        raw = b'{"complete":true}' + b" " * MAX_RESPONSE_BYTES
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            declared = refusal(
                root, ["selected", "process", "source", "--out-dir", "out", "--json"], "old"
            )
            declared["execution"]["stdout"] = write_reference(root, "old.stdout", raw)
            original, admission = new_original(0, declared), CaptureAdmission()
            with patch.object(Path, "read_bytes", side_effect=AssertionError("unbounded read")):
                payload = acquire_original_stream(original, "stdout", root, admission)
            self.assertIsNone(payload)
            self.assertEqual(original["stdout"]["sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(original["stdout"]["retained_bytes"], len(raw))
            self.assertEqual(original["stdout"]["observed_extent"], "exact")
            self.assertEqual(original["observed_publication"], "unknown")
            self.assertEqual((root / "old.stdout").read_bytes(), raw)
            self.assertEqual(original["declared"], declared)

    def test_growing_metadata_stops_at_initial_extent_plus_one(self) -> None:
        """Growing metadata stops at initial extent plus one."""
        initial_payload = b'{"x":1}'
        requests: list[int] = []
        delivered: list[int] = []

        class GrowingHandle:
            def __init__(self, stream: ReadSource, path: Path) -> None:
                """Retain the actual source and append destination for one bounded read."""
                self.stream, self.path, self.first = stream, path, True

            def fileno(self) -> int:
                """Expose the same real descriptor to the acquisition owner."""
                return self.stream.fileno()

            def read(self, size: int) -> bytes | None:
                """Append actual bytes once before the real bounded read."""
                requests.append(size)
                if self.first:
                    self.first = False
                    with self.path.open("ab") as writer:
                        writer.write(b" " * (MAX_RESPONSE_BYTES + 100))
                raw = self.stream.read(size)
                if raw is not None:
                    delivered.append(len(raw))
                return raw

        def grow(
            stream: ReadSource, initial: os.stat_result, limit: int | None, result: AcquiredFile
        ) -> None:
            return read_extent(GrowingHandle(stream, root / "growing.json"), initial, limit, result)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            reference = write_reference(root, "growing.json", initial_payload)
            with patch("quality_file_snapshot.read_extent", grow):
                acquired = acquire_file(root, reference)
            self.assertEqual(sum(delivered), len(initial_payload) + 1)
            self.assertTrue(all(0 < size <= CHUNK_BYTES for size in requests))
            self.assertIsNone(acquired.payload)
            self.assertFalse(acquired.identity_bound)
            self.assertTrue(acquired.failures)
            self.assertTrue(acquired.closed)
            self.assertGreater((root / "growing.json").stat().st_size, len(initial_payload) + 1)

    def test_same_parsed_snapshot_is_not_reopened_after_identity_check(self) -> None:
        """Same parsed snapshot is not reopened after identity check."""
        first, second = {"judgment": "earlier harmful"}, {"judgment": "later favorable"}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            reference = write_reference(root, "revision.json", first)
            original_read_extent = read_extent

            def replace_after_acquisition(
                stream: ReadSource, initial: os.stat_result, limit: int | None, result: AcquiredFile
            ) -> None:
                original_read_extent(stream, initial, limit, result)
                # A replacement after the one acquisition is detected, rather than parsed afresh.
                replacement = root / "replacement"
                replacement.write_text(json.dumps(second))
                replacement.replace(root / "revision.json")

            with patch("quality_file_snapshot.read_extent", replace_after_acquisition):
                acquired = acquire_file(root, copy.deepcopy(reference))
            self.assertFalse(acquired.identity_bound)
            self.assertIsNone(acquired.payload)
            if isinstance(acquired.failures.get("read"), PermissionError):
                self.assertEqual(json.loads((root / "revision.json").read_text()), first)
                self.assertTrue((root / "replacement").is_file())
            else:
                self.assertIn("stat", acquired.failures)
                self.assertEqual(json.loads((root / "revision.json").read_text()), second)

    def test_original_overflow_closes_every_later_launch_but_accounts_all_intended_roles(
        self,
    ) -> None:
        """Original overflow closes every later launch but accounts all intended roles."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable, _record = delivered_candidate(root)
            first = plan["assessments"][0]["candidate"]
            raw = json.dumps(first["execution"]["response"]).encode() + b" " * MAX_RESPONSE_BYTES
            first["execution"]["stdout"] = write_reference(root, "oversized.stdout", raw)
            later_directory = root / "candidate-1"
            later_directory.mkdir()
            later = successful_response(later_directory, binary_record("b" * 32))
            later["output"] = "candidate-1/result.png"
            plan["assessments"][1]["candidate"] = {
                "outcome": "completed",
                "bundle": "candidate-1",
                "execution": {
                    "argv": plan["protocol"]["pages"][1]["commands"]["candidate"],
                    "response": later,
                    "exit_code": 0,
                    "timed_out": False,
                    "publication": "completed",
                    "stdout": write_reference(root, "later.stdout", later),
                    "stderr": write_reference(root, "later.stderr", b""),
                },
            }
            run = QualityRun(component_inputs(plan, root, executable), root / "quality-run.json")
            try:
                checks = BundleChecks(run, {}, read_bundle, replay, fidelity)
                with patch("quality_capture.owned_process") as launch:
                    pages = collect(plan, checks)
                    launch.assert_not_called()
                first_observation = pages[0]["roles"]["candidate"]["original"]
                later_observation = pages[1]["roles"]["candidate"]["original"]
                self.assertEqual(first_observation["errors"]["raw_stdout"]["code"], "overflow")
                self.assertEqual(first_observation["observed_publication"], "unknown")
                self.assertEqual(later_observation["observed_publication"], "completed")
                self.assertEqual((root / "oversized.stdout").read_bytes(), raw)
                self.assertEqual(
                    first_observation["stdout"]["sha256"], hashlib.sha256(raw).hexdigest()
                )
                self.assertEqual(len(run.originals), 6)
                self.assertEqual(len(run.finished), 18)
                self.assertTrue(
                    all(attempt["artifact_directory"] is None for attempt in run.attempts)
                )
                value = run.finish(
                    report(plan, plan["protocol"]["executable_sha256"], pages, run.errors, {})
                )
                self.assertEqual(value["terminal"]["event_count"], 42)
                self.assertEqual(value["terminal"]["launch_admission"], "closed")
                self.assertEqual(value["report"]["decision"], "abstain")
                self.assertTrue(
                    all(
                        item["count"] == EXPECTED_PAGE_COUNT
                        for item in value["report"]["classes"].values()
                    )
                )
            finally:
                run.close()
