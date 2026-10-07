# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real CMake compiler entries prove coverage; comments and dead branches cannot."""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import check_build_coverage

CMAKE = shutil.which("cmake")


class BuildCoverageTests(unittest.TestCase):
    """Exercise compiler coverage and final target admission at the CMake boundary."""

    def test_commented_or_disabled_source_remains_an_orphan(self) -> None:
        """An actual configure writes only the active compilation entries."""
        self.assertIsNotNone(CMAKE, "CMake is required for compiler-coverage controls")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            for name in ("real", "orphan", "disabled"):
                (root / f"src/{name}.cpp").write_text(f"int {name}() {{ return 0; }}\n")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.31)\nproject(Coverage LANGUAGES CXX)\n"
                "# ${PROJECT_SOURCE_DIR}/src/orphan.cpp\n"
                "if(FALSE)\n  add_library(disabled STATIC src/disabled.cpp)\nendif()\n"
                "add_library(real STATIC src/real.cpp)\n"
            )
            build = root / "out/build"
            subprocess.run(
                [
                    str(CMAKE),
                    "-S",
                    str(root),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            errors = check_build_coverage.source_coverage_errors(root, build)
            self.assertEqual(len(errors), 2, errors)
            self.assertTrue(any("src/orphan.cpp" in error for error in errors))
            self.assertTrue(any("src/disabled.cpp" in error for error in errors))
            (root / "src/orphan.cpp").unlink()
            (root / "src/disabled.cpp").unlink()
            self.assertEqual(check_build_coverage.source_coverage_errors(root, build), [])
            database = build / "compile_commands.json"
            database.write_text("[]")
            self.assertTrue(check_build_coverage.source_coverage_errors(root, build))
            database.write_text(json.dumps([{"file": "src/real.cpp"}]))
            self.assertTrue(check_build_coverage.source_coverage_errors(root, build))

    def test_final_target_properties_reject_comments_and_late_overrides(self) -> None:
        """Both root and nested compile targets need actual options and the exact linter."""
        self.assertIsNotNone(CMAKE, "CMake is required for compiler-coverage controls")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/a.cpp").write_text("int a() { return 0; }\n")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.31)\nproject(Options LANGUAGES CXX)\n"
                "add_library(de_project_options INTERFACE)\n"
                "add_library(DocEnhance::options ALIAS de_project_options)\n"
                "set(DE_ENABLE_CLANG_TIDY TRUE)\n"
                'set(DE_CLANG_TIDY_COMMAND "clang-tidy;--warnings-as-errors=*")\n'
                f'include("{(ROOT / "cmake/TargetOptions.cmake").as_posix()}")\n'
                "add_subdirectory(src)\n"
            )
            good = (
                "add_library(a STATIC a.cpp)\n"
                "target_link_libraries(a PRIVATE DocEnhance::options)\n"
                'set_property(TARGET a PROPERTY CXX_CLANG_TIDY "${DE_CLANG_TIDY_COMMAND}")\n'
            )
            cases = (
                (good, None),
                ("add_library(a STATIC a.cpp)\n# de_apply_options(a)\n", "actual"),
                (good + 'set_property(TARGET a PROPERTY CXX_CLANG_TIDY "")\n', "effective"),
                (good + 'set_property(TARGET a PROPERTY LINK_LIBRARIES "")\n', "actual"),
                (
                    good + "set_source_files_properties(a.cpp PROPERTIES SKIP_LINTING TRUE)\n",
                    "SKIP_LINTING",
                ),
                (good.replace("STATIC a.cpp", 'STATIC "${CMAKE_CURRENT_SOURCE_DIR}/a.cpp"'), None),
                (
                    good.replace("STATIC a.cpp", 'STATIC "${CMAKE_CURRENT_SOURCE_DIR}/a.cpp"')
                    + 'set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/a.cpp" '
                    "PROPERTIES SKIP_LINTING TRUE)\n",
                    "SKIP_LINTING",
                ),
                (good.replace("STATIC a.cpp", "STATIC ../src/a.cpp"), None),
                (
                    good.replace("STATIC a.cpp", "STATIC ../src/a.cpp")
                    + "set_source_files_properties(../src/a.cpp PROPERTIES SKIP_LINTING TRUE)\n",
                    "SKIP_LINTING",
                ),
            )
            for index, (text, expected) in enumerate(cases):
                (root / "src/CMakeLists.txt").write_text(text)
                result = subprocess.run(
                    [
                        str(CMAKE),
                        "--trace-expand",
                        "--trace-source",
                        str(ROOT / "cmake/TargetOptions.cmake"),
                        "-S",
                        str(root),
                        "-B",
                        str(root / f"out/build-{index}"),
                        "-G",
                        "Ninja",
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(
                    result.returncode == 0,
                    expected is None,
                    f"case {index}: {text!r}\n{result.stdout}{result.stderr}",
                )
                if expected is not None:
                    self.assertIn(expected, result.stderr)
