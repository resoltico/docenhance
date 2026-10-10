# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent synthetic engine evidence rejects startup bypasses and incomplete findings."""

from __future__ import annotations

import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import fuzz_execution
import fuzz_manifest

sys.path.insert(0, str(ROOT / "tests/product_failures"))
from failure_audit import Audit


class FuzzExecutionTests(unittest.TestCase):
    """Engine fixtures test rejection/reporting, never claim native fuzzing execution."""

    def run_spec(self) -> fuzz_execution.Run:
        """Use a real target declaration with an explicitly synthetic executable identity."""
        return fuzz_execution.Run(fuzz_manifest.targets()[0], Path(sys.executable), "afl", 1)

    def test_afl_environment_cannot_skip_seed_failures_or_change_engine(self) -> None:
        """Inherited bypasses disappear while owned failure controls override prior values."""
        injected = {
            "AFL_IGNORE_SEED_PROBLEMS": "1",
            "AFL_NO_STARTUP_CALIBRATION": "1",
            "AFL_CUSTOM_MUTATOR_ONLY": "1",
            "AFL_CUSTOM_MUTATOR_LIBRARY": "/unreviewed/plugin",
            "AFL_EXIT_ON_SEED_ISSUES": "0",
            "AFL_CRASHING_SEEDS_AS_NEW_CRASH": "0",
            "AFL_SKIP_CRASHES": "1",
            "ASAN_OPTIONS": "halt_on_error=0",
            "DE_ENV_TEST": "ordinary environment preserved",
        }
        with patch.dict(os.environ, injected):
            environment = fuzz_execution.environment(self.run_spec())
        owned = {key: value for key, value in environment.items() if key.startswith("AFL_")}
        self.assertEqual(
            owned,
            {
                "AFL_NO_UI": "1",
                "AFL_SKIP_CPUFREQ": "1",
                "AFL_EXIT_ON_SEED_ISSUES": "1",
            },
        )
        self.assertEqual(environment["DE_ENV_TEST"], "ordinary environment preserved")
        self.assertIn("abort_on_error=1", environment["ASAN_OPTIONS"])
        self.assertNotIn("halt_on_error=0", environment["ASAN_OPTIONS"])

    def test_afl_crashes_hangs_and_statistics_counters_reject_completion(self) -> None:
        """Actual finding files and saved-finding counts independently veto positive work."""
        for kind in ("crashes", "hangs"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                output = root / "afl/default"
                (output / kind).mkdir(parents=True)
                stats = output / "fuzzer_stats"
                stats.write_text("execs_done : 42\nsaved_crashes : 0\nsaved_hangs : 0\n")
                executions, findings = fuzz_execution.engine_evidence(self.run_spec(), root)
                self.assertTrue(fuzz_execution.completed(0, executions, findings, 2, 1))
                (output / kind / "id_000000").write_bytes(b"retained failing input")
                executions, findings = fuzz_execution.engine_evidence(self.run_spec(), root)
                self.assertEqual(findings, [f"afl/default/{kind}/id_000000"])
                self.assertFalse(fuzz_execution.completed(0, executions, findings, 2, 1))
                (output / kind / "id_000000").unlink()
                stats.write_text(f"execs_done : 42\nsaved_{kind} : 1\n")
                executions, findings = fuzz_execution.engine_evidence(self.run_spec(), root)
                self.assertEqual(findings, ["afl/default/fuzzer_stats"])
                self.assertFalse(fuzz_execution.completed(0, executions, findings, 2, 1))

    def test_afl_nonzero_and_zero_execution_cannot_pass(self) -> None:
        """A clean-looking statistics file does not excuse engine refusal or absent work."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "afl/default"
            output.mkdir(parents=True)
            (output / "fuzzer_stats").write_text("execs_done : 0\n")
            executions, findings = fuzz_execution.engine_evidence(self.run_spec(), root)
            self.assertFalse(fuzz_execution.completed(0, executions, findings, 2, 1))
            (output / "fuzzer_stats").write_text("execs_done : 42\n")
            executions, findings = fuzz_execution.engine_evidence(self.run_spec(), root)
            self.assertFalse(fuzz_execution.completed(1, executions, findings, 2, 1))

    def test_missing_statistics_preserve_engine_exit_and_finding_files(self) -> None:
        """A startup refusal cannot be hidden behind an absent statistics file."""
        for exit_code in (0, 1):
            with self.subTest(exit_code=exit_code), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)

                def refused(
                    command: list[str],
                    env: dict[str, str],
                    log: Path,
                    timeout: int,
                    *,
                    result_code: int = exit_code,
                ) -> int:
                    del command, env, timeout
                    log.write_text("synthetic startup refusal\n")
                    crashes = log.parent / "afl/default/crashes"
                    crashes.mkdir(parents=True)
                    (crashes / "id_000000").write_bytes(b"synthetic finding")
                    return result_code

                with (
                    patch("fuzz_execution.shutil.which", return_value="/fixture/afl-fuzz"),
                    patch("fuzz_execution.bounded_process", side_effect=refused),
                ):
                    directory = fuzz_execution.execute(self.run_spec(), work)
                report = json.loads((directory / "result.json").read_text())
                self.assertFalse(report["passed"])
                self.assertEqual(report["exit_code"], exit_code)
                self.assertEqual(report["executions"], 0)
                self.assertEqual(report["findings"], ["afl/default/crashes/id_000000"])
                self.assertIn("engine.log", report["error"])
                if exit_code:
                    self.assertIn("exited with 1", report["error"])
                    self.assertIn("fuzzer_stats", report["statistics_error"])
                self.assertEqual(
                    (directory / "engine.log").read_text(), "synthetic startup refusal\n"
                )


class FailureAuditIdentityTests(unittest.TestCase):
    """Independent byte changes cannot produce a passing binary-bound audit."""

    def test_changed_or_missing_executable_fails_even_with_passing_observations(self) -> None:
        """Preserved identity passes; changed and removed executable identities fail."""
        for mutation in ("unchanged", "changed", "removed"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                executable = root / "executable-identity-fixture"
                executable.write_bytes(b"original identity; no executable is run")
                audit = Audit(executable, root / "evidence")
                audit.check(
                    "independent-passing-observation", condition=True, detail="fixture observation"
                )
                if mutation == "changed":
                    executable.write_bytes(b"different executable identity")
                elif mutation == "removed":
                    executable.unlink()
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(audit.write_results(), mutation == "unchanged")
                report = json.loads((audit.evidence / "results.json").read_text())
                self.assertEqual(report["executable_unchanged"], mutation == "unchanged")
                self.assertTrue(all(case["passed"] for case in report["cases"]))
