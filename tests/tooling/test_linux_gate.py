# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""The local Linux gate must not skip failures, drift pins or reuse host native outputs."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import tools_path  # noqa: F401 -- Bootstrap direct tool imports for standalone unittest discovery.

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
            _, original = check_linux.image_identity("linux/arm64", root)
            (root / "tools/install_llvm.py").write_text("changed", encoding="utf-8")
            self.assertNotEqual(original, check_linux.image_identity("linux/arm64", root)[1])
            pins.write_text(json.dumps({"linux_container": {"image": "ubuntu:latest"}}))
            with self.assertRaises(ValueError):
                check_linux.image_identity("linux/arm64", root)

    def test_native_state_is_isolated_and_host_cache_read_only(self) -> None:
        """Every Linux native output is a Docker volume, never a host build tree."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "deps").mkdir()
            for name in ("tools.json", "lock.json", "features.json"):
                (root / "deps" / name).write_text("{}", encoding="utf-8")
            (root / ".cache/deps").mkdir(parents=True)
            with patch("check_linux.docker_executable", return_value="/usr/bin/docker"):
                arguments = check_linux.run_arguments("image", "linux/arm64", root)
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

    def test_architecture_image_and_recipe_separate_native_state(self) -> None:
        """No native volume or image can be shared by another architecture or build recipe."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in check_linux.IMAGE_FILES:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture", encoding="utf-8")
            (root / "deps/tools.json").write_text(
                json.dumps({"linux_container": {"image": "ubuntu@sha256:" + "a" * 64}})
            )
            for name in ("lock.json", "features.json"):
                (root / "deps" / name).write_text("{}")
            _, arm = check_linux.image_identity("linux/arm64", root)
            _, intel = check_linux.image_identity("linux/amd64", root)
            self.assertNotEqual(arm, intel)
            before = check_linux.cache_identity(arm, "linux/arm64", root)
            self.assertNotEqual(before, check_linux.cache_identity(intel, "linux/amd64", root))
            recipe = root / "cmake/recipe.cmake"
            recipe.parent.mkdir()
            recipe.write_text("changed")
            self.assertNotEqual(before, check_linux.cache_identity(arm, "linux/arm64", root))
            before_adapter = check_linux.cache_identity(arm, "linux/arm64", root)
            adapter = root / "cmake/dependencies/json/CMakeLists.txt"
            adapter.parent.mkdir(parents=True)
            adapter.write_text("checked header recipe")
            self.assertNotEqual(
                before_adapter, check_linux.cache_identity(arm, "linux/arm64", root)
            )

    def test_native_platform_and_job_spelling_are_explicit(self) -> None:
        """Ambient emulation and leading-zero job counts cannot diverge from CMake's contract."""
        with (
            patch.dict("check_linux.os.environ", {"DOCKER_DEFAULT_PLATFORM": "linux/amd64"}),
            self.assertRaisesRegex(ValueError, "DOCKER_DEFAULT_PLATFORM"),
        ):
            check_linux.native_platform()
        with (
            patch("check_linux.docker_executable", return_value="docker"),
            patch("check_linux.subprocess.run") as run,
        ):
            run.return_value.stdout = "linux/aarch64\n"
            with patch.dict("check_linux.os.environ", {"DOCKER_DEFAULT_PLATFORM": ""}):
                self.assertEqual(check_linux.native_platform(), "linux/arm64")
            run.return_value.stdout = "windows/amd64"
            with (
                patch.dict("check_linux.os.environ", {"DOCKER_DEFAULT_PLATFORM": ""}),
                self.assertRaisesRegex(ValueError, "Unsupported Docker"),
            ):
                check_linux.native_platform()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "deps").mkdir()
            for name in ("tools.json", "lock.json", "features.json"):
                (root / "deps" / name).write_text("{}")
            with patch("check_linux.docker_executable", return_value="docker"):
                for jobs in ("02", "0", "65", "-1", "1.0"):
                    with (
                        patch.dict("check_linux.os.environ", {"DE_BUILD_JOBS": jobs}),
                        self.assertRaises(ValueError),
                    ):
                        check_linux.run_arguments("image", "linux/arm64", root)

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
