# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Native initialization counters distinguish libFuzzer seed work from later callbacks."""

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


def fixture_engine(
    markers: str, final: int, exit_code: int, *, finding: bool
) -> Callable[..., int]:
    """Emit synthetic retained diagnostics without changing any native engine control."""

    def engine(
        command: list[str],
        environment: dict[str, str],
        log: Path,
        timeout: float,
        policy: CapturePolicy,
    ) -> int:
        del command, environment, timeout, policy
        log.write_text(markers + f"stat::number_of_executed_units: {final}\n")
        (log.parent / "corpus/generated").write_bytes(b"retained input")
        if finding:
            (log.parent / "artifacts/crash-input").write_bytes(b"exact finding")
        return exit_code

    return engine


class LibFuzzerExposureTests(unittest.TestCase):
    """Real execute-path controls reject seed-only work after arbitrarily long process time."""

    def test_post_initialization_callbacks_control_cleanup(self) -> None:
        """Missing, malformed, negative, duplicate and non-increasing markers fail closed."""
        cases = [
            ("", 100, None, False),
            ("[engine output omitted: 100 bytes]\n", 100, None, False),
            ("#bad\tINITED cov: 1\n", 100, None, False),
            ("#" + "9" * 5000 + "\tINITED cov: 1\n", 100, None, False),
            ("#-1\tINITED cov: 1\n", 100, None, False),
            ("#1.0\tINITED cov: 1\n", 100, None, False),
            ("#\tINITED cov: 1\n", 100, None, False),
            ("#1\tINITED cov: 1\n#1\tINITED cov: 1\n", 100, None, False),
            ("#1\tINITED cov: 1\n#bad\tINITED cov: 1\n", 100, None, False),
            ("#484\tINITED cov: 1\n", 483, 484, False),
            ("#484\tINITED cov: 1\n", 484, 484, False),
            ("#484\tINITED cov: 1\n", 485, 484, True),
            ("#484\tINITED cov: 1\n[engine output omitted: 100 bytes]\n", 485, 484, True),
            ("#0\tINITED cov: 1\n", 1, 0, True),
        ]
        for markers, final, initialized, passed in cases:
            with self.subTest(markers=markers, final=final), tempfile.TemporaryDirectory() as raw:
                parent = Path(raw)
                old = parent / "prior-evidence"
                old.write_bytes(b"prior evidence")
                run = fuzz_execution.Run(
                    fuzz_manifest.targets()[0], Path(sys.executable), "libfuzzer", 2
                )
                with (
                    patch(
                        "fuzz_execution.bounded_capture",
                        side_effect=fixture_engine(markers, final, 0, finding=False),
                    ),
                    patch("fuzz_execution.time.monotonic", side_effect=[0, 0, 33, 34]),
                ):
                    directory = fuzz_execution.execute(run, parent, disk_parent=parent)
                report = json.loads((directory / "result.json").read_text())
                self.assertEqual(report["passed"], passed)
                self.assertEqual(report["executions"], final)
                self.assertEqual(report["libfuzzer_inited_executions"], initialized)
                self.assertEqual(report["engine_elapsed_seconds"], 33)
                self.assertEqual((directory / "corpus").exists(), not passed)
                self.assertEqual(old.read_bytes(), b"prior evidence")
                self.assertEqual(
                    (directory / "engine.log").read_text(),
                    markers + f"stat::number_of_executed_units: {final}\n",
                )
                if not passed:
                    self.assertIn("INITED", report["error"])
                    self.assertEqual(
                        (directory / "corpus/generated").read_bytes(), b"retained input"
                    )

    def test_marker_refusal_preserves_findings_and_exit_error(self) -> None:
        """A missing marker retains exact findings without replacing a real engine failure."""
        for markers, exit_code in (
            ("", 77),
            ("#" + "9" * 5000 + "\tINITED cov: 1\n", 77),
            ("#1\tINITED cov: 1\n", 0),
        ):
            with self.subTest(exit_code=exit_code), tempfile.TemporaryDirectory() as raw:
                parent = Path(raw)
                run = fuzz_execution.Run(
                    fuzz_manifest.targets()[0], Path(sys.executable), "libfuzzer", 2
                )
                with (
                    patch(
                        "fuzz_execution.bounded_capture",
                        side_effect=fixture_engine(markers, 42, exit_code, finding=True),
                    ),
                    patch("fuzz_execution.time.monotonic", side_effect=[0, 0, 33, 34]),
                    patch("fuzz_execution.fuzz_storage.remove_successful_scratch") as cleanup,
                ):
                    directory = fuzz_execution.execute(run, parent, disk_parent=parent)
                cleanup.assert_not_called()
                report = json.loads((directory / "result.json").read_text())
                self.assertFalse(report["passed"])
                self.assertEqual(report["executions"], 42)
                self.assertEqual(report["findings"], ["artifacts/crash-input"])
                self.assertEqual(
                    (directory / "artifacts/crash-input").read_bytes(), b"exact finding"
                )
                self.assertTrue((directory / "corpus/generated").is_file())
                if exit_code:
                    self.assertIn("77", report["error"])
                    self.assertIn("INITED", report["statistics_error"])

    def test_coordinator_rejects_untyped_or_seed_only_counts(self) -> None:
        """Only an actual nonnegative integer and a larger final count establish post-init work."""
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            directory = root / "probe"
            directory.mkdir()
            binary = root / "fuzz/de_fuzz_probe"
            binary.parent.mkdir()
            binary.write_bytes(b"synthetic coordinator executable")
            (root / "CMakeCache.txt").write_text("DE_FUZZ_ENGINE:STRING=libfuzzer\n")
            report = {
                "engine": "libfuzzer",
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
            final_executions = 483
            for initialized in (None, True, -1, 1.0, "1", 483, 484, 0, 482):
                with self.subTest(initialized=initialized):
                    actual = dict(report)
                    if initialized is not None:
                        actual["libfuzzer_inited_executions"] = initialized
                    fuzz_execution.write_json(directory / "result.json", actual)
                    expected = type(initialized) is int and 0 <= initialized < final_executions
                    self.assertEqual(
                        run_fuzz_campaign.complete_reports(root, {"probe"}, root, 2), expected
                    )
