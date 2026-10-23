#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Observe paired-performance host capability without building or measuring a product."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any

from capture_reader import capture_failure, capture_or_drain, finish_capture_reader
from check_runtime_platform import runtime_errors
from process_execution import owned_process
from select_xcode import confirm_version, developer_directory

if TYPE_CHECKING:
    from typing import IO

ROOT = Path(__file__).resolve().parents[1]
STREAM_BYTES = 65536
TOTAL_SECONDS = 80
WORK_SECONDS = 60
CLEANUP_SECONDS = 5
EVIDENCE_BYTES = 4 * 1024 * 1024
RESULT_BYTES = 2 * 1024 * 1024
CPU_PERCENT = 100
MEMORY_BYTES = 6 * 1024**3
FREE_DISK_BYTES = 5 * 1024**3
NO_WARNING = (
    "Note: No thermal warning level has been recorded\n"
    "Note: No performance warning level has been recorded\n"
    "Note: No CPU power status has been recorded"
)
LOW_POWER_SCRIPT = (
    'ObjC.import("Foundation"); const value = $.NSProcessInfo.processInfo.isLowPowerModeEnabled; '
    'if (typeof value !== "boolean") { throw Error("Low-power state unavailable"); } '
    "JSON.stringify({low_power_mode_enabled: value});"
)
QUERIES = {
    "OS": ["/usr/bin/sw_vers"],
    "machine": ["/usr/sbin/sysctl", "-n", "hw.memsize", "hw.ncpu", "machdep.cpu.brand_string"],
    "power": ["/usr/bin/pmset", "-g", "batt"],
    "power_modes": ["/usr/bin/pmset", "-g", "custom"],
    "low_power": ["/usr/bin/osascript", "-l", "JavaScript", "-e", LOW_POWER_SCRIPT],
    "thermal": ["/usr/bin/pmset", "-g", "therm"],
    "memory": ["/usr/bin/vm_stat"],
}


def _start(
    child: subprocess.Popen[bytes],
    directory: Path,
    readers: list[tuple[threading.Thread | None, IO[bytes] | None, IO[bytes] | None]],
    failures: list[OSError | ValueError | RuntimeError],
) -> None:
    for name in ("stdout", "stderr"):
        source = getattr(child, name)
        if source is None:
            message = "Capability child has no selected output pipe"
            raise RuntimeError(message)
        readers.append((None, source, None))
    for index, (name, (_, source, _)) in enumerate(zip(("stdout", "stderr"), readers, strict=True)):
        sink = (directory / name).open("xb")
        readers[index] = (None, source, sink)
        reader = threading.Thread(
            target=capture_or_drain, args=(source, sink, STREAM_BYTES, failures), daemon=True
        )
        readers[index] = (reader, source, sink)
        reader.start()


def _finish(
    readers: list[tuple[threading.Thread | None, IO[bytes] | None, IO[bytes] | None]],
    original: BaseException | None,
) -> None:
    if not readers:
        return
    first, *second = readers
    finished = False
    try:
        finish_capture_reader(*first, CLEANUP_SECONDS, original)
        finished = True
    finally:
        failure = original if finished else sys.exception()
        if second:
            finish_capture_reader(*second[0], CLEANUP_SECONDS, failure)


def capture(
    command: list[str], directory: Path, seconds: float, environment: dict[str, str] | None = None
) -> dict[str, Any]:
    """Reuse joined bounded pipe owners, preserving timeout/control and cleanup errors."""
    directory.mkdir()
    readers: list[tuple[threading.Thread | None, IO[bytes] | None, IO[bytes] | None]] = []
    failures: list[OSError | ValueError | RuntimeError] = []
    complete = False
    original: BaseException | None = None
    try:
        with owned_process(
            command,
            dict(os.environ) if environment is None else environment,
            (subprocess.PIPE, subprocess.PIPE),
        ) as child:
            body_complete = False
            try:
                _start(child, directory, readers, failures)
                code = child.wait(timeout=seconds)
                body_complete = True
            finally:
                if not body_complete:
                    original = sys.exception()
        complete = True
    finally:
        failure = capture_failure(original, complete=complete)
        _finish(readers, failure)
        if original is not None:
            raise original
    if failures:
        message = "Capability output capture failed"
        raise OSError(message) from failures[0]
    streams = {}
    for name in ("stdout", "stderr"):
        data = (directory / name).read_bytes()
        if len(data) > STREAM_BYTES:
            message = f"Capability {name} exceeded its complete capture allowance"
            raise ValueError(message)
        streams[name] = {
            "path": str(directory / name),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "text": data.decode("utf-8", "strict"),
        }
    return {"argv": command, "exit_code": code, "streams": streams, "children_readers_closed": True}


def _power_causes(observed: dict[str, str]) -> list[str]:
    causes = []
    if (observed["power"].splitlines() or [""])[0] != "Now drawing from 'AC Power'":
        causes.append("ACpowernotobserved")
    modes = re.findall(r"(?m)^.*\blowpowermode\b.*$", observed["power_modes"])
    if observed.get("low_power", "").strip() != '{"low_power_mode_enabled":false}' or any(
        re.fullmatch(r"\s*lowpowermode\s+0\s*", line) is None for line in modes
    ):
        causes.append("Lowpoweroffnotestablished")
    thermal = observed["thermal"].strip()
    if thermal == NO_WARNING:
        return causes
    fields = [
        part
        for part in re.split(r"[;\n]", thermal)
        if re.search(r"\bCPU_(Speed|Scheduler)_Limit\b", part)
    ]
    parsed = [
        re.fullmatch(r"\s*CPU_(Speed|Scheduler)_Limit\s*=\s*(\d+)\s*", part) for part in fields
    ]
    limits = [match for match in parsed if match is not None]
    if (
        len(limits) != len(fields)
        or {match[1] for match in limits} != {"Speed", "Scheduler"}
        or any(int(match[2]) > CPU_PERCENT for match in limits)
    ):
        causes.append("Unknownthermalobservation")
    if any(int(match[2]) < CPU_PERCENT for match in limits):
        causes.append("ReportedCPUthrottle")
    return causes


def evaluate(observed: dict[str, str], free_bytes: int) -> dict[str, Any]:
    """Unknown power, thermal or producer headroom refuses capability without waivers."""
    causes = _power_causes(observed)
    memory = observed["memory"]
    page = re.findall(
        r"(?m)^Mach Virtual Memory Statistics: \(page size of (\d+) bytes\)\s*$", memory
    )
    labels = ("Pages free", "Pages inactive", "Pages speculative")
    groups = [re.findall(r"(?m)^" + re.escape(label) + r":\s*(\d+)\.$", memory) for label in labels]
    unique = all(
        len(re.findall(r"(?m)^" + re.escape(label) + ":", memory)) == 1 for label in labels
    )
    unique = unique and len(re.findall(r"(?m)^Mach Virtual Memory Statistics:", memory)) == 1
    available = None
    if unique and len(page) == 1 and int(page[0]) > 0 and all(len(group) == 1 for group in groups):
        available = int(page[0]) * sum(int(group[0]) for group in groups)
    if available is None or available < MEMORY_BYTES:
        causes.append("ProducerMemoryHeadroomUnavailableOrBelow6GiB")
    if free_bytes < FREE_DISK_BYTES:
        causes.append("DiskFreeBelow5GiB")
    return {
        "capability_admitted": not causes,
        "causes": causes,
        "free_inactive_speculative_bytes": available,
        "free_disk_bytes": free_bytes,
        "measurement_eligibility": False,
        "full_R17_acceptance": False,
    }


def _observe(
    output: Path, started: float, rows: dict[str, Any], actual_host: dict[str, str]
) -> dict[str, Any]:
    errors = runtime_errors(
        "macos-15-x86_64", actual_host["system"], actual_host["machine"], actual_host["version"]
    )
    if errors:
        raise ValueError("; ".join(errors))
    version = json.loads((ROOT / "deps/tools.json").read_bytes())["macos"]["minimum_runner_xcode"]
    selected = developer_directory(Path("/Applications"), version)
    environment = dict(os.environ, DEVELOPER_DIR=str(selected))
    observations = {}
    queries = [
        ("Xcode", [str(selected / "usr/bin/xcodebuild"), "-version"]),
        *QUERIES.items(),
        ("disk", ["/bin/df", "-Pk", str(output)]),
    ]
    for name, command in queries:
        remaining = WORK_SECONDS - (time.monotonic() - started)
        if remaining <= 0:
            message = "Capability original work deadline exhausted"
            raise TimeoutError(message)
        row = capture(command, output / name, min(4, remaining), environment)
        rows[name] = row
        if row["exit_code"] != 0 or row["streams"]["stderr"]["text"]:
            message = f"Unavailable or adverse capability observation: {name}"
            raise ValueError(message)
        observations[name] = row["streams"]["stdout"]["text"]
        if name == "Xcode":
            confirm_version(observations[name], version)
            row["selected_developer_directory"] = str(selected)
        if not observations[name].strip():
            message = f"Empty capability observation: {name}"
            raise ValueError(message)
    return evaluate(observations, shutil.disk_usage(output).free)


def run(output: Path) -> dict[str, Any]:
    """Exclusively retain one cheap attempt; no source acquisition, build, SUT or idle claim."""
    output.mkdir()
    started = time.monotonic()
    result: dict[str, Any] = {
        "capability_admitted": False,
        "measurement_eligibility": False,
        "full_R17_acceptance": False,
        "source_sha": os.getenv("GITHUB_SHA"),
        "host": {
            "system": platform.system(),
            "machine": platform.machine(),
            "version": platform.mac_ver()[0],
        },
        "observations": {},
        "product_builds": 0,
        "SUT_invocations": 0,
    }
    try:
        result.update(_observe(output, started, result["observations"], result["host"]))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        result.update(error=repr(error), notes=getattr(error, "__notes__", []))
    result["elapsed_seconds"] = time.monotonic() - started
    if result["elapsed_seconds"] >= TOTAL_SECONDS:
        result.update(
            capability_admitted=False, error="Capability original total deadline exhausted"
        )
    for row in result["observations"].values():
        for stream in row["streams"].values():
            stream.pop("text")
    data = (json.dumps(result, indent=2) + "\n").encode()
    logical = sum(p.stat().st_size for p in output.rglob("*") if p.is_file())
    if len(data) > RESULT_BYTES or logical + len(data) > EVIDENCE_BYTES:
        message = "Capability retained evidence exceeds its fixed allowance"
        raise ValueError(message)
    with (output / "result.json").open("xb") as destination:
        destination.write(data)
    return result


def main() -> int:
    """A capability observation can pass only its own host predicates, never qualification."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run(args.output.absolute())
    print(json.dumps(result, sort_keys=True))
    return 0 if result["capability_admitted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
