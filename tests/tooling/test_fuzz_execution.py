# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent synthetic engine evidence rejects startup bypasses and incomplete findings."""

from __future__ import annotations

import io
import json
import os
import stat
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import patch

if TYPE_CHECKING:
    from collections.abc import Callable

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


class FuzzScratchTests(unittest.TestCase):
    """Completed input scratch is disposable; provenance and failure evidence are not."""

    def fixture_engine(
        self, command: list[str], env: dict[str, str], log: Path, timeout: int
    ) -> int:
        """Write explicitly synthetic positive work and queued bytes, not a native fuzz result."""
        del command, env, timeout
        log.write_text("stat::number_of_executed_units: 42\n")
        (log.parent / "corpus/generated").write_bytes(b"successful mutation")
        queue = log.parent / "afl/default/queue"
        (queue / ".state").mkdir(parents=True)
        (queue / "id_000001").write_bytes(b"successful queue input")
        (queue / ".state/deterministic_done").write_bytes(b"queue state")
        (queue.parent / "fuzzer_stats").write_text(
            "execs_done : 42\nsaved_crashes : 0\nsaved_hangs : 0\n"
        )
        return 0

    def test_completed_runs_remove_only_owned_scratch_after_engine_return(self) -> None:
        """Positive engine work retains the index/log/statistics and removes disposable inputs."""
        for engine in ("libfuzzer", "afl"):
            with self.subTest(engine=engine), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                run = fuzz_execution.Run(
                    fuzz_manifest.targets()[0], Path(sys.executable), engine, 1
                )
                with (
                    patch("fuzz_execution.shutil.which", return_value="/fixture/afl-fuzz"),
                    patch("fuzz_execution.bounded_process", side_effect=self.fixture_engine),
                    patch("fuzz_execution.time.monotonic", side_effect=[0, 0, 2, 3]),
                ):
                    directory = fuzz_execution.execute(run, work)
                report = json.loads((directory / "result.json").read_text())
                self.assertTrue(report["passed"])
                self.assertFalse((directory / "corpus").exists())
                self.assertTrue((directory / "corpus-index.json").is_file())
                self.assertIn("42", (directory / "engine.log").read_text())
                self.assertTrue((directory / "afl/default/fuzzer_stats").is_file())
                self.assertEqual((directory / "afl/default/queue").exists(), engine != "afl")

    def test_findings_and_cleanup_errors_cannot_pass(self) -> None:
        """A finding keeps exact scratch; cleanup failure remains visible with records intact."""
        for finding in (False, True):
            with self.subTest(finding=finding), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                run = fuzz_execution.Run(fuzz_manifest.targets()[0], Path(sys.executable), "afl", 1)

                def engine(
                    command: list[str],
                    env: dict[str, str],
                    log: Path,
                    timeout: int,
                    *,
                    finding_case: bool = finding,
                ) -> int:
                    result = self.fixture_engine(command, env, log, timeout)
                    if finding_case:
                        crashes = log.parent / "afl/default/crashes"
                        crashes.mkdir()
                        (crashes / "id_000000").write_bytes(b"exact failing input")
                    return result

                with (
                    patch("fuzz_execution.shutil.which", return_value="/fixture/afl-fuzz"),
                    patch("fuzz_execution.bounded_process", side_effect=engine),
                    patch("fuzz_execution.time.monotonic", side_effect=[0, 0, 2, 3]),
                    patch(
                        "fuzz_execution.remove_successful_scratch",
                        side_effect=PermissionError("owned scratch cleanup refused"),
                    ) as cleanup,
                ):
                    directory = fuzz_execution.execute(run, work)
                report = json.loads((directory / "result.json").read_text())
                self.assertFalse(report["passed"])
                self.assertTrue((directory / "corpus-index.json").is_file())
                self.assertEqual(
                    (directory / "corpus/generated").read_bytes(), b"successful mutation"
                )
                if finding:
                    cleanup.assert_not_called()
                    self.assertEqual(
                        (directory / "afl/default/crashes/id_000000").read_bytes(),
                        b"exact failing input",
                    )
                else:
                    self.assertIn("cleanup refused", report["error"])

    def test_all_ancestors_and_entries_are_checked_before_any_removal(self) -> None:
        """An unsafe second scratch tree cannot delete even the first corpus."""
        for mutation in ("linked-parent", "special-entry", "file-root"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directory = root / "owned"
                (directory / "corpus").mkdir(parents=True)
                (directory / "corpus/seed").write_bytes(b"keep on refusal")
                (directory / "afl").mkdir()
                if mutation == "linked-parent":
                    external = root / "external"
                    external.mkdir()
                    sentinel = external / "sentinel"
                    sentinel.write_bytes(b"outside owner")
                    (directory / "afl/default").symlink_to(external, target_is_directory=True)
                    with self.assertRaises(fuzz_manifest.FuzzError):
                        fuzz_execution.remove_successful_scratch(directory, "afl")
                    self.assertEqual(sentinel.read_bytes(), b"outside owner")
                elif mutation == "file-root":
                    queue = directory / "afl/default/queue"
                    queue.parent.mkdir()
                    queue.write_bytes(b"not a scratch directory")
                    with self.assertRaises(fuzz_manifest.FuzzError):
                        fuzz_execution.remove_successful_scratch(directory, "afl")
                    self.assertEqual(queue.read_bytes(), b"not a scratch directory")
                else:
                    special = directory / "afl/default/queue/.state/special"
                    special.parent.mkdir(parents=True)
                    special.write_bytes(b"synthetic special-entry identity")
                    original = Path.lstat

                    def unsupported(
                        path: Path,
                        *,
                        special_path: Path = special,
                        inspect: Callable[[Path], os.stat_result] = original,
                    ) -> os.stat_result:
                        observed = inspect(path)
                        if path == special_path:
                            fields = list(observed)
                            fields[0] = stat.S_IFIFO | 0o600
                            return os.stat_result(fields)
                        return observed

                    with (
                        patch.object(Path, "lstat", unsupported),
                        self.assertRaises(fuzz_manifest.FuzzError),
                    ):
                        fuzz_execution.remove_successful_scratch(directory, "afl")
                    self.assertTrue(special.is_file())
                self.assertEqual((directory / "corpus/seed").read_bytes(), b"keep on refusal")


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
