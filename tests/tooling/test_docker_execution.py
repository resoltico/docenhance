# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Daemon ownership and absence need explicit evidence, not Docker CLI termination."""

from __future__ import annotations

import contextlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import docker_execution
from cache_lock import WriterStateUnknownError, exclusive_cache


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
                        docker_execution.remove_owned("docker", "gate", "owned")
                    self.assertEqual(run.call_count, 1)
                else:
                    docker_execution.remove_owned("docker", "gate", "owned")
                    self.assertEqual(run.call_args.args[0], ["docker", "rm", "--force", identity])

    def test_absence_ambiguity_and_failed_removal_are_not_interchangeable(self) -> None:
        """Empty successful query is safe; ambiguous or still-present work is a refusal."""
        with (
            patch("docker_execution.query", return_value=[]),
            patch("docker_execution.subprocess.run") as run,
        ):
            docker_execution.remove_owned("docker", "gate", "token")
            run.assert_not_called()
        with (
            patch("docker_execution.query", return_value=["a", "b"]),
            self.assertRaisesRegex(RuntimeError, "ambiguous"),
        ):
            docker_execution.remove_owned("docker", "gate", "token")
        identity = "b" * 64
        with (
            patch("docker_execution.query", return_value=[identity]),
            patch("docker_execution.subprocess.run") as run,
            self.assertRaisesRegex(RuntimeError, "unconfirmed"),
        ):
            run.return_value.stdout = json.dumps(
                [{"Id": identity, "Config": {"Labels": {docker_execution.LABEL: "token"}}}]
            )
            docker_execution.remove_owned("docker", "gate", "token")

    def test_cli_interrupt_still_reconciles_daemon_ownership(self) -> None:
        """Raised interruption cannot skip the distinct container cleanup boundary."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with (
                patch("docker_execution.termination_unwind", return_value=contextlib.nullcontext()),
                patch("docker_execution.owned_process") as owned,
                patch("docker_execution.remove_owned") as cleanup,
            ):
                cleanup.return_value = True
                owned.return_value.__enter__.return_value.wait.side_effect = InterruptedError
                with self.assertRaises(InterruptedError):
                    docker_execution.run_container(
                        ["docker", "run"], root / "log", ROOT, "gate", "token"
                    )
                cleanup.assert_called_once_with("docker", "gate", "token")

    def test_uncertain_creation_keeps_writer_claim_and_refuses_reuse(self) -> None:
        """An empty observation cannot release shared volumes to a second writer."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            claim = root / "active"
            with (
                patch("docker_execution.owned_process") as owned,
                patch("docker_execution.remove_owned", return_value=False),
            ):
                for failure in (InterruptedError(), None):
                    owned.return_value.__enter__.return_value.wait.side_effect = failure
                    owned.return_value.__enter__.return_value.wait.return_value = 1
                    with (
                        self.subTest(failure=failure),
                        self.assertRaisesRegex(
                            WriterStateUnknownError, "creation remains unconfirmed"
                        ),
                        exclusive_cache(claim),
                    ):
                        docker_execution.run_container(
                            ["docker", "run"], root / "log", ROOT, "gate", "token"
                        )
                    self.assertTrue(claim.is_file())
                    with self.assertRaisesRegex(RuntimeError, "inspect"), exclusive_cache(claim):
                        self.fail("Unknown writer claim was adopted")
                    claim.unlink()

    def test_daemon_failure_preserves_claim_but_completed_absence_releases_it(self) -> None:
        """Cleanup failure retains ownership; successful CLI completion permits an empty query."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            claim = root / "active"
            with patch("docker_execution.owned_process") as owned:
                owned.return_value.__enter__.return_value.wait.return_value = 0
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
                            ["docker", "run"], root / "log", ROOT, "gate", "token"
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
                            ["docker", "run"], root / "log", ROOT, "gate", "token"
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
            docker_execution.remove_owned("docker", "gate", "token")
