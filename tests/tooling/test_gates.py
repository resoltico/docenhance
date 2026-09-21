# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""The quality gates catch every way around them, and the repository passes them all."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

import check_gates
import config_gates
import repo_hygiene
import suppressions


def write(root: Path, rel: str, text: str) -> Path:
    """Create a file (and its parents) below root."""
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def registry(root: Path, entries: dict[str, object]) -> Path:
    """Write a suppression registry with the given entries."""
    return write(root, "tests/exceptions/registry.json", json.dumps({"exceptions": entries}))


class GateTestCase(unittest.TestCase):
    """Each test gets an empty temporary repository."""

    @override
    def setUp(self) -> None:
        """Create the temporary repository root."""
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)

    @override
    def tearDown(self) -> None:
        """Remove the temporary repository."""
        self._temp.cleanup()


class GodFileTests(GateTestCase):
    """Size limits apply to every code file and have no waiver mechanism."""

    def test_limits_per_category(self) -> None:
        """Production, test and build files each have their own ceiling."""
        limits = check_gates.LINE_LIMITS
        write(self.root, "src/ok.cpp", "x\n" * limits["production"])
        write(self.root, "src/big.cpp", "x\n" * (limits["production"] + 1))
        write(self.root, "tools/big.py", "x\n" * (limits["production"] + 1))
        write(self.root, "tests/ok.cpp", "x\n" * limits["test"])
        write(self.root, "fuzz/big.cpp", "x\n" * (limits["test"] + 1))
        write(self.root, "cmake/big.cmake", "x\n" * (limits["build"] + 1))
        errors = check_gates.god_file_errors(self.root)
        flagged = sorted(e.split()[2] for e in errors)
        self.assertEqual(
            flagged, ["cmake/big.cmake", "fuzz/big.cpp", "src/big.cpp", "tools/big.py"]
        )

    def test_no_directory_is_exempt(self) -> None:
        """A directory named like a data directory is not exempt below the root."""
        write(self.root, "src/core/big.cpp", "x\n" * (check_gates.LINE_LIMITS["production"] + 1))
        write(self.root, "include/x/deps/big.hpp", "x\n" * 400)
        self.assertEqual(len(check_gates.god_file_errors(self.root)), 2)

    def test_uncommon_extensions_are_code(self) -> None:
        """New C++ extensions and CMake templates cannot dodge the gates."""
        for name in ("a.cc", "a.cxx", "a.hh", "a.ipp", "a.inl", "a.hpp.in", "CMakeLists.txt"):
            self.assertIsNotNone(check_gates.source_kind(Path(name)), name)


class RegistryTests(GateTestCase):
    """Suppressions must be registered with a reason; registrations must be in use."""

    def keys(self, rel: str, kind: str = "cxx") -> list[str]:
        """The registry keys of every suppression in a file of the temporary repository."""
        path = self.root / rel
        return [s.key for s in suppressions.scan(path, rel, kind).suppressions]

    def test_unregistered_registered_and_stale(self) -> None:
        """Only an exact, explained registration passes; unused entries fail."""
        write(self.root, "src/a.cpp", "int a; // NOLINT(misc-x)\nint b; // NOLINT(misc-y)\n")
        first, second = self.keys("src/a.cpp")
        path = registry(
            self.root,
            {
                first: {"explanation": "A reason of adequate length."},
                "src/a.cpp:clang-tidy/misc-z@deadbeef": {"explanation": "A stale reason, long."},
            },
        )
        errors = check_gates.suppression_errors(self.root, path)
        self.assertEqual(len(errors), 2)
        self.assertIn(f"Unregistered suppression {second}", errors[0])
        self.assertIn("Stale suppression registry entry: src/a.cpp:clang-tidy/misc-z", errors[1])

    def test_key_survives_line_shifts_but_not_edits(self) -> None:
        """Moving a suppression keeps its key; changing the suppressed line does not."""
        write(self.root, "src/a.cpp", "int a; // NOLINT(misc-x)\n")
        original = self.keys("src/a.cpp")
        write(self.root, "src/a.cpp", "// a new header line\nint a;   // NOLINT(misc-x)\n")
        self.assertEqual(self.keys("src/a.cpp"), original)
        write(self.root, "src/a.cpp", "int b; // NOLINT(misc-x)\n")
        self.assertNotEqual(self.keys("src/a.cpp"), original)

    def test_registry_schema(self) -> None:
        """Explanations must be real sentences and entries carry nothing else."""
        write(self.root, "src/a.cpp", "int a; // NOLINT(misc-x)\n")
        key = self.keys("src/a.cpp")[0]
        for entry in ({"explanation": "ok"}, {"explanation": "Long enough reason here.", "x": 1}):
            path = registry(self.root, {key: entry})
            self.assertEqual(len(check_gates.suppression_errors(self.root, path)), 1, entry)


class CoverageTests(GateTestCase):
    """Every translation unit is built, and every target gets warnings and clang-tidy."""

    def test_unbuilt_translation_unit(self) -> None:
        """A .cpp that no CMake file names would never be compiled or linted."""
        write(self.root, "src/CMakeLists.txt", "add_library(a STATIC a.cpp)\n")
        write(self.root, "src/a.cpp", "")
        write(self.root, "src/orphan.cpp", "")
        errors = check_gates.build_coverage_errors(self.root)
        self.assertEqual(
            errors, ["src/orphan.cpp is not compiled by any CMake target, so it is never linted"]
        )

    def test_target_without_project_options(self) -> None:
        """A target that skips de_apply_options() gets neither warnings nor clang-tidy."""
        text = (
            "add_library(good STATIC a.cpp)\nde_apply_options(good)\n"
            "add_executable(bad b.cpp)\nadd_library(iface INTERFACE)\n"
        )
        write(self.root, "src/CMakeLists.txt", text)
        errors = check_gates.target_option_errors(self.root)
        self.assertEqual(errors, ["src/CMakeLists.txt: target bad lacks de_apply_options(bad)"])


class ConfigGateTests(GateTestCase):
    """Configuration cannot quietly relax the linters."""

    def test_clang_tidy_disabled_checks_need_reasons(self) -> None:
        """A disabled check must be named in a comment; nested files cannot relax options."""
        root_config = (
            "# -a-check is off because of a reason.\n"
            "Checks: >\n  -*,\n  a-*,\n  -a-check,\n  -b-check\n"
            "WarningsAsErrors: '*'\n"
        )
        path = write(self.root, ".clang-tidy", root_config)
        self.assertEqual(
            config_gates.clang_tidy_errors(self.root, path),
            [".clang-tidy: -b-check is disabled without a documented reason in the file"],
        )
        nested = write(self.root, "src/.clang-tidy", "Checks: '-*'\nCheckOptions:\n  x: 1\n")
        self.assertEqual(len(config_gates.clang_tidy_errors(self.root, nested)), 3)

    def test_ruff_ignores_need_reasons_and_select_all(self) -> None:
        """Ruff must select ALL, exclude only artifacts, and justify every ignore."""
        text = (
            'extend-exclude = [".cache", "out", "dist", "tools"]\n[lint]\nselect = ["E"]\n'
            'ignore = [\n    "D100", # A reason.\n    "E501",\n]\n'
        )
        write(self.root, "ruff.toml", text)
        self.assertEqual(len(config_gates.ruff_errors(self.root)), 3)

    def test_mypy_escape_hatches(self) -> None:
        """Module overrides, disabled codes, missing strictness and uncovered files fail."""
        text = (
            "[mypy]\nfiles = tools\ndisable_error_code = misc\n[mypy-deps]\nignore_errors = True\n"
        )
        write(self.root, "mypy.ini", text)
        stray = write(self.root, "scripts/x.py", "")
        errors = config_gates.mypy_errors(self.root, [stray])
        self.assertEqual(len(errors), 4)

    def test_nested_configs_and_disabled_formatting(self) -> None:
        """Only root ruff.toml/mypy.ini exist; clang-format is never disabled."""
        files = [
            write(self.root, "tools/ruff.toml", ""),
            write(self.root, "pyproject.toml", ""),
            write(self.root, "src/.clang-format", "BasedOnStyle: LLVM\n"),
            write(self.root, ".clang-format", "DisableFormat: true\n"),
        ]
        self.assertEqual(len(config_gates.nested_config_errors(self.root, files)), 4)

    def test_presets_cannot_disable_enforcement(self) -> None:
        """No preset may turn off clang-tidy or warnings-as-errors."""
        presets = {
            "configurePresets": [
                {"name": "base", "cacheVariables": {"DE_ENABLE_CLANG_TIDY": True}},
                {"name": "quick", "cacheVariables": {"DE_WARNINGS_AS_ERRORS": False}},
            ]
        }
        write(self.root, "CMakePresets.json", json.dumps(presets))
        self.assertEqual(len(config_gates.preset_errors(self.root)), 2)


class HygieneTests(GateTestCase):
    """Repository hygiene: stray files, names, text shape, modes, attributes and references."""

    def prepare(self) -> None:
        """Give the temporary repository the files every hygiene check expects."""
        write(self.root, ".gitignore", ".DS_Store\n*.pyc\n")
        write(self.root, ".gitattributes", "fuzz/corpus/** binary\nfuzz/regressions/** binary\n")
        for name in repo_hygiene.REQUIRED_DOCUMENTS:
            write(self.root, name, "# Document\n")

    def errors(self) -> list[str]:
        """Run the hygiene gates over the temporary repository."""
        files = [
            (p, p.relative_to(self.root).as_posix()) for p in self.root.rglob("*") if p.is_file()
        ]
        return repo_hygiene.check(self.root, files)

    def test_clean_repository_passes(self) -> None:
        """A tree with the required documents and no droppings passes."""
        self.prepare()
        self.assertEqual(self.errors(), [])

    def test_stray_and_ignored_files(self) -> None:
        """Unignored droppings fail; names .gitignore already excludes do not."""
        self.prepare()
        write(self.root, "src/a.cpp.orig", "x\n")
        write(self.root, ".DS_Store", "ignored\n")
        errors = self.errors()
        self.assertEqual([e for e in errors if "Stray" in e], ["Stray file: src/a.cpp.orig"])

    def test_text_shape(self) -> None:
        """CRLF endings, a missing final newline, trailing blanks and tabs each fail."""
        self.prepare()
        write(self.root, "src/crlf.cpp", "int a;\r\nint b;\r\n")
        write(self.root, "src/tail.cpp", "int a;")
        write(self.root, "src/blank.cpp", "int a; \n")
        write(self.root, "src/tab.cpp", "int\ta;\n")
        (self.root / "src/empty.cpp").write_text("")
        found = " ".join(self.errors())
        for expected in (
            "CRLF line endings",
            "No final newline",
            "Trailing whitespace",
            "Tab indentation",
            "Empty file",
        ):
            self.assertIn(expected, found)

    def test_markdown_keeps_trailing_spaces(self) -> None:
        """Trailing spaces are significant line breaks in Markdown."""
        self.prepare()
        write(self.root, "docs/note.md", "line  \nnext\n")
        self.assertEqual([e for e in self.errors() if "note.md" in e], [])

    def test_executable_bit_matches_shebang(self) -> None:
        """A script without a shebang must not be executable, and vice versa."""
        self.prepare()
        script = write(self.root, "tools/a.py", "# SPDX\n")
        script.chmod(0o755)
        self.assertTrue(any("shebang" in e for e in self.errors()))

    def test_binary_trees_and_documents_are_declared(self) -> None:
        """Byte-exact corpora must be declared binary and required documents must exist."""
        self.prepare()
        write(self.root, ".gitattributes", "* text=auto eol=lf\n")
        (self.root / ".github/SECURITY.md").unlink()
        found = " ".join(self.errors())
        self.assertIn("must declare fuzz/corpus/** binary", found)
        self.assertIn("Missing required document: .github/SECURITY.md", found)

    def test_documentation_references_must_exist(self) -> None:
        """Documentation may not quote repository paths that do not exist."""
        self.prepare()
        write(self.root, "docs/guide.md", "See `tools/gone.py` and `docs/status.md`.\n")
        self.assertEqual(
            [e for e in self.errors() if "does not exist" in e],
            ["docs/guide.md refers to a path that does not exist: tools/gone.py"],
        )


class RepositoryTests(unittest.TestCase):
    """The repository itself passes every gate, with nothing grandfathered."""

    def test_repository_passes_all_gates(self) -> None:
        """No god files, unregistered suppressions, unlinted files or relaxed configs."""
        self.assertEqual(check_gates.check_gates(ROOT), [])


if __name__ == "__main__":
    unittest.main()
