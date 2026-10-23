# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reject mismatched ambient LLVM before any AFL++ acquisition or compilation."""

from __future__ import annotations

import json
import subprocess
import unittest
from typing import override
from unittest.mock import patch

from tools_path import ROOT

import install_aflplusplus


class AFLToolchainTests(unittest.TestCase):
    """Exercise actual installer ordering with isolated subprocess/acquisition boundaries."""

    @override
    def setUp(self) -> None:
        """Use the reviewed repository policy even when checking an isolated proposal."""
        owner = patch.object(install_aflplusplus, "ROOT", ROOT)
        owner.start()
        self.addCleanup(owner.stop)
        self.major = json.loads((ROOT / "deps/tools.json").read_text())["fuzzing"]["llvm_major"]

    def test_matching_qualified_and_generic_tools_reach_the_build(self) -> None:
        """Both lookup paths retain the verified executable in the make command."""
        for qualified in (True, False):
            with self.subTest(qualified=qualified):
                events: list[str] = []
                selected = (
                    f"/fixture/llvm-config-{self.major}" if qualified else "/fixture/llvm-config"
                )

                def lookup(
                    name: str, *, selected: str = selected, qualified: bool = qualified
                ) -> str | None:
                    if name == "make":
                        return "/fixture/make"
                    return selected if (name.endswith(f"-{self.major}") == qualified) else None

                def invoke(
                    command: list[str],
                    selected: str = selected,
                    events: list[str] = events,
                    **kwargs: object,
                ) -> subprocess.CompletedProcess[str]:
                    if command == [selected, "--version"]:
                        self.assertEqual(kwargs["timeout"], 10)
                        events.append("version")
                        return subprocess.CompletedProcess(command, 0, f"{self.major}.1.4\n", "")
                    self.assertIn(f"LLVM_CONFIG={selected}", command)
                    events.append("build")
                    return subprocess.CompletedProcess(command, 0, "", "")

                with (
                    patch("install_aflplusplus.shutil.which", side_effect=lookup),
                    patch("install_aflplusplus.subprocess.run", side_effect=invoke),
                    patch(
                        "install_aflplusplus.fetch",
                        side_effect=lambda *_, events=events: events.append("fetch"),
                    ),
                    patch.dict("os.environ", {"GITHUB_PATH": ""}),
                ):
                    self.assertEqual(install_aflplusplus.main(), 0)
                self.assertEqual(events, ["version", "fetch", "build"])

    def test_wrong_or_malformed_versions_refuse_before_fetch(self) -> None:
        """A version-qualified filename cannot bless the wrong reported version."""
        for version in (
            f"{int(self.major) - 1}.1.0",
            f"{self.major}0.1.0",
            "",
            "x" * 4096,
            f"{self.major}.invalid",
            f"version {self.major}.1.4",
            f"{self.major}.1.4\n22.0",
        ):
            with (
                self.subTest(version=version),
                patch(
                    "install_aflplusplus.shutil.which", side_effect=lambda name: f"/fixture/{name}"
                ),
                patch("install_aflplusplus.subprocess.run") as invoked,
                patch("install_aflplusplus.fetch") as fetched,
            ):
                invoked.return_value = subprocess.CompletedProcess([], 0, version, "")
                with self.assertRaisesRegex(
                    install_aflplusplus.InstallError, f"{self.major}.x is required"
                ) as caught:
                    install_aflplusplus.main()
                self.assertLess(len(str(caught.exception)), 1024)
                fetched.assert_not_called()
                self.assertEqual(invoked.call_count, 1)

    def test_failed_or_stalled_version_query_refuses_before_fetch(self) -> None:
        """Failed version inquiry never mutates the existing checkout or launches make."""
        for failure in (
            subprocess.CalledProcessError(1, ["llvm-config", "--version"]),
            subprocess.TimeoutExpired(["llvm-config", "--version"], 10),
            FileNotFoundError("selected llvm-config disappeared"),
        ):
            with (
                self.subTest(failure=type(failure).__name__),
                patch(
                    "install_aflplusplus.shutil.which", side_effect=lambda name: f"/fixture/{name}"
                ),
                patch("install_aflplusplus.subprocess.run", side_effect=failure) as invoked,
                patch("install_aflplusplus.fetch") as fetched,
                self.assertRaises(type(failure)),
            ):
                install_aflplusplus.main()
            fetched.assert_not_called()
            self.assertEqual(invoked.call_count, 1)
