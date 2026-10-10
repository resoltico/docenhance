# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Negative controls prove retained acceptance rules detect specific misleading outcomes."""

from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

sys.path.insert(0, str(ROOT / "tests/product"))
sys.path.insert(0, str(ROOT / "tests/cli"))

from continuous_fixtures import Fixture
from matrix_assertions import compare_outputs, quality, sample_check
from matrix_cases import active_coverage, matrix
from matrix_fixtures import HEIGHT, MARKS, WIDTH, png_sources
from matrix_types import Case
from run_matrix import Run


class MatrixAcceptanceTests(unittest.TestCase):
    """Independent corruptions must fail while subsequent audit cases still execute."""

    def test_no_op_outputs_fail_every_synthetic_benefit_target(self) -> None:
        """Schema-valid reports and changed labels cannot turn an unchanged page into benefit."""
        cases = [case for case in matrix() if case.group == "benefit"]
        self.assertTrue(cases)
        for case in cases:
            with self.subTest(case=case.id):
                self.assertFalse(quality(case, png_sources()[case.source])["accepted"])

    def test_visible_mark_contrast_does_not_excuse_failed_dark_mark_target(self) -> None:
        """The synthetic threshold can fail while local punctuation contrast remains visible."""
        pixels = [(210,)] * (WIDTH * HEIGHT)
        for x, y in MARKS:
            pixels[y * WIDTH + x] = (120,)
        output = Fixture(WIDTH, HEIGHT, tuple(pixels))
        case = Case("strong-smoothing", "benefit", "noisy", check="noise")
        measured = quality(case, output)
        self.assertTrue(measured["noise_reduction_accepted"])
        self.assertFalse(measured["accepted"])
        self.assertEqual(measured["dark_disconnected_marks_of_3"], 0)
        self.assertEqual([mark["after"] for mark in measured["marks"]], [120] * 3)
        self.assertEqual([mark["local_contrast_after"] for mark in measured["marks"]], [90] * 3)

    def test_wrong_turn_fails_the_independent_asymmetric_sample_oracle(self) -> None:
        """An unchanged asymmetric raster cannot satisfy an enabled clockwise quarter-turn."""
        case = Case("wrong-turn", "geometry", "geometry-8-1", ("--rotate", "90"), check="geometry")
        source = png_sources()[case.source]
        with self.assertRaisesRegex(AssertionError, "G01/G02 direction"):
            sample_check(case, source, source.encoded())

    def test_changed_protected_destination_fails_exact_sample_comparison(self) -> None:
        """A single altered protected sample is detected even when the rest of a page agrees."""
        source = png_sources()["shaded"]
        pixels = list(source.pixels)
        pixels[0] = (pixels[0][0] + 1,)
        output = Fixture(source.width, source.height, tuple(pixels))
        case = Case("bad-mask", "protection", "shaded", check="protected")
        with self.assertRaisesRegex(AssertionError, "protected destinations changed"):
            compare_outputs(case, output, source)

    def test_identity_only_method_cannot_satisfy_active_coverage(self) -> None:
        """Removing sharpening's benefit case cannot be concealed by its identity cases."""
        cases = [case for case in matrix() if case.id != "active-S01"]
        with self.assertRaisesRegex(ValueError, "S01"):
            active_coverage(cases)

    def test_schema_failure_is_retained_and_the_next_case_executes(self) -> None:
        """Malformed schema observations become failed evidence instead of terminating replay."""
        help_response = {
            "schema_version": json.loads(
                (ROOT / "schemas/command-response.schema.json").read_text(encoding="utf-8")
            )["properties"]["schema_version"]["const"],
            "command": "root",
            "version": "0.7.0",
            "exit_code": 0,
            "usage": "docenhance COMMAND [OPTIONS]",
            "options": [],
        }
        observed = [
            subprocess.CompletedProcess[bytes](args=[], returncode=0, stdout=b"{}", stderr=b""),
            subprocess.CompletedProcess[bytes](
                args=[], returncode=0, stdout=json.dumps(help_response).encode(), stderr=b""
            ),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "response-fixture"
            binary.write_bytes(b"fixture identity only; subprocess observations are injected")
            with patch("run_matrix.write_sources", return_value={}):
                audit = Run(binary, root / "evidence")
            with (
                patch("run_matrix.subprocess.run", side_effect=observed) as invoked,
                redirect_stdout(io.StringIO()),
            ):
                audit.execute(Case("malformed-response", "control", "", command="root"))
                audit.execute(Case("next-response", "control", "", command="root"))
            self.assertEqual(invoked.call_count, 2)
            self.assertEqual([item["contract"] for item in audit.results], ["FAIL", "PASS"])
            self.assertIn("schema_version", audit.results[0]["failure"])
            retained = json.loads((audit.directory / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(retained["executed_count"], 2)
            self.assertEqual(retained["contract_counts"], {"FAIL": 1, "PASS": 1})


if __name__ == "__main__":
    unittest.main()
