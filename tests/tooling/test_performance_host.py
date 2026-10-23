# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Cheap capability observations preserve unknown refusals and bounded capture ownership."""

from __future__ import annotations

import hashlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING, Any
from unittest.mock import Mock, patch

if TYPE_CHECKING:
    import threading
    from collections.abc import Callable
    from typing import IO

from tools_path import ROOT

import check_performance_host as host
from capture_reader import finish_capture_reader
from check_performance_host import _finish, _start


def healthy() -> dict[str, str]:
    """A synthetic machine exactly meets the unchanged producer headroom floors."""
    return {
        "power": "Now drawing from 'AC Power'\n",
        "power_modes": "lowpowermode 0\n",
        "low_power": '{"low_power_mode_enabled":false}\n',
        "thermal": host.NO_WARNING,
        "memory": (
            "Mach Virtual Memory Statistics: (page size of 4096 bytes)\n"
            "Pages free: 1572864.\nPages inactive: 0.\nPages speculative: 0.\n"
        ),
    }


class PerformanceHostTests(unittest.TestCase):
    """No real pmset, idle sample, compiler, build or product is invoked by these controls."""

    def test_exact_floors_admit_only_capability_and_keep_measurement_unearned(self) -> None:
        """A healthy capability result cannot claim idle eligibility or completed R17."""
        result = host.evaluate(healthy(), host.FREE_DISK_BYTES)
        self.assertTrue(result["capability_admitted"])
        self.assertEqual(result["free_inactive_speculative_bytes"], host.MEMORY_BYTES)
        self.assertFalse(result["measurement_eligibility"])
        self.assertFalse(result["full_R17_acceptance"])
        observed = healthy()
        observed["thermal"] = "CPU_Speed_Limit = 100; CPU_Scheduler_Limit = 100;"
        self.assertTrue(host.evaluate(observed, host.FREE_DISK_BYTES)["capability_admitted"])

    def test_unknown_or_adverse_observations_refuse_without_waivers(self) -> None:
        """Missing power, low-power mode, thermal identity or sufficient memory is not success."""
        for key, text in (
            ("power", "Battery Power"),
            ("power", "Now drawing from 'Battery Power'\nAC Power available"),
            ("power", ""),
            ("power_modes", "lowpowermode 1"),
            ("power_modes", "lowpowermode 2"),
            ("power_modes", "lowpowermode 0.5"),
            ("power_modes", "lowpowermode 0 junk"),
            ("power_modes", "lowpowermode 0\nlowpowermode unavailable"),
            ("power_modes", "lowpowermode 0\nlowpowermode 1"),
            ("thermal", "unknown"),
            ("thermal", "CPU_Speed_Limit = 100"),
            ("thermal", "CPU_Speed_Limit = 101; CPU_Scheduler_Limit = 100;"),
            (
                "thermal",
                "CPU_Speed_Limit = 100; CPU_Scheduler_Limit = 100;CPU_Speed_Limit = unknown",
            ),
            (
                "thermal",
                "CPU_Speed_Limit = 100; CPU_Scheduler_Limit = 100;CPU_Scheduler_Limit = 100.5",
            ),
            ("thermal", "CPU_Speed_Limit = 99; CPU_Scheduler_Limit = 100;"),
            ("thermal", "CPU_Speed_Limit = 99; CPU_Speed_Limit = 100; CPU_Scheduler_Limit = 100;"),
            ("memory", ""),
            ("memory", healthy()["memory"] + "Mach Virtual Memory Statistics: bad\n"),
            ("memory", healthy()["memory"].replace("1572864.", "9999999.bad")),
            ("memory", healthy()["memory"] + "Pages free: 9999999.bad\n"),
            ("memory", healthy()["memory"].replace("4096 bytes)", "4096 bytes) bad")),
            ("memory", "page size of 4096 bytes\nPages free: 1."),
            ("memory", healthy()["memory"] + "Pages free: 1572864.\n"),
        ):
            with self.subTest(key=key, text=text):
                data = healthy()
                data[key] = text
                self.assertFalse(host.evaluate(data, host.FREE_DISK_BYTES)["capability_admitted"])
        self.assertFalse(host.evaluate(healthy(), host.FREE_DISK_BYTES - 1)["capability_admitted"])

    def test_native_low_power_boolean_is_required_even_without_pmset_field(self) -> None:
        """Only direct OS false admits; absent pmset does not invent disabled mode."""
        observed = healthy()
        observed["power_modes"] = "AC Power:\n standby 0\n"
        self.assertTrue(host.evaluate(observed, host.FREE_DISK_BYTES)["capability_admitted"])
        for payload in (
            "",
            "undefined",
            "null",
            "false",
            "{}",
            '{"low_power_mode_enabled":true}',
            '{"low_power_mode_enabled":0}',
            '{"low_power_mode_enabled":"false"}',
            '{"low_power_mode_enabled":null}',
            '{"low_power_mode_enabled":false,"extra":false}',
            '{"low_power_mode_enabled":true,"low_power_mode_enabled":false}',
            '{"low_power_mode_enabled":false} trailing',
        ):
            with self.subTest(payload=payload):
                observed["low_power"] = payload
                self.assertFalse(
                    host.evaluate(observed, host.FREE_DISK_BYTES)["capability_admitted"]
                )
        observed.pop("low_power")
        self.assertFalse(host.evaluate(observed, host.FREE_DISK_BYTES)["capability_admitted"])
        self.assertEqual(
            host.QUERIES["low_power"][:4], ["/usr/bin/osascript", "-l", "JavaScript", "-e"]
        )
        self.assertIn('typeof value !== "boolean"', host.LOW_POWER_SCRIPT)
        self.assertFalse({"compiler", "SDK", "linker"} & host.QUERIES.keys())

    def test_native_low_power_timeout_preserves_original_failure_and_raw_streams(self) -> None:
        """The fixed native query has the original4s bound and cannot fall back to pmset."""
        original = subprocess.TimeoutExpired(host.QUERIES["low_power"], 4)
        capture = self.fixture_capture(healthy())

        def timeout(
            command: list[str],
            directory: Path,
            seconds: float,
            environment: dict[str, str] | None = None,
        ) -> dict[str, Any]:
            if directory.name == "low_power":
                self.assertEqual(command, host.QUERIES["low_power"])
                self.assertLessEqual(seconds, 4)
                directory.mkdir()
                (directory / "stdout").write_bytes(b"")
                (directory / "stderr").write_bytes(b"")
                raise original
            return capture(command, directory, seconds, environment)

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "refused"
            result = self.run_fixture(output, capture_override=timeout)
            self.assertFalse(result["capability_admitted"])
            self.assertEqual(result["error"], repr(original))
            self.assertFalse((output / "thermal").exists())
            self.assertTrue((output / "low_power/stdout").exists())
            self.assertEqual(result["product_builds"], 0)
            self.assertEqual(result["SUT_invocations"], 0)

    def test_actual_noisy_child_cannot_write_unbounded_diagnostics(self) -> None:
        """A real 8 MiB stream retains bounded head/tail and refuses overflow."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "capture"
            body = (
                "import os; [os.write(1,b'x'*65536) for _ in range(128)]; "
                "os.write(1,b'FINAL'); os.write(2,b'original stderr')"
            )
            with self.assertRaisesRegex(ValueError, "complete capture allowance"):
                host.capture([sys.executable, "-c", body], directory, 4)
            stdout = (directory / "stdout").read_bytes()
            self.assertLess(len(stdout), host.STREAM_BYTES + 128)
            self.assertTrue(stdout.endswith(b"FINAL"))
            self.assertIn(b"output omitted:", stdout)
            self.assertEqual((directory / "stderr").read_bytes(), b"original stderr")

    def test_actual_timeout_joins_both_readers_and_never_becomes_a_capability_pass(self) -> None:
        """A blocked synthetic process times out with both pipe borrows closed."""
        finished: list[tuple[threading.Thread | None, IO[bytes] | None, IO[bytes] | None]] = []

        def finish(
            reader: threading.Thread | None,
            source: IO[bytes] | None,
            sink: IO[bytes] | None,
            timeout: float,
            original: BaseException | None = None,
        ) -> None:
            finish_capture_reader(reader, source, sink, timeout, original)
            finished.append((reader, source, sink))

        with (
            tempfile.TemporaryDirectory() as temporary,
            patch.object(host, "finish_capture_reader", side_effect=finish),
            self.assertRaises(subprocess.TimeoutExpired),
        ):
            host.capture(
                [sys.executable, "-c", "import threading; threading.Event().wait()"],
                Path(temporary) / "capture",
                1,
            )
        self.assertEqual(len(finished), 2)
        self.assertTrue(all(item[0] is None or not item[0].is_alive() for item in finished))
        self.assertTrue(all(item[1] is None or item[1].closed for item in finished))
        self.assertTrue(all(item[2] is None or item[2].closed for item in finished))

    def test_reader_cleanup_failure_keeps_timeout_and_closes_the_other_pipe(self) -> None:
        """An annotated original timeout remains fatal while the second borrow still closes."""
        original = subprocess.TimeoutExpired(["synthetic context"], 4)
        reader = Mock(ident=1)
        reader.is_alive.return_value = True
        source, sink = io.BytesIO(), io.BytesIO()
        with self.assertRaises(subprocess.TimeoutExpired) as caught:
            _finish([(reader, None, None), (None, source, sink)], original)
        self.assertIs(caught.exception, original)
        self.assertTrue(getattr(original, "__notes__", []))
        self.assertTrue(source.closed)
        self.assertTrue(sink.closed)
        with (
            tempfile.TemporaryDirectory() as temporary,
            patch.object(host, "_observe", side_effect=original),
        ):
            result = host.run(Path(temporary) / "refused")
        self.assertFalse(result["capability_admitted"])
        self.assertEqual(result["notes"], original.__notes__)

    def test_partial_start_registers_both_pipe_owners_and_preserves_failure(self) -> None:
        """Failure to start a reader cannot lose the second pipe or bless unknown cleanup."""
        original = RuntimeError("second reader startup refused")
        child = Mock(stdout=io.BytesIO(), stderr=io.BytesIO())
        first, second = Mock(ident=1), Mock(ident=None)
        first.is_alive.return_value = False
        second.start.side_effect = original
        resources: list[tuple[threading.Thread | None, IO[bytes] | None, IO[bytes] | None]] = []
        first.start.side_effect = lambda: self.assertEqual(len(resources), 2)
        with tempfile.TemporaryDirectory() as temporary:
            try:
                with (
                    patch("check_performance_host.threading.Thread", side_effect=[first, second]),
                    self.assertRaises(RuntimeError) as caught,
                ):
                    _start(child, Path(temporary), resources, [])
                self.assertIs(caught.exception, original)
                self.assertIs(resources[0][1], child.stdout)
                self.assertIs(resources[1][1], child.stderr)
                with self.assertRaises(RuntimeError) as cleanup:
                    _finish(resources, original)
                self.assertIs(cleanup.exception, original)
                self.assertTrue(getattr(original, "__notes__", []))
                self.assertTrue(child.stdout.closed)
            finally:
                # The fixture knows its fake second thread never ran; production retains unknowns.
                for _, source, sink in resources:
                    if source is not None:
                        source.close()
                    if sink is not None:
                        sink.close()

    def fixture_capture(
        self, observations: dict[str, str], adverse: str | None = None
    ) -> Callable[[list[str], Path, float, dict[str, str] | None], dict[str, Any]]:
        """Create original tiny files while mocking only native context-query acquisition."""

        def capture(
            command: list[str],
            directory: Path,
            seconds: float,
            environment: dict[str, str] | None = None,
        ) -> dict[str, Any]:
            self.assertGreater(seconds, 0)
            self.assertLessEqual(seconds, 4)
            self.assertIsNotNone(environment)
            directory.mkdir()
            text = observations.get(directory.name, "observed context\n")
            if directory.name == "Xcode":
                version = json.loads((ROOT / "deps/tools.json").read_text())["macos"][
                    "minimum_runner_xcode"
                ]
                text = f"Xcode {version}\nBuild version controlled\n"
            streams = {}
            for name, data in (
                ("stdout", text.encode()),
                ("stderr", b"warning" if adverse == directory.name else b""),
            ):
                path = directory / name
                path.write_bytes(data)
                streams[name] = {
                    "path": str(path),
                    "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "text": data.decode(),
                }
            return {
                "argv": command,
                "exit_code": 0,
                "streams": streams,
                "children_readers_closed": True,
            }

        return capture

    def run_fixture(
        self,
        output: Path,
        adverse: str | None = None,
        capture_override: Callable[..., dict[str, Any]] | None = None,
    ) -> dict[str, Any]:
        """Bind actual version policy, keeping all native queries mocked and deterministic."""
        with (
            patch.object(host, "ROOT", ROOT),
            patch("check_performance_host.platform.system", return_value="Darwin"),
            patch("check_performance_host.platform.machine", return_value="x86_64"),
            patch("check_performance_host.platform.mac_ver", return_value=("15.7.2", (), "")),
            patch.object(host, "developer_directory", return_value=Path("/selected/Xcode")),
            patch.object(
                host,
                "capture",
                side_effect=capture_override or self.fixture_capture(healthy(), adverse),
            ),
            patch("check_performance_host.shutil.disk_usage") as usage,
        ):
            usage.return_value.free = host.FREE_DISK_BYTES
            return host.run(output)

    def test_complete_mocked_capability_has_no_build_idle_or_sut_side_effect(self) -> None:
        """Every query is retained with one result and no source edits."""
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "observations"
            result = self.run_fixture(output)
            self.assertTrue(result["capability_admitted"])
            self.assertEqual(result["product_builds"], 0)
            self.assertEqual(result["SUT_invocations"], 0)
            self.assertFalse(result["measurement_eligibility"])
            self.assertFalse(result["full_R17_acceptance"])
            self.assertEqual(len(result["observations"]), len(host.QUERIES) + 2)
            self.assertFalse(any("cpu" in key.lower() for key in result["observations"]))
            original = (output / "result.json").read_bytes()
            with self.assertRaises(FileExistsError):
                self.run_fixture(output)
            self.assertEqual((output / "result.json").read_bytes(), original)

    def test_newer_macos_cannot_substitute_for_minimum_host_capability(self) -> None:
        """The existing platform authority refuses macOS 26 for the macOS 15 Intel lane."""
        with (
            tempfile.TemporaryDirectory() as temporary,
            patch("check_performance_host.platform.system", return_value="Darwin"),
            patch("check_performance_host.platform.machine", return_value="x86_64"),
            patch("check_performance_host.platform.mac_ver", return_value=("26.6", (), "")),
            patch.object(host, "capture") as capture,
        ):
            result = host.run(Path(temporary) / "wrong-major")
        self.assertFalse(result["capability_admitted"])
        self.assertEqual(result["host"]["version"], "26.6")
        capture.assert_not_called()

    def test_query_stderr_or_unknown_platform_is_retained_as_refusal(self) -> None:
        """Context warnings and unsupported host identity never admit a later measurement."""
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "warning"
            result = self.run_fixture(output, "power")
            self.assertFalse(result["capability_admitted"])
            self.assertIn("adverse capability observation: power", result["error"])
            self.assertEqual((output / "power/stderr").read_bytes(), b"warning")
            with (
                patch("check_performance_host.platform.system", return_value="other"),
                patch.object(host, "capture") as capture,
            ):
                refused = host.run(Path(temporary) / "unsupported")
            self.assertFalse(refused["capability_admitted"])
            capture.assert_not_called()
