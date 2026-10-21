# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Tiny synthetic parser/ownership controls do not claim actual MSVC qualification."""

from __future__ import annotations

import copy
import gzip
import hashlib
import io
import json
import tempfile
import time
import unittest
from pathlib import Path
from typing import Any, override
from unittest.mock import patch

from tools_path import prepare_tool_imports

import windows_resource_artifacts as artifacts
import windows_resource_execution as execution
import windows_resource_proof as proof
import windows_resource_records as records
from workflow_config import workflow
from workflow_windows_resources import require_windows_resource_proof

prepare_tool_imports()


def calibration_body() -> bytes:
    """Mirror exact four control rows and one truthful native contract result."""
    values = []
    for measured in (True, False):
        for family, size in (("c", 137), ("cpp", 211)):
            values.append(
                {
                    "calibration_family": family,
                    "measured": measured,
                    "allocated": True,
                    "acquired": size if measured else 0,
                    "backing_valid": True,
                    "attempts": int(measured),
                    "c_attempts": int(measured and family == "c"),
                    "peak": size if measured else 0,
                    "live": 0,
                }
            )
    values.append(
        {
            "release_reservation_protocol": True,
            "native_c_zero_errno_and_injected_refusal": True,
            "provider_exception_attainment": "unavailable",
        }
    )
    return b"\n".join(json.dumps(value, separators=(",", ":")).encode() for value in values)


def map_text(*, debug: bool = False) -> str:
    """Synthetic actual-format public rows exercise owner matching, not native linkage."""
    suffix, provider = ("dbg", "libucrtd") if debug else ("base", "libucrt")
    return "\n".join(
        (
            " 0001:00000000 malloc 0000000140001000 f allocation_interpose.cpp.obj",
            " 0001:00000010 free 0000000140001010 f allocation_interpose.cpp.obj",
            f" 0001:00000020 _malloc_{suffix} 0000000140001020 f {provider}:malloc.obj",
            f" 0001:00000030 _free_{suffix} 0000000140001030 f {provider}:free.obj",
        )
    )


class WindowsResourceProofTests(unittest.TestCase):
    """Negative controls preserve real-route admission and owned diagnostic behavior."""

    @override
    def setUp(self) -> None:
        """Give the mocked child owner a real isolated evidence directory."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        (directory / "debug").mkdir()
        patcher = patch.object(execution, "OUT", directory)
        patcher.start()
        self.addCleanup(patcher.stop)
        root_patcher = patch.object(execution, "ROOT", directory)
        root_patcher.start()
        self.addCleanup(root_patcher.stop)

    def test_exact_static_map_routes_and_wrong_provider_refusal(self) -> None:
        """Release cannot accidentally accept a similarly named Debug provider."""
        for debug in (False, True):
            self.assertEqual(len(artifacts.map_routes(map_text(debug=debug), debug=debug)), 4)
        for value in (
            map_text().replace("libucrt:", "libucrtd:"),
            map_text() + "\n" + map_text(),
            map_text().replace("allocation_interpose", "foreign"),
        ):
            with self.subTest(value=value), self.assertRaises(ValueError):
                artifacts.map_routes(value, debug=False)

    def test_full_calibrations_fail_for_missing_unobserved_or_zero_counter(self) -> None:
        """C137/CPP211 and unmeasured controls remain mandatory, including native contract."""
        body = calibration_body()
        self.assertEqual(len(records.calibrations(body)), 5)
        for changed in (
            body.replace(b'"acquired":137', b'"acquired":0'),
            body.split(b"\n", 1)[1],
            body.replace(
                b'"release_reservation_protocol":true', b'"release_reservation_protocol":false'
            ),
        ):
            with self.subTest(body=changed), self.assertRaises(ValueError):
                records.calibrations(changed)

    def test_retained_actual_case_parser_requires_both_passed_cases_and_all_rows(self) -> None:
        """Tiny synthetic gzip records exercise full raw provenance and missing/skipped refusal."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            journal = b"\n".join(
                str(index).encode()
                + b"/2 Testing: "
                + name.encode()
                + b"\n"
                + calibration_body()
                + b"\n<end of output>"
                for index, name in enumerate(records.CASES, 1)
            )
            junit = (
                "<testsuite>"
                + "".join('<testcase name="' + name + '" status="run"/>' for name in records.CASES)
                + "</testsuite>"
            ).encode()
            metadata: dict[str, Any] = {"errors": [], "artifacts": {}}
            for key, data in (("journal", journal), ("junit", junit)):
                path = directory / (key + ".gz")
                path.write_bytes(gzip.compress(data, mtime=0))
                metadata["artifacts"][key] = {
                    "archive": str(path),
                    "archive_sha256": artifacts.identity(path)["sha256"],
                    "raw_bytes": len(data),
                    "raw_sha256": hashlib.sha256(data).hexdigest(),
                    "status": "diagnostic_only_complete",
                }
            output = directory / "native-output.json"
            output.write_text(json.dumps(metadata))
            self.assertEqual(
                set(
                    records.native_records(
                        directory, [directory.stat().st_dev, directory.stat().st_ino]
                    )["cases"]
                ),
                set(records.CASES),
            )
            for bad in (b"<testsuite/>", junit.replace(b'status="run"', b'status="notrun"')):
                path = directory / "junit.gz"
                path.write_bytes(gzip.compress(bad, mtime=0))
                metadata["artifacts"]["junit"].update(
                    archive_sha256=artifacts.identity(path)["sha256"],
                    raw_bytes=len(bad),
                    raw_sha256=hashlib.sha256(bad).hexdigest(),
                )
                output.write_text(json.dumps(metadata))
                with self.assertRaises(ValueError):
                    records.native_records(
                        directory, [directory.stat().st_dev, directory.stat().st_ino]
                    )

    def test_map_archive_exact_roundtrip_and_exclusive_output(self) -> None:
        """Compression retains every byte and cannot overwrite an earlier map artifact."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "actual.map"
            data = map_text().encode()
            path.write_bytes(data)
            text, receipt = artifacts.archive_map(path, root)
            self.assertEqual(text, data)
            self.assertEqual(gzip.decompress(Path(receipt["gzip"]["path"]).read_bytes()), data)
            self.assertEqual(path.read_bytes(), data)
            with self.assertRaises(FileExistsError):
                artifacts.archive_map(path, root)

    def test_map_raw_compressed_and_symlink_bounds(self) -> None:
        """Admission and every compressed write are bounded; symlinks never become maps."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "actual.map"
            path.write_bytes(b"too large")
            with patch.object(artifacts, "MAP_BYTES", 1), self.assertRaises(ValueError):
                artifacts.archive_map(path, root)
            with patch.object(artifacts, "GZIP_BYTES", 1), self.assertRaises(ValueError):
                artifacts.MapWriter(io.BytesIO()).write(b"xx")
            link = root / "link.map"
            link.symlink_to(path)
            with self.assertRaises(ValueError):
                artifacts.archive_map(link, root)

    def test_fresh_release_selection_never_uses_newest_stale_directory(self) -> None:
        """The exact invocation adds one owner; absent or ambiguous new owners refuse."""
        saved = {"sources": {}, "head": "head", "release_before": ["old"]}
        with (
            patch.object(proof, "sources", return_value={}),
            patch("windows_resource_proof.platform.system", return_value="Windows"),
            patch.dict("windows_resource_proof.os.environ", {"GITHUB_SHA": "head"}),
        ):
            for directories in (["old"], ["old", "new", "foreign"]):
                with (
                    patch.object(
                        proof,
                        "release_directories",
                        return_value={name: [1, 2] for name in directories},
                    ),
                    self.assertRaises(ValueError),
                ):
                    proof.admitted_release(saved)
            with patch.object(
                proof, "release_directories", return_value={"old": [1, 2], "new": [3, 4]}
            ):
                self.assertEqual(proof.admitted_release(saved), {"path": "new", "identity": [3, 4]})

    def test_interruption_identity_survives_archive_failure(self) -> None:
        """The same control exception escapes after retention failure is attached."""
        original = KeyboardInterrupt("controlled")
        with (
            patch.object(execution, "snapshot_ctest_output", return_value={}),
            patch.object(execution, "bounded_capture", side_effect=original),
            patch.object(execution, "archive_ctest_output", side_effect=OSError("archive")),
            self.assertRaises(KeyboardInterrupt) as raised,
        ):
            execution.debug_cases(time.monotonic() + 100, {})
        self.assertIs(raised.exception, original)
        self.assertIn("archival also failed", original.__notes__[0])

    def test_nonzero_child_and_archive_failure_both_remain_recorded(self) -> None:
        """A failed archive never erases the actual nonzero CTest outcome."""
        record: dict[str, Any] = {}
        with (
            patch.object(execution, "snapshot_ctest_output", return_value={}),
            patch.object(execution, "bounded_capture", return_value=8),
            patch.object(execution, "archive_ctest_output", side_effect=OSError("archive")),
            self.assertRaises(OSError),
        ):
            execution.debug_cases(time.monotonic() + 100, record)
        self.assertEqual(record["exit_code"], 8)
        self.assertIn("archive", record["archive_error"])

    def test_debug_build_selects_only_two_real_executable_targets(self) -> None:
        """The configure step owns pinned dependencies without building every replay/test."""
        with (
            patch.object(execution, "run") as run,
            patch.object(execution, "compilation", return_value={}),
            patch.object(execution, "capture_maps", return_value=[]),
            patch.object(execution, "directives"),
        ):
            execution.debug_producer(time.monotonic() + 100, {})
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(commands), 3)
        self.assertIn("de_native-configure", commands[1])
        self.assertEqual(
            commands[2][4:7], ["de_fft_resources", "de_restoration_resources", "--parallel"]
        )
        self.assertNotIn("de_native", commands[2])

    def test_preexisting_debug_producer_is_refused_before_any_build(self) -> None:
        """The real freshness guard still refuses within this fixture's owned root."""
        (execution.ROOT / "out/dev").mkdir(parents=True)
        with (
            patch.object(execution, "run") as run,
            self.assertRaisesRegex(ValueError, "fresh private producer"),
        ):
            execution.debug_producer(time.monotonic() + 100, {})
        run.assert_not_called()

    def test_upload_policy_is_required_windows_only_and_exact(self) -> None:
        """No raw broad upload, missing artifact, conditional success or moved ordering passes."""
        path = Path(__file__).resolve().parents[2] / ".github/workflows/ci.yml"
        native = workflow(path)["jobs"]["native"]
        owner = next(
            step for step in native["steps"] if step.get("run") == "./tools/ci_windows.ps1"
        )
        require_windows_resource_proof(native, owner)
        index = next(
            i
            for i, step in enumerate(native["steps"])
            if step.get("name") == "Retain Windows CRT resource proof"
        )
        for section, key, value in (
            ("", "if", "success()"),
            ("with", "path", "out/**"),
            ("with", "retention-days", "1"),
            ("with", "if-no-files-found", "warn"),
        ):
            changed = copy.deepcopy(native)
            step = changed["steps"][index]
            (step[section] if section else step)[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                require_windows_resource_proof(
                    changed, changed["steps"][native["steps"].index(owner)]
                )
