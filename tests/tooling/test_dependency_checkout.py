# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Pinned Git source bytes do not inherit platform-native text checkout policies."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import tools_path  # noqa: F401 -- Bootstrap direct tool imports for standalone unittest discovery.

import dep_acquire
import dep_verify


class CheckoutTests(unittest.TestCase):
    """Exercise real Git attributes, pinned-object fetch and source-byte checkout."""

    def test_declared_text_uses_lf_under_inherited_crlf_policy(self) -> None:
        """A global native-EOL policy cannot change reviewed C++ source bytes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            upstream = root / "upstream"
            upstream.mkdir()
            dep_verify.run("git", "init", str(upstream))
            dep_verify.run("git", "config", "user.email", "fixture@example.invalid", cwd=upstream)
            dep_verify.run("git", "config", "user.name", "Fixture", cwd=upstream)
            (upstream / ".gitattributes").write_text("*.cpp text\n*.cmd text eol=crlf\n")
            source_name = "source" + ".cpp"
            command_name = "command" + ".cmd"
            (upstream / source_name).write_bytes(b"int value = 1;\n")
            (upstream / command_name).write_bytes(b"exit /b 0\n")
            dep_verify.run("git", "add", ".", cwd=upstream)
            dep_verify.run(
                "git", "-c", "commit.gpgsign=false", "commit", "-m", "Fixture", cwd=upstream
            )
            dep_verify.run("git", "-c", "tag.gpgsign=false", "tag", "v1", cwd=upstream)
            pinned = dep_verify.run("git", "rev-parse", "refs/tags/v1", cwd=upstream)
            global_config = root / "global-config"
            global_config.write_text("[core]\n eol=crlf\n autocrlf=false\n")
            staging = root / "staging"

            def fixture_run(*arguments: str, cwd: Path | None = None) -> str:
                # Only the test's local repository transport is changed; all acquisition,
                # object verification, attributes and checkout operations remain real Git.
                values = [
                    "protocol.file.allow=always" if value == "protocol.file.allow=never" else value
                    for value in arguments
                ]
                return dep_verify.run(*values, cwd=cwd)

            with (
                mock.patch.dict(
                    os.environ,
                    {"GIT_CONFIG_GLOBAL": str(global_config), "GIT_CONFIG_NOSYSTEM": "1"},
                ),
                mock.patch.object(dep_acquire, "run", side_effect=fixture_run),
            ):
                dep_acquire.fetch_git(
                    {
                        "name": "fixture",
                        "repository": str(upstream),
                        "ref": "refs/tags/v1",
                        "object": pinned,
                    },
                    staging,
                )
            self.assertEqual((staging / source_name).read_bytes(), b"int value = 1;\n")
            self.assertEqual((staging / command_name).read_bytes(), b"exit /b 0\r\n")


if __name__ == "__main__":
    unittest.main()
