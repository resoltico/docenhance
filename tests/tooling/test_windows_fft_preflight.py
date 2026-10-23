# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Early actual-target admission and evidence controls; no native build or runtime claims."""

from __future__ import annotations

import json
import shutil
import tempfile
import time
import unittest
from pathlib import Path
from typing import TYPE_CHECKING, Any, override
from unittest.mock import patch

from tools_path import prepare_tool_imports

from restoration_resource_fixture import calibration_rows, framed

import windows_resource_execution as execution
import windows_resource_proof as proof

prepare_tool_imports()

if TYPE_CHECKING:
    from native_history import NativeHistory


class WindowsFftPreflightTests(unittest.TestCase):
    """The early case refuses mismatched commands, adverse evidence, drift and reruns."""

    @override
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "out/release/app"
        self.output = self.root / "out/windows-resource-proof"
        self.binary = self.build / "tests/de_fft_resources.exe"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"executable identity fixture only")
        self.output.mkdir()
        self.manifest = self.build / "native-tests.json"
        self.manifest.write_text(
            json.dumps({"resource_binaries": {"de_fft_resources": str(self.binary)}})
        )
        self.selected = [
            {
                "name": "fft-native-resources",
                "command": [str(self.binary)],
                "properties": [{"name": "WORKING_DIRECTORY", "value": str(self.binary.parent)}],
            }
        ]
        self.journal = framed({"fft-native-resources": calibration_rows()})
        for module in (execution, proof):
            for name, value in (("ROOT", self.root), ("OUT", self.output)):
                patcher = patch.object(module, name, value)
                patcher.start()
                self.addCleanup(patcher.stop)

    def child(self, command: list[str], history: NativeHistory, count: int) -> int:
        """Stand in for the already tested bounded/archive owner with literal one-case JUnit."""
        build, directory, deadline = history.build, history.directory, history.deadline
        if directory is None:
            self.fail("owned context did not create its current directory")
        self.assertEqual(build, self.build)
        self.assertEqual(count, 1)
        self.assertGreater(deadline, time.monotonic())
        self.assertEqual(command[command.index("-R") + 1], "^fft-native-resources$")
        self.assertEqual(command[command.index("--timeout") + 1], "90")
        (directory / "ctest.xml").write_text(
            '<testsuite tests="1" failures="0" errors="0" skipped="0" disabled="0">'
            '<testcase name="fft-native-resources" status="run"/></testsuite>'
        )
        (directory / "native-output.json").write_text("{}")
        return 0

    def test_one_actual_command_full_junit_calibrations_and_identity_pass(self) -> None:
        """The fixed one-case registered route uses the existing owned execution owner once."""
        record: dict[str, Any] = {}
        deadline = time.monotonic() + 120
        with (
            patch.object(execution, "discovery", return_value=self.selected),
            patch.object(execution, "_execute_owned", side_effect=self.child) as child,
            patch.object(execution, "retained_bodies", return_value={"journal": self.journal}),
        ):
            execution.release_fft_preflight(deadline, record)
        self.assertEqual(child.call_count, 1)
        self.assertEqual(child.call_args.args[1].deadline, deadline)
        self.assertEqual(record["exit_code"], 0)
        self.assertIn("evidence", record)
        self.assertFalse((self.build / "native-preflight").exists())

    def test_missing_duplicate_extra_argument_or_foreign_registered_command_refuses(self) -> None:
        """No selected case or unowned executable is an admission failure before native effects."""
        for candidate in (
            [],
            self.selected * 2,
            [{"name": "foreign", "command": [str(self.binary)]}],
            [{"name": "fft-native-resources", "command": [str(self.binary), "ignored"]}],
            [{"name": "fft-native-resources", "command": ["/foreign/fft.exe"]}],
        ):
            selected = [dict(test, properties=self.selected[0]["properties"]) for test in candidate]
            with (
                self.subTest(selected=selected),
                patch.object(execution, "discovery", return_value=selected),
                patch.object(execution, "_execute_owned") as child,
                self.assertRaises(ValueError),
            ):
                execution.release_fft_preflight(time.monotonic() + 120, {})
            child.assert_not_called()

    def test_nonzero_or_timeout_retains_original_adverse_outcome(self) -> None:
        """An early runtime failure never becomes a successful calibration."""
        for failure in (8, TimeoutError("actual bounded child expired")):
            directory = self.output / "release-preflight"
            if directory.exists():
                shutil.rmtree(directory)
            record: dict[str, Any] = {}
            with (
                patch.object(execution, "discovery", return_value=self.selected),
                patch.object(
                    execution,
                    "_execute_owned",
                    side_effect=failure if isinstance(failure, BaseException) else None,
                    return_value=failure,
                ) as child,
                patch.object(execution, "retained_bodies") as retained,
                self.assertRaises((ValueError, TimeoutError)),
            ):
                execution.release_fft_preflight(time.monotonic() + 120, record)
            self.assertEqual(child.call_count, 1)
            retained.assert_not_called()
            self.assertTrue(directory.is_dir())

    def test_changed_binary_or_missing_calibration_refuses(self) -> None:
        """A healthy JUnit alone cannot replace exact producer bytes and real C/C++ controls."""

        def changed(command: list[str], history: NativeHistory, count: int) -> int:
            result = self.child(command, history, count)
            self.binary.write_bytes(b"different executable epoch")
            return result

        with (
            patch.object(execution, "discovery", return_value=self.selected),
            patch.object(execution, "_execute_owned", side_effect=changed),
            patch.object(execution, "retained_bodies", return_value={"journal": self.journal}),
            self.assertRaisesRegex(ValueError, "producer changed"),
        ):
            execution.release_fft_preflight(time.monotonic() + 120, {})

    def test_missing_actual_calibration_refuses_healthy_junit(self) -> None:
        """An empty raw body cannot be substituted for real calibration rows."""
        with (
            patch.object(execution, "discovery", return_value=self.selected),
            patch.object(execution, "_execute_owned", side_effect=self.child),
            patch.object(
                execution,
                "retained_bodies",
                return_value={"journal": framed({"fft-native-resources": []})},
            ),
            self.assertRaisesRegex(ValueError, "CRT contract rows differ"),
        ):
            execution.release_fft_preflight(time.monotonic() + 120, {})

    def test_snapshot_admission_failure_and_report_are_once_only(self) -> None:
        """Changed invocation refuses before runtime; failure metadata prevents retry."""
        saved = {"head": "a" * 40, "sources": {}}
        (self.output / "snapshot.json").write_text(json.dumps(saved))
        with (
            patch.object(proof, "sources", return_value={}),
            patch("windows_resource_proof.platform.system", return_value="Windows"),
            patch.dict("os.environ", {"GITHUB_SHA": "b" * 40}),
            patch.object(proof, "release_fft_preflight") as child,
            self.assertRaisesRegex(ValueError, "invocation differs"),
        ):
            proof.preflight()
        child.assert_not_called()
        record = json.loads((self.output / "preflight.json").read_text())
        self.assertEqual(record["status"], "incomplete")
        self.assertIn("invocation differs", record["error"])
        self.assertEqual(record["total_seconds"], 120)
        with self.assertRaisesRegex(ValueError, "already attempted"):
            proof.preflight()

    def test_final_source_observation_cannot_finish_after_original_deadline(self) -> None:
        """A source rehash consuming the final budget cannot mint a late passing result."""
        saved = {"head": "a" * 40, "sources": {}}
        (self.output / "snapshot.json").write_text(json.dumps(saved))
        clock = [0.0]
        calls: list[str] = []

        def observation(_saved: dict[str, Any]) -> None:
            if calls:
                clock[0] = 121.0
            calls.append("source")

        with (
            patch("windows_resource_proof.time.monotonic", side_effect=lambda: clock[0]),
            patch.object(proof, "current_invocation", side_effect=observation),
            patch.object(proof, "release_fft_preflight") as child,
            self.assertRaisesRegex(ValueError, "original120second deadline"),
        ):
            proof.preflight()
        self.assertEqual(child.call_args.args[0], 120.0)
        self.assertEqual(
            json.loads((self.output / "preflight.json").read_text())["status"], "incomplete"
        )

    def test_runtime_failure_survives_metadata_failure(self) -> None:
        """Retention errors keep the original runtime failure and never trigger another run."""
        saved = {"head": "a" * 40, "sources": {}}
        (self.output / "snapshot.json").write_text(json.dumps(saved))
        primary = TimeoutError("early actual child failed")
        with (
            patch.object(proof, "current_invocation"),
            patch.object(proof, "release_fft_preflight", side_effect=primary) as child,
            patch.object(proof, "write_json", side_effect=OSError("report unavailable")),
            self.assertRaises(TimeoutError) as raised,
        ):
            proof.preflight()
        self.assertIs(raised.exception, primary)
        self.assertEqual(child.call_count, 1)
        self.assertIn("metadata also failed", primary.__notes__[0])
