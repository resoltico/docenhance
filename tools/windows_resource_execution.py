# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""The selected Windows shell's scoped Debug producer and actual resource execution."""

from __future__ import annotations

import json
import os
import re
import sys
import time
from pathlib import Path
from typing import Any

from native_history import owned_native_history, regular_directory
from native_journal import snapshot_ctest_output
from native_output import archive_ctest_output
from native_resource_capture import calibrations, retained_bodies
from process_execution import CapturePolicy, bounded_capture
from restoration_resource_cases import RESOURCE_PATTERN, resource_cases
from restoration_resource_records import section
from run_native_suite import MAX_TEST_PROCESSES, _execute_owned
from test_evidence import complete_junit, discovery, report_xml
from windows_resource_artifacts import identity
from windows_resource_producer import compilation, native_context
from windows_resource_records import capture_maps, native_records, retained_owner

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/windows-resource-proof"
FFT_PATTERN = r"^fft-native-resources$"


def run(command: list[str], name: str, cap: float, deadline: float, record: dict[str, Any]) -> None:
    """Bound concrete build output and refuse truncation instead of hiding linker evidence."""
    remaining = min(cap, deadline - time.monotonic())
    if remaining <= 0:
        message = "Windows producer deadline exhausted"
        raise ValueError(message)
    log = OUT / (name + ".log")
    step: dict[str, Any] = {"argv": command, "cap_seconds": remaining}
    record.setdefault("commands", []).append(step)
    entered = time.monotonic()
    try:
        step["exit_code"] = bounded_capture(
            command,
            dict(os.environ),
            log,
            remaining,
            CapturePolicy(2 * 1024 * 1024, lambda: None, 1),
        )
    finally:
        error = sys.exception()
        step["error"] = repr(error) if error is not None else None
        step["elapsed_seconds"] = time.monotonic() - entered
        if log.exists():
            step["log"] = identity(log)
    if step["exit_code"] != 0 or b"[engine output omitted:" in log.read_bytes():
        message = f"Windows {name} failed or its retained diagnostic was truncated"
        raise ValueError(message)


def debug_producer(deadline: float, record: dict[str, Any]) -> None:
    """Configure the actual private Debug deps, then only the two EXE producer closures."""
    if (ROOT / "out/dev").exists():
        message = "Debug proof requires its fresh private producer directory"
        raise ValueError(message)
    run(["cmake", "--preset", "dev"], "debug-configure", 180, deadline, record)
    run(
        ["cmake", "--build", "out/dev", "--target", "de_native-configure", "--parallel", "2"],
        "debug-dependencies",
        1800,
        deadline,
        record,
    )
    run(
        [
            "cmake",
            "--build",
            "out/dev/app",
            "--target",
            "de_fft_resources",
            "de_restoration_resources",
            "--parallel",
            "2",
        ],
        "debug-targets",
        600,
        deadline,
        record,
    )
    build = ROOT / "out/dev"
    record["producer"], record["maps"] = {}, []
    compilation(build, record["producer"], debug=True)
    capture_maps(build, OUT / "debug", record["maps"], debug=True)
    directives(record, "debug", deadline)


def directives(record: dict[str, Any], phase: str, deadline: float) -> None:
    """Read real direct-object DEFAULTLIB directives with the selected native tool."""
    producer = record["producer"]
    tool = Path(producer["linker"]["path"]).with_name("dumpbin.exe")
    record["dumpbin"] = identity(tool)
    record["CRT_context"] = native_context(debug=phase == "debug")
    for target in producer["targets"]:
        name = phase + "-" + target["target"] + "-directives"
        run([str(tool), "/DIRECTIVES", target["object"]["path"]], name, 10, deadline, record)
        text = (OUT / (name + ".log")).read_text(encoding="utf-8")
        matches = re.findall(r'/DEFAULTLIB:(?:"([^"\r\n]+)"|([^\s]+))', text, re.IGNORECASE)
        libraries = {(quoted or bare).casefold().removesuffix(".lib") for quoted, bare in matches}
        expected = "libcmtd" if phase == "debug" else "libcmt"
        if expected not in libraries or libraries & {"msvcrt", "msvcrtd"}:
            message = "Actual direct observer DEFAULTLIB static CRT differs"
            raise ValueError(message)
        target["actual_DEFAULTLIB"] = sorted(libraries)


def resource_discovery(build: Path, pattern: str) -> list[dict[str, Any]]:
    """Query the owned partial-build scope and bind each actual registered working directory."""
    regular_directory(build)
    regular_directory(build / "tests")
    tests = discovery("ctest", build, pattern=pattern)
    for test in tests:
        properties = {item["name"]: item["value"] for item in test.get("properties", [])}
        working = properties.get("WORKING_DIRECTORY")
        if not isinstance(working, str) or Path(working) != build / "tests":
            message = "Resource registration working directory differs from its producer"
            raise ValueError(message)
    return tests


def debug_cases(deadline: float, record: dict[str, Any], executables: dict[str, str]) -> None:
    """Retain fresh complete raw/JUnit after owned child cleanup."""
    build, directory = ROOT / "out/dev/app", OUT / "debug"
    with owned_native_history(build, deadline) as history:
        owner = directory.lstat()
        expected_owner = [owner.st_dev, owner.st_ino]
        record["selected_owner"] = {"path": str(directory), "identity": expected_owner}
        retained_owner(record["selected_owner"])
        # Retained Debug maps share this current owner; there is no additional allowance.
        history.directory = directory
        history.admit()
        retained_owner(record["selected_owner"])
        tests = resource_discovery(build, RESOURCE_PATTERN)
        names = resource_cases(tests, executables)
        record["selected_cases"] = sorted(names)
        before = snapshot_ctest_output(build)
        command = [
            "ctest",
            "--test-dir",
            str(build),
            "-R",
            RESOURCE_PATTERN,
            "--no-tests=error",
            "--stop-on-failure",
            "--parallel",
            str(MAX_TEST_PROCESSES),
            "--timeout",
            "90",
            "--output-junit",
            str(directory / "ctest.xml"),
        ]
        record["argv"] = command
        slots = (len(names) + MAX_TEST_PROCESSES - 1) // MAX_TEST_PROCESSES
        remaining = min(90 * slots, deadline - time.monotonic() - 30)
        if remaining <= 0:
            message = "Debug test admission exhausted its archive reserve"
            raise ValueError(message)
        failure: BaseException | None = None
        exit_code: int | None = None
        finished = False
        try:
            exit_code = bounded_capture(
                command,
                dict(os.environ),
                OUT / "debug-tests.log",
                remaining,
                CapturePolicy(2 * 1024 * 1024, history.check, 1),
            )
            finished = True
        finally:
            failure = None if finished else sys.exception()
            record["exit_code"] = exit_code
            record["child_error"] = repr(failure) if failure is not None else None
            try:
                archive_ctest_output(build, directory, before, deadline, len(names))
            except BaseException as error:
                record["archive_error"] = repr(error)
                if failure is not None:
                    failure.add_note(f"Windows diagnostic archival also failed: {error!r}")
                    raise failure from error
                raise
        if failure is not None:
            raise failure
        if exit_code != 0:
            message = f"Debug resource CTest failed: {exit_code}"
            raise ValueError(message)
        exact_debug_junit(directory / "ctest.xml", names)
        record["evidence"] = native_records(directory, expected_owner, tests, executables)


def exact_debug_junit(path: Path, names: set[str]) -> None:
    """Debug requires every discovered resource case and truthful zero-adverse counters."""
    complete_junit(path, names)
    suite = report_xml(path.read_text(encoding="utf-8"))
    if suite.get("tests") != str(len(names)) or any(
        suite.get(key, "0") != "0" for key in ("failures", "errors", "skipped", "disabled")
    ):
        message = "Debug JUnit suite counters differ from executed healthy resource cases"
        raise ValueError(message)


def release_fft_preflight(deadline: float, record: dict[str, Any]) -> None:
    """Run the actual FFT target early with full raw diagnostics and no suite claim."""
    build, directory = ROOT / "out/release/app", OUT / "release-preflight"
    manifest = build / "native-tests.json"
    expected = (build / "tests/de_fft_resources.exe").resolve()
    binary = Path(json.loads(manifest.read_text())["resource_binaries"]["de_fft_resources"])
    selected = resource_discovery(build, FFT_PATTERN)
    if (
        binary.resolve() != expected
        or len(selected) != 1
        or selected[0]["name"] != "fft-native-resources"
        or len(selected[0]["command"]) != 1
        or Path(selected[0]["command"][0]).resolve() != expected
    ):
        message = "Early FFT must use its one actual registered executable command"
        raise ValueError(message)
    record["manifest"], record["binary"] = identity(manifest), identity(expected)
    command = [
        "ctest",
        "--test-dir",
        str(build),
        "-R",
        FFT_PATTERN,
        "--no-tests=error",
        "--stop-on-failure",
        "--parallel",
        "1",
        "--timeout",
        "90",
        "--output-junit",
        str(directory / "ctest.xml"),
    ]
    record["argv"] = command
    with owned_native_history(build, deadline, directory) as history:
        record["exit_code"] = _execute_owned(command, history, 1)
        if record["exit_code"] != 0:
            message = "Early actual FFT resource runtime failed"
            raise ValueError(message)
        exact_debug_junit(directory / "ctest.xml", {"fft-native-resources"})
        bodies = retained_bodies(directory)
        calibrations(section(bodies["journal"], "fft-native-resources"))
        if (
            identity(manifest) != record["manifest"]
            or identity(expected) != record["binary"]
            or time.monotonic() >= deadline
        ):
            message = "Early FFT producer changed or its original deadline expired"
            raise ValueError(message)
        record["evidence"] = identity(directory / "native-output.json")
