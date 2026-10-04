# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Actual subprocess failures and malformed GitHub response envelopes remain refusals."""

from __future__ import annotations

import subprocess
import sys
import unittest
from unittest.mock import patch

from tools_path import ROOT

from changelog import ReleaseError
from github_release_api import GitHubAPI, run


class GitHubTransportTests(unittest.TestCase):
    """No permissive decoding, summary-only success or timeout retry hides transport failure."""

    def test_actual_process_failure_and_invalid_utf8_refused(self) -> None:
        """Exercise real processes, independent of an installed/authenticated GitHub CLI."""
        self.assertTrue((ROOT / "tools/github_release_api.py").is_file())
        for code in ("raise SystemExit(3)", "import os; os.write(1, bytes([255]))"):
            with self.subTest(code=code), self.assertRaises(ReleaseError):
                run([sys.executable, "-c", code])
        for payload in (b"observed\n", b"observed\r\n"):
            with self.subTest(payload=payload):
                code = f"import os; os.write(1, {payload!r})"
                self.assertEqual(run([sys.executable, "-c", code]), payload.decode("utf-8"))

    def test_timeout_is_one_refusal_without_retry(self) -> None:
        """A reported transport timeout cannot silently repeat a possible remote effect."""
        with patch(
            "github_release_api.subprocess.run",
            side_effect=subprocess.TimeoutExpired("controlled", 120),
        ) as execute:
            with self.assertRaisesRegex(ReleaseError, "reconcile remote state"):
                run(["controlled"])
            self.assertEqual(execute.call_count, 1)

    def test_invalid_json_and_tag_envelopes_refused(self) -> None:
        """The real request/tag validators reject malformed transport data before use."""
        with patch("github_release_api.shutil.which", return_value=sys.executable):
            api = GitHubAPI("example/project")
        with (
            patch("github_release_api.run", return_value="not-json"),
            self.assertRaises(ReleaseError),
        ):
            api.request("GET", "repos/example/project/releases")
        with (
            patch.object(
                api, "request", return_value={"object": {"sha": "a" * 40, "type": "tree"}}
            ),
            self.assertRaises(ReleaseError),
        ):
            api.tag_commit("v0.5.0")
