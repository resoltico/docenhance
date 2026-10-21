# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Compilation metadata retains its real array root and the same acquired byte identity."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_evidence_fixture import write_reference

from quality_json import read_json
from quality_native_commands import command_scope
from quality_native_contract import compilation_values, instrumented
from quality_native_evidence import _same_snapshot

prepare_tool_imports()


def compile_body(command: str) -> dict[str, Any]:
    """Only the command component inputs; this incomplete body cannot admit native execution."""
    reference = {"path": "component-only.json", "sha256": "a" * 64}
    entry = {
        "directory": "/build",
        "file": "/repo/source dir/$selected.cpp",
        "output": "/build/obj/selected.o",
        "command": command,
    }
    return {
        "configuration": {
            "reference": reference,
            "value": {"CMAKE_SYSTEM_NAME": "Darwin", "CMAKE_CXX_COMPILER": "/tool/c++"},
        },
        "compile_commands": {"reference": reference, "value": [entry]},
    }


class QualityNativeSnapshotTests(unittest.TestCase):
    """Bind native metadata to its actual acquired bytes."""

    def test_actual_compile_operands_bind_source_and_output_in_either_option_order(self) -> None:
        """Actual compile operands bind source and output in either option order."""
        source = "'../repo/source dir/$selected.cpp'"
        prefix = (
            "/tool/c++ -I 'include dir.cpp' -include 'forced file.cpp' -D 'NAME=-c' -arch arm64 "
        )
        absolute = "'/repo/source dir/$selected.cpp'"
        for suffix in (
            "-o obj/selected.o -c " + source,
            "-c " + absolute + " -o obj/selected.o -O2",
        ):
            body = compile_body(prefix + suffix)
            with (
                self.subTest(suffix=suffix),
                patch("quality_native_commands.link_scope") as link,
                patch("quality_native_commands.header_scope") as headers,
            ):
                command_scope(body, {"/repo/source dir/$selected.cpp"})
                link.assert_called_once()
                headers.assert_called_once()

    def test_actual_compile_operand_mismatches_refuse_before_link_or_header_evidence(self) -> None:
        """Actual compile operand mismatches refuse before link or header evidence."""
        source = "'../repo/source dir/$selected.cpp'"
        good = "/tool/c++ -c " + source + " -o obj/selected.o"
        mutations = (
            good.replace(source, "other.cpp"),
            good.replace(source, ""),
            good + " other.cpp",
            good.replace("obj/selected.o", "obj/other.o"),
            good.replace("-o obj/selected.o", ""),
            good + " -o obj/other.o",
            good.replace("-c ", ""),
            good + " -c",
            good + " -I",
            good + " -oobj/other.o",
            good + " --output=obj/other.o",
        )
        for command in mutations:
            with (
                self.subTest(command=command),
                patch("quality_native_commands.link_scope") as link,
                patch("quality_native_commands.header_scope") as headers,
            ):
                with self.assertRaises(ValueError):
                    command_scope(compile_body(command), {"/repo/source dir/$selected.cpp"})
                link.assert_not_called()
                headers.assert_not_called()

    def test_unbound_compiler_and_linker_plugins_cannot_admit_ordinary_capture(self) -> None:
        """Unbound compiler and linker plugins cannot admit ordinary capture."""
        self.assertFalse(instrumented("c++ -O2 -c a.cpp -o a.o", "Linux"))
        for option in (
            "-fpass-plugin=/selected/pass.so",
            "-Wl,--plugin=/selected/pass.so",
            "-Wl,-plugin,/selected/pass.so",
            "-Xlinker --plugin=/selected/pass.so",
            "-Xclang -load -Xclang /selected/pass.so",
        ):
            with self.subTest(option=option):
                self.assertTrue(instrumented("c++ " + option + " -c a.cpp -o a.o", "Linux"))

    def test_actual_compilation_array_uses_one_identity_bound_acquisition(self) -> None:
        """Actual compilation array uses one identity bound acquisition."""
        raw = (
            b'[{"directory":"/build","command":"c++ -c a.cpp -o a.o",'
            b'"file":"a.cpp","output":"a.o"}]'
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            reference = write_reference(root, "compile_commands.json", raw)
            snapshot = {"reference": reference, "value": compilation_values(raw)}
            acquired: dict[tuple[str, str], bytes] = {}
            _same_snapshot(root, snapshot, acquired)
            self.assertEqual(acquired[(reference["path"], reference["sha256"])], raw)
            (root / reference["path"]).write_bytes(b"changed after initial acquisition")
            with patch("quality_native_evidence.acquire_payload") as reopen:
                _same_snapshot(root, snapshot, acquired)
                reopen.assert_not_called()
            with self.assertRaises(TypeError):
                read_json(raw)

    def test_wrong_root_and_strict_json_mutations_refuse(self) -> None:
        """Wrong root and strict json mutations refuse."""
        with self.assertRaisesRegex(TypeError, "must be an array"):
            compilation_values(b"{}")
        invalid = (
            b'[{"x":1,"x":2}]',
            b"[NaN]",
            b"[1e10000]",
            b'["\\ud800"]',
            b"[" * 65 + b"0" + b"]" * 65,
        )
        for raw in invalid:
            with self.subTest(raw=raw[:40]), self.assertRaises((ValueError, UnicodeError)):
                compilation_values(raw)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            reference = write_reference(root, "wrong-root.json", {})
            with self.assertRaisesRegex(TypeError, "must be an array"):
                _same_snapshot(root, {"reference": reference, "value": []}, {})
