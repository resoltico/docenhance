# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Every suppression syntax is found, attributed to its rules, or rejected outright."""

from __future__ import annotations

import unittest

from tools_path import ROOT

import check_gates
import suppressions


def rules(result: suppressions.ScanResult) -> list[str]:
    """Return the tool-qualified rules of every suppression found."""
    return [s.rule for s in result.suppressions]


class CxxSuppressionTests(unittest.TestCase):
    """clang-tidy, compiler, formatter and sanitizer suppressions in C and C++."""

    def test_named_nolint_forms_are_attributed(self) -> None:
        """NOLINT and NOLINTNEXTLINE record each named check."""
        text = "int a; // NOLINT(misc-a, misc-b)\n// NOLINTNEXTLINE(bugprone-c)\nint b;\n"
        result = suppressions.scan_cxx("x.cpp", text)
        self.assertEqual(result.errors, [])
        expected = ["misc-a", "misc-b", "bugprone-c"]
        self.assertEqual(rules(result), [f"clang-tidy/{r}" for r in expected])
        self.assertEqual([s.line for s in result.suppressions], [1, 1, 2])

    def test_blanket_nolint_is_rejected(self) -> None:
        """A NOLINT without a check list silences everything and is never allowed."""
        for text in ("int a; // NOLINT\n", "// NOLINTNEXTLINE\n", "int a; // nolint()\n"):
            result = suppressions.scan_cxx("x.cpp", text)
            self.assertEqual(rules(result), [], text)
            self.assertEqual(len(result.errors), 1, text)

    def test_string_literals_cannot_fake_or_hide_markers(self) -> None:
        """Markers inside literals are ignored; apostrophes in comments do not hide markers."""
        fake = 'const char* s = "// NOLINT(misc-x)"; auto r = R"(// NOLINT(misc-y))";\n'
        self.assertEqual(rules(suppressions.scan_cxx("x.cpp", fake)), [])
        hidden = "int a; // it's quiet NOLINT(misc-z) isn't it\n"
        self.assertEqual(rules(suppressions.scan_cxx("x.cpp", hidden)), ["clang-tidy/misc-z"])
        quoted = 'const char* t = "__attribute__((no_sanitize(\\"address\\")))";\n'
        self.assertEqual(rules(suppressions.scan_cxx("x.cpp", quoted)), [])
        separators = "int n = 1'000'000; // NOLINT(misc-w)\n"
        self.assertEqual(rules(suppressions.scan_cxx("x.cpp", separators)), ["clang-tidy/misc-w"])

    def test_range_nolint_is_rejected_even_when_balanced(self) -> None:
        """A named range can conceal unlimited future code and is never registrable."""
        for text in (
            "// NOLINTBEGIN(misc-a)\nint a;\n",
            "// NOLINTBEGIN(misc-a)\nint a;\n// NOLINTEND(misc-a)\n",
        ):
            result = suppressions.scan_cxx("x.cpp", text)
            self.assertTrue(result.errors)
            self.assertEqual(result.suppressions, [])

    def test_next_line_key_covers_the_actual_target(self) -> None:
        """Changing suppressed code changes its key; unrelated line shifts do not."""
        original = "// NOLINTNEXTLINE(misc-a)\nint a;\n"
        key = suppressions.scan_cxx("x.cpp", original).suppressions[0].key
        moved = suppressions.scan_cxx("x.cpp", "// other\n" + original).suppressions[0].key
        changed = suppressions.scan_cxx("x.cpp", original.replace("int a", "int b"))
        self.assertEqual(key, moved)
        self.assertNotEqual(key, changed.suppressions[0].key)
        self.assertTrue(suppressions.scan_cxx("x.cpp", original.splitlines()[0]).errors)

    def test_compiler_and_formatter_suppressions(self) -> None:
        """Diagnostic pragmas, MSVC warning pragmas and clang-format off are all recorded."""
        text = (
            '#pragma GCC diagnostic ignored "-Wshadow"\n'
            '#pragma clang diagnostic warning "-Wconversion"\n'
            "#pragma warning(disable: 4996 4244)\n"
            "// clang-format off\n"
            "[[gsl::suppress(type.1)]] void f();\n"
            '__attribute__((no_sanitize("undefined"))) void g();\n'
        )
        self.assertEqual(
            rules(suppressions.scan_cxx("x.cpp", text)),
            [
                "compiler/-Wshadow",
                "compiler/-Wconversion",
                "msvc/4996",
                "msvc/4244",
                "clang-format/off",
                "gsl/type.1",
                "sanitizer/undefined",
            ],
        )

    def test_system_header_pragma_is_forbidden(self) -> None:
        """Declaring a first-party header a system header hides all its warnings."""
        result = suppressions.scan_cxx("x.hpp", "#pragma GCC system_header\n")
        self.assertEqual(len(result.errors), 1)


class PythonSuppressionTests(unittest.TestCase):
    """Ruff, mypy, pyright, pylint, formatter and coverage suppressions in Python."""

    def test_named_suppressions_are_attributed(self) -> None:
        """Each named rule becomes one registrable suppression."""
        text = (
            "a = 1  # noqa: E501, S101\n"
            "b: int = c  # type: ignore[assignment]\n"
            "d = 2  # pyright: ignore[reportGeneralTypeIssues]\n"
            "# fmt: off\n"
            "e = 3  # pragma: no cover\n"
            "f = 4  # pylint: disable=invalid-name\n"
        )
        self.assertEqual(
            rules(suppressions.scan_python("x.py", text)),
            [
                "ruff/E501",
                "ruff/S101",
                "mypy/assignment",
                "pyright/reportGeneralTypeIssues",
                "ruff-format/off",
                "coverage/cover",
                "pylint/invalid-name",
            ],
        )

    def test_python_key_covers_code_not_just_the_comment(self) -> None:
        """A copied identical marker cannot approve different Python code."""
        original = "a = 1  # noqa: S101\n"
        key = suppressions.scan_python("x.py", original).suppressions[0].key
        moved = suppressions.scan_python("x.py", "# header\n" + original).suppressions[0].key
        changed = suppressions.scan_python("x.py", "b = 2  # noqa: S101\n").suppressions[0].key
        self.assertEqual(key, moved)
        self.assertNotEqual(key, changed)

    def test_blanket_and_file_wide_forms_are_rejected(self) -> None:
        """Unnamed, file-wide, range-wide and inline-config forms are never allowed."""
        for line in (
            "a = 1  # noqa\n",
            "a = 1  # type: ignore\n",
            "# ruff: noqa\n",
            "# ruff: noqa: E501\n",
            "# flake8: noqa\n",
            "# ruff: disable[E501]\n",
            "# mypy: ignore-errors\n",
        ):
            result = suppressions.scan_python("x.py", line)
            self.assertEqual(rules(result), [], line)
            self.assertTrue(result.errors, line)

    def test_strings_and_docstrings_are_not_comments(self) -> None:
        """Only comment tokens are scanned, so documentation cannot trip or hide a marker."""
        text = '"""Use # noqa sparingly."""\nX = "# type: ignore"\n'
        result = suppressions.scan_python("x.py", text)
        self.assertEqual((rules(result), result.errors), ([], []))


class CMakeSuppressionTests(unittest.TestCase):
    """Warning-disabling flags and lint-skipping properties in CMake."""

    def test_flags_and_properties_are_recorded(self) -> None:
        """-Wno-*, -w, /wd, SKIP_LINTING and SYSTEM includes each need registration."""
        text = (
            "target_compile_options(a PRIVATE -Wno-shadow -w /wd4996)\n"
            "set_source_files_properties(b.cpp PROPERTIES SKIP_LINTING ON)\n"
            "target_include_directories(a SYSTEM PRIVATE include)\n"
            "target_compile_options(a PRIVATE -Wall -Wextra --warnings-as-errors=* /W4)\n"
        )
        self.assertEqual(
            rules(suppressions.scan_cmake("CMakeLists.txt", text)),
            [
                "compiler/-Wno-shadow",
                "compiler/-w",
                "msvc//wd4996",
                "cmake/SKIP_LINTING",
                "cmake/SYSTEM",
            ],
        )


class RepositoryTests(unittest.TestCase):
    """The repository itself uses no rejected suppression form."""

    def test_repository_has_no_rejected_forms(self) -> None:
        """Scanning every code file yields suppressions only, never rejected forms."""
        errors = []
        for path, rel, kind in check_gates.code_files(ROOT):
            errors.extend(suppressions.scan(path, rel, kind).errors)
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
