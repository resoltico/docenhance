# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""The local Linux gate must not skip failures, drift pins or reuse host native outputs."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import check_all
import check_linux


class LinuxGateTests(unittest.TestCase):
    """Test orchestration boundaries without replacing native workflow evidence."""

    def test_non_linux_local_gate_includes_docker(self) -> None:
        """Both developer host families run Linux checks; Linux avoids recursive containers."""
        for system in ("Darwin", "Windows"):
            self.assertIn(
                ("Linux native workflow (Docker)", ("tools/check_linux.py",)),
                check_all.local_checks(system),
            )
        self.assertEqual(check_all.local_checks("Linux"), check_all.CHECKS)

    def test_pinned_image_and_tool_change_invalidate_cache(self) -> None:
        """A moving tag is refused and tool installation changes produce a different image."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in check_linux.IMAGE_FILES:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture", encoding="utf-8")
            pins = root / "deps/tools.json"
            pins.write_text(json.dumps({"linux_container": {"image": "ubuntu@sha256:" + "a" * 64}}))
            _, original = check_linux.image_identity(root)
            (root / "tools/install_llvm.py").write_text("changed", encoding="utf-8")
            self.assertNotEqual(original, check_linux.image_identity(root)[1])
            pins.write_text(json.dumps({"linux_container": {"image": "ubuntu:latest"}}))
            with self.assertRaises(ValueError):
                check_linux.image_identity(root)

    def test_native_state_is_isolated_and_host_cache_read_only(self) -> None:
        """Every Linux native output is a Docker volume, never a host build tree."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "deps").mkdir()
            for name in ("tools.json", "lock.json", "features.json"):
                (root / "deps" / name).write_text("{}", encoding="utf-8")
            (root / ".cache/deps").mkdir(parents=True)
            with patch("check_linux.docker_executable", return_value="/usr/bin/docker"):
                arguments = check_linux.run_arguments("image", root)
            mounts = [
                arguments[i + 1] for i, argument in enumerate(arguments) if argument == "--mount"
            ]
            for name in ("out", ".cache", "dist"):
                self.assertTrue(
                    any(m.startswith("type=volume,") and m.endswith("/" + name) for m in mounts)
                )
            self.assertIn(
                f"type=bind,source={root / '.cache/deps'},target=/host-deps,readonly", mounts
            )
            self.assertEqual(arguments[-3:], ["image", "sh", "tools/linux_gate.sh"])

    def test_missing_docker_and_failed_subprocess_are_failures(self) -> None:
        """Unavailable engines and nonzero native results cannot be reported as a pass."""
        with patch("check_linux.shutil.which", return_value=None), self.assertRaises(ValueError):
            check_linux.docker_executable()
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "log"
            with patch("check_linux.subprocess.run") as run:
                run.return_value.returncode = 9
                self.assertFalse(check_linux.invoke(["/usr/bin/false"], log))
            self.assertIn("/usr/bin/false", log.read_text())
