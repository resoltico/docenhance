# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed diagnostic scopes retain full default coverage and refuse decorative capability jobs."""

from __future__ import annotations

import copy
import json
import re
import unittest
from typing import Any

from tools_path import ROOT

from ci_contract import NATIVE_MATRIX_INCLUDE, NATIVE_RUNNERS
from workflow_config import workflow
from workflow_integrity import quality_workflow


class PerformanceDiagnosticWorkflowTests(unittest.TestCase):
    """Challenge actual workflow/policy bytes rather than an independent boolean truth table."""

    def document(self) -> dict[str, Any]:
        """Own a mutable copy of the reviewed real workflow."""
        return copy.deepcopy(workflow(ROOT / ".github/workflows/ci.yml"))

    def policy(self, document: dict[str, Any]) -> None:
        """Evaluate the same mandatory full-workflow guard used by check_all."""
        major = json.loads((ROOT / "deps/tools.json").read_text())["fuzzing"]["llvm_major"]
        quality_workflow(document, major)

    def test_one_closed_choice_rejects_obsolete_aliases_and_unknown_scopes(self) -> None:
        """There is one default-none choice; old booleans cannot silently select full fuzz work."""
        document = self.document()
        self.policy(document)
        inputs = document["on"]["workflow_dispatch"]["inputs"]
        self.assertEqual(set(inputs), {"diagnostic"})
        self.assertEqual(inputs["diagnostic"]["type"], "choice")
        self.assertEqual(inputs["diagnostic"]["default"], "none")
        self.assertEqual(
            inputs["diagnostic"]["options"],
            ["none", "native", "tooling", "intel", "paired_capability"],
        )
        for mutation in ("native_diagnostic", "tooling_diagnostic", "unknown"):
            changed = self.document()
            declaration = changed["on"]["workflow_dispatch"]["inputs"]
            if mutation == "unknown":
                declaration["diagnostic"]["options"].append("all_diagnostics")
            else:
                declaration[mutation] = {"type": "boolean", "default": "false"}
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.policy(changed)

    def test_exact_conditional_matrix_preserves_full_lanes_and_only_current_intel(self) -> None:
        """Decode the two actual JSON branches; dropping a default lane or the event guard fails."""
        document = self.document()
        actual = document["jobs"]["native"]["strategy"]["matrix"]["include"]
        self.assertEqual(actual, NATIVE_MATRIX_INCLUDE)
        self.assertTrue(
            actual.startswith("${{ fromJSON(github.event_name == 'workflow_dispatch' && ")
        )
        branches = re.findall(r"'(\[.*?\])'", actual)
        self.assertEqual(len(branches), 2)
        self.assertEqual(
            json.loads(branches[0]), [{"platform": "macos-x86_64", "os": "macos-26-intel"}]
        )
        self.assertEqual(
            json.loads(branches[1]),
            [{"platform": platform, "os": runner} for platform, runner in NATIVE_RUNNERS.items()],
        )
        for before, after in (
            ("github.event_name == 'workflow_dispatch' && ", ""),
            ("inputs.diagnostic == 'intel'", "inputs.diagnostic == 'none'"),
            ("macos-26-intel", "macos-15-intel"),
        ):
            changed = self.document()
            changed["jobs"]["native"]["strategy"]["matrix"]["include"] = actual.replace(
                before, after
            )
            with self.subTest(before=before), self.assertRaises(ValueError):
                self.policy(changed)

    def test_capability_scope_has_no_build_or_timing_and_keeps_required_gate(self) -> None:
        """A bounded read-only command has fatal retention and an unchanged aggregate."""
        document = self.document()
        job = document["jobs"]["paired_capability"]
        self.assertEqual(job["runs-on"], "macos-15-intel")
        self.assertEqual(job["timeout-minutes"], "5")
        self.assertEqual(
            job["if"],
            "github.event_name == 'workflow_dispatch' && inputs.diagnostic == 'paired_capability'",
        )
        self.assertEqual(
            [step["run"] for step in job["steps"] if "run" in step],
            [
                (
                    "python tools/check_performance_host.py --output "
                    '"${{ runner.temp }}/paired-capability"'
                )
            ],
        )
        self.assertEqual(document["jobs"]["gate"]["if"], "always()")
        self.assertEqual(
            set(document["jobs"]["gate"]["needs"]), {"structural", "native", "fuzz", "sanitize"}
        )
        for mutation in ("command", "optional", "upload", "ref"):
            changed = self.document()
            selected = changed["jobs"]["paired_capability"]
            if mutation == "command":
                selected["steps"][2]["run"] = "cmake --workflow --preset release"
            elif mutation == "optional":
                selected["continue-on-error"] = "true"
            elif mutation == "upload":
                selected["steps"][-1]["with"]["if-no-files-found"] = "warn"
            else:
                selected["steps"][0]["with"]["ref"] = "main"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.policy(changed)

    def test_every_role_rejects_broader_or_unknown_mode_conditions(self) -> None:
        """No invalid manual mode can gain native work, tooling or a fuzz campaign."""
        for role in ("structural", "native", "fuzz", "sanitize", "tooling", "paired_capability"):
            changed = self.document()
            changed["jobs"][role]["if"] = "inputs.diagnostic != 'tooling'"
            with self.subTest(role=role), self.assertRaises(ValueError):
                self.policy(changed)
