# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Supervision controls retain original child outcomes alongside archived diagnostics."""

from __future__ import annotations

import argparse
import html
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING, override

if TYPE_CHECKING:
    from collections.abc import Callable
from unittest import mock

from tools_path import prepare_tool_imports

from native_output_fixture import journal_bytes

import run_native_suite as suite
from native_resource_capture import retained_bodies
from test_evidence import EvidenceError

prepare_tool_imports()

OMITTED_CASE_COUNT = 2


class SupervisorControlError(BaseException):
    """A nonstandard process control must retain its exact identity across cleanup."""


class NativeOutputIntegrationTests(unittest.TestCase):
    """Real supervisor finally/qualification ordering with one tiny mocked child outcome."""

    @override
    def setUp(self) -> None:
        """Keep actual supervisor artifacts while mocking only the owned child."""
        self.temporary = tempfile.TemporaryDirectory(prefix="native-output-integration-")
        self.addCleanup(self.temporary.cleanup)
        self.build = Path(self.temporary.name)
        (self.build / "Testing/Temporary").mkdir(parents=True)
        self.assertions = 1
        self.exit_code = 0
        self.timeout = False
        self.partial = False
        self.no_junit = False
        self.temporary_junit = False
        self.journal_override: bytes | None = None
        self.expected_count = 1
        self.control: BaseException | None = None

    def child(
        self,
        command: list[str],
        _env: dict[str, str],
        timeout: float,
        monitor: Callable[[], None],
        *,
        graceful: bool,
    ) -> int:
        """Return a fixed child outcome after producing real complete or partial files."""
        self.assertNotIn("--output-on-failure", command)
        self.assertTrue(graceful)
        self.assertTrue(callable(monitor))
        self.assertGreater(timeout, 0)
        directory = Path(command[-1]).parent
        name = "LastTest.log.tmpabcde" if self.partial else "LastTest.log"
        (self.build / "Testing/Temporary" / name).write_bytes(
            self.journal_override
            if self.journal_override is not None
            else journal_bytes(b"complete tiny child diagnostics\n")
        )
        catch = (
            f'<Catch2TestRun><TestCase name="unit"/>'
            f'<OverallResults successes="{self.assertions}" failures="0" '
            'expectedFailures="0" skips="0"/></Catch2TestRun>'
        )
        if not self.no_junit:
            junit = directory / ("ctest.xml.tmp" if self.temporary_junit else "ctest.xml")
            junit.write_text(
                '<testsuite><testcase name="unit" status="run"><system-out>'
                + html.escape(catch)
                + "</system-out></testcase>"
                + (
                    '<testcase name="other" status="run"/>'
                    if self.expected_count == OMITTED_CASE_COUNT
                    else ""
                )
                + "</testsuite>"
            )
        if self.control is not None:
            raise self.control
        if self.timeout:
            raise subprocess.TimeoutExpired(command, timeout)
        return self.exit_code

    def execute(self) -> int:
        """Run the actual supervisor with original JUnit/Catch reconciliation."""
        args = argparse.Namespace(build=self.build, ctest="ctest", jobs=1)
        with (
            mock.patch.object(
                suite,
                "discovery",
                return_value=[{"name": "unit"}]
                + ([{"name": "other"}] if self.expected_count == OMITTED_CASE_COUNT else []),
            ),
            mock.patch.object(suite, "compilation"),
            mock.patch.object(
                suite,
                "resource_results",
                side_effect=lambda directory, *_: retained_bodies(directory)["junit"],
            ),
            mock.patch.object(suite, "registrations", return_value={"unit"}),
            mock.patch.object(suite, "monitored_process", side_effect=self.child),
        ):
            return suite.run(args)

    def evidence(self) -> Path:
        """The supervisor must create exactly one fresh evidence owner."""
        directories = list(self.build.glob("native-*"))
        self.assertEqual(len(directories), 1)
        return directories[0]

    def test_real_finally_archives_both_before_exact_catch_checks(self) -> None:
        """An executed Catch case with an assertion still uses the original raw JUnit checker."""
        self.assertEqual(self.execute(), 0)
        self.assertFalse((self.evidence() / "ctest.xml").exists())
        self.assertTrue((self.evidence() / "ctest-output.gz").exists())
        self.assertTrue((self.evidence() / "ctest.xml.gz").exists())

    def test_resource_reconciliation_cannot_extend_the_execution_deadline(self) -> None:
        """Successful archive and parsing still refuse an exhausted original execution clock."""

        def clock() -> float:
            coverage = suite.resource_results
            return 2000.0 if isinstance(coverage, mock.Mock) and coverage.called else 0.0

        with (
            mock.patch("run_native_suite.time.monotonic", side_effect=clock),
            self.assertRaisesRegex(EvidenceError, "execution deadline"),
        ):
            self.execute()
        self.assertTrue((self.evidence() / "ctest-output.gz").exists())
        self.assertTrue((self.evidence() / "ctest.xml.gz").exists())

    def test_zero_assertion_catch_cannot_pass_after_successful_archival(self) -> None:
        """Archival is not qualification: the unchanged real Catch oracle still rejects zero."""
        self.assertions = 0
        with self.assertRaisesRegex(EvidenceError, "no assertions"):
            self.execute()
        self.assertTrue((self.evidence() / "ctest.xml.gz").exists())
        self.assertTrue((self.evidence() / "ctest-output.gz").exists())

    def test_actual_exit_eight_survives_successful_diagnostic_retention(self) -> None:
        """A coherent JUnit never changes the actual nonzero child exit into success."""
        self.exit_code = 8
        self.assertEqual(self.execute(), 8)
        self.assertTrue((self.evidence() / "native-output.json").exists())
        self.assertTrue((self.evidence() / "ctest.xml").exists())

    def test_timeout_retains_partial_and_original_exception(self) -> None:
        """The finally path runs after a reaped timeout without treating a partial log as pass."""
        self.timeout = True
        self.partial = True
        with self.assertRaises(subprocess.TimeoutExpired):
            self.execute()
        self.assertTrue((self.evidence() / "ctest-output-partial.gz").exists())
        self.assertTrue((self.evidence() / "ctest.xml.gz").exists())

    def test_temporary_junit_never_qualifies_a_zero_exit(self) -> None:
        """Even complete-looking temporary XML cannot establish completed native execution."""
        self.temporary_junit = True
        with self.assertRaisesRegex(EvidenceError, "partial CTest JUnit"):
            self.execute()
        self.assertTrue((self.evidence() / "ctest.xml.gz").exists())

    def test_both_exact_junit_names_must_be_absent_before_child(self) -> None:
        """An owned pre-existing temporary report is stale, not this child's output."""
        self.control = SupervisorControlError("unexpected child launch")
        for name in ("ctest.xml", "ctest.xml.tmp"):
            directory = self.build / ("native-stale-" + name)
            directory.mkdir()
            (directory / name).write_text("old")
            with (
                mock.patch("run_native_suite.tempfile.mkdtemp", return_value=str(directory)),
                self.assertRaisesRegex(EvidenceError, "stale JUnit"),
            ):
                self.execute()
            self.assertFalse((directory / "native-output.json").exists())

    def test_partial_journal_never_qualifies_a_zero_exit(self) -> None:
        """Even coherent JUnit cannot turn an incomplete journal epoch into qualification."""
        self.partial = True
        with self.assertRaisesRegex(EvidenceError, "partial CTest journal"):
            self.execute()
        self.assertTrue((self.evidence() / "ctest-output-partial.gz").exists())

    def test_empty_fresh_journal_cannot_qualify_valid_junit(self) -> None:
        """Freshness alone never establishes complete raw diagnostics."""
        self.journal_override = b""
        with self.assertRaisesRegex(EvidenceError, "partial CTest journal"):
            self.execute()
        self.assertTrue((self.evidence() / "ctest-output-partial.gz").exists())

    def test_missing_footer_cannot_qualify_valid_junit(self) -> None:
        """A cut final journal remains lossless partial diagnostics only."""
        self.journal_override = journal_bytes(b"bytes").split(b"End testing:")[0]
        with self.assertRaisesRegex(EvidenceError, "partial CTest journal"):
            self.execute()
        self.assertTrue((self.evidence() / "ctest.xml.gz").exists())

    def test_truncated_case_with_footer_cannot_qualify_valid_junit(self) -> None:
        """A footer cannot hide missing ordered case result and end boundaries."""
        self.journal_override = (
            journal_bytes(b"bytes").split(b"Test time =")[0] + b"End testing: Oct 20 10:00 EEST\n"
        )
        with self.assertRaisesRegex(EvidenceError, "partial CTest journal"):
            self.execute()

    def test_malformed_case_boundaries_cannot_qualify_valid_junit(self) -> None:
        """Balanced outer tokens still require the actual ordered case framing."""
        self.journal_override = journal_bytes(b"bytes").replace(b"1/1 Test:", b"1/1 Testing:")
        with self.assertRaisesRegex(EvidenceError, "partial CTest journal"):
            self.execute()

    def test_whole_case_omission_cannot_hide_behind_complete_framing(self) -> None:
        """Raw count uses existing discovery, while exact JUnit remains identity authority."""
        self.expected_count = OMITTED_CASE_COUNT
        with self.assertRaisesRegex(EvidenceError, "partial CTest journal"):
            self.execute()
        self.assertTrue((self.evidence() / "ctest.xml.gz").exists())

    def test_archive_failure_records_child_failure_too(self) -> None:
        """Missing fresh JUnit cannot erase exit8, and no success archive receipt is written."""
        self.exit_code = 8
        self.no_junit = True
        with self.assertRaisesRegex(EvidenceError, "CTest exit 8"):
            self.execute()
        metadata = json.loads((self.evidence() / "native-run-error.json").read_text())
        self.assertIn("CTest exit 8", metadata["test_error"])
        self.assertIn("JUnit", metadata["archive_error"])
        self.assertFalse((self.evidence() / "native-output.json").exists())

    def assert_dual_control(self, error: BaseException) -> None:
        """The exact original child exception survives a separate archival failure."""
        self.control = error
        self.no_junit = True
        with self.assertRaises(type(error)) as caught:
            self.execute()
        self.assertIs(caught.exception, error)
        metadata = json.loads((self.evidence() / "native-run-error.json").read_text())
        self.assertIn(type(error).__name__, metadata["test_error"])
        self.assertIn("JUnit", metadata["archive_error"])
        self.assertTrue(any("Diagnostic archive also failed" in note for note in error.__notes__))
        self.assertTrue((self.evidence() / "ctest-output.gz").exists())

    def test_keyboard_interrupt_identity_survives_dual_failure(self) -> None:
        """Archive refusal cannot turn the user's interruption into ordinary evidence failure."""
        self.assert_dual_control(KeyboardInterrupt("original interrupt"))

    def test_system_exit_identity_and_code_survive_dual_failure(self) -> None:
        """Preserve the selected process exit as the same SystemExit instance."""
        error = SystemExit(17)
        self.assert_dual_control(error)
        self.assertEqual(error.code, 17)

    def test_timeout_identity_survives_dual_failure(self) -> None:
        """A reaped timeout stays the actual original timeout, with both errors recorded."""
        self.assert_dual_control(subprocess.TimeoutExpired(["fixed-child"], 1))

    def test_interrupted_error_identity_survives_dual_failure(self) -> None:
        """The owned process helper's interruption type retains its original identity too."""
        self.assert_dual_control(InterruptedError("original termination unwind"))

    def assert_archive_control(self, error: BaseException) -> None:
        """An interruption originating during retention is never wrapped as EvidenceError."""
        with (
            mock.patch.object(suite, "archive_ctest_output", side_effect=error),
            self.assertRaises(type(error)) as caught,
        ):
            self.execute()
        self.assertIs(caught.exception, error)
        metadata = json.loads((self.evidence() / "native-run-error.json").read_text())
        self.assertEqual(metadata["test_error"], "CTest exit 0")
        self.assertIn(type(error).__name__, metadata["archive_error"])

    def test_archive_keyboard_interrupt_survives_without_child_failure(self) -> None:
        """A successful child does not authorize swallowing an archival interruption."""
        self.assert_archive_control(KeyboardInterrupt("archive interrupt"))

    def test_archive_system_exit_preserves_selected_code(self) -> None:
        """The archival control exception remains the same instance and exit code."""
        error = SystemExit(23)
        self.assert_archive_control(error)
        self.assertEqual(error.code, 23)

    def test_ambient_exception_does_not_invent_a_child_or_metadata_failure(self) -> None:
        """A caller's active handler is not an exception from a successful nested operation."""
        ambient = RuntimeError("unrelated outer handler")
        try:
            raise ambient
        except RuntimeError:
            self.assertEqual(self.execute(), 0)
        self.assertFalse(hasattr(ambient, "__notes__"))

    def test_custom_base_control_identity_survives_dual_failure(self) -> None:
        """The original-control rule covers BaseException subclasses beyond built-in signals."""
        self.assert_dual_control(SupervisorControlError("original custom control"))

    def test_resource_failure_preserves_raw(self) -> None:
        """Lossless archival alone cannot retire an unreconciled resource result."""
        original = retained_bodies

        def refuse(directory: Path) -> bytes:
            original(directory)
            message = "resource reconciliation refused"
            raise EvidenceError(message)

        with (
            mock.patch(__name__ + ".retained_bodies", side_effect=refuse),
            self.assertRaisesRegex(EvidenceError, "resource reconciliation refused"),
        ):
            self.execute()
        self.assertTrue((self.evidence() / "ctest.xml").exists())
