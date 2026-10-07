# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real suppression counterexamples and closed registry admission controls."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

import check_gates
import config_suppressions
import suppressions


class ExceptionPolicyTests(unittest.TestCase):
    """Every approval binds preserved source, bounded scope and an exact occurrence count."""

    @override
    def setUp(self) -> None:
        """Own source fixtures; never mutate the repository or its compiler workflows."""
        temporary = tempfile.TemporaryDirectory(prefix="exception-policy-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.registry = self.root / "registry.json"

    def test_literal_and_whitespace_edits_change_full_binding(self) -> None:
        """Whitespace inside literals is code, and every bound byte requires renewed approval."""
        first = suppressions.scan_python("x.py", 'value = "a  b"  # noqa: S105\n').suppressions[0]
        changed = suppressions.scan_python("x.py", 'value = "a b"  # noqa: S105\n').suppressions[0]
        indented = suppressions.scan_python("x.py", 'value = "a  b" # noqa: S105\n').suppressions[0]
        self.assertNotEqual(first.key, changed.key)
        self.assertNotEqual(first.key, indented.key)
        self.assertEqual(len(first.key.rsplit("@", 1)[1]), 64)

    def test_duplicate_registry_keys_and_wrong_shapes_are_refused(self) -> None:
        """No overwrite, old digest, untyped count or open entry can manufacture admission."""
        key = "x.py:ruff/S105@" + ("a" * 64)
        entry = {"explanation": "An explicit reason for review.", "occurrences": 1}
        duplicate = json.dumps({"exceptions": {key: entry}}).replace(
            '"exceptions":', '"exceptions": {}, "exceptions":', 1
        )
        for text in (
            duplicate,
            '{"exceptions": []}',
            '{"exceptions": null}',
            '{"exceptions": 1}',
            '{"exceptions": {}, "extra": 1}',
        ):
            with self.subTest(text=text):
                self.registry.write_text(text)
                self.assertTrue(check_gates.load_registry(self.registry)[1])
        for change in (
            {"occurrences": True},
            {"occurrences": 0},
            {"extra": 1},
            {"explanation": "short"},
        ):
            self.registry.write_text(json.dumps({"exceptions": {key: entry | change}}))
            self.assertTrue(check_gates.load_registry(self.registry)[1])
        self.registry.write_text(json.dumps({"exceptions": {key[:-56]: entry}}))
        self.assertTrue(check_gates.load_registry(self.registry)[1])
        self.registry.write_text(json.dumps({"exceptions": {key: entry}}))
        self.assertEqual(check_gates.load_registry(self.registry)[1], [])

    def test_duplicating_a_site_requires_a_new_declared_count(self) -> None:
        """One approved line cannot silently expand into unlimited identical exemptions."""
        source = self.root / "x.py"
        line = 'value = "literal"  # noqa: S105\n'
        source.write_text(line)
        key = suppressions.scan_python("x.py", line).suppressions[0].key
        self.registry.write_text(
            json.dumps(
                {
                    "exceptions": {
                        key: {
                            "explanation": "A single code-bound test fixture site.",
                            "occurrences": 1,
                        }
                    }
                }
            )
        )
        self.assertEqual(check_gates.suppression_errors(self.root, self.registry), [])
        source.write_text(line * 2)
        self.assertTrue(
            any(
                "occurrence count" in error
                for error in check_gates.suppression_errors(self.root, self.registry)
            )
        )

    def test_region_body_and_restoration_are_bound(self) -> None:
        """Format and native warning regions cannot grow without changing their approval."""
        for original in (
            "// clang-format off\nint a;\n// clang-format on\n",
            "#pragma warning(push)\n#pragma warning(disable: 4611)\nint a;\n#pragma warning(pop)\n",
            (
                "#pragma clang diagnostic push\n"
                '#pragma clang diagnostic ignored "-Wshadow"\n'
                "int a;\n#pragma clang diagnostic pop\n"
            ),
        ):
            with self.subTest(original=original):
                first = suppressions.scan_cxx("x.cpp", original)
                self.assertEqual(first.errors, [])
                changed = suppressions.scan_cxx("x.cpp", original.replace("int a;", "int b;"))
                self.assertNotEqual(first.suppressions[0].key, changed.suppressions[0].key)
                self.assertTrue(suppressions.scan_cxx("x.cpp", original.rsplit("\n", 2)[0]).errors)

    def test_push_gap_still_binds_the_actual_restoration_scope(self) -> None:
        """A blank/comment gap cannot exclude the real push directive from the source binding."""
        code = (
            "#pragma warning(push)\n\n// native frame\n"
            "#pragma warning(disable: 4611)\nint a;\n#pragma warning(pop)\n"
        )
        first = suppressions.scan_cxx("x.cpp", code)
        self.assertEqual(first.errors, [])
        changed = suppressions.scan_cxx(
            "x.cpp", code.replace("warning(push)", "warning(push) // scope")
        )
        self.assertNotEqual(first.suppressions[0].key, changed.suppressions[0].key)
        self.assertIn("#pragma warning(push)", first.suppressions[0].text)

    def test_inline_complexity_and_size_cannot_be_approved(self) -> None:
        """Central registration cannot override the source's unsuppressible body limits."""
        for rule in ("readability-function-size", "readability-function-cognitive-complexity"):
            result = suppressions.scan_cxx("x.cpp", f"void function(); // NOLINT({rule})\n")
            self.assertTrue(result.errors)
            self.assertEqual(result.suppressions, [])
        for rule in ("C901", "PLR0911", "PLR0912", "PLR0913", "PLR0915"):
            result = suppressions.scan_python("x.py", f"value = 1 # noqa: {rule}\n")
            self.assertTrue(result.errors)
            self.assertEqual(result.suppressions, [])

    def test_quoted_restoration_cannot_shorten_real_scope(self) -> None:
        """A restore phrase in a literal does not end the actual disabled-format region."""
        code = (
            '// clang-format off\nconst char* token = "clang-format on";\n'
            "int a;\n// clang-format on\n"
        )
        first = suppressions.scan_cxx("x.cpp", code)
        changed = suppressions.scan_cxx("x.cpp", code.replace("int a;", "int b;"))
        self.assertNotEqual(first.suppressions[0].key, changed.suppressions[0].key)
        self.assertEqual(first.errors, [])
        documentation = '// _Pragma("GCC diagnostic ignored "-Wshadow"")\n'
        self.assertEqual(suppressions.scan_cxx("x.cpp", documentation).errors, [])

    def test_sanitizer_and_range_routes_cannot_be_registered(self) -> None:
        """Unsupported instrumentation opt-outs and formatter blankets are fatal policy failures."""
        for text in (
            '__attribute__((no_sanitize("address", "undefined"))) void f();\n',
            "__attribute__((disable_sanitizer_instrumentation)) void f();\n",
            "#pragma warning(disable: 4611)\n",
        ):
            result = suppressions.scan_cxx("x.cpp", text)
            self.assertTrue(result.errors)
            self.assertEqual(result.suppressions, [])
        for text in ("# fmt: off\n", "# isort: skip_file\n", "# pylint: disable=all\n"):
            self.assertTrue(suppressions.scan_python("x.py", text).errors)

    def test_real_compiler_hidden_pragma_is_refused(self) -> None:
        """A real compiler suppression succeeds, and the source guard refuses its hidden route."""
        major = json.loads((ROOT / "deps/tools.json").read_text())["fuzzing"]["llvm_major"]
        compiler = (
            shutil.which(f"clang++-{major}") or shutil.which("clang++") or shutil.which("c++")
        )
        self.assertIsNotNone(compiler, "A native C++ compiler is required")
        source = self.root / "probe.cpp"
        body = "int probe(int value) { { int value = 2; return value; } return value; }\n"
        for prefix, expected in (
            ("", False),
            ('_Pragma("GCC diagnostic ignored \\"-Wshadow\\"")\n', True),
        ):
            source.write_text(prefix + body)
            result = subprocess.run(
                [str(compiler), "-Wshadow", "-Werror", "-fsyntax-only", str(source)],
                capture_output=True,
                text=True,
                check=False,
                timeout=30,
            )
            self.assertEqual(result.returncode == 0, expected, result.stderr)
            if prefix:
                self.assertTrue(suppressions.scan_cxx("probe.cpp", prefix + body).errors)

    def test_real_ruff_suppression_cannot_reuse_changed_literal_approval(self) -> None:
        """Ruff accepts a named opt-out, but its approval rejects changed string bytes."""
        source = self.root / "probe.py"
        source.write_text('password = "a  b"\n')
        command = [
            sys.executable,
            "-m",
            "ruff",
            "check",
            "--isolated",
            "--select",
            "S105",
            str(source),
        ]
        control = subprocess.run(command, capture_output=True, text=True, check=False, timeout=30)
        self.assertNotEqual(control.returncode, 0, control.stdout + control.stderr)
        source.write_text('password = "a  b"  # noqa: S105\n')
        suppressed = subprocess.run(
            command, capture_output=True, text=True, check=False, timeout=30
        )
        self.assertEqual(suppressed.returncode, 0, suppressed.stdout + suppressed.stderr)
        key = suppressions.scan_python("probe.py", source.read_text()).suppressions[0].key
        self.registry.write_text(
            json.dumps(
                {
                    "exceptions": {
                        key: {
                            "explanation": "A deliberate hardcoded-password negative test fixture.",
                            "occurrences": 1,
                        }
                    }
                }
            )
        )
        self.assertEqual(check_gates.suppression_errors(self.root, self.registry), [])
        source.write_text(source.read_text().replace("a  b", "a b"))
        still_suppressed = subprocess.run(
            command, capture_output=True, text=True, check=False, timeout=30
        )
        self.assertEqual(still_suppressed.returncode, 0)
        self.assertTrue(
            any(
                "Unregistered" in error
                for error in check_gates.suppression_errors(self.root, self.registry)
            )
        )

    def test_every_check_option_requires_its_own_scope_approval(self) -> None:
        """A newly added option cannot hide among already approved disabled-check declarations."""
        path = self.root / ".clang-tidy"
        path.write_text(
            'Checks: "-*,misc-*"\nCheckOptions:\n'
            '  misc-include-cleaner.IgnoreHeaders: "foreign.h"\n'
        )
        result = config_suppressions.scan(path, ".clang-tidy")
        self.assertEqual(result.errors, [])
        self.assertEqual(len(result.suppressions), 1)
        item = result.suppressions[0]
        self.assertIn("clang-tidy-option/misc-include-cleaner.IgnoreHeaders", item.key)
        self.registry.write_text(
            json.dumps(
                {
                    "exceptions": {
                        item.key: {
                            "explanation": "Only the fixture foreign provider header is ignored.",
                            "occurrences": 1,
                        }
                    }
                }
            )
        )
        self.assertEqual(check_gates.suppression_errors(self.root, self.registry), [])
        path.write_text(
            path.read_text() + '  readability-identifier-naming.FunctionIgnoredRegexp: ".*"\n'
        )
        errors = check_gates.suppression_errors(self.root, self.registry)
        self.assertTrue(
            any(
                "clang-tidy-option/readability-identifier-naming.FunctionIgnoredRegexp" in error
                for error in errors
            )
        )
        self.assertTrue(any("Stale" in error for error in errors))

    def test_registry_entry_cannot_legalize_blanket_compiler_switch(self) -> None:
        """A directly forged current-format approval cannot turn the blanket route into success."""
        source = self.root / "CMakeLists.txt"
        text = "target_compile_options(a PRIVATE -w)\n"
        source.write_text(text)
        key = suppressions.Suppression("CMakeLists.txt", 1, "compiler/-w", text.rstrip("\n")).key
        self.registry.write_text(
            json.dumps(
                {
                    "exceptions": {
                        key: {
                            "explanation": "No blanket flag is admissible here.",
                            "occurrences": 1,
                        }
                    }
                }
            )
        )
        errors = check_gates.suppression_errors(self.root, self.registry)
        self.assertTrue(any("blanket warning opt-out" in error for error in errors))

    def test_actual_compiler_blanket_pragma_cannot_be_approved(self) -> None:
        """A scoped pragma disables real diagnostics but remains a nonregistrable blanket."""
        major = json.loads((ROOT / "deps/tools.json").read_text())["fuzzing"]["llvm_major"]
        compiler = shutil.which(f"clang++-{major}") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "The installed LLVM compiler is required")
        source = self.root / "probe.cpp"
        body = "int probe(int value) { { int value = 2; return value; } return value; }\n"
        source.write_text(body)
        command = [str(compiler), "-Wshadow", "-Werror", "-fsyntax-only", str(source)]
        control = subprocess.run(command, capture_output=True, text=True, check=False, timeout=30)
        self.assertNotEqual(control.returncode, 0)
        text = (
            "#pragma clang diagnostic push\n"
            '#pragma clang diagnostic ignored "-Weverything"\n'
            + body
            + "#pragma clang diagnostic pop\n"
        )
        source.write_text(text)
        suppressed = subprocess.run(
            command, capture_output=True, text=True, check=False, timeout=30
        )
        self.assertEqual(suppressed.returncode, 0, suppressed.stderr)
        key = suppressions.Suppression(
            "probe.cpp", 2, "compiler/-Weverything", text.rstrip("\n")
        ).key
        self.registry.write_text(
            json.dumps(
                {
                    "exceptions": {
                        key: {
                            "explanation": "A forged blanket diagnostic approval must fail.",
                            "occurrences": 1,
                        }
                    }
                }
            )
        )
        errors = check_gates.suppression_errors(self.root, self.registry)
        self.assertTrue(any("blanket warning opt-out" in error for error in errors))
        self.assertEqual(suppressions.scan_cxx("probe.cpp", text).suppressions, [])

    def test_config_exceptions_bind_scope_and_reject_yaml_overwrite(self) -> None:
        """Central approvals bind actual configuration scopes and refuse YAML key overwrites."""
        path = self.root / "ruff.toml"
        path.write_text('[lint.per-file-ignores]\n"tools/*.py" = ["INP001"]\n')
        first = config_suppressions.scan(path, "ruff.toml")
        path.write_text(path.read_text().replace("tools/*.py", "**/*.py"))
        changed = config_suppressions.scan(path, "ruff.toml")
        self.assertNotEqual(first.suppressions[0].key, changed.suppressions[0].key)
        path.write_text('[lint]\nignore = ["S"]\n')
        self.assertTrue(config_suppressions.scan(path, "ruff.toml").errors)
        path = self.root / ".clang-tidy"
        path.write_text('Checks: "-misc-x"\nChecks: "-*"\n')
        self.assertTrue(config_suppressions.scan(path, ".clang-tidy").errors)
