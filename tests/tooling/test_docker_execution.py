# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Daemon ownership and absence need explicit evidence, not Docker CLI termination."""

from __future__ import annotations

import contextlib
import json
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import prepare_tool_imports

import docker_execution
from cache_lock import WriterStateUnknownError, exclusive_cache

prepare_tool_imports()


class DockerExecutionTests(unittest.TestCase):
    """Synthetic daemon boundaries test refusals; they do not claim real Docker execution."""

    def test_only_exact_labelled_container_can_be_removed(self) -> None:
        """A collision stays untouched; the correct owner must disappear after removal."""
        identity = "a" * 64
        for label in ("foreign", "owned"):
            with (
                self.subTest(label=label),
                patch("docker_execution.query", side_effect=[[identity], []]),
                patch("docker_execution.subprocess.run") as run,
            ):
                run.return_value.stdout = json.dumps(
                    [{"Id": identity, "Config": {"Labels": {docker_execution.LABEL: label}}}]
                )
                if label == "foreign":
                    with self.assertRaisesRegex(RuntimeError, "ownership differs"):
                        docker_execution.remove_owned(
                            "docker", "gate", "owned", time.monotonic() + 1000
                        )
                    self.assertEqual(run.call_count, 1)
                else:
                    docker_execution.remove_owned(
                        "docker", "gate", "owned", time.monotonic() + 1000
                    )
                    self.assertEqual(run.call_args.args[0], ["docker", "rm", "--force", identity])

    def test_absence_ambiguity_and_failed_removal_are_not_interchangeable(self) -> None:
        """Empty successful query is safe; ambiguous or still-present work is a refusal."""
        with (
            patch("docker_execution.query", return_value=[]),
            patch("docker_execution.subprocess.run") as run,
        ):
            docker_execution.remove_owned("docker", "gate", "token", time.monotonic() + 1000)
            run.assert_not_called()
        with (
            patch("docker_execution.query", return_value=["a", "b"]),
            self.assertRaisesRegex(RuntimeError, "ambiguous"),
        ):
            docker_execution.remove_owned("docker", "gate", "token", time.monotonic() + 1000)
        identity = "b" * 64
        with (
            patch("docker_execution.query", return_value=[identity]),
            patch("docker_execution.subprocess.run") as run,
            self.assertRaisesRegex(RuntimeError, "unconfirmed"),
        ):
            run.return_value.stdout = json.dumps(
                [{"Id": identity, "Config": {"Labels": {docker_execution.LABEL: "token"}}}]
            )
            docker_execution.remove_owned("docker", "gate", "token", time.monotonic() + 1000)

    def test_cli_interrupt_still_reconciles_daemon_ownership(self) -> None:
        """Raised interruption cannot skip the distinct container cleanup boundary."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "log").write_text("synthetic bounded Docker CLI diagnostics")
            with (
                patch("docker_execution.termination_unwind", return_value=contextlib.nullcontext()),
                patch("docker_execution.bounded_capture") as owned,
                patch("docker_execution.remove_owned") as cleanup,
            ):
                cleanup.return_value = True
                owned.side_effect = InterruptedError
                with self.assertRaises(InterruptedError):
                    docker_execution.run_container(
                        ["docker", "run"], root / "log", "gate", "token", time.monotonic() + 1000
                    )
                cleanup.assert_called_once()
                self.assertEqual(cleanup.call_args.args[:3], ("docker", "gate", "token"))

    def test_uncertain_creation_keeps_writer_claim_and_refuses_reuse(self) -> None:
        """An empty observation cannot release shared volumes to a second writer."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "log").write_text("synthetic bounded Docker CLI diagnostics")
            claim = root / "active"
            with (
                patch("docker_execution.bounded_capture") as owned,
                patch("docker_execution.remove_owned", return_value=False),
            ):
                for failure in (InterruptedError(), None):
                    owned.side_effect = failure
                    owned.return_value = 1
                    with (
                        self.subTest(failure=failure),
                        self.assertRaisesRegex(
                            WriterStateUnknownError, "creation remains unconfirmed"
                        ),
                        exclusive_cache(claim),
                    ):
                        docker_execution.run_container(
                            ["docker", "run"],
                            root / "log",
                            "gate",
                            "token",
                            time.monotonic() + 1000,
                        )
                    self.assertTrue(claim.is_file())
                    with self.assertRaisesRegex(RuntimeError, "inspect"), exclusive_cache(claim):
                        self.fail("Unknown writer claim was adopted")
                    claim.unlink()

    def test_daemon_failure_preserves_claim_but_completed_absence_releases_it(self) -> None:
        """Cleanup failure retains ownership; successful CLI completion permits an empty query."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "log").write_text("synthetic bounded Docker CLI diagnostics")
            claim = root / "active"
            with patch("docker_execution.bounded_capture") as owned:
                owned.return_value = 0
                for error in (
                    OSError("daemon unavailable"),
                    KeyboardInterrupt(),
                    TypeError("record"),
                ):
                    with (
                        self.subTest(error=error),
                        patch("docker_execution.remove_owned", side_effect=error),
                        self.assertRaisesRegex(WriterStateUnknownError, "gate.*token") as caught,
                        exclusive_cache(claim),
                    ):
                        docker_execution.run_container(
                            ["docker", "run"],
                            root / "log",
                            "gate",
                            "token",
                            time.monotonic() + 1000,
                        )
                    self.assertIs(caught.exception.__cause__, error)
                    self.assertTrue(claim.is_file())
                    claim.unlink()
                with (
                    patch("docker_execution.remove_owned", return_value=False),
                    exclusive_cache(claim),
                ):
                    self.assertTrue(
                        docker_execution.run_container(
                            ["docker", "run"],
                            root / "log",
                            "gate",
                            "token",
                            time.monotonic() + 1000,
                        )
                    )
                self.assertFalse(claim.exists())

    def test_daemon_query_failure_cannot_establish_no_effects(self) -> None:
        """A failed connection is distinct from an empty successful container list."""
        with (
            patch(
                "docker_execution.subprocess.run",
                side_effect=subprocess.CalledProcessError(1, ["docker", "ps"]),
            ),
            self.assertRaises(subprocess.CalledProcessError),
        ):
            docker_execution.remove_owned("docker", "gate", "token", time.monotonic() + 1000)


class BoundedContainerTests(unittest.TestCase):
    """Real tiny CLI groups exercise capture; daemon operations remain explicit mocks."""

    def test_primary_control_and_cleanup_error_both_survive_unknown_writer(self) -> None:
        """An unknown writer retains the original exception object and the cleanup cause."""
        for primary in (RuntimeError("primary"), KeyboardInterrupt(), SystemExit(17)):
            with self.subTest(primary=type(primary)), tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                cleanup = OSError("daemon unavailable")
                claim = root / "active"
                with (
                    patch("docker_execution.bounded_capture", side_effect=primary),
                    patch("docker_execution.remove_owned", side_effect=cleanup),
                    self.assertRaises(docker_execution.ContainerStateUnknownError) as caught,
                    exclusive_cache(claim),
                ):
                    docker_execution.run_container(
                        ["docker", "run"], root / "log", "gate", "token", time.monotonic() + 1000
                    )
                self.assertIs(caught.exception.primary, primary)
                self.assertIs(caught.exception.cleanup, cleanup)
                self.assertIs(caught.exception.__cause__, cleanup)
                self.assertTrue(claim.is_file())

    def test_all_daemon_controls_share_the_original_remaining_deadline(self) -> None:
        """Each RPC consumes elapsed time instead of receiving a new thirty-second window."""
        identity = "a" * 64
        records = [{"Id": identity, "Config": {"Labels": {docker_execution.LABEL: "token"}}}]
        responses = [
            subprocess.CompletedProcess([], 0, stdout=identity + "\n"),
            subprocess.CompletedProcess([], 0, stdout=json.dumps(records)),
            subprocess.CompletedProcess([], 0),
            subprocess.CompletedProcess([], 0, stdout=""),
        ]
        with (
            patch("docker_execution.time.monotonic", side_effect=[95, 96, 97, 98]),
            patch("docker_execution.subprocess.run", side_effect=responses) as invoked,
        ):
            self.assertTrue(docker_execution.remove_owned("docker", "gate", "token", 100))
        self.assertEqual([call.kwargs["timeout"] for call in invoked.call_args_list], [5, 4, 3, 2])

    def test_real_cli_timeout_reaps_then_reconciles_the_exact_owner(self) -> None:
        """A blocked Python fixture proves timeout cleanup independently from a Docker daemon."""
        with tempfile.TemporaryDirectory() as raw:
            log = Path(raw) / "log"
            deadline = time.monotonic() + 2
            command = [sys.executable, "-c", "import threading; threading.Event().wait()"]
            with (
                patch("docker_execution.CLEANUP_SECONDS", 0.5),
                patch("docker_execution.remove_owned", return_value=True) as cleanup,
                self.assertRaises((subprocess.TimeoutExpired, TimeoutError)) as caught,
            ):
                docker_execution.run_container(command, log, "gate", "nonce", deadline)
            if isinstance(caught.exception, subprocess.TimeoutExpired):
                self.assertEqual(caught.exception.cmd, command)
            else:
                self.assertIn("deadline exhausted", str(caught.exception))
            cleanup.assert_called_once_with(command[0], "gate", "nonce", deadline)
            self.assertTrue(log.is_file())

    def test_real_oversized_cli_diagnostics_cannot_qualify_exit_zero(self) -> None:
        """A finite synthetic output reaches the real bounded writer and omission refusal."""
        with tempfile.TemporaryDirectory() as raw:
            log = Path(raw) / "log"
            command = [sys.executable, "-c", "print('x' * 8192)"]
            with (
                patch("docker_execution.CLEANUP_SECONDS", 0.5),
                patch("docker_execution.LOG_LIMIT_BYTES", 4096),
                patch("docker_execution.remove_owned", return_value=True),
                self.assertRaisesRegex(RuntimeError, "diagnostics exceeded"),
            ):
                docker_execution.run_container(command, log, "gate", "nonce", time.monotonic() + 3)
            self.assertLessEqual(log.stat().st_size, 4096)
            self.assertIn(b"[engine output omitted:", log.read_bytes())

    def test_failed_child_reconciles_owned_container_and_releases_claim(self) -> None:
        """A confirmed owned removal preserves failure without leaving a writer claim."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            log = root / "log"
            log.write_text("synthetic failed native diagnostics")
            claim = root / "active"
            with (
                patch("docker_execution.bounded_capture", return_value=1) as capture,
                patch("docker_execution.remove_owned", return_value=True) as cleanup,
                exclusive_cache(claim),
            ):
                self.assertFalse(
                    docker_execution.run_container(
                        ["docker", "run"], log, "gate", "owned", time.monotonic() + 1000
                    )
                )
            capture.assert_called_once()
            cleanup.assert_called_once()
            self.assertEqual(cleanup.call_args.args[:3], ("docker", "gate", "owned"))
            self.assertFalse(claim.exists())
