# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual expanded Ninja rules expose generator-expression and per-instance lint bypasses."""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import check_build_coverage
import ninja_lint_coverage
from architecture_api import find_clang_query
from audit_build import read_cache
from check_reference_suite import STRICT_WARNINGS


class NinjaLintTests(unittest.TestCase):
    """Probe real configured rules rather than comments or unevaluated target declarations."""

    def configure(self, root: Path, declarations: str) -> Path:
        """Configure a complete, isolated miniature with the real target guard."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        tidy = Path(find_clang_query()).resolve().with_name("clang-tidy")
        (root / "src").mkdir(exist_ok=True)
        (root / "src/a.cpp").write_text("int callback() { return 0; }\n")
        flags = " ".join([*STRICT_WARNINGS, "-Werror"])
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 4.4)\nproject(Lint LANGUAGES CXX)\n"
            "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
            "add_library(options INTERFACE)\nadd_library(DocEnhance::options ALIAS options)\n"
            "set(DE_ENABLE_CLANG_TIDY TRUE)\n"
            "if(MSVC)\ntarget_compile_options(options INTERFACE /W4 /WX)\nelse()\n"
            f"target_compile_options(options INTERFACE {flags})\nendif()\n"
            f'set(DE_CLANG_TIDY "{tidy.as_posix()}" CACHE FILEPATH "Admitted tool")\n'
            'set(DE_CLANG_TIDY_COMMAND "${DE_CLANG_TIDY};--warnings-as-errors=*;'
            '--extra-arg=-Wno-unknown-warning-option;--use-color")\n'
            "if(MSVC)\nlist(APPEND DE_CLANG_TIDY_COMMAND --extra-arg-before=/EHsc)\nendif()\n"
            f'include("{(ROOT / "cmake/TargetOptions.cmake").as_posix()}")\n' + declarations,
        )
        build = root / "out/build"
        subprocess.run(
            [str(cmake), "-S", str(root), "-B", str(build), "-G", "Ninja"],
            capture_output=True,
            text=True,
            check=True,
        )
        return build

    def test_genexpr_skip_is_caught_by_actual_compiler_rule(self) -> None:
        """A consumed object in the CMake database can still have no linter execution."""
        declarations = (
            'add_library(a OBJECT "$<$<BOOL:TRUE>:src/a.cpp>")\n'
            "target_link_libraries(a PRIVATE DocEnhance::options)\n"
            'set_property(TARGET a PROPERTY CXX_CLANG_TIDY "${DE_CLANG_TIDY_COMMAND}")\n'
            "add_executable(consumer $<TARGET_OBJECTS:a>)\n"
            "target_link_libraries(consumer PRIVATE DocEnhance::options)\n"
            'set_property(TARGET consumer PROPERTY CXX_CLANG_TIDY "${DE_CLANG_TIDY_COMMAND}")\n'
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = self.configure(root, declarations)
            entries = json.loads((build / "compile_commands.json").read_text())
            self.assertEqual(len(entries), 1)
            self.assertEqual(check_build_coverage.coverage_errors(root, build), [])
            cache = read_cache(build / "CMakeCache.txt")
            entry = next(
                item
                for item in ninja_lint_coverage.expanded_entries(build, cache)
                if item["file"].endswith("a.cpp")
            )
            self.assertIsNone(ninja_lint_coverage.wrapper_error(entry, cache))
            for old, replacement, expected in (
                ("--warnings-as-errors=*", "--warnings-as-errors=clang-analyzer-*", "options"),
                (cache["DE_CLANG_TIDY"], "/unadmitted/clang-tidy", "executable"),
                ("--source=", "--wrong-source=", "arguments"),
            ):
                changed = {**entry, "command": entry["command"].replace(old, replacement)}
                error = ninja_lint_coverage.wrapper_error(changed, cache)
                self.assertIsNotNone(error)
                self.assertIn(expected, str(error))
            for flag in ("-w", "-Wno-error", "-Wno-everything", "/w", "/W0", "/WX-"):
                changed = {**entry, "command": entry["command"] + " " + flag}
                error = ninja_lint_coverage.wrapper_error(changed, cache)
                self.assertIsNotNone(error, flag)
                self.assertIn("blanket", str(error))
            build = self.configure(
                root,
                declarations
                + "set_source_files_properties(src/a.cpp PROPERTIES SKIP_LINTING TRUE)\n",
            )
            errors = check_build_coverage.coverage_errors(root, build)
            self.assertEqual(len(errors), 1, errors)
            self.assertIn("no clang-tidy wrapper", errors[0])

    def test_every_compiler_instance_and_unadmitted_extension_are_checked(self) -> None:
        """One healthy target cannot approve another skipped instance of the same source."""
        declarations = (
            "add_library(a STATIC src/a.cpp)\n"
            "target_link_libraries(a PRIVATE DocEnhance::options)\n"
            'set_property(TARGET a PROPERTY CXX_CLANG_TIDY "${DE_CLANG_TIDY_COMMAND}")\n'
            "add_subdirectory(child)\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "child").mkdir()
            (root / "child/CMakeLists.txt").write_text(
                'add_library(b STATIC "$<$<BOOL:TRUE>:../src/a.cpp>")\n'
                "target_link_libraries(b PRIVATE DocEnhance::options)\n"
                'set_property(TARGET b PROPERTY CXX_CLANG_TIDY "${DE_CLANG_TIDY_COMMAND}")\n'
                "set_source_files_properties(../src/a.cpp PROPERTIES SKIP_LINTING TRUE)\n"
            )
            build = self.configure(root, declarations)
            entries = json.loads((build / "compile_commands.json").read_text())
            self.assertEqual(len(entries), 2)
            errors = check_build_coverage.coverage_errors(root, build)
            self.assertEqual(len(errors), 1, errors)
            self.assertIn("child/CMakeFiles/b.dir", errors[0].replace("\\", "/"))
            (root / "src/a.unadmitted").write_text("int other() { return 0; }\n")
            build = self.configure(
                root,
                declarations.replace("add_subdirectory(child)\n", "")
                + "set_source_files_properties(src/a.unadmitted PROPERTIES LANGUAGE CXX)\n"
                + "target_sources(a PRIVATE src/a.unadmitted)\n",
            )
            errors = check_build_coverage.coverage_errors(root, build)
            self.assertTrue(any("extension has no admitted" in error for error in errors), errors)
