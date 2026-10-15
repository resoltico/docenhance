# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Calibration wall time cannot substitute for AFL's requested active-run exposure."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import patch

from tools_path import ROOT

import fuzz_execution
import fuzz_manifest
import run_fuzz_campaign

if TYPE_CHECKING:
    from collections.abc import Callable

    from process_execution import CapturePolicy


def fixture_engine(runtime: str | None, finding: str, exit_code: int) -> Callable[..., int]:
    """Return explicit synthetic native facts after a simulated thirty-second calibration."""

    def engine(
        command: list[str],
        environment: dict[str, str],
        log: Path,
        timeout: float,
        policy: CapturePolicy,
    ) -> int:
        del command, environment, timeout, policy
        log.write_text("synthetic calibration-only or complete exposure fixture\n")
        output = log.parent / "afl/default"
        queue = output / "queue"
        queue.mkdir(parents=True)
        (queue / "id_000000").write_bytes(b"retained queue")
        (log.parent / "corpus/generated").write_bytes(b"retained corpus")
        stats = "execs_done : 483\ncalibration_time : 30\nsaved_hangs : 0\n"
        stats += "saved_crashes : " + ("1\n" if finding == "counter" else "0\n")
        if runtime is not None:
            stats += f"run_time : {runtime}\n"
        (output / "fuzzer_stats").write_text(stats)
        if finding == "file":
            crashes = output / "crashes"
            crashes.mkdir()
            (crashes / "id_000000").write_bytes(b"exact counterexample")
        return exit_code

    return engine


class AFLExposureTests(unittest.TestCase):
    """Exercise the actual runner decision and preservation independently of native fuzzing."""

    def test_native_exposure_controls_cleanup_after_long_calibration(self) -> None:
        """Missing/invalid/short native time fails despite long wall time and positive counts."""
        for runtime in (None, "invalid", "True", "-1", "2.0", "9" * 5000, "0", "1", "2", "3"):
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory() as raw:
                parent = Path(raw)
                old = parent / "prior-evidence"
                old.write_bytes(b"prior evidence")
                run = fuzz_execution.Run(
                    fuzz_manifest.targets()[0], Path(sys.executable), "afl", 2, ROOT
                )
                with (
                    patch("fuzz_execution.shutil.which", return_value="/fixture/afl-fuzz"),
                    patch(
                        "fuzz_execution.bounded_capture", side_effect=fixture_engine(runtime, "", 0)
                    ),
                    patch("fuzz_execution.time.monotonic", side_effect=[0, 0, 33, 34]),
                ):
                    directory = fuzz_execution.execute(run, parent, disk_parent=parent)
                report = json.loads((directory / "result.json").read_text())
                passed = runtime in {"2", "3"}
                self.assertEqual(report["passed"], passed)
                self.assertEqual(report["executions"], 483)
                self.assertEqual(report["exit_code"], 0)
                self.assertEqual(report["engine_elapsed_seconds"], 33)
                self.assertEqual(
                    report["afl_run_time_seconds"],
                    int(runtime) if runtime in {"0", "1", "2", "3"} else None,
                )
                self.assertEqual((directory / "corpus").exists(), not passed)
                self.assertEqual((directory / "afl/default/queue").exists(), not passed)
                self.assertEqual(old.read_bytes(), b"prior evidence")
                self.assertIn(
                    "calibration_time : 30", (directory / "afl/default/fuzzer_stats").read_text()
                )
                self.assertIn("synthetic", (directory / "engine.log").read_text())
                if not passed:
                    self.assertIn("AFL native run_time", report["error"])
                    self.assertEqual(
                        (directory / "corpus/generated").read_bytes(), b"retained corpus"
                    )

    def test_findings_and_real_error_precedence_survive_exposure_refusal(self) -> None:
        """Saved input and counter-only findings retain scratch, counts and real exit errors."""
        for runtime, finding, exit_code in (
            ("0", "file", 0),
            (None, "counter", 0),
            ("0", "counter", 77),
            ("9" * 5000, "counter", 77),
            ("2", "file", 0),
            ("2", "counter", 0),
        ):
            with (
                self.subTest(runtime=runtime, finding=finding, exit_code=exit_code),
                tempfile.TemporaryDirectory() as raw,
            ):
                parent = Path(raw)
                run = fuzz_execution.Run(fuzz_manifest.targets()[0], Path(sys.executable), "afl", 2)
                with (
                    patch("fuzz_execution.shutil.which", return_value="/fixture/afl-fuzz"),
                    patch(
                        "fuzz_execution.bounded_capture",
                        side_effect=fixture_engine(runtime, finding, exit_code),
                    ),
                    patch("fuzz_execution.time.monotonic", side_effect=[0, 0, 33, 34]),
                    patch("fuzz_execution.fuzz_storage.remove_successful_scratch") as cleanup,
                ):
                    directory = fuzz_execution.execute(run, parent, disk_parent=parent)
                cleanup.assert_not_called()
                report = json.loads((directory / "result.json").read_text())
                self.assertFalse(report["passed"])
                self.assertEqual(report["executions"], 483)
                self.assertTrue((directory / "afl/default/queue/id_000000").is_file())
                expected = (
                    "afl/default/crashes/id_000000"
                    if finding == "file"
                    else "afl/default/fuzzer_stats"
                )
                self.assertEqual(report["findings"], [expected])
                if finding == "file":
                    self.assertEqual((directory / expected).read_bytes(), b"exact counterexample")
                if exit_code:
                    self.assertIn("77", report["error"])
                    self.assertIn("AFL native run_time", report["statistics_error"])

    def test_coordinator_requires_type_exact_native_exposure(self) -> None:
        """A passing report cannot replace native exposure with calibration or Boolean facts."""
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            directory = root / "probe"
            directory.mkdir()
            binary = root / "fuzz/de_fuzz_probe"
            binary.parent.mkdir()
            binary.write_bytes(b"synthetic coordinator executable")
            (root / "CMakeCache.txt").write_text("DE_FUZZ_ENGINE:STRING=afl\n")
            report = {
                "engine": "afl",
                "seconds": 2,
                "engine_elapsed_seconds": 33,
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "manifest_sha256": hashlib.sha256(
                    (ROOT / "fuzz/targets.json").read_bytes()
                ).hexdigest(),
                "target": "probe",
                "passed": True,
                "exit_code": 0,
                "executions": 483,
                "findings": [],
            }
            requested_seconds = 2
            for runtime in (None, True, -1, 0, 1, 2.0, "2", 2, 3):
                with self.subTest(runtime=runtime):
                    actual = dict(report)
                    if runtime is not None:
                        actual["afl_run_time_seconds"] = runtime
                    fuzz_execution.write_json(directory / "result.json", actual)
                    expected = type(runtime) is int and runtime >= requested_seconds
                    self.assertEqual(
                        run_fuzz_campaign.complete_reports(root, {"probe"}, root, 2), expected
                    )

            for invalid in (True, 1.0):
                with self.subTest(one_second_native_fact=invalid):
                    actual = dict(report, seconds=1, afl_run_time_seconds=invalid)
                    fuzz_execution.write_json(directory / "result.json", actual)
                    self.assertFalse(run_fuzz_campaign.complete_reports(root, {"probe"}, root, 1))
