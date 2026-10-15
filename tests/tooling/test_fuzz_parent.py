# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Campaign evidence nesting must not hide retained work from aggregate disk supervision."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import patch

from tools_path import ROOT

import fuzz_manifest
import fuzz_storage
import run_fuzzers

if TYPE_CHECKING:
    from collections.abc import Callable

    from process_execution import CapturePolicy


def growing_sibling(parent: Path, kind: str) -> Callable[..., int]:
    """Grow retained evidence only after the current run's initial check succeeds."""

    def engine(
        command: list[str],
        environment: dict[str, str],
        log: Path,
        timeout: float,
        policy: CapturePolicy,
    ) -> int:
        del command, environment, timeout
        old = parent / "old/finding"
        if kind == "bytes":
            name = "MAX_WORK_PARENT_BYTES"
            ceiling = fuzz_storage.occupied_bytes(parent) + 4096
        else:
            name = "MAX_WORK_ENTRIES"
            ceiling = len(list(parent.rglob("*"))) + 2
        log.write_bytes(b"retained diagnostics")
        with patch.object(fuzz_storage, name, ceiling):
            policy.monitor()
            old.write_bytes(b"old counterexample" * 4096)
            (old.parent / "additional").touch()
            artifact = log.parent / "artifacts/crash-input"
            artifact.write_bytes(b"current counterexample")
            policy.monitor()
        message = "Aggregate supervision must reject growth outside the fresh campaign"
        raise AssertionError(message)

    return engine


class FuzzParentTests(unittest.TestCase):
    """An explicit monitored parent owns both prior and newly nested campaign evidence."""

    def test_old_sibling_growth_stops_nested_engine_without_deleting_evidence(self) -> None:
        """Both byte and entry limits include retained sibling runs during execution."""
        for kind in ("bytes", "entries"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as raw:
                parent = Path(raw)
                old = parent / "old/finding"
                old.parent.mkdir()
                old.write_bytes(b"old counterexample")
                fuzz_storage.admit_work_parent(parent)
                work = parent / "campaign-new"
                work.mkdir()
                argv = [
                    str(ROOT / "tools/run_fuzzers.py"),
                    "--target",
                    fuzz_manifest.targets()[0].name,
                    "--binary",
                    sys.executable,
                    "--engine",
                    "libfuzzer",
                    "--seconds",
                    "2",
                    "--work",
                    str(parent),
                ]
                with (
                    patch.object(sys, "argv", argv),
                    patch.dict(os.environ, {"DE_FUZZ_RUN_ROOT": str(work)}),
                    patch(
                        "fuzz_execution.bounded_capture", side_effect=growing_sibling(parent, kind)
                    ),
                ):
                    self.assertEqual(run_fuzzers.main(), 1)
                directories = list(work.iterdir())
                self.assertEqual(len(directories), 1)
                directory = directories[0]
                report = json.loads((directory / "result.json").read_text())
                self.assertFalse(report["passed"])
                self.assertIn("disk stop threshold", report["error"])
                self.assertEqual(report["findings"], ["artifacts/crash-input"])
                self.assertEqual(old.read_bytes(), b"old counterexample" * 4096)
                self.assertEqual(
                    (directory / "artifacts/crash-input").read_bytes(), b"current counterexample"
                )
                self.assertTrue((directory / "corpus").is_dir())

    def test_standalone_declared_parent_remains_healthy(self) -> None:
        """A positive standalone run monitors its declared work and cleans only owned scratch."""
        with tempfile.TemporaryDirectory() as raw:
            parent = Path(raw)
            argv = [
                str(ROOT / "tools/run_fuzzers.py"),
                "--target",
                fuzz_manifest.targets()[0].name,
                "--binary",
                sys.executable,
                "--engine",
                "libfuzzer",
                "--seconds",
                "2",
                "--work",
                str(parent),
            ]

            def healthy(
                command: list[str],
                environment: dict[str, str],
                log: Path,
                timeout: float,
                policy: CapturePolicy,
            ) -> int:
                del command, environment, timeout
                policy.monitor()
                log.write_text("#1\tINITED cov: 1\nstat::number_of_executed_units: 42\n")
                return 0

            with (
                patch.object(sys, "argv", argv),
                patch.dict(os.environ),
                patch("fuzz_execution.bounded_capture", side_effect=healthy),
                patch("fuzz_execution.time.monotonic", side_effect=[0, 0, 3, 4]),
            ):
                os.environ.pop("DE_FUZZ_RUN_ROOT", None)
                self.assertEqual(run_fuzzers.main(), 0)
            directory = next(parent.iterdir())
            report = json.loads((directory / "result.json").read_text())
            self.assertTrue(report["passed"])
            self.assertFalse((directory / "corpus").exists())
