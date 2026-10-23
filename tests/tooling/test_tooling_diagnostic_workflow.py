# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Manual tooling portability checks cannot earn or bypass required native qualification."""

from __future__ import annotations

import copy
import json
import unittest
from typing import Any

from tools_path import ROOT

import workflow_integrity as integrity
from ci_contract import (
    FULL_QUALITY_CONDITION,
    NATIVE_QUALITY_CONDITION,
    TOOLING_DIAGNOSTIC_CONDITION,
)
from workflow_config import workflow


class ToolingDiagnosticWorkflowTests(unittest.TestCase):
    """Challenge actual route expressions, native shell ownership and fatal complete work."""

    def document(self) -> dict[str, Any]:
        """Own a fresh mutable workflow for each negative control."""
        return copy.deepcopy(workflow(ROOT / ".github/workflows/ci.yml"))

    def policy(self, document: dict[str, Any]) -> None:
        """Use the same authoritative full-workflow policy as check_all."""
        major = json.loads((ROOT / "deps/tools.json").read_text())["fuzzing"]["llvm_major"]
        integrity.quality_workflow(document, major)

    def test_reviewed_routes_and_required_gate_remain_truthful(self) -> None:
        """Only the explicit manual tooling route can omit native work; its aggregate stays red."""
        document = self.document()
        self.policy(document)
        self.assertEqual(
            FULL_QUALITY_CONDITION,
            "github.event_name != 'workflow_dispatch' || inputs.diagnostic == 'none' || "
            "inputs.diagnostic == 'native'",
        )
        self.assertEqual(
            TOOLING_DIAGNOSTIC_CONDITION,
            "github.event_name == 'workflow_dispatch' && inputs.diagnostic == 'tooling'",
        )
        for name in ("structural", "sanitize"):
            self.assertEqual(document["jobs"][name]["if"], FULL_QUALITY_CONDITION)
        self.assertEqual(document["jobs"]["native"]["if"], NATIVE_QUALITY_CONDITION)
        gate = document["jobs"]["gate"]
        self.assertEqual(gate["if"], "always()")
        self.assertEqual(set(gate["needs"]), {"structural", "native", "fuzz", "sanitize"})
        self.assertEqual(
            set(gate["steps"][0]["run"].splitlines()),
            {
                f'test "${name}_RESULT" = success'
                for name in ("STRUCTURAL", "NATIVE", "FUZZ", "SANITIZE")
            },
        )

    def test_route_or_default_mutations_are_rejected(self) -> None:
        """PR/main suppression and implicit opt-in cannot become accepted CI policy."""
        for name in ("structural", "native", "sanitize", "fuzz", "tooling", "gate"):
            with self.subTest(name=name):
                document = self.document()
                document["jobs"][name]["if"] = "!inputs.tooling_diagnostic"
                with self.assertRaises(ValueError):
                    self.policy(document)
        for key, value in (("default", "tooling"), ("type", "boolean"), ("required", "true")):
            document = self.document()
            document["on"]["workflow_dispatch"]["inputs"]["diagnostic"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.policy(document)

    def test_complete_three_os_tooling_and_fatal_commands_are_required(self) -> None:
        """A missing platform, pinned installer or complete strict run cannot silently pass."""
        for mutation in ("matrix", "compiler", "runner", "optional", "ref", "python", "jobs"):
            with self.subTest(mutation=mutation):
                document = self.document()
                job = document["jobs"]["tooling"]
                if mutation == "matrix":
                    job["strategy"]["matrix"]["os"].pop()
                elif mutation == "optional":
                    job["continue-on-error"] = "true"
                elif mutation == "ref":
                    job["steps"][0]["with"]["ref"] = "main"
                elif mutation == "python":
                    job["steps"][2]["with"]["python-version"] = "3.14"
                else:
                    position = {"compiler": 5, "runner": 7, "jobs": 8}[mutation]
                    job["steps"][position]["run"] = "python -c 'print(\"passed\")'"
                with self.assertRaises(ValueError):
                    self.policy(document)

    def test_windows_compiler_environment_and_error_propagation_stay_in_same_step(self) -> None:
        """Losing VS activation or its exit status cannot bless portability diagnostics."""
        for original, replacement in (
            ("& $devshell -Arch amd64", "# no activation -Arch amd64"),
            ("exit $LASTEXITCODE", "exit 0"),
            ("python tools/run_tooling_tests.py --jobs 2", "Write-Host passed"),
        ):
            document = self.document()
            step = document["jobs"]["tooling"]["steps"][-1]
            self.assertIn(original, step["run"])
            step["run"] = step["run"].replace(original, replacement)
            with self.subTest(original=original), self.assertRaises(ValueError):
                self.policy(document)
