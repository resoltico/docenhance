# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Failed and uncertain replay attempts retain delivery, exact status and their owned artifacts."""

from __future__ import annotations

import contextlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import TYPE_CHECKING
from unittest.mock import patch

from tools_path import ROOT

from quality_evidence_fixture import write_reference

from evaluate_document_quality import replay
from process_execution import owned_process
from quality_evidence import digest
from quality_execution import MAX_RESPONSE_BYTES
from quality_replay import execute_replay

if TYPE_CHECKING:
    from collections.abc import Iterator
    from typing import IO


class QualityReplayTests(unittest.TestCase):
    """Owned-process fault controls use no timing sleeps and never retry failed effects."""

    def test_actual_exit5_and_committed_stdout_are_retained_without_cleanup(self) -> None:
        """A real subprocess's failed status cannot erase its confirmed publication response."""
        self.assertTrue((ROOT / "tools/quality_replay.py").is_file())
        response = {
            "schema_version": 14,
            "command": "process",
            "version": "0.7.0",
            "exit_code": 5,
            "publication": "completed",
            "error": {"code": "E_OUTPUT_VERIFY", "message": "synthetic retained commit"},
        }
        code = f"print({json.dumps(json.dumps(response))}); raise SystemExit(5)"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source = root / "source"
            source.write_bytes(b"synthetic source")
            calls = []

            def controlled(
                command: list[str],
                env: dict[str, str],
                streams: tuple[IO[bytes], IO[bytes]],
                *,
                cwd: Path | None = None,
            ) -> contextlib.AbstractContextManager[subprocess.Popen[bytes]]:
                calls.append(command)
                return owned_process([sys.executable, "-c", code], env, streams, cwd=cwd)

            with (
                patch("quality_replay.owned_process", side_effect=controlled),
                patch("quality_replay.digest", wraps=digest),
                patch(
                    "quality_replay.verify_replay", wraps=__import__("quality_replay").verify_replay
                ),
            ):
                observed = execute_replay(
                    [sys.executable, "process", "source", "--out-dir", "original", "--json"],
                    root,
                    lambda *_args: {},
                )
            self.assertEqual(len(calls), 1)
            self.assertIsNone(observed.record)
            attempt = observed.attempt
            self.assertEqual(attempt["exit_code"], 5)
            self.assertEqual(attempt["publication"], "completed")
            self.assertEqual(attempt["response"], response)
            directory = root / attempt["artifact_directory"]
            self.assertTrue(directory.is_dir())
            self.assertEqual(digest(root / attempt["stdout"]["path"]), attempt["stdout"]["sha256"])
            self.assertTrue((directory / "stderr").is_file())

    def test_full_frozen_argv_changes_only_output_destination(self) -> None:
        """Valid ./ input spelling and reordered options remain byte-identical through replay."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / "source").write_bytes(b"synthetic input")
            command = [
                sys.executable,
                "process",
                "./source",
                "--fixed-threshold",
                "0.5",
                "--output-mode",
                "bw",
                "--out-dir",
                "./old//",
                "--binarize",
                "fixed",
                "--json",
            ]
            captured = []
            record = {"record": {"run": "a" * 32}}

            @contextlib.contextmanager
            def controlled(
                argv: list[str],
                env: dict[str, str],
                streams: tuple[IO[bytes], IO[bytes]],
                *,
                cwd: Path | None = None,
            ) -> Iterator[SimpleNamespace]:
                del env, cwd
                captured.append(argv.copy())
                output = Path(argv[8])
                output.mkdir()
                ref = write_reference(output, "run.json", record)
                response = {
                    "schema_version": 14,
                    "command": "process",
                    "version": "0.7.0",
                    "exit_code": 0,
                    "publication": "completed",
                    "method": "B03",
                    "method_version": 1,
                    "binarization": None,
                    "rotation_degrees": 0,
                    "source_decoding": None,
                    "output": str(output) + "/result.png",
                    "record": {
                        "run": "a" * 32,
                        "path": "run.json",
                        "sha256": ref["sha256"],
                        "bytes": (output / "run.json").stat().st_size,
                    },
                }
                streams[0].write(json.dumps(response).encode())
                yield SimpleNamespace(wait=lambda **_kwargs: 0)

            with (
                patch("quality_replay.owned_process", controlled),
                patch("evaluate_document_quality.read_bundle", return_value=record),
            ):
                observed = replay(command, root)
            self.assertEqual(observed.attempt["outcome"], "completed", observed.attempt["errors"])
            self.assertEqual(len(captured), 1)
            expected = command.copy()
            expected[8] = captured[0][8]
            self.assertEqual(captured[0], expected)
            self.assertEqual(command[8], "./old//")
            self.assertEqual(observed.attempt["argv"], expected)

    def test_timeout_and_oversized_response_preserve_unknown_effects_once(self) -> None:
        """Deterministic wait refusal and bounded parsing retain complete raw stream identities."""
        for timeout, data in ((True, b'{"partial":'), (False, b"x" * (MAX_RESPONSE_BYTES + 1))):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                source = root / "source"
                source.write_bytes(b"synthetic source")
                calls: list[int] = []

                def wait(
                    *, timeout: int, observed: list[int] = calls, payload: bytes = data
                ) -> int:
                    observed.append(timeout)
                    if payload.startswith(b"{"):
                        command = "controlled"
                        raise subprocess.TimeoutExpired(command, timeout)
                    return 0

                @contextlib.contextmanager
                def controlled(
                    command: list[str],
                    env: dict[str, str],
                    streams: tuple[IO[bytes], IO[bytes]],
                    *,
                    cwd: Path | None = None,
                    payload: bytes = data,
                ) -> Iterator[SimpleNamespace]:
                    del command, env, cwd
                    streams[0].write(payload)
                    yield SimpleNamespace(wait=wait)

                with (
                    patch("quality_replay.owned_process", controlled),
                    patch(
                        "quality_replay.verify_replay",
                        wraps=__import__("quality_replay").verify_replay,
                    ),
                ):
                    observed = execute_replay(
                        [sys.executable, "process", "source", "--out-dir", "original", "--json"],
                        root,
                        lambda *_args: {},
                    )
                self.assertEqual(calls, [600])
                self.assertEqual(observed.attempt["timed_out"], timeout)
                self.assertEqual(observed.attempt["publication"], "unknown")
                self.assertEqual(observed.attempt["response_truncated"], not timeout)
                raw = root / observed.attempt["stdout"]["path"]
                self.assertEqual(raw.read_bytes(), data)
                self.assertIsNone(observed.record)
