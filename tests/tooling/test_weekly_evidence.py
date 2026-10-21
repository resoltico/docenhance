# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Synthetic negative controls for weekly evidence and fail-closed findings."""

from __future__ import annotations

import hashlib
import json
import tarfile
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import patch

from tools_path import ROOT

from check_advisories import scan
from fuzz_archive_inputs import source_entries
from fuzz_manifest import targets
from package_fuzz_evidence import ArchiveWriter, archive_evidence, describe
from triage_fuzz_findings import triage

if TYPE_CHECKING:
    from collections.abc import Iterator


class WeeklyEvidenceTests(unittest.TestCase):
    """Fake campaign metadata must never be mistaken for full engine work."""

    def test_archive_writer_checks_before_crossing_exact_capacity(self) -> None:
        """The exact cap fits; the next byte is refused before reaching the file."""
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "bounded"
            with (
                patch("package_fuzz_evidence.MAX_ARCHIVE_BYTES", 4),
                ArchiveWriter(output.open("wb", buffering=0)) as stream,
            ):
                with self.assertRaisesRegex(ValueError, "compressed bytes"):
                    stream.write(memoryview(b"fourfour").cast("I"))
                self.assertEqual(stream.tell(), 0)
                self.assertEqual(stream.write(b"four"), 4)
                with self.assertRaisesRegex(ValueError, "compressed bytes"):
                    stream.write(b"fifth")
                stream.flush()
                self.assertEqual(output.read_bytes(), b"four")

    def test_archive_cannot_include_itself_or_replace_foreign_temporary(self) -> None:
        """Nested output is rejected and a preexisting sibling .tmp is untouched."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "app"
            work = build / "fuzz-work"
            work.mkdir(parents=True)
            (work / "retained").write_bytes(b"finding")
            with self.assertRaisesRegex(ValueError, "outside"):
                archive_evidence(build, "afl", "a" * 40, work / "evidence.tar.gz")
            destination = build / "evidence.tar.gz"
            sibling = destination.with_suffix(".tmp")
            sibling.write_bytes(b"unrelated")
            archive_evidence(build, "afl", "a" * 40, destination)
            self.assertEqual(sibling.read_bytes(), b"unrelated")

    def test_archive_inventory_stops_before_reading_unbounded_tree(self) -> None:
        """The entry ceiling interrupts enumeration rather than collecting every path first."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            root = build / "fuzz-work"
            root.mkdir()

            def growing_tree(_root: Path) -> Iterator[Path]:
                for _ in range(3):
                    yield root
                self.fail("Inventory continued past its admitted entry budget")

            with (
                patch("fuzz_archive_inputs.MAX_FILES", 2),
                patch("fuzz_archive_inputs._walk_entries", growing_tree),
                self.assertRaisesRegex(ValueError, "archive budget"),
            ):
                source_entries(build)

    def test_oversized_evidence_is_retained_without_partial_archive(self) -> None:
        """Packaging refusal preserves findings and a previously completed archive."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            finding = build / "fuzz-work/finding"
            finding.parent.mkdir()
            finding.write_bytes(b"crash")
            output = build / "evidence.tar.gz"
            output.write_bytes(b"previous archive")
            with (
                patch("fuzz_archive_inputs.MAX_SOURCE_BYTES", 1),
                self.assertRaisesRegex(ValueError, "archive budget"),
            ):
                archive_evidence(build, "afl", "a" * 40, output)
            self.assertEqual(finding.read_bytes(), b"crash")
            self.assertEqual(output.read_bytes(), b"previous archive")
            self.assertFalse(list(build.glob(".evidence.tar.gz-*")))

    def test_compressed_archive_cap_preserves_previous_archive_and_findings(self) -> None:
        """Tar headers and compressed output cannot grow beyond a separately bounded file."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            finding = build / "fuzz-work/finding"
            finding.parent.mkdir()
            finding.write_bytes(b"retained crash bytes")
            output = build / "evidence.tar.gz"
            output.write_bytes(b"previous archive")
            with (
                patch("package_fuzz_evidence.MAX_ARCHIVE_BYTES", 32),
                self.assertRaisesRegex(ValueError, "compressed bytes"),
            ):
                archive_evidence(build, "afl", "a" * 40, output)
            self.assertEqual(finding.read_bytes(), b"retained crash bytes")
            self.assertEqual(output.read_bytes(), b"previous archive")
            self.assertFalse(list(build.glob(".evidence.tar.gz-*")))

    def test_missing_and_partial_campaign_cannot_be_reported_complete(self) -> None:
        """Absent or incomplete target reports never become a full completed campaign."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "app"
            build.mkdir()
            destination = Path(temporary) / "evidence.tar.gz"
            result = archive_evidence(build, "afl", "a" * 40, destination)
            self.assertEqual(result["state"], "setup_or_build_failed")
            self.assertFalse(result["campaign_passed"])
            self.assertFalse((build / "fuzz-work").exists())
            with tarfile.open(destination, "r:gz") as archive:
                self.assertEqual(archive.getnames(), ["manifest.json"])
                stream = archive.extractfile("manifest.json")
                if stream is None:
                    self.fail("Missing expected archive manifest")
                manifest = json.loads(stream.read())
                self.assertEqual(manifest["source_commit"], "a" * 40)
                self.assertEqual(manifest["state"], "setup_or_build_failed")
            work = build / "fuzz-work/campaign-fixture"
            work.mkdir(parents=True)
            (work / "campaign.json").write_text(json.dumps({"passed": True}))
            (work / "ctest.xml").write_text("<testsuites/>")
            (work / "ctest.log").write_text("fake")
            self.assertEqual(
                describe(source_entries(build), "afl", "a" * 40)["state"],
                "campaign_failed_or_incomplete",
            )
            for target in targets():
                child = work / f"{target.name}-sample"
                child.mkdir()
                (child / "result.json").write_text(
                    json.dumps({"target": target.name, "passed": True})
                )
            self.assertEqual(
                describe(source_entries(build), "afl", "a" * 40)["state"], "complete_campaign"
            )
            (work / f"{targets()[0].name}-sample/result.json").unlink()
            self.assertEqual(
                describe(source_entries(build), "afl", "a" * 40)["state"],
                "campaign_failed_or_incomplete",
            )

    def test_unsafe_evidence_and_unpinned_source_are_rejected(self) -> None:
        """Reject symlinked data and nonimmutable source identity."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "app"
            work = build / "fuzz-work"
            work.mkdir(parents=True)
            target = Path(temporary) / "foreign"
            target.write_bytes(b"outside-owned-file")
            (work / "link").symlink_to(target)
            with self.assertRaises(ValueError):
                archive_evidence(build, "afl", "a" * 40, build / "archive.tar.gz")
            (work / "link").unlink()
            with self.assertRaises(ValueError):
                archive_evidence(build, "afl", "main", build / "archive.tar.gz")

    def test_saved_input_identity_is_checked_before_replay(self) -> None:
        """Verify the exact original executable and crash input identity."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            name = targets()[0].name
            binary = build / "fuzz" / f"de_fuzz_{name}"
            binary.parent.mkdir()
            binary.write_bytes(b"synthetic executable identity")
            target = build / "fuzz-work/campaign-fixture" / f"{name}-sample"
            finding = target / "afl/default/crashes/id000000"
            finding.parent.mkdir(parents=True)
            finding.write_bytes(b"synthetic crash input")
            report = {
                "target": name,
                "engine": "afl",
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "findings": ["afl/default/crashes/id000000"],
            }
            (target / "result.json").write_text(json.dumps(report))
            with patch(
                "triage_fuzz_findings.replay",
                return_value={
                    "exit_code": -6,
                    "timed_out": False,
                    "reproduced": True,
                    "output": "fuzz property violated: synthetic",
                    "truncated": False,
                },
            ) as runner:
                result = triage(build, "afl")
                self.assertEqual(result["status"], "complete")
                self.assertEqual(len(result["cases"]), 1)
                self.assertEqual(
                    result["cases"][0]["sha256"], hashlib.sha256(finding.read_bytes()).hexdigest()
                )
                runner.assert_called_once()
            binary.write_bytes(b"changed executable identity")
            with self.assertRaises(ValueError):
                triage(build, "afl")

    def test_advisory_observation_failure_is_written_not_silenced(self) -> None:
        """A network failure persists evidence and still raises the original error."""
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "report.json"
            dependency = {"name": "jpeg", "object": "a" * 40}
            with (
                patch("check_advisories.load_lock", return_value={"dependencies": [dependency]}),
                patch("check_advisories.source_query", return_value={"commit": "a" * 40}),
                patch("check_advisories.matches", side_effect=OSError("offline sentinel")),
                self.assertRaises(OSError),
            ):
                scan(ROOT / ".cache/deps", destination)
            saved = json.loads(destination.read_text())
            self.assertEqual(saved["status"], "observation_failed")
            self.assertEqual(saved["error"], "offline sentinel")
            self.assertEqual(saved["sources"][0]["name"], "jpeg")
            self.assertEqual(saved["sources"][0]["query"], {"commit": "a" * 40})
