# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Real environment refusal and delivered-documentation negative controls."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

from documentation import local_link_errors


class CleanBuildTests(unittest.TestCase):
    """Compiler injection must fail before work; omitted contract documents must be visible."""

    def test_build_environment_rejects_implicit_compiler_inputs(self) -> None:
        """The same production script checks configure and build-time inherited inputs."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        command = [str(cmake), "-P", str(ROOT / "cmake/BuildEnvironment.cmake")]
        names = (
            "CFLAGS",
            "CXXFLAGS",
            "CPPFLAGS",
            "LDFLAGS",
            "CPATH",
            "C_INCLUDE_PATH",
            "CPLUS_INCLUDE_PATH",
            "OBJC_INCLUDE_PATH",
            "LIBRARY_PATH",
            "COMPILER_PATH",
            "GCC_EXEC_PREFIX",
            "CL",
            "_CL_",
            "LINK",
            "_LINK_",
            "CCC_OVERRIDE_OPTIONS",
        )
        environment = {key: value for key, value in os.environ.items() if key not in names}
        benign = subprocess.run(
            command, env=environment, capture_output=True, check=False, text=True, timeout=30
        )
        self.assertEqual(benign.returncode, 0, benign.stderr)
        for name in names:
            with self.subTest(name=name):
                result = subprocess.run(
                    command,
                    env={**environment, name: "unreviewed"},
                    capture_output=True,
                    check=False,
                    text=True,
                    timeout=30,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"{name} is unsupported", result.stderr)

    def test_compiler_environment_is_rechecked_before_existing_target(self) -> None:
        """A configured target must refuse newly inherited search paths before compilation."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "probe.cpp").write_text("int main() { return 0; }\n")
            (root / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 4.4)
project(Environment CXX)
set(DE_CXX_STANDARD 23)
include("{ROOT.as_posix()}/cmake/ProjectOptions.cmake")
add_executable(probe probe.cpp)
de_apply_options(probe)
""")
            build = root / "build"
            subprocess.run(
                [str(cmake), "-S", str(root), "-B", str(build), "-G", "Ninja"],
                check=True,
                capture_output=True,
                timeout=90,
            )
            result = subprocess.run(
                [str(cmake), "--build", str(build), "--target", "probe"],
                env={**os.environ, "CPATH": str(root)},
                capture_output=True,
                text=True,
                check=False,
                timeout=90,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CPATH is unsupported", result.stdout + result.stderr)
            self.assertFalse((build / "probe").exists())
            self.assertFalse((build / "probe.exe").exists())

    def test_missing_delivered_contract_links_are_rejected(self) -> None:
        """Presence of a README is insufficient when its required contracts are absent."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "README.md").write_text(
                "[contract](docs/input.md) [web](https://example.org)\n"
            )
            self.assertEqual(
                local_link_errors(root), ["Broken local link: README.md -> docs/input.md"]
            )
            (root / "docs").mkdir()
            document = root / "docs/input.md"
            document.write_text("[schema](../schemas/input.json) [heading](#rules)\n")
            self.assertEqual(
                local_link_errors(root),
                ["Broken local link: docs/input.md -> ../schemas/input.json"],
            )
            (root / "schemas").mkdir()
            (root / "schemas/input.json").write_text("{}\n")
            self.assertEqual(local_link_errors(root), [])

    def test_source_local_cmake_gets_actionable_refusal(self) -> None:
        """The production bootstrap refuses tools in source/build before language setup."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for location in (root / "source/tools", root / "build/tools"):
                with self.subTest(location=location):
                    script = root / "bootstrap.cmake"
                    script.write_text(f"""set(CMAKE_SOURCE_DIR "{(root / "source").as_posix()}")
set(CMAKE_BINARY_DIR "{(root / "build").as_posix()}")
set(CMAKE_ROOT "{location.as_posix()}")
include("{ROOT.as_posix()}/cmake/BuildPolicy.cmake")
""")
                    result = subprocess.run(
                        [str(cmake), "-P", str(script)],
                        capture_output=True,
                        check=False,
                        text=True,
                        timeout=30,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("Install CMake outside the source and build trees", result.stderr)
