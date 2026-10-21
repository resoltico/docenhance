# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real configuration path controls preserve physical ownership and output boundaries."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

from test_build_configuration import CONFIGURE_TIMEOUT


class BuildLocationTests(unittest.TestCase):
    """The pre-project guard uses physical containment and the reserved root out component."""

    def test_physical_locations_are_admitted_before_compiler_work(self) -> None:
        """Internal aliases fail early; canonical/external trees and external out links pass."""
        with tempfile.TemporaryDirectory(prefix="docenhance-location-") as temporary:
            root = Path(temporary).resolve()
            source = root / "checkout"
            source.mkdir()
            (source / "src").mkdir()
            (source / "CMakeLists.txt").write_text(
                f"""cmake_minimum_required(VERSION 4.4)
include("{ROOT.as_posix()}/cmake/BuildPolicy.cmake")
file(WRITE "${{CMAKE_BINARY_DIR}}/admitted" "yes")
project(Location LANGUAGES NONE)
""",
                encoding="utf-8",
            )
            cmake = shutil.which("cmake")
            self.assertIsNotNone(cmake)
            environment = os.environ.copy()
            for name in ("CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS"):
                environment.pop(name, None)
            alias = root / "source-alias"
            alias.symlink_to(source, target_is_directory=True)
            for build in (
                source,
                source / "src/build",
                source / "out-other",
                alias,
                alias / "src/build",
            ):
                with self.subTest(build=build):
                    result = subprocess.run(
                        [str(cmake), "-S", str(source), "-B", str(build), "-G", "Ninja"],
                        env=environment,
                        capture_output=True,
                        text=True,
                        check=False,
                        timeout=CONFIGURE_TIMEOUT,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("must be beneath its root out", result.stdout + result.stderr)
                    self.assertFalse((build / "admitted").exists())
                    self.assertNotIn("compiler identification", result.stdout + result.stderr)
            for build in (source / "out/dev", root / "external build"):
                for _ in range(2):
                    result = subprocess.run(
                        [str(cmake), "-S", str(source), "-B", str(build), "-G", "Ninja"],
                        env=environment,
                        capture_output=True,
                        text=True,
                        check=False,
                        timeout=CONFIGURE_TIMEOUT,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertTrue((build / "admitted").is_file())
            redirected = root / "redirected-checkout"
            shutil.copytree(
                source,
                redirected,
                ignore=shutil.ignore_patterns("out", "CMakeFiles", "CMakeCache.txt"),
            )
            (redirected / "out").symlink_to(redirected / "src", target_is_directory=True)
            result = subprocess.run(
                [
                    str(cmake),
                    "-S",
                    str(redirected),
                    "-B",
                    str(redirected / "out/build"),
                    "-G",
                    "Ninja",
                ],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
                timeout=CONFIGURE_TIMEOUT,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((redirected / "src/build/admitted").exists())
            (redirected / "out").unlink()
            external = root / "external-output"
            external.mkdir()
            (redirected / "out").symlink_to(external, target_is_directory=True)
            result = subprocess.run(
                [
                    str(cmake),
                    "-S",
                    str(redirected),
                    "-B",
                    str(redirected / "out/dev"),
                    "-G",
                    "Ninja",
                ],
                env=environment,
                capture_output=True,
                text=True,
                check=False,
                timeout=CONFIGURE_TIMEOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((external / "dev/admitted").is_file())
