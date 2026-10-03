# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Negative controls for actual source-level sanitizer instrumentation claims."""

from __future__ import annotations

import unittest

from tools_path import ROOT

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
