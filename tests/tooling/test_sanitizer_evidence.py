# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Negative controls for actual source-level sanitizer instrumentation claims."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_sanitizers
import sanitizer_evidence
from test_evidence import EvidenceError


class CompilationTests(unittest.TestCase):
    """Cache intent cannot excuse source-specific instrumentation or recovery overrides."""

    def test_requested_modes_and_ordered_opt_outs(self) -> None:
        """Reject missing modes, source opt-outs and nonfatal reports."""
        source = str(ROOT / "src/image/numeric.cpp")
        cache = {"DE_ENABLE_ASAN": "ON", "DE_ENABLE_UBSAN": "TRUE"}
        good = f"clang++ -fsanitize=address,undefined -fno-sanitize-recover=all -c {source}"
        sanitizer_evidence.check_compilation(cache, [{"file": "source.cpp", "command": good}])
        for command in (
            good.replace("address,undefined", "address"),
            good + " -fno-sanitize=all",
            good + " -fsanitize-ignorelist=excluded.txt",
            good + " -fsanitize-blacklist=excluded.txt",
            good + " -fno-sanitize=undefined",
            good + " -fsanitize-recover=undefined",
            good + " -fno-sanitize=all -fsanitize=address,undefined",
            good.replace("-fno-sanitize-recover=all", ""),
        ):
            with self.subTest(command=command), self.assertRaises(EvidenceError):
                sanitizer_evidence.check_compilation(
                    cache, [{"file": "source.cpp", "command": command}]
                )
        with self.assertRaises(EvidenceError):
            sanitizer_evidence.check_compilation(cache, [])
        sanitizer_evidence.check_compilation({}, [])
        sanitizer_evidence.check_compilation(
            cache,
            [
                {
                    "file": "source.cpp",
                    "arguments": (
                        good + " -fsanitize-recover=undefined -fno-sanitize-recover=all"
                    ).split(),
                }
            ],
        )
        with self.assertRaises(EvidenceError):
            sanitizer_evidence.check_compilation(
                {"DE_ENABLE_TSAN": "ON"}, [{"file": "source.cpp", "command": good}]
            )


class DetectionDiagnosticsTests(unittest.TestCase):
    """A failed runtime control must expose its cause while retaining full bounded evidence."""

    def test_runtime_failure_is_not_hidden_by_summary(self) -> None:
        """A startup crash fails detection and reports codes/stderr without flooding CTest."""
        benign = subprocess.CompletedProcess(["probe"], 0, b"", b"")
        diagnostic = b"FATAL: controlled runtime startup failure\n" + b"x" * 65536
        fault = subprocess.CompletedProcess(["probe"], 66, b"", diagnostic)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with patch("check_sanitizers.subprocess.run", side_effect=[benign, fault]):
                failures = check_sanitizers.check(Path("probe"), {"thread"}, directory)
            self.assertEqual(len(failures), 1)
            self.assertIn("benign exit=0, fault exit=66", failures[0])
            self.assertIn("FATAL: controlled runtime startup failure", failures[0])
            self.assertLess(len(failures[0]), check_sanitizers.DIAGNOSTIC_BYTES + 1024)
            self.assertEqual((directory / "thread-fault.log").read_bytes(), diagnostic)
