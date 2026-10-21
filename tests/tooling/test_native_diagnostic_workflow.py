# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual native upload policies refuse raw evidence and optional diagnostic delivery."""

from __future__ import annotations

import copy
import json
import unittest

from tools_path import ROOT

import workflow_integrity as integrity
from workflow_config import workflow

CONFIGURATIONS = (("ci.yml", "native"), ("ci.yml", "sanitize"), ("weekly.yml", "sanitize"))


class NativeDiagnosticWorkflowTests(unittest.TestCase):
    """Native and sanitizer jobs retain only their declared bounded evidence."""

    def test_current_native_uploads_match_actual_policy(self) -> None:
        """Every current workflow independently satisfies the complete semantic policy."""
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        for filename in ("ci.yml", "weekly.yml"):
            policy = (
                integrity.quality_workflow if filename == "ci.yml" else integrity.weekly_workflow
            )
            with self.subTest(filename=filename):
                policy(workflow(ROOT / ".github/workflows" / filename), major)

    def test_native_upload_mutations_refuse_missing_raw_and_optional_evidence(self) -> None:
        """All three actual jobs reject altered archive identity, bounds, fields and order."""
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        for filename, job_name in CONFIGURATIONS:
            original = workflow(ROOT / ".github/workflows" / filename)
            policy = (
                integrity.quality_workflow if filename == "ci.yml" else integrity.weekly_workflow
            )
            steps = original["jobs"][job_name]["steps"]
            position = next(
                i
                for i, step in enumerate(steps)
                if step.get("name") == "Retain complete native diagnostics"
            )
            for section, key, value in (
                ("", "name", "Ignore native evidence"),
                ("", "if", "success()"),
                ("", "continue-on-error", "true"),
                ("", "uses", "actions/upload-artifact@" + "0" * 40),
                ("with", "if-no-files-found", "warn"),
                ("with", "compression-level", "1"),
                ("with", "retention-days", "1"),
                ("with", "name", "unbound-diagnostic"),
                ("with", "path", "out/**"),
                ("with", "path", "out/release/app/native-*/ctest-output*"),
            ):
                changed = copy.deepcopy(original)
                step = changed["jobs"][job_name]["steps"][position]
                target = step[section] if section else step
                target[key] = value
                with (
                    self.subTest(file=filename, job=job_name, key=key, value=value),
                    self.assertRaisesRegex(ValueError, "Required native diagnostic upload"),
                ):
                    policy(changed, major)
            for alteration in ("missing", "duplicate", "before-build"):
                changed = copy.deepcopy(original)
                altered = changed["jobs"][job_name]["steps"]
                step = altered.pop(position)
                if alteration == "duplicate":
                    altered.extend((step, copy.deepcopy(step)))
                elif alteration == "before-build":
                    altered.insert(0, step)
                with (
                    self.subTest(file=filename, job=job_name, alteration=alteration),
                    self.assertRaisesRegex(ValueError, "Required native diagnostic upload"),
                ):
                    policy(changed, major)

    def test_manual_diagnostic_input_and_exact_fuzz_route(self) -> None:
        """Opt-in is typed/default-false; every unrelated required job remains unconditional."""
        original = workflow(ROOT / ".github/workflows/ci.yml")
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        integrity.quality_workflow(original, major)
        for key, value in (
            ("default", "true"),
            ("default", None),
            ("type", "string"),
            ("required", "true"),
        ):
            changed = copy.deepcopy(original)
            declaration = changed["on"]["workflow_dispatch"]["inputs"]["native_diagnostic"]
            if value is None:
                declaration.pop(key)
            else:
                declaration[key] = value
            with (
                self.subTest(input=key, value=value),
                self.assertRaisesRegex(ValueError, "default-false boolean"),
            ):
                integrity.quality_workflow(changed, major)
        null_condition = copy.deepcopy(original)
        null_condition["jobs"]["native"]["if"] = None
        with self.assertRaisesRegex(ValueError, "Required job"):
            integrity.quality_workflow(null_condition, major)
        for job, condition in (
            ("fuzz", None),
            ("fuzz", "false"),
            ("fuzz", "!inputs.native_diagnostic"),
            ("native", "!inputs.native_diagnostic"),
            ("sanitize", "false"),
            ("structural", "false"),
        ):
            changed = copy.deepcopy(original)
            if condition is None:
                changed["jobs"][job].pop("if")
            else:
                changed["jobs"][job]["if"] = condition
            with (
                self.subTest(job=job, condition=condition),
                self.assertRaisesRegex(ValueError, "Required job"),
            ):
                integrity.quality_workflow(changed, major)

    def test_diagnostic_cannot_turn_skipped_fuzz_into_gate_success(self) -> None:
        """Even diagnostic dispatch preserves the actual fatal fuzz-success assertion."""
        original = workflow(ROOT / ".github/workflows/ci.yml")
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        for alteration in ("omit-fuzz", "allow-skipped", "optional", "success-only"):
            changed = copy.deepcopy(original)
            gate = changed["jobs"]["gate"]
            if alteration == "omit-fuzz":
                gate["needs"].remove("fuzz")
            elif alteration == "allow-skipped":
                gate["steps"][0]["run"] = gate["steps"][0]["run"].replace(
                    'test "$FUZZ_RESULT" = success', 'test "$FUZZ_RESULT" != failure'
                )
            elif alteration == "optional":
                gate["continue-on-error"] = "true"
            else:
                gate["if"] = "success()"
            with (
                self.subTest(alteration=alteration),
                self.assertRaisesRegex(ValueError, "Aggregate"),
            ):
                integrity.quality_workflow(changed, major)
