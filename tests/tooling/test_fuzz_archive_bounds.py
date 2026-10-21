# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Finite source inventory and actual archive payload ownership boundaries."""

from __future__ import annotations

import json
import os
import tarfile
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from typing import TYPE_CHECKING, Any, BinaryIO
from unittest.mock import Mock, patch

from tools_path import ROOT

from fuzz_archive_inputs import Entry, regular, source_entries
from fuzz_archive_inputs import metadata as read_metadata
from package_fuzz_evidence import _add_entry, archive_evidence, describe

if TYPE_CHECKING:
    from collections.abc import Iterator


class FuzzArchiveBoundsTests(unittest.TestCase):
    """Real file/directory mutations cannot bypass the admitted table or byte allowance."""

    def test_queued_directory_replacement_refuses_before_foreign_metadata_or_payload(self) -> None:
        """Reject actual queued-directory replacement before reading foreign payloads."""
        kinds = ("directory", "symlink") if os.name == "posix" else ("directory",)
        for kind in kinds:
            self.check_queued_replacement(kind)

    def check_queued_replacement(self, kind: str) -> None:
        """Retain one replacement variant and its actual source/foreign byte witnesses."""
        with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "app"
            queued = build / "fuzz-work/campaign-queued"
            queued.mkdir(parents=True)
            (queued / "campaign.json").write_bytes(b'{"passed":false}')
            foreign = root / "foreign"
            foreign.mkdir()
            foreign_bytes = b'{"passed":true,"foreign":true}'
            (foreign / "campaign.json").write_bytes(foreign_bytes)
            retained = queued.with_name("retained-original")
            output = build / "archive.tar.gz"
            output.write_bytes(b"previous archive")
            actual_scandir = os.scandir
            replacements = []

            def replaced(directory: Path) -> Iterator[os.DirEntry[str]]:
                if Path(directory) == queued:
                    queued.rename(retained)
                    if kind == "symlink":
                        queued.symlink_to(foreign, target_is_directory=True)
                    else:
                        queued.mkdir()
                        (queued / "campaign.json").write_bytes(foreign_bytes)
                    replacements.append(kind)
                return actual_scandir(directory)

            with (
                patch("fuzz_archive_inputs.os.scandir", replaced),
                patch("fuzz_archive_inputs.metadata") as metadata,
                patch("package_fuzz_evidence.describe") as description,
            ):
                with self.assertRaisesRegex(ValueError, "directory changed during inventory"):
                    archive_evidence(build, "afl", "a" * 40, output)
                metadata.assert_not_called()
                description.assert_not_called()
            self.assertEqual(replacements, [kind])
            self.assertEqual(output.read_bytes(), b"previous archive")
            self.assertEqual((retained / "campaign.json").read_bytes(), b'{"passed":false}')
            self.assertEqual((foreign / "campaign.json").read_bytes(), foreign_bytes)
            self.assertFalse(list(build.glob(".archive.tar.gz-*")))

    def test_inventory_refuses_before_description_or_metadata_acquisition(self) -> None:
        """Inventory refuses before description or metadata acquisition."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            work = build / "fuzz-work"
            work.mkdir()
            (work / "finding").write_bytes(b"retained")
            with (
                patch("fuzz_archive_inputs.MAX_FILES", 2),
                patch("package_fuzz_evidence.describe") as description,
                patch("fuzz_archive_inputs.metadata") as metadata,
            ):
                with self.assertRaisesRegex(ValueError, "archive budget"):
                    archive_evidence(build, "afl", "a" * 40, build / "archive.tar.gz")
                description.assert_not_called()
                metadata.assert_not_called()

    def test_manifest_occupies_one_of_the_actual_archive_member_slots(self) -> None:
        """Manifest occupies one of the actual archive member slots."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            work = build / "fuzz-work"
            work.mkdir()
            (work / "finding").write_bytes(b"retained")
            output = build / "archive.tar.gz"
            with patch("fuzz_archive_inputs.MAX_FILES", 3):
                summary = archive_evidence(build, "afl", "a" * 40, output)
                with tarfile.open(output, "r:gz") as archive:
                    self.assertEqual(len(archive.getnames()), 3)
                self.assertEqual(summary["archived_entries"], 3)
                previous = output.read_bytes()
                (work / "extra").write_bytes(b"one member too many")
                with self.assertRaisesRegex(ValueError, "archive budget"):
                    archive_evidence(build, "afl", "a" * 40, output)
                self.assertEqual(output.read_bytes(), previous)

    def test_description_consumes_only_the_admitted_table(self) -> None:
        """Description consumes only the admitted table."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            campaign = build / "fuzz-work/campaign-admitted"
            campaign.mkdir(parents=True)
            metadata = campaign / "campaign.json"
            payload = json.dumps({"passed": False, "note": "x" * 1024}).encode()
            metadata.write_bytes(payload)
            with patch("fuzz_archive_inputs.MAX_SOURCE_BYTES", len(payload)):
                admitted = source_entries(build)
            (campaign.parent / "campaign-not-admitted").mkdir()
            actual_read_text = Path.read_text

            def declared_text(
                path: Path,
                encoding: str | None = None,
                errors: str | None = None,
            ) -> str:
                if path != ROOT / "fuzz/targets.json":
                    message = "Unbounded campaign metadata read"
                    raise AssertionError(message)
                return actual_read_text(path, encoding=encoding, errors=errors)

            with (
                patch.object(Path, "glob", side_effect=AssertionError("unadmitted walk")),
                patch.object(Path, "read_text", declared_text),
            ):
                self.assertEqual(
                    describe(admitted, "afl", "a" * 40)["state"], "campaign_failed_or_incomplete"
                )

    def test_metadata_replacement_refuses_before_json_is_parsed(self) -> None:
        """Metadata replacement refuses before json is parsed."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            work = build / "fuzz-work"
            work.mkdir()
            metadata = work / "campaign.json"
            metadata.write_bytes(b'{"passed":false}')
            entry = next(item for item in source_entries(build) if item.path == metadata)
            metadata.rename(work / "retained-original")
            metadata.write_bytes(b'{"passed":true }')
            with patch("fuzz_archive_inputs.json.loads") as parse:
                with self.assertRaisesRegex(ValueError, "owner changed"):
                    read_metadata(entry)
                parse.assert_not_called()
            metadata.unlink()
            metadata.mkdir()
            with self.assertRaises((OSError, ValueError)):
                read_metadata(entry)
            metadata.rmdir()
            if os.name == "posix":
                os.mkfifo(metadata)
                with self.assertRaisesRegex(ValueError, "regular file owner changed"):
                    read_metadata(entry)

    def test_post_inventory_compressible_growth_cannot_cross_logical_source_budget(self) -> None:
        """Post inventory compressible growth cannot cross logical source budget."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            finding = build / "fuzz-work/finding"
            finding.parent.mkdir()
            finding.write_bytes(b"four")
            output = build / "archive.tar.gz"
            output.write_bytes(b"previous archive")

            def grown(entries: list[Entry], engine: str, commit: str) -> dict[str, Any]:
                summary = describe(entries, engine, commit)
                finding.write_bytes(b"\0" * 33)
                return summary

            with (
                patch("fuzz_archive_inputs.MAX_SOURCE_BYTES", 32),
                patch("package_fuzz_evidence.describe", side_effect=grown),
                self.assertRaisesRegex(ValueError, "archive budget"),
            ):
                archive_evidence(build, "afl", "a" * 40, output)
            self.assertEqual(output.read_bytes(), b"previous archive")
            self.assertEqual(finding.read_bytes(), b"\0" * 33)
            self.assertFalse(list(build.glob(".archive.tar.gz-*")))

    def test_metadata_initial_extent_and_post_read_growth_refuse_before_parse(self) -> None:
        """Metadata initial extent and post read growth refuse before parse."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            path = build / "fuzz-work/campaign.json"
            path.parent.mkdir()
            payload = b'{"passed":false}'
            path.write_bytes(payload)
            entry = next(item for item in source_entries(build) if item.path == path)
            counts = []

            @contextmanager
            def growing(item: Entry) -> Iterator[BinaryIO]:
                with regular(item) as original:
                    observed = Mock(wraps=original)

                    def read(count: int) -> bytes:
                        counts.append(count)
                        data = original.read(count)
                        path.write_bytes(payload + b"growth")
                        return data

                    observed.read.side_effect = read
                    yield observed

            with (
                patch("fuzz_archive_inputs.regular", growing),
                patch("fuzz_archive_inputs.json.loads") as parse,
            ):
                with self.assertRaisesRegex(ValueError, "bounded acquisition"):
                    read_metadata(entry)
                parse.assert_not_called()
            self.assertEqual(counts, [len(payload) + 1])

    def test_actual_tar_sizes_share_one_remaining_payload_allowance(self) -> None:
        """Actual tar sizes share one remaining payload allowance."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            work = build / "fuzz-work"
            work.mkdir()
            for name in ("first", "second"):
                (work / name).write_bytes(b"x" * 20)
            entries = [item for item in source_entries(build) if item.path.is_file()]
            output = build / "bounded.tar"
            with tarfile.open(output, "w", dereference=True) as archive:
                spent = _add_entry(archive, entries[0], 32)
                self.assertEqual(spent, 20)
                with self.assertRaisesRegex(ValueError, "archive budget"):
                    _add_entry(archive, entries[1], 32 - spent)
            with tarfile.open(output, "r") as archive:
                self.assertEqual(archive.getnames(), [entries[0].member])

    def test_growth_after_actual_payload_copy_refuses_without_replacing_previous_archive(
        self,
    ) -> None:
        """Growth after actual payload copy refuses without replacing previous archive."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            finding = build / "fuzz-work/finding"
            finding.parent.mkdir()
            finding.write_bytes(b"original")
            output = build / "archive.tar.gz"
            output.write_bytes(b"previous archive")
            actual_add = tarfile.TarFile.addfile

            def copied(
                archive: tarfile.TarFile, info: tarfile.TarInfo, source: BinaryIO | None = None
            ) -> None:
                actual_add(archive, info, source)
                if info.name == "fuzz-work/finding":
                    with finding.open("ab") as changed:
                        changed.write(b"growth sentinel")

            with (
                patch.object(tarfile.TarFile, "addfile", copied),
                self.assertRaisesRegex(ValueError, "bounded copying"),
            ):
                archive_evidence(build, "afl", "a" * 40, output)
            self.assertEqual(output.read_bytes(), b"previous archive")
            self.assertEqual(finding.read_bytes(), b"originalgrowth sentinel")
            self.assertFalse(list(build.glob(".archive.tar.gz-*")))
