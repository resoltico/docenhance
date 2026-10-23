# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real CTest partial-build discovery preserves selected-scope admission and family closure."""

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

import test_evidence
import windows_resource_execution as execution
from restoration_resource_cases import RESOURCE_PATTERN, resource_cases

prepare_tool_imports()

if TYPE_CHECKING:
    from native_history import NativeHistory


class PartialNativeDiscoveryTests(unittest.TestCase):
    """Use actual CTest JSON with a real unrelated missing-target registration; no native build."""

    @override
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "out/release/app"
        self.directory = self.root / "out/windows-resource-proof"
        self.directory.mkdir(parents=True)
        (self.build / "tests").mkdir(parents=True)
        self.executables = {
            name: str(self.build / "tests" / (name + ".exe"))
            for name in ("de_fft_resources", "de_restoration_resources")
        }
        for executable in self.executables.values():
            Path(executable).write_bytes(b"discovery-only fixture, never executed")
        self.ctest = shutil.which("ctest")
        self.assertIsNotNone(self.ctest)
        self.rows = [self.row("fft-native-resources", "de_fft_resources")]
        self.rows += [
            self.row("restoration-native-controls", "de_restoration_resources", "--controls")
        ]
        self.rows += [
            self.row(
                "restoration-native-resources-" + str(i),
                "de_restoration_resources",
                "--shard",
                str(i),
            )
            for i in range(16)
        ]
        self.rows.append('add_test(de_tests_NOT_BUILT "de_tests_NOT_BUILT")')
        self.save()

    def row(self, name: str, executable: str, *arguments: str) -> str:
        """Write the concrete generated CTest registration syntax without configuring a compiler."""
        command = " ".join(
            json.dumps(value.replace("\\", "/"))
            for value in (self.executables[executable], *arguments)
        )
        working = (self.build / "tests").as_posix()
        return (
            f'add_test("{name}" {command})\n'
            f'set_tests_properties("{name}" PROPERTIES WORKING_DIRECTORY "{working}")'
        )

    def save(self) -> None:
        """Keep the actual CTest testfile in the owned temporary build only."""
        (self.build / "CTestTestfile.cmake").write_text("\n".join(self.rows) + "\n")

    def query(self, pattern: str) -> list[dict[str, Any]]:
        """Use the same partial-scope helper as both Windows callers, with real CTest."""
        with patch.object(
            execution,
            "discovery",
            side_effect=lambda _ctest, build, **kw: test_evidence.discovery(
                str(self.ctest), build, **kw
            ),
        ):
            return execution.resource_discovery(self.build, pattern)

    def test_actual_partial_build_filters_unrelated_sentinel_but_full_discovery_refuses(
        self,
    ) -> None:
        """Default full discovery still rejects missing Catch, while both owned queries work."""
        with self.assertRaisesRegex(test_evidence.EvidenceError, "de_tests_NOT_BUILT"):
            test_evidence.discovery(str(self.ctest), self.build)
        self.assertEqual(
            [test["name"] for test in self.query(execution.FFT_PATTERN)], ["fft-native-resources"]
        )
        self.assertEqual(len(resource_cases(self.query(RESOURCE_PATTERN), self.executables)), 18)

    def test_selected_disabled_skippable_missing_and_duplicate_refuse_both_queries(self) -> None:
        """Filtering does not exempt any returned registration from strict validation."""
        original = list(self.rows)
        for mutation in (
            "set_tests_properties(fft-native-resources PROPERTIES DISABLED TRUE)",
            "set_tests_properties(fft-native-resources PROPERTIES SKIP_RETURN_CODE 1)",
            "set_tests_properties(fft-native-resources PROPERTIES SKIP_REGULAR_EXPRESSION ignored)",
            'add_test(fft-native-resources "missing-selected-executable")',
            self.row("fft-native-resources", "de_fft_resources"),
        ):
            self.rows = [*original, mutation]
            self.save()
            for pattern in (execution.FFT_PATTERN, RESOURCE_PATTERN):
                with (
                    self.subTest(mutation=mutation, pattern=pattern),
                    self.assertRaises(test_evidence.EvidenceError),
                ):
                    self.query(pattern)
        self.rows = list(original)
        self.rows[0] = self.rows[0].replace(
            self.executables["de_fft_resources"].replace("\\", "/"), "missing-selected-executable"
        )
        self.save()
        for pattern in (execution.FFT_PATTERN, RESOURCE_PATTERN):
            with self.subTest(pattern=pattern), self.assertRaises(test_evidence.EvidenceError):
                self.query(pattern)
        self.rows = [row for row in original if '"fft-native-resources"' not in row]
        self.save()
        with self.assertRaises(test_evidence.EvidenceError):
            self.query(execution.FFT_PATTERN)
        with self.assertRaises(ValueError):
            resource_cases(self.query(RESOURCE_PATTERN), self.executables)

    def test_malformed_resource_family_members_remain_visible_and_refused(self) -> None:
        """The Debug query cannot hide previously rejected unknown roles or leading-zero shards."""
        original = list(self.rows)
        for name in (
            "fft-native-rogue",
            "restoration-native-resources-01",
            "restoration-native-rogue",
        ):
            self.rows = [*original, self.row(name, "de_restoration_resources")]
            self.save()
            with self.subTest(name=name), self.assertRaises(ValueError):
                resource_cases(self.query(RESOURCE_PATTERN), self.executables)

    def test_wrong_working_directory_refuses_both_partial_routes(self) -> None:
        """Actual CTest working-directory metadata must name the owned test directory."""
        self.rows.append(
            "set_tests_properties(fft-native-resources PROPERTIES WORKING_DIRECTORY "
            f'"{self.root.as_posix()}")'
        )
        self.save()
        for pattern in (execution.FFT_PATTERN, RESOURCE_PATTERN):
            with (
                self.subTest(pattern=pattern),
                self.assertRaisesRegex(ValueError, "working directory"),
            ):
                self.query(pattern)

    def test_actual_preflight_caller_uses_selected_query_and_same_execution_pattern(self) -> None:
        """The early caller reaches its once-only execution boundary despite missing Catch."""
        (self.build / "native-tests.json").write_text(
            json.dumps({"resource_binaries": self.executables})
        )

        def child(command: list[str], history: NativeHistory, count: int) -> int:
            if history.directory is None:
                self.fail("fresh owner missing its current directory")
            self.assertEqual(count, 1)
            self.assertEqual(command[command.index("-R") + 1], execution.FFT_PATTERN)
            (history.directory / "ctest.xml").write_text(
                '<testsuite tests="1">'
                '<testcase name="fft-native-resources" status="run"/></testsuite>'
            )
            (history.directory / "native-output.json").write_text("{}")
            return 0

        with (
            patch.object(execution, "ROOT", self.root),
            patch.object(execution, "OUT", self.directory),
            patch.object(
                execution,
                "discovery",
                side_effect=lambda _ctest, build, **kw: test_evidence.discovery(
                    str(self.ctest), build, **kw
                ),
            ),
            patch.object(execution, "_execute_owned", side_effect=child) as invoked,
            patch.object(
                execution,
                "retained_bodies",
                return_value={"journal": framed({"fft-native-resources": calibration_rows()})},
            ),
        ):
            execution.release_fft_preflight(time.monotonic() + 120, {})
        self.assertEqual(invoked.call_count, 1)

    def test_actual_debug_caller_preserves_complete_family_admission_before_execution(self) -> None:
        """Debug queries and runs the same family only after exact18-role admission."""
        previous = self.build
        previous.parent.rename(self.root / "out/dev")
        self.build = self.root / "out/dev/app"
        self.rows = [row.replace(previous.as_posix(), self.build.as_posix()) for row in self.rows]
        self.executables = {
            name: str(self.build / "tests" / (name + ".exe")) for name in self.executables
        }
        self.save()
        directory = self.directory / "debug"
        directory.mkdir()

        def child(command: list[str], *_arguments: object) -> int:
            self.assertEqual(command[command.index("-R") + 1], RESOURCE_PATTERN)
            names = resource_cases(self.query(RESOURCE_PATTERN), self.executables)
            (directory / "ctest.xml").write_text(
                '<testsuite tests="18">'
                + "".join(f'<testcase name="{name}" status="run"/>' for name in sorted(names))
                + "</testsuite>"
            )
            return 0

        record: dict[str, Any] = {}
        with (
            patch.object(execution, "ROOT", self.root),
            patch.object(execution, "OUT", self.directory),
            patch.object(
                execution,
                "discovery",
                side_effect=lambda _ctest, build, **kw: test_evidence.discovery(
                    str(self.ctest), build, **kw
                ),
            ),
            patch.object(execution, "bounded_capture", side_effect=child) as invoked,
            patch.object(execution, "archive_ctest_output"),
            patch.object(execution, "native_records", return_value={}) as records,
        ):
            original = list(self.rows)
            self.rows.append(
                self.row("restoration-native-resources-01", "de_restoration_resources")
            )
            self.save()
            with self.assertRaisesRegex(ValueError, "noncontiguous"):
                execution.debug_cases(time.monotonic() + 120, {}, self.executables)
            invoked.assert_not_called()
            records.assert_not_called()
            self.rows = original
            self.save()
            execution.debug_cases(time.monotonic() + 120, record, self.executables)
        self.assertEqual(invoked.call_count, 1)
        self.assertEqual(len(records.call_args.args[2]), 18)
        self.assertEqual(len(record["selected_cases"]), 18)
