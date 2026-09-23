# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Negative controls for manifest ownership, corpus integrity and execution evidence."""

from __future__ import annotations

import copy
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from tools_path import ROOT

import fuzz_execution
import fuzz_manifest
import run_fuzz_campaign


class FuzzManifestTests(unittest.TestCase):
    """Invalid declarations and unbounded execution requests fail before launch."""

    def test_real_inventory_is_complete(self) -> None:
        """The same declarations account for every committed harness and corpus directory."""
        self.assertEqual(fuzz_manifest.inventory_errors(), [])

    def test_invalid_bounds_are_rejected(self) -> None:
        """Zero, negatives, booleans and excessive times/concurrency never mean unlimited."""
        for value in (0, -1, True, 1801):
            with self.subTest(value=value), self.assertRaises(fuzz_manifest.FuzzError):
                fuzz_manifest.positive(value, fuzz_manifest.MAX_SECONDS, "seconds")
        for value in (0, -1, True, 3):
            with self.subTest(value=value), self.assertRaises(fuzz_manifest.FuzzError):
                fuzz_manifest.campaign_timeout(8, 60, value)
        self.assertGreater(fuzz_manifest.campaign_timeout(8, 1800, 1), 9000)
        self.assertLess(fuzz_manifest.campaign_timeout(8, 1800, 2), 9000)

    def test_duplicate_keys_and_invalid_records_fail(self) -> None:
        """Duplicate ownership, arbitrary flags, paths and link syntax cannot enter CMake."""
        with self.assertRaises(fuzz_manifest.FuzzError):
            fuzz_manifest.unique_object([("a", 1), ("a", 2)])
        base = {"source": "probe.cpp", "links": ["de_core"], "max_len": 64}
        for change in (
            {"source": "../escape.cpp"},
            {"links": ["de_core", "de_core"]},
            {"links": ["${escape}"]},
            {"max_len": True},
            {"full_length": 1},
            {"libfuzzer_options": ["-runs=0"]},
        ):
            with self.subTest(change=change), self.assertRaises(fuzz_manifest.FuzzError):
                fuzz_manifest.target_record("probe", base | change)


class CorpusTests(unittest.TestCase):
    """Same-name inputs, prior run evidence and invalid source entries have explicit treatment."""

    def test_same_name_different_bytes_and_equal_bytes_preserve_origins(self) -> None:
        """Content-addressing prevents overwrites and retains provenance when deduplicating."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for kind, data in (("corpus", b"seed"), ("regressions", b"regression")):
                directory = root / "fuzz" / kind / "probe"
                directory.mkdir(parents=True)
                (directory / "same-name").write_bytes(data)
            target = fuzz_manifest.Target("probe", "probe.cpp", ("de_core",), 64)
            run = fuzz_execution.Run(target, Path(sys.executable), "libfuzzer", 1, root)
            first = fuzz_execution.new_directory(root / "work", "probe-")
            corpus = fuzz_execution.stage_corpus(run, first)
            self.assertEqual(
                {path.read_bytes() for path in corpus.iterdir()}, {b"seed", b"regression"}
            )
            (root / "fuzz/regressions/probe/same-name").write_bytes(b"seed")
            second = fuzz_execution.new_directory(root / "work", "probe-")
            self.assertNotEqual(first, second)
            self.assertEqual(len(list(fuzz_execution.stage_corpus(run, second).iterdir())), 1)
            index = json.loads((second / "corpus-index.json").read_text(encoding="utf-8"))
            self.assertEqual(len(index), 2)
            self.assertEqual(len(list(corpus.iterdir())), 2)

    def test_nonregular_and_oversized_inputs_fail(self) -> None:
        """A nested directory or oversized seed is not silently ignored or truncated."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "fuzz/corpus/probe"
            source.mkdir(parents=True)
            (root / "fuzz/regressions/probe").mkdir(parents=True)
            run = fuzz_execution.Run(
                fuzz_manifest.Target("probe", "probe.cpp", ("de_core",), 4),
                Path(sys.executable),
                "libfuzzer",
                1,
                root,
            )
            (source / "nested").mkdir()
            with self.assertRaises(fuzz_manifest.FuzzError):
                fuzz_execution.stage_corpus(run, fuzz_execution.new_directory(root, "nested-"))
            (source / "nested").rmdir()
            (source / "oversized").write_bytes(b"12345")
            with self.assertRaises(fuzz_manifest.FuzzError):
                fuzz_execution.stage_corpus(run, fuzz_execution.new_directory(root, "size-"))


class EvidenceTests(unittest.TestCase):
    """A successful process is necessary, but not sufficient, for a passing campaign."""

    def test_complete_reports_reject_missing_duplicate_failed_and_zero_work(self) -> None:
        """Fresh per-target evidence is reconciled against the expected target set."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "probe-1"
            directory.mkdir()
            report = {
                "target": "probe",
                "passed": True,
                "exit_code": 0,
                "executions": 1,
                "findings": [],
            }
            path = directory / "result.json"
            self.assertFalse(run_fuzz_campaign.complete_reports(root, {"probe"}))
            for change in (
                {"passed": False},
                {"executions": 0},
                {"executions": True},
                {"exit_code": 7},
                {"findings": ["crash"]},
            ):
                fuzz_execution.write_json(path, report | change)
                self.assertFalse(run_fuzz_campaign.complete_reports(root, {"probe"}))
            fuzz_execution.write_json(path, report)
            self.assertTrue(run_fuzz_campaign.complete_reports(root, {"probe"}))
            (root / "probe-2").mkdir()
            fuzz_execution.write_json(root / "probe-2/result.json", report)
            self.assertFalse(run_fuzz_campaign.complete_reports(root, {"probe"}))

    def test_early_exit_is_not_a_completed_campaign(self) -> None:
        """Native work statistics alone cannot justify a run that stopped before its budget."""
        self.assertFalse(fuzz_execution.completed(0, 42, [], 0.1, 60))
        self.assertFalse(fuzz_execution.completed(0, 0, [], 61, 60))
        self.assertFalse(fuzz_execution.completed(77, 42, ["crash"], 61, 60))
        self.assertTrue(fuzz_execution.completed(0, 42, [], 61, 60))

    def test_engine_statistics_require_nonempty_execution(self) -> None:
        """Native statistics, not exit zero or a made-up PASS line, establish actual work."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "artifacts").mkdir()
            run = fuzz_execution.Run(
                fuzz_manifest.targets()[0], Path(sys.executable), "libfuzzer", 1
            )
            (root / "engine.log").write_text("PASS\n", encoding="utf-8")
            self.assertEqual(fuzz_execution.engine_evidence(run, root), (0, []))
            (root / "engine.log").write_text(
                "stat::number_of_executed_units: 42\n", encoding="utf-8"
            )
            self.assertEqual(fuzz_execution.engine_evidence(run, root), (42, []))
            (root / "artifacts/crash").write_bytes(b"input")
            self.assertEqual(fuzz_execution.engine_evidence(run, root), (42, ["artifacts/crash"]))

    def test_watchdog_records_output_and_reaps_the_process(self) -> None:
        """The standalone runner is bounded even without an outer CTest timeout."""
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "engine.log"
            with self.assertRaises(subprocess.TimeoutExpired):
                fuzz_execution.bounded_process(
                    [
                        sys.executable,
                        "-c",
                        "import time; print('started', flush=True); time.sleep(60)",
                    ],
                    dict(os.environ),
                    log,
                    1,
                )
            self.assertIn("started", log.read_text(encoding="utf-8"))

    def test_registration_requires_every_declared_target_and_exact_budget(self) -> None:
        """Deleting, duplicating, disabling or reconfiguring a harness fails admission."""
        tests: list[dict[str, Any]] = [
            {
                "name": f"fuzz-{target.name}",
                "command": [
                    sys.executable,
                    str(ROOT / "tools/run_fuzzers.py"),
                    "--seconds",
                    "60",
                    "--target",
                    target.name,
                    "--engine",
                    "libfuzzer",
                    "--binary",
                    str(ROOT / "fuzz" / f"de_fuzz_{target.name}"),
                ],
                "properties": [{"name": "TIMEOUT", "value": fuzz_manifest.target_timeout(60)}],
            }
            for target in fuzz_manifest.targets()
        ]
        with (
            patch("run_fuzz_campaign.subprocess.run") as command,
            patch("run_fuzz_campaign.configured_engine", return_value="libfuzzer"),
        ):
            command.return_value = subprocess.CompletedProcess([], 0, json.dumps({"tests": tests}))
            self.assertEqual(
                run_fuzz_campaign.registration("ctest", ROOT, 60),
                {target.name for target in fuzz_manifest.targets()},
            )
            altered = copy.deepcopy(tests)
            altered[0]["properties"] = [{"name": "DISABLED", "value": True}]
            wrong_binary = copy.deepcopy(tests)
            wrong_binary[0]["command"][-1] = "/not/the/harness"
            wrong_engine = copy.deepcopy(tests)
            wrong_engine[0]["command"][-3] = "afl"
            wrong_timeout = copy.deepcopy(tests)
            wrong_timeout[0]["properties"] = [{"name": "TIMEOUT", "value": 1}]
            for records in (
                tests[:-1],
                [*tests, tests[0]],
                altered,
                wrong_binary,
                wrong_engine,
                wrong_timeout,
            ):
                command.return_value = subprocess.CompletedProcess(
                    [], 0, json.dumps({"tests": records})
                )
                with self.assertRaises(fuzz_manifest.FuzzError):
                    run_fuzz_campaign.registration("ctest", ROOT, 60)
            command.return_value = subprocess.CompletedProcess([], 0, json.dumps({"tests": tests}))
            with self.assertRaises(fuzz_manifest.FuzzError):
                run_fuzz_campaign.registration("ctest", ROOT, 1)
