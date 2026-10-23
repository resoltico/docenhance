# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual Debug workspace monitors with tiny files and a mocked owned native child boundary."""

from __future__ import annotations

import tempfile
import time
import unittest
from pathlib import Path
from typing import TYPE_CHECKING, Any, override
from unittest.mock import patch

from tools_path import prepare_tool_imports

from restoration_resource_fixture import resource_executables, resource_fixture

import native_history
import windows_resource_execution as execution

if TYPE_CHECKING:
    from process_execution import CapturePolicy

prepare_tool_imports()


class WindowsDebugHistoryTests(unittest.TestCase):
    """Raw/JUnit growth and retained maps share the original concrete Debug owner."""

    @override
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "out/dev/app"
        self.directory = self.root / "out/windows-resource-proof/debug"
        (self.build / "tests").mkdir(parents=True)
        self.executables = resource_executables(self.build)
        self.tests = resource_fixture(self.build)[0]
        for test in self.tests:
            test["properties"] = [{"name": "WORKING_DIRECTORY", "value": str(self.build / "tests")}]
        self.directory.mkdir(parents=True)
        self.map = self.directory / "retained.map.gz"
        self.map.write_bytes(b"map")
        for name, value in (("ROOT", self.root), ("OUT", self.directory.parent)):
            owner = patch.object(execution, name, value)
            owner.start()
            self.addCleanup(owner.stop)

    def test_healthy_monitor_and_actual_qualification_keep_one_claim(self) -> None:
        """Existing maps, the live callback and final native record checks share the owned scope."""
        record: dict[str, Any] = {}

        def capture(
            _argv: list[str],
            _env: dict[str, str],
            _log: Path,
            seconds: float,
            policy: CapturePolicy,
        ) -> int:
            self.assertGreater(seconds, 0)
            self.assertLessEqual(seconds, 70)
            (self.directory / "ctest.xml").write_bytes(b"ok")
            policy.monitor()
            return 0

        def qualification(
            _path: Path,
            _owner: list[int],
            _tests: list[dict[str, Any]],
            _executables: dict[str, str],
        ) -> dict[str, Any]:
            self.assertTrue((self.build / ".native-history-active").exists())
            return {"fixture": True}

        with (
            patch("windows_resource_execution.time.monotonic", return_value=1000.0),
            patch.object(execution, "discovery", return_value=self.tests),
            patch.object(execution, "bounded_capture", side_effect=capture) as child,
            patch.object(execution, "archive_ctest_output"),
            patch.object(execution, "exact_debug_junit"),
            patch.object(execution, "native_records", side_effect=qualification),
        ):
            execution.debug_cases(time.monotonic() + 100, record, self.executables)
        self.assertEqual(child.call_count, 1)
        self.assertEqual(record["exit_code"], 0)
        self.assertEqual(self.map.read_bytes(), b"map")
        self.assertFalse((self.build / ".native-history-active").exists())

    def test_actual_external_raw_junit_growth_refuses_and_retains(self) -> None:
        """A tiny logical overrun invokes the real history guard through CapturePolicy.monitor."""
        record: dict[str, Any] = {}

        def grow(
            _argv: list[str],
            _env: dict[str, str],
            _log: Path,
            _seconds: float,
            policy: CapturePolicy,
        ) -> int:
            (self.directory / "ctest.xml").write_bytes(b"oversized")
            policy.monitor()
            return 0

        with (
            patch.object(execution, "discovery", return_value=self.tests),
            patch.object(native_history, "RAW_BYTES", 8),
            patch.object(execution, "bounded_capture", side_effect=grow) as child,
            patch.object(execution, "archive_ctest_output") as archive,
            self.assertRaisesRegex(ValueError, "raw JUnit/journal exceeds"),
        ):
            execution.debug_cases(time.monotonic() + 100, record, self.executables)
        self.assertEqual(child.call_count, 1)
        self.assertEqual(archive.call_count, 1)
        self.assertIsNone(record["exit_code"])
        self.assertIn("exceeds", record["child_error"])
        self.assertNotIn("evidence", record)
        self.assertEqual((self.directory / "ctest.xml").read_bytes(), b"oversized")
        self.assertFalse((self.build / ".native-history-active").exists())

    def test_retained_maps_consume_current_owner_before_child(self) -> None:
        """The second admission includes already-retained maps instead of granting a fresh quota."""
        with (
            patch.object(execution, "discovery", return_value=self.tests),
            patch.object(native_history, "RUN_BYTES", 2),
            patch.object(execution, "bounded_capture") as child,
            self.assertRaisesRegex(ValueError, "run owner exceeds"),
        ):
            execution.debug_cases(time.monotonic() + 100, {}, self.executables)
        child.assert_not_called()
        self.assertEqual(self.map.read_bytes(), b"map")
        self.assertFalse((self.directory / "ctest.xml").exists())
        self.assertFalse((self.build / ".native-history-active").exists())
