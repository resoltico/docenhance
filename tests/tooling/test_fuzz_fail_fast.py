# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Saved counterexamples stop mutation without losing findings or claiming completion."""

from __future__ import annotations

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

if TYPE_CHECKING:
    from collections.abc import Callable

    from process_execution import CapturePolicy


def save_finding(relative: str) -> Callable[..., int]:
    """Construct a fixture that checks ordinary startup, then saves a counterexample."""

    def finding(
        command: list[str],
        environment: dict[str, str],
        log: Path,
        timeout: float,
        policy: CapturePolicy,
    ) -> int:
        del command, environment, timeout
        policy.monitor()
        artifact = log.parent / relative
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_bytes(b"exact counterexample")
        log.write_bytes(b"retained engine diagnostic")
        policy.monitor()
        message = "Saved findings must stop the owned engine"
        raise AssertionError(message)

    return finding


class FuzzFailFastTests(unittest.TestCase):
    """Synthetic monitors exercise both engines without running a native campaign."""

    def test_saved_findings_abort_and_preserve_evidence(self) -> None:
        """A crash or hang fails before the time budget; no success cleanup occurs."""
        for engine, relative in (
            ("afl", "afl/default/crashes/id:000000"),
            ("afl", "afl/default/hangs/id:000000"),
            ("libfuzzer", "artifacts/crash-input"),
        ):
            with (
                self.subTest(engine=engine, relative=relative),
                tempfile.TemporaryDirectory() as tmp,
            ):
                work = Path(tmp)
                run = fuzz_execution.Run(
                    fuzz_manifest.targets()[0], Path(sys.executable), engine, 1, ROOT
                )

                with (
                    patch("fuzz_execution.shutil.which", return_value="/fixture/afl-fuzz"),
                    patch("fuzz_execution.bounded_capture", side_effect=save_finding(relative)),
                    patch("fuzz_storage.remove_successful_scratch") as cleanup,
                ):
                    directory = fuzz_execution.execute(run, work, disk_parent=work)
                cleanup.assert_not_called()
                report = json.loads((directory / "result.json").read_text())
                self.assertFalse(report["passed"])
                self.assertIsNone(report["exit_code"])
                self.assertEqual(report["findings"], [relative])
                self.assertIn("Saved fuzz finding", report["error"])
                self.assertEqual((directory / relative).read_bytes(), b"exact counterexample")
                self.assertTrue((directory / "corpus").is_dir())
                self.assertEqual(
                    (directory / "engine.log").read_bytes(), b"retained engine diagnostic"
                )
