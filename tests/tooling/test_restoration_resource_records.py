# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Synthetic journals challenge restoration shard reconciliation without native execution."""

from __future__ import annotations

import copy
import gzip
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from typing import BinaryIO
from unittest.mock import patch

from tools_path import prepare_tool_imports

from restoration_resource_fixture import (
    RESOURCE_EXECUTABLES,
    calibration_rows,
    framed,
    protocol_rows,
    resource_fixture,
    write_archives,
)

import native_resource_capture as capture
import restoration_resource_cases as cases
import restoration_resource_records as records

prepare_tool_imports()


class RestorationResourceRecordsTests(unittest.TestCase):
    """Require real parser acceptance and counterexamples at the full raw archive boundary."""

    def test_complete_two_shards_and_controls_from_raw(self) -> None:
        """All ordinals, healthy traces and controls reconcile independently of clipped JUnit."""
        tests, journal, _ = resource_fixture()
        actual = records.reconcile_restoration_records(tests, journal, RESOURCE_EXECUTABLES)
        self.assertEqual(actual["shard_count"], 2)
        self.assertEqual(
            actual["healthy_attempts"],
            {"1x1": 4, "1x409": 4, "419x1": 4, "129x97": 4, "503x211": 4},
        )
        self.assertEqual(set(actual["cases"]), {t["name"] for t in tests})

    def test_exact_release_backing_overhead_is_separate_and_bounded(self) -> None:
        """Single-block raw backing metadata never becomes charged payload or an open allowance."""
        tests, _, _ = resource_fixture()
        bodies = {
            "fft-native-resources": calibration_rows(),
            cases.CONTROLS: protocol_rows(None),
            cases.PREFIX + "0": protocol_rows(0),
            cases.PREFIX + "1": protocol_rows(1),
        }
        for rows in bodies.values():
            for row in rows:
                if "actual_core_payload_bytes" in row:
                    raw = row["actual_core_payload_bytes"] + 71
                    row.update(
                        source_C_provider_route="static_CRT_malloc_base_free_base",
                        observed_core_heap_bytes=raw,
                        core_backing_overhead_bytes=71,
                        attempts=1,
                        c_attempts=1,
                        events=[[raw, 0], [raw, 1]],
                    )
                if "whole_method" in row:
                    row.update(
                        observed_core_backing_overhead_envelope=213,
                        observed_core_payload_envelope=80,
                        unobserved_core_payload_envelope=0,
                        payload_envelope=80,
                        source_scratch_peak_candidate=100,
                    )
        self.assertEqual(
            records.reconcile_restoration_records(tests, framed(bodies), RESOURCE_EXECUTABLES)[
                "shard_count"
            ],
            2,
        )
        visible = bodies[cases.PREFIX + "0"][6]
        for key, value in (
            ("core_backing_overhead_bytes", 72),
            ("core_backing_overhead_bytes", 0),
            ("core_backing_overhead_bytes", True),
            ("observed_core_heap_bytes", visible["observed_core_heap_bytes"] - 1),
            ("source_C_provider_route", "static_debug_CRT_malloc_dbg_free_dbg"),
            ("c_attempts", 0),
            ("events", [[1, 0], [1, 1]]),
        ):
            changed = copy.deepcopy(visible)
            changed[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                capture.visibility(changed)
        for value in (0, 142, 214):
            changed = copy.deepcopy(bodies)
            changed[cases.PREFIX + "0"][10]["observed_core_backing_overhead_envelope"] = value
            with self.subTest(envelope=value), self.assertRaises(ValueError):
                records.reconcile_restoration_records(tests, framed(changed), RESOURCE_EXECUTABLES)

    def test_discovery_refuses_missing_duplicate_unknown_or_changed_commands(self) -> None:
        """Actual executable and complete contiguous registered role arguments are mandatory."""
        tests, _, _ = resource_fixture()
        bad = [
            tests[:-1],
            resource_fixture(Path("/foreign"))[0],
            [*tests, tests[-1]],
            [*tests, {"name": "restoration-native-unknown", "command": []}],
            [*tests, {"name": "fft-native-foreign", "command": []}],
        ]
        for command in (
            ["/build/foreign", "--shard", "1"],
            ["/build/de_restoration_resources", "--shard", "0"],
            ["/build/de_restoration_resources", "--shard", "1", "ignored"],
        ):
            changed = copy.deepcopy(tests)
            changed[-1]["command"] = command
            bad.append(changed)
        for value in bad:
            with self.subTest(value=value), self.assertRaises(ValueError):
                cases.resource_cases(value, RESOURCE_EXECUTABLES)

    def test_protocol_negative_controls(self) -> None:
        """No count, completion, healthy identity, ordinal, trace or controls shortcut qualifies."""
        tests, _, _ = resource_fixture()
        original = protocol_rows(1)
        variants = []
        for index, key, value in (
            (5, "shard_count", 3),
            (5, "shard_index", 0),
            (10, "attempts", 5),
            (11, "fail_n", 1),
            (11, "fail_n", 0),
            (11, "attained", 0),
            (11, "live_after", 1),
            (11, "observed_peak", 999),
        ):
            changed = copy.deepcopy(original)
            changed[index][key] = value
            variants.append(changed)
        changed = copy.deepcopy(original)
        changed[10]["events"].reverse()
        variants.append(changed)
        changed = copy.deepcopy(original)
        changed[6]["actual_core_payload_bytes"] += 1
        variants.append(changed)
        changed = copy.deepcopy(original)
        changed[10]["events"][:2] = [[11, 0], [11, 1]]
        changed[10]["observed_peak"] = 11
        variants.append(changed)
        for field, invalid in (
            ("mode", "foreign"),
            ("live_after", False),
            ("native_capacity", -1),
            ("unknown", 1),
            ("events", [[10, 3]]),
        ):
            changed = copy.deepcopy(original)
            changed[11][field] = invalid
            variants.append(changed)
        changed = copy.deepcopy(original)
        changed[10]["attempts"] = 1
        changed[10]["events"] = [[10, 0], [10, 1]]
        variants.append(changed)
        variants.extend(
            (
                original[:-1],
                original[:11] + original[12:],
                [*original, original[-1]],
                original[:10] + original[11:],
            )
        )
        for altered in variants:
            with self.subTest(value=altered), self.assertRaises(ValueError):
                records.reconcile_restoration_records(
                    tests,
                    framed(
                        {
                            "fft-native-resources": calibration_rows(),
                            cases.CONTROLS: protocol_rows(None),
                            cases.PREFIX + "0": protocol_rows(0),
                            cases.PREFIX + "1": altered,
                        }
                    ),
                    RESOURCE_EXECUTABLES,
                )

    def test_missing_or_duplicate_frames_and_control_attainment_refuse(self) -> None:
        """Raw framing and all five control extents and one/two/seven overlaps are mandatory."""
        tests, journal, _ = resource_fixture()
        for value in (
            journal.replace(b"<end of output>", b"unfinished"),
            journal + journal,
            journal + framed({"restoration-native-foreign": calibration_rows()}),
            journal.replace(b"Output:", b"Absent:", 1),
            journal.replace(
                b'{"restoration_resource_role":', b'{"shard_index":0,"restoration_resource_role":'
            ),
        ):
            with self.subTest(value=value[:60]), self.assertRaises(ValueError):
                records.reconcile_restoration_records(tests, value, RESOURCE_EXECUTABLES)
        for position in (6, 15, -1):
            controls = protocol_rows(None)
            del controls[position]
            with self.subTest(position=position), self.assertRaises(ValueError):
                records.reconcile_restoration_records(
                    tests,
                    framed(
                        {
                            "fft-native-resources": calibration_rows(),
                            cases.CONTROLS: controls,
                            cases.PREFIX + "0": protocol_rows(0),
                            cases.PREFIX + "1": protocol_rows(1),
                        }
                    ),
                    RESOURCE_EXECUTABLES,
                )

    def test_lossless_archives_not_junit_stdout_supply_rows(self) -> None:
        """Existing archive caps, digests, completeness and full raw bytes remain the authority."""
        tests, journal, junit = resource_fixture()
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            metadata = write_archives(directory, journal, junit)
            self.assertEqual(
                records.reconcile_restoration_records(
                    tests, capture.retained_bodies(directory)["journal"], RESOURCE_EXECUTABLES
                )["shard_count"],
                2,
            )
            for bound in ("ARCHIVE_LIMIT", "RAW_BYTES"):
                with patch.object(capture, bound, 1), self.assertRaises(ValueError):
                    capture.retained_bodies(directory)
            with patch.object(records, "ARCHIVE_LIMIT", 1), self.assertRaises(ValueError):
                records.row_pairs(b"{}")
            packed_path = directory / "journal.gz"
            retained = packed_path.read_bytes()
            packed_path.unlink()
            packed_path.symlink_to(directory / "junit.gz")
            with self.assertRaises(ValueError):
                capture.retained_bodies(directory)
            packed_path.unlink()
            packed_path.write_bytes(retained)
            metadata["artifacts"]["journal"]["raw_sha256"] = "0" * 64
            (directory / "native-output.json").write_text(json.dumps(metadata))
            with self.assertRaises(ValueError):
                capture.retained_bodies(directory)

    def test_compressed_snapshot_refuses_mixed_hash_decode_epoch(self) -> None:
        """A(hash), B(decode path), A(recheck) cannot qualify B under A's compressed receipt."""
        _, journal, junit = resource_fixture()
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            metadata = write_archives(directory, journal, junit)
            path = directory / "journal.gz"
            original = path.read_bytes()
            changed = journal.replace(b"137", b"138")
            metadata["artifacts"]["journal"]["raw_sha256"] = hashlib.sha256(changed).hexdigest()
            (directory / "native-output.json").write_text(json.dumps(metadata))
            decoder = gzip.GzipFile
            used = []

            def decode(*, fileobj: BinaryIO) -> gzip.GzipFile:
                used.append(fileobj.read())
                fileobj.seek(0)
                path.write_bytes(gzip.compress(changed, mtime=1))
                stream = decoder(fileobj=fileobj)
                path.write_bytes(original)
                return stream

            with (
                patch.object(gzip, "GzipFile", decode),
                self.assertRaisesRegex(ValueError, "Native full archive identity"),
            ):
                capture.retained_bodies(directory)
            self.assertEqual(used, [original])
            self.assertEqual(path.read_bytes(), original)
