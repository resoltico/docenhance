# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""The local Linux gate must not skip failures, drift pins or reuse host native outputs."""

from __future__ import annotations

import io
import json
import subprocess
import tempfile
import time
import unittest
from contextlib import redirect_stderr
from pathlib import Path, PurePosixPath
from unittest.mock import patch

import tools_path  # noqa: F401 -- Bootstrap direct tool imports for standalone unittest discovery.

import check_all
import check_linux
import linux_gate_logs
from docker_execution import LOG_LIMIT_BYTES


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

    def test_source_failure_reports_all_checks_without_native_launch(self) -> None:
        """A first source failure still reports all eight and never invokes Docker."""
        for system in ("Darwin", "Windows"):
            output = io.StringIO()
            with (
                self.subTest(system=system),
                patch("check_all.platform.system", return_value=system),
                patch("check_all.sys.argv", ["check_all.py", "--quiet"]),
                patch("check_all.run", side_effect=[False, *([True] * 8)]) as invoked,
                redirect_stderr(output),
            ):
                self.assertEqual(check_all.main(), 1)
            self.assertEqual(invoked.call_count, 8)
            self.assertEqual([call.args for call in invoked.call_args_list], list(check_all.CHECKS))
            self.assertTrue(all(call.kwargs == {"quiet": True} for call in invoked.call_args_list))
            self.assertIn("NOT RUN Linux native workflow (Docker)", output.getvalue())
            self.assertIn("FAILED: structure and contracts", output.getvalue())

    def test_healthy_source_requires_exactly_one_native_result(self) -> None:
        """All nine reviewed checks run and native failure cannot become a local pass."""
        for system in ("Darwin", "Windows"):
            for native_good in (True, False):
                output = io.StringIO()
                with (
                    self.subTest(system=system, native_good=native_good),
                    patch("check_all.platform.system", return_value=system),
                    patch("check_all.sys.argv", ["check_all.py"]),
                    patch("check_all.run", side_effect=[*([True] * 8), native_good]) as invoked,
                    redirect_stderr(output),
                ):
                    self.assertEqual(check_all.main(), 0 if native_good else 1)
                self.assertEqual(invoked.call_count, 9)
                self.assertEqual(
                    [call.args for call in invoked.call_args_list],
                    list(check_all.local_checks(system)),
                )
                arguments = [call.args[1] for call in invoked.call_args_list]
                self.assertEqual(arguments.count(("tools/check_linux.py",)), 1)
                self.assertNotIn("NOT RUN", output.getvalue())

    def test_linux_source_result_never_launches_recursive_native_work(self) -> None:
        """Linux keeps exactly eight checks with both healthy and adverse source outcomes."""
        for source_good in (True, False):
            with (
                self.subTest(source_good=source_good),
                patch("check_all.platform.system", return_value="Linux"),
                patch("check_all.sys.argv", ["check_all.py"]),
                patch("check_all.run", side_effect=[source_good, *([True] * 7)]) as invoked,
                redirect_stderr(io.StringIO()),
            ):
                self.assertEqual(check_all.main(), 0 if source_good else 1)
            self.assertEqual(invoked.call_count, 8)
            self.assertEqual([call.args for call in invoked.call_args_list], list(check_all.CHECKS))

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
            (root / ".cache/deps/archives").mkdir(parents=True)
            (root / "tools").mkdir()
            for name in ("dep_acquire.py", "dep_verify.py"):
                (root / "tools" / name).write_text("fixture")
            with patch("check_linux.docker_executable", return_value="/usr/bin/docker"):
                arguments = check_linux.run_arguments("image", "linux/arm64", "gate", "token", root)
            self.assertNotIn("--rm", arguments)
            mounts = [
                arguments[i + 1] for i, argument in enumerate(arguments) if argument == "--mount"
            ]
            for name in ("out", ".cache", "dist"):
                self.assertTrue(
                    any(m.startswith("type=volume,") and m.endswith("/" + name) for m in mounts)
                )
            self.assertIn(f"type=bind,source={root},target=/source,readonly", mounts)
            self.assertIn(
                f"type=bind,source={root / '.cache/deps'},target=/host-deps,readonly", mounts
            )
            self.assertIn(
                f"type=bind,source={root / '.cache/deps/archives'},"
                "target=/source/.cache/deps/archives,readonly",
                mounts,
            )
            self.assertIn("RUFF_CACHE_DIR=/source/.cache/ruff", arguments)
            self.assertIn("MYPY_CACHE_DIR=/source/.cache/mypy", arguments)
            self.assertIn("TMPDIR=/tmp/docenhance-linux-tmp/token", arguments)
            temporary = PurePosixPath(
                next(value[7:] for value in arguments if value.startswith("TMPDIR="))
            )
            self.assertFalse(temporary.is_relative_to(PurePosixPath("/source")))
            self.assertFalse(any("target=/tmp" in mount for mount in mounts))
            script = Path(check_linux.__file__).with_name("linux_gate.sh").read_text()
            self.assertIn(f"temporary_root={temporary.parent}", script)
            self.assertIn("never", arguments)
            self.assertIn("none", arguments)
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
            # One recipe tag can be rebuilt into different immutable filesystem images.
            source_identity = check_linux.source_cache_identity(root)
            first_image = "sha256:" + "a" * 64
            rebuilt_image = "sha256:" + "b" * 64
            self.assertNotEqual(
                check_linux.cache_identity(first_image, "linux/arm64", root),
                check_linux.cache_identity(rebuilt_image, "linux/arm64", root),
            )
            self.assertEqual(source_identity, check_linux.source_cache_identity(root))
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
            check_linux.native_platform(time.monotonic() + 1000)
        with (
            patch("check_linux.docker_executable", return_value="docker"),
            patch("check_linux.subprocess.run") as run,
        ):
            run.return_value.stdout = "linux/aarch64\n"
            with patch.dict("check_linux.os.environ", {"DOCKER_DEFAULT_PLATFORM": ""}):
                self.assertEqual(
                    check_linux.native_platform(time.monotonic() + 1000), "linux/arm64"
                )
            run.return_value.stdout = "windows/amd64"
            with (
                patch.dict("check_linux.os.environ", {"DOCKER_DEFAULT_PLATFORM": ""}),
                self.assertRaisesRegex(ValueError, "Unsupported Docker"),
            ):
                check_linux.native_platform(time.monotonic() + 1000)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "deps").mkdir()
            for name in ("tools.json", "lock.json", "features.json"):
                (root / "deps" / name).write_text("{}")
            (root / "tools").mkdir()
            for name in ("dep_acquire.py", "dep_verify.py"):
                (root / "tools" / name).write_text("fixture")
            with patch("check_linux.docker_executable", return_value="docker"):
                for jobs in ("02", "0", "65", "-1", "1.0"):
                    with (
                        patch.dict("check_linux.os.environ", {"DE_BUILD_JOBS": jobs}),
                        self.assertRaises(ValueError),
                    ):
                        check_linux.run_arguments("image", "linux/arm64", "gate", "token", root)

    def test_source_cache_does_not_duplicate_native_recipes(self) -> None:
        """Source pins govern sharing; compiler recipes still isolate native outputs."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("deps/lock.json", "tools/dep_acquire.py", "tools/dep_verify.py"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("source contract")
            original = check_linux.source_cache_identity(root)
            (root / "cmake").mkdir()
            (root / "cmake/recipe.cmake").write_text("native recipe changed")
            self.assertEqual(original, check_linux.source_cache_identity(root))
            (root / "deps/lock.json").write_text("different immutable source")
            self.assertNotEqual(original, check_linux.source_cache_identity(root))
            before = check_linux.source_cache_identity(root)
            (root / "tools/dep_verify.py").write_text("different verification")
            self.assertNotEqual(before, check_linux.source_cache_identity(root))

    def test_missing_docker_and_failed_subprocess_are_failures(self) -> None:
        """Unavailable engines and nonzero native results cannot be reported as a pass."""
        with patch("check_linux.shutil.which", return_value=None), self.assertRaises(ValueError):
            check_linux.docker_executable()
        result = subprocess.CompletedProcess([], 1, stdout="", stderr="missing")
        with (
            patch("check_linux.docker_executable", return_value="docker"),
            patch("check_linux.subprocess.run", return_value=result) as invoked,
        ):
            with self.assertRaisesRegex(ValueError, "Required cached Linux image"):
                check_linux.cached_image("image", "linux/arm64", time.monotonic() + 1000)
            self.assertEqual(invoked.call_count, 1)
            self.assertNotIn("build", invoked.call_args.args[0])

    def test_cached_image_launch_identity_and_platform_are_exact(self) -> None:
        """A mutable tag selects one inspected immutable ID; wrong platform is a failure."""
        identity = "sha256:" + "b" * 64
        result = subprocess.CompletedProcess([], 0, stdout=identity + " linux/arm64")
        with (
            patch("check_linux.docker_executable", return_value="docker"),
            patch("check_linux.subprocess.run", return_value=result),
        ):
            self.assertEqual(
                check_linux.cached_image("tag", "linux/arm64", time.monotonic() + 1000), identity
            )
            with self.assertRaisesRegex(ValueError, "platform differs"):
                check_linux.cached_image("tag", "linux/amd64", time.monotonic() + 1000)

    def test_diagnostic_admission_preserves_evidence_and_counts_hardlinks_once(self) -> None:
        """Finite aggregate retention refuses new work without sweeping earlier diagnostics."""
        with tempfile.TemporaryDirectory() as directory:
            logs = Path(directory)
            original = logs / "old.log"
            original.write_bytes(b"retained")
            (logs / "latest.log").hardlink_to(original)
            reserve = LOG_LIMIT_BYTES + linux_gate_logs.COMMAND_LIMIT_BYTES
            with patch("linux_gate_logs.RETAINED_LIMIT_BYTES", reserve + len(b"retained")):
                linux_gate_logs.admit_diagnostics(logs)
            with (
                patch("linux_gate_logs.RETAINED_LIMIT_BYTES", reserve),
                self.assertRaisesRegex(ValueError, "diagnostics are full"),
            ):
                linux_gate_logs.admit_diagnostics(logs)
            with (
                patch("linux_gate_logs.RETAINED_LIMIT_ENTRIES", 5),
                self.assertRaisesRegex(ValueError, "diagnostics are full"),
            ):
                linux_gate_logs.admit_diagnostics(logs)
            self.assertEqual(original.read_bytes(), b"retained")
            self.assertEqual(len(list(logs.iterdir())), 2)

    def test_latest_link_error_preserves_primary_failed_status(self) -> None:
        """A convenience-link error cannot replace a confirmed failed gate outcome."""
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "diagnostics.log"
            log.write_text("failed", encoding="utf-8")
            primary = RuntimeError("failed status")
            with patch("linux_gate_logs.os.link", side_effect=OSError("link refused")):
                linux_gate_logs.publish_latest(log, log.with_name("latest.log"), primary)
            self.assertIn("link refused", primary.__notes__[0])

    def test_preflight_refusal_never_writes_through_dangling_latest(self) -> None:
        """Diagnostic retention cannot create a foreign symlink target after admission refusal."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outside = root / "foreign.log"
            logs = root / "logs"
            logs.mkdir()
            latest = logs / "latest.log"
            latest.symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "redirected entry"):
                linux_gate_logs.admit_diagnostics(logs)
            primary = ValueError("refused redirect")
            with patch.object(Path, "open", side_effect=OSError("redirected open")) as opening:
                linux_gate_logs.retain_preflight_failure(latest, primary)
            opening.assert_not_called()
            self.assertFalse(outside.exists())
            self.assertTrue(latest.is_symlink())
            self.assertIn("retention also failed", primary.__notes__[0])

    def test_gate_timeout_uses_real_workflow_string_scalar_contract(self) -> None:
        """Parse the actual CI authority and reject noncanonical timeout forms via its loader."""
        self.assertEqual(check_linux.gate_seconds(), 180 * 60)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / ".github/workflows/ci.yml"
            path.parent.mkdir(parents=True)
            for value in (
                "1",
                "180",
                "0",
                "-1",
                "+1",
                "01",
                "1.0",
                " 1",
                "true",
                "١٨٠",
                "${{ 180 }}",
            ):
                with self.subTest(value=value):
                    path.write_text(
                        "jobs:\n  native:\n    timeout-minutes: " + json.dumps(value) + "\n",
                        encoding="utf-8",
                    )
                    with patch("check_linux.ROOT", root):
                        if value in {"1", "180"}:
                            self.assertEqual(check_linux.gate_seconds(), int(value) * 60)
                        else:
                            with self.assertRaisesRegex(ValueError, "positive integer"):
                                check_linux.gate_seconds()
            path.write_text("jobs:\n  native:\n    timeout-minutes: []\n", encoding="utf-8")
            with (
                patch("check_linux.ROOT", root),
                self.assertRaisesRegex(ValueError, "positive integer"),
            ):
                check_linux.gate_seconds()
