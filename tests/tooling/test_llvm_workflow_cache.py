# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual runner environment changes cannot repeatedly miss an immutable outer cache."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

from tools_path import ROOT

from workflow_cache import llvm_cache
from workflow_config import workflow


class LLVMWorkflowCacheTests(unittest.TestCase):
    """Execute the actual fingerprint and challenge its restore/save binding."""

    def native(self) -> dict[str, Any]:
        """Each control owns a separate parsed native job."""
        return copy.deepcopy(workflow(ROOT / ".github/workflows/ci.yml")["jobs"]["native"])

    def test_environment_fingerprint_matches_installer_fields(self) -> None:
        """OS updates and selected Xcode directories independently change the outer key."""
        job = self.native()
        command = next(s["run"] for s in job["steps"] if s.get("id") == "intel-llvm-environment")
        observed = set()
        for version, developer in [
            ("15.7", None),
            ("15.8", None),
            ("15.7", "/Xcode16"),
            ("15.7", ""),
        ]:
            with (
                self.subTest(version=version, developer=developer),
                tempfile.TemporaryDirectory() as raw,
            ):
                output = Path(raw) / "output"
                env = {**os.environ, "GITHUB_OUTPUT": str(output)}
                env.pop("DEVELOPER_DIR", None)
                if developer is not None:
                    env["DEVELOPER_DIR"] = developer
                result = subprocess.run(
                    [
                        sys.executable,
                        "-c",
                        f'import platform\nplatform.mac_ver = lambda: ({version!r}, (), "")\n'
                        + command,
                    ],
                    env=env,
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                identity = {
                    "host_os_version": version,
                    "developer_directory": developer if developer is not None else "system-default",
                }
                expected = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
                self.assertEqual(output.read_text(), f"identity={expected}\n")
                observed.add(expected)
        self.assertEqual(len(observed), 4)

    def test_environment_binding_and_order_are_required(self) -> None:
        """Both keys require the fingerprint, computed after selection and before restore."""
        for mutation in (
            "restore-key",
            "save-key",
            "environment",
            "condition",
            "order",
            "missing-install",
        ):
            with self.subTest(mutation=mutation):
                job = self.native()
                steps = job["steps"]
                fingerprint = next(s for s in steps if s.get("id") == "intel-llvm-environment")
                restore = next(s for s in steps if s.get("id") == "intel-llvm-cache")
                save = next(s for s in steps if s.get("name") == "Save verified Intel LLVM tools")
                selection = next(s for s in steps if s.get("run") == "python tools/select_xcode.py")
                llvm_cache(job, selection)
                if mutation.endswith("-key"):
                    selected = restore if mutation == "restore-key" else save
                    selected["with"]["key"] = selected["with"]["key"].replace(
                        "${{ steps.intel-llvm-environment.outputs.identity }}-", ""
                    )
                elif mutation == "environment":
                    fingerprint["run"] = fingerprint["run"].replace("system-default", "other")
                elif mutation == "condition":
                    fingerprint["if"] = "false"
                elif mutation == "missing-install":
                    steps[:] = [s for s in steps if s.get("run") != "python tools/install_llvm.py"]
                else:
                    steps.remove(fingerprint)
                    steps.insert(steps.index(selection), fingerprint)
                with self.assertRaisesRegex(ValueError, "Intel LLVM cache"):
                    llvm_cache(job, selection)
