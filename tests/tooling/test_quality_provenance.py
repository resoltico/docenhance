# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Missing execution premises refuse before effects; component receipts never qualify a build."""

from __future__ import annotations

import copy
import operator
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_evidence_fixture import disk_plan

from evaluate_document_quality import evaluate
from quality_inputs import FrozenEvidence
from quality_provenance import admit_provenance, child_environment
from quality_provenance_contract import capture_references, environment_shape, provenance_shape

WINDOWS_SYSTEM_ROOT = "SystemRoot"


prepare_tool_imports()


class QualityProvenanceTests(unittest.TestCase):
    """Refuse missing execution premises before any new effects."""

    def test_scope_references_reuse_one_acquisition_and_survive_later_path_change(self) -> None:
        """Scope references reuse one acquisition and survive later path change."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, _executable = disk_plan(root, count=1)
            snapshots = FrozenEvidence(plan, root)
            receipt = snapshots.value(plan["protocol"]["build_provenance"])
            references = capture_references(receipt)
            self.assertEqual([entry["reference"] for entry in snapshots.entries[-2:]], references)
            for reference in references:
                acquired = snapshots.value(reference)
                (root / reference["path"]).write_bytes(b'{"changed":true}')
                self.assertIs(snapshots.value(reference), acquired)
                self.assertEqual(acquired, {})

    def test_pending_producer_and_runtime_cannot_create_journal_directory_or_child(self) -> None:
        """Pending producer and runtime cannot create journal directory or child."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root, count=1)
            output = root / "quality-run.json"
            before = set(root.iterdir())
            with (
                patch("evaluate_document_quality.QualityRun") as run,
                patch("quality_capture.owned_process") as launch,
            ):
                with self.assertRaises(ValueError):
                    evaluate(plan, root, executable, output)
                run.assert_not_called()
                launch.assert_not_called()
            self.assertFalse(output.exists())
            self.assertEqual(set(root.iterdir()), before)
            self.assertEqual(executable.read_bytes(), b"selected executable fixture")

    def test_empty_build_missing_scope_and_old_inline_forms_refuse(self) -> None:
        """Empty build missing scope and old inline forms refuse."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, _executable = disk_plan(root, count=1)
            receipt = FrozenEvidence(plan, root).value(plan["protocol"]["build_provenance"])
            for change in ("build", "capture_scope", "inline"):
                altered = copy.deepcopy(receipt)
                if change == "build":
                    altered["build"] = {}
                elif change == "capture_scope":
                    del altered[change]
                else:
                    altered["native_build"] = altered.pop("capture_scope")
                with self.subTest(change=change), self.assertRaises(ValueError):
                    provenance_shape(altered)
            for role in ("native_producer", "interpreter"):
                receipt["capture_scope"][role]["bytes"] = 1
                with self.subTest(role=role), self.assertRaises(ValueError):
                    provenance_shape(receipt)
                del receipt["capture_scope"][role]["bytes"]

    def test_environment_is_explicit_immutable_and_ignores_ambient_loader_values(self) -> None:
        """Environment is explicit immutable and ignores ambient loader values."""
        before = dict(os.environ)
        with patch.dict(os.environ, {"LD_PRELOAD": "unreviewed", "PYTHONPATH": "unreviewed"}):
            environment = child_environment()
            expected = {} if os.name == "posix" else {"SystemRoot": os.environ[WINDOWS_SYSTEM_ROOT]}
            self.assertEqual(dict(environment), expected)
            self.assertRaises(TypeError, operator.setitem, environment, "PATH", "ambient")
            for invalid in (
                {"PATH": "ambient"},
                {"SystemRoot": "relative"},
                {"SystemRoot": "C:\\Windows\0"},
                {"SystemRoot": "\\\\host\\share"},
            ):
                with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                    environment_shape(invalid)
        self.assertEqual(dict(os.environ), before)

    def test_changed_frozen_environment_refuses_before_either_evidence_owner(self) -> None:
        """Changed frozen environment refuses before either evidence owner."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root, count=1)
            snapshots = FrozenEvidence(plan, root)
            receipt = snapshots.value(plan["protocol"]["build_provenance"])
            receipt["capture_scope"]["child_environment"] = (
                {"SystemRoot": "C:\\different"} if os.name == "posix" else {}
            )
            with (
                patch("quality_runtime_evidence.admit_runtime") as runtime,
                patch("quality_native_evidence.admit_native") as native,
            ):
                with self.assertRaisesRegex(ValueError, "child environment"):
                    admit_provenance(plan, root, executable, snapshots)
                runtime.assert_not_called()
                native.assert_not_called()
