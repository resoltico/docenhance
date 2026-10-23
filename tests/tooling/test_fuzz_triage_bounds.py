# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Replay diagnostics bound actual writes and launch once per original byte identity."""

from __future__ import annotations

import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import TYPE_CHECKING, cast
from unittest.mock import Mock, patch

from tools_path import TOOLS

import capture_reader
import triage_fuzz_findings as triage
from fuzz_manifest import targets
from process_execution import CapturePolicy, bounded_capture

if TYPE_CHECKING:
    from collections.abc import Callable


class FuzzTriageBoundsTests(unittest.TestCase):
    """Synthetic child programs and evidence never establish native fuzz coverage."""

    def test_noisy_replay_caps_actual_temporary_bytes_and_preserves_final_diagnostic(self) -> None:
        """A real 8 MiB stream stores only bounded head/tail diagnostics before deleting scratch."""
        sizes = []
        paths = []
        body = (
            "import os; os.write(1,b'H'*4096); "
            "[os.write(1,b'x'*65536) for _ in range(128)]; "
            "os.write(2,b'FINAL-DIAGNOSTIC'); raise SystemExit(77)"
        )

        def capture(
            command: list[str],
            env: dict[str, str],
            log: Path,
            timeout: float,
            policy: CapturePolicy,
        ) -> int:
            self.assertEqual(command[1], "-runs=1")
            paths.append(log.parent)
            code = bounded_capture([sys.executable, "-c", body], env, log, timeout, policy)
            sizes.append(log.stat().st_size)
            return code

        with patch.object(triage, "bounded_capture", side_effect=capture):
            result = triage.replay(Path(sys.executable), TOOLS / "triage_fuzz_findings.py")
        self.assertEqual(result["exit_code"], 77)
        self.assertTrue(result["reproduced"])
        self.assertTrue(result["truncated"])
        self.assertTrue(result["output"].startswith("H" * 4096))
        self.assertTrue(result["output"].endswith("FINAL-DIAGNOSTIC"))
        self.assertIn("output omitted:", result["output"])
        self.assertTrue(
            all(triage.MAX_CAPTURE_BYTES < size < triage.MAX_CAPTURE_BYTES + 128 for size in sizes)
        )
        self.assertTrue(all(not path.exists() for path in paths))

    def test_timeout_keeps_bounded_diagnostics_and_cleanup_errors_remain_fatal(self) -> None:
        """A noisy never-ending child is joined; capture failures cannot become nonreproduction."""
        body = "import os;\nwhile True: os.write(1,b'x'*65536)"
        paths = []

        def capture(
            command: list[str],
            env: dict[str, str],
            log: Path,
            timeout: float,
            policy: CapturePolicy,
        ) -> int:
            del command
            paths.append(log.parent)
            try:
                return bounded_capture([sys.executable, "-c", body], env, log, timeout, policy)
            finally:
                self.assertLess(log.stat().st_size, triage.MAX_CAPTURE_BYTES + 128)

        with patch.object(triage, "bounded_capture", side_effect=capture):
            result = triage.replay(Path(sys.executable), Path("unused"), seconds=1)
        self.assertTrue(result["timed_out"])
        self.assertFalse(result["reproduced"])
        self.assertIsNone(result["exit_code"])
        self.assertTrue(result["truncated"])
        self.assertTrue(all(not path.exists() for path in paths))
        with (
            patch.object(triage, "bounded_capture", side_effect=OSError("capture failed")),
            self.assertRaisesRegex(OSError, "capture failed"),
        ):
            triage.replay(Path(sys.executable), Path("unused"))

    def test_timeout_with_unconfirmed_cleanup_propagates_the_original_failure(self) -> None:
        """Actual capture-owner notes cannot turn cleanup uncertainty into complete triage."""

        def capture(
            failure: subprocess.TimeoutExpired, stage: str
        ) -> Callable[[list[str], dict[str, str], Path, float, CapturePolicy], int]:
            def failed_capture(
                command: list[str],
                env: dict[str, str],
                log: Path,
                timeout: float,
                policy: CapturePolicy,
            ) -> int:
                del command, env, timeout, policy
                log.write_bytes(b"diagnostic before cleanup refusal")
                if stage == "owned-group":
                    refused = Mock(
                        side_effect=OSError("Synthetic owned-group cleanup is unconfirmed")
                    )
                    try:
                        refused()
                    except OSError:
                        retained = capture_reader.capture_failure(failure, complete=False)
                        if retained is None:
                            self.fail("Capture owner discarded the original timeout")
                        raise retained from None
                reader = Mock(ident=1)
                reader.is_alive.return_value = True
                capture_reader.finish_capture_reader(reader, None, None, 0, failure)
                self.fail("Live capture reader was reported as joined")

            return failed_capture

        for kind in ("owned-group", "reader-join"):
            with self.subTest(kind=kind):
                original = subprocess.TimeoutExpired(["synthetic-replay"], 1)
                with (
                    patch.object(triage, "bounded_capture", side_effect=capture(original, kind)),
                    self.assertRaises(subprocess.TimeoutExpired) as caught,
                ):
                    triage.replay(Path(sys.executable), Path("unused"))
                self.assertIs(caught.exception, original)
                self.assertTrue(getattr(caught.exception, "__notes__", []))

    def fixture(
        self, root: Path, findings: dict[str, bytes]
    ) -> tuple[Path, Path, dict[str, object]]:
        """Write original candidate bytes and an immutable synthetic binary receipt."""
        name = targets()[0].name
        binary = root / "fuzz" / f"de_fuzz_{name}"
        binary.parent.mkdir()
        binary.write_bytes(b"synthetic executable; never executed")
        directory = root / "fuzz-work/campaign-fixture" / f"{name}-fixture"
        artifacts = directory / "artifacts"
        artifacts.mkdir(parents=True)
        for filename, data in findings.items():
            (artifacts / filename).write_bytes(data)
        report: dict[str, object] = {
            "target": name,
            "engine": "libfuzzer",
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "findings": [f"artifacts/{filename}" for filename in findings],
        }
        (directory / "result.json").write_text(json.dumps(report))
        return directory, binary, report

    def test_duplicates_do_not_launch_again_or_consume_unique_allowance(self) -> None:
        """All aliases retain their original bytes while only distinct target/digest pairs run."""
        replay_limit = 32
        for count in (1, replay_limit, replay_limit + 1):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                findings = {f"{index:03}-a": str(index).encode() for index in range(count)}
                findings.update({f"{index:03}-b": str(index).encode() for index in range(count)})
                directory, _binary, _report = self.fixture(root, findings)
                with patch.object(triage, "replay", return_value={"reproduced": False}) as replay:
                    result = triage.triage(root, "libfuzzer")
                self.assertEqual(replay.call_count, min(count, replay_limit))
                self.assertEqual(len(result["cases"]), min(count, replay_limit))
                self.assertEqual(
                    result["status"], "findings_limit" if count > replay_limit else "complete"
                )
                self.assertTrue(all(case["input"].endswith("-a") for case in result["cases"]))
                for filename, data in findings.items():
                    self.assertEqual((directory / "artifacts" / filename).read_bytes(), data)

    def test_malformed_findings_refuse_without_replay(self) -> None:
        """Malformed paths cannot be discarded into an apparently empty complete diagnostic."""
        for invalid in ([42], ["../foreign"], ["artifacts\\foreign"], ["foreign/x"], "artifacts/x"):
            with self.subTest(invalid=invalid), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directory, _binary, report = self.fixture(root, {"finding": b"original"})
                report["findings"] = invalid
                (directory / "result.json").write_text(json.dumps(report))
                with (
                    patch.object(triage, "replay") as replay,
                    self.assertRaises((ValueError, TypeError)),
                ):
                    triage.triage(root, "libfuzzer")
                replay.assert_not_called()
                self.assertEqual((directory / "artifacts/finding").read_bytes(), b"original")

    def test_afl_failure_statistics_marker_is_preserved_without_becoming_an_input(self) -> None:
        """Actual AFL saved-count failure reports admit their known non-input marker only."""
        for with_crash in (False, True):
            with self.subTest(with_crash=with_crash), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directory, _binary, report = self.fixture(root, {})
                stats = directory / "afl/default/fuzzer_stats"
                stats.parent.mkdir(parents=True)
                stats.write_bytes(b"saved_crashes : 1\n")
                report["engine"] = "afl"
                report["passed"] = False
                findings = ["afl/default/fuzzer_stats"]
                if with_crash:
                    finding = directory / "afl/default/crashes/id000000"
                    finding.parent.mkdir()
                    finding.write_bytes(b"original crash")
                    findings.append("afl/default/crashes/id000000")
                report["findings"] = findings
                report_path = directory / "result.json"
                report_path.write_text(json.dumps(report))
                original = report_path.read_bytes()
                with patch.object(triage, "replay", return_value={"reproduced": False}) as replay:
                    result = triage.triage(root, "afl")
                self.assertEqual(result["status"], "complete")
                self.assertEqual(replay.call_count, int(with_crash))
                self.assertEqual(len(result["cases"]), int(with_crash))
                self.assertEqual(stats.read_bytes(), b"saved_crashes : 1\n")
                self.assertEqual(report_path.read_bytes(), original)
                report["engine"] = "libfuzzer"
                report_path.write_text(json.dumps(report))
                with patch.object(triage, "replay") as replay, self.assertRaises(ValueError):
                    triage.triage(root, "libfuzzer")
                replay.assert_not_called()

    def test_symlinked_evidence_ancestors_refuse_before_foreign_replay(self) -> None:
        """Every actually traversed evidence directory must be owned rather than a symlink."""
        for owner in ("workspace", "campaign", "target", "artifacts", "report"):
            with self.subTest(owner=owner), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directory, _binary, _report = self.fixture(root, {"finding": b"original"})
                path = {
                    "workspace": root / "fuzz-work",
                    "campaign": directory.parent,
                    "target": directory,
                    "artifacts": directory / "artifacts",
                    "report": directory / "result.json",
                }[owner]
                actual_lstat = Path.lstat
                observed = actual_lstat(path)
                reparse = cast(
                    "os.stat_result",
                    SimpleNamespace(
                        st_mode=observed.st_mode,
                        st_file_attributes=stat.FILE_ATTRIBUTE_REPARSE_POINT,
                    ),
                )

                def inspect(
                    candidate: Path,
                    *,
                    owner: Path = path,
                    replacement: os.stat_result = reparse,
                    probe: Callable[[Path], os.stat_result] = actual_lstat,
                ) -> os.stat_result:
                    return replacement if candidate == owner else probe(candidate)

                with (
                    patch.object(Path, "lstat", side_effect=inspect, autospec=True),
                    patch.object(triage, "replay") as replay,
                    self.assertRaisesRegex(ValueError, "evidence owner"),
                ):
                    triage.triage(root, "libfuzzer")
                replay.assert_not_called()
                if os.name == "posix":
                    original = root / f"foreign-{owner}"
                    path.rename(original)
                    path.symlink_to(original, target_is_directory=owner != "report")
                    with (
                        patch.object(triage, "replay") as replay,
                        self.assertRaisesRegex(ValueError, "evidence owner"),
                    ):
                        triage.triage(root, "libfuzzer")
                    replay.assert_not_called()
