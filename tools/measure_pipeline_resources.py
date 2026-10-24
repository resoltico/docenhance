#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Fresh-process measured RSS/CPU/wall/disk/thread observations for explicit stage topologies."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib
import json
import os
import platform
import selectors
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

from capture_reader import capture_failure
from process_execution import owned_process

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/cli"))
FIXTURES = importlib.import_module("continuous_fixtures")
SAMPLE_INTERVAL = 0.05
CASE_TIMEOUT = 600
CAPTURE_LIMIT = 1024 * 1024
WORD_MAXIMUM = 65535
# Two 8 MiB image/record staging sets plus equal operational headroom; sampled, not a quota.
CASE_DISK_LIMIT = 32 * 1024 * 1024
CASE_ENTRY_LIMIT = 1024


def threads(pid: int, deadline: float) -> int:
    """Count live threads at one sampling instant, inside the remaining case clock."""
    if platform.system() == "Darwin":
        payload, _ = capture(
            ["/bin/ps", "-M", "-p", str(pid), "-o", "pid="],
            None,
            min(5, max(0, deadline - time.monotonic())),
        )
        return len(payload.splitlines())
    status = Path(f"/proc/{pid}/status")
    try:
        return next(
            int(line.split()[1])
            for line in status.read_text().splitlines()
            if line.startswith("Threads:")
        )
    except FileNotFoundError:
        return 0


def disk_bytes(root: Path) -> int:
    """Sample this cooperative owned tree; observed links refuse, not a hostile-writer sandbox."""
    total, entries = 0, 0
    pending = [root]
    while pending:
        path = pending.pop()
        try:
            observed = path.lstat()
        except FileNotFoundError:
            continue
        entries += 1
        if getattr(observed, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            message = "Measured scratch contains a redirected entry"
            raise ValueError(message)
        if stat.S_ISREG(observed.st_mode):
            total += observed.st_size
        elif stat.S_ISDIR(observed.st_mode):
            with os.scandir(path) as children:
                for child in children:
                    pending.append(Path(child.path))
                    if entries + len(pending) > CASE_ENTRY_LIMIT:
                        message = "Measured scratch entry allowance exceeded"
                        raise ValueError(message)
        else:
            message = "Measured scratch contains a redirected or special entry"
            raise ValueError(message)
        if total > CASE_DISK_LIMIT:
            message = "Measured scratch exceeds its sampled 32 MiB allowance"
            raise ValueError(message)
    return total


def read_ready(selector: selectors.BaseSelector, payload: bytearray, timeout: float) -> None:
    """Keep at most one complete response in memory; any stderr is a real refusal."""
    for key, _ in selector.select(max(0, timeout)):
        data = os.read(key.fd, 65536)
        if not data:
            selector.unregister(key.fileobj)
        elif key.data == "stderr" or len(payload) + len(data) > CAPTURE_LIMIT:
            message = "Measured command response was overlarge or failed delivery"
            raise ValueError(message)
        else:
            payload.extend(data)


def register_pipes(
    child: subprocess.Popen[bytes], stack: contextlib.ExitStack, selector: selectors.BaseSelector
) -> None:
    """Own both pipe closes before registering either potentially failing selector entry."""
    for stream in (child.stdout, child.stderr):
        if stream is not None:
            stack.callback(stream.close)
    for stream, label in ((child.stdout, "stdout"), (child.stderr, "stderr")):
        if stream is None:
            message = "Measured child output pipe is unavailable"
            raise RuntimeError(message)
        selector.register(stream, selectors.EVENT_READ, label)


def require_command_success(command: list[str], code: int | None, payload: bytearray) -> None:
    """Only ps's exact empty missing-PID result may accompany exit 1."""
    if code == 0:
        return
    match command:
        case ["/bin/ps", "-M", "-p", pid, "-o", "pid="] if (
            code == 1 and not payload and pid.isdecimal()
        ):
            return
    message = "Resource measurements require successful completed processing"
    raise ValueError(message)


def capture(command: list[str], root: Path | None, timeout: float) -> tuple[bytes, dict[str, Any]]:
    """Own one POSIX group and reap its exact direct child with wait4 before Popen can poll it."""
    if timeout <= 0:
        message = "Measured case has no remaining wall-work envelope"
        raise TimeoutError(message)
    started = time.monotonic()
    deadline = started + timeout
    payload = bytearray()
    sampled_threads, sampled_disk = 0, 0
    original: BaseException | None = None
    complete = False
    try:
        with contextlib.ExitStack() as stack:
            selector = stack.enter_context(selectors.DefaultSelector())
            with owned_process(
                command, dict(os.environ), (subprocess.PIPE, subprocess.PIPE)
            ) as child:
                try:
                    register_pipes(child, stack, selector)
                    while True:
                        if time.monotonic() >= deadline:
                            message = "Measured case exceeded its declared wall-work envelope"
                            raise TimeoutError(message)
                        read_ready(
                            selector, payload, min(SAMPLE_INTERVAL, deadline - time.monotonic())
                        )
                        if root is not None:
                            sampled_threads = max(sampled_threads, threads(child.pid, deadline))
                            sampled_disk = max(sampled_disk, disk_bytes(root))
                        pid, status, usage = os.wait4(child.pid, os.WNOHANG)
                        if pid:
                            child.returncode = os.waitstatus_to_exitcode(status)
                            break
                finally:
                    original = sys.exception()
            # The existing owner has now stopped descendants before waiting for pipe EOF.
            while selector.get_map():
                if time.monotonic() >= deadline:
                    message = "Measured output did not close inside its case clock"
                    raise TimeoutError(message)
                read_ready(selector, payload, min(SAMPLE_INTERVAL, deadline - time.monotonic()))
        complete = True
    finally:
        failure = capture_failure(original, complete=complete)
        if failure is not None:
            raise failure
    require_command_success(command, child.returncode, payload)
    return bytes(payload), {
        "wall_seconds": time.monotonic() - started,
        "user_cpu_seconds": usage.ru_utime,
        "system_cpu_seconds": usage.ru_stime,
        "peak_rss_bytes": usage.ru_maxrss * (1 if platform.system() == "Darwin" else 1024),
        "sampled_threads_peak": sampled_threads,
        "sampled_stage_and_output_bytes_peak": sampled_disk,
        "native_interval_seconds": None,
    }


def observe(executable: Path, source: Path, root: Path, options: list[str]) -> dict[str, Any]:
    """Measure one bounded child; RSS/CPU are direct-child observations, not group maxima."""
    root.mkdir()
    payload, result = capture(
        [
            str(executable),
            "process",
            str(source),
            "--out-dir",
            str(root / "result"),
            *options,
            "--json",
        ],
        root,
        CASE_TIMEOUT,
    )
    response = json.loads(payload)
    if response["exit_code"] != 0 or response["publication"] != "completed":
        message = "Resource measurements require successful completed processing"
        raise ValueError(message)
    result["sampled_stage_and_output_bytes_peak"] = max(
        result["sampled_stage_and_output_bytes_peak"], disk_bytes(root)
    )
    return {**result, "response": response}


def main() -> int:
    """Named repeatable fixtures cover alpha, masks and each retained photometric stage."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Per-process wait4 observations require macOS or Linux")
    executable = args.executable.resolve()
    build = json.loads(capture([str(executable), "version", "--json"], None, CASE_TIMEOUT)[0])
    cases = []
    with tempfile.TemporaryDirectory(prefix="docenhance-resource-") as temporary:
        root = Path(temporary)
        width, height = 1024, 768
        source = root / "alpha16.png"
        source.write_bytes(
            FIXTURES.Fixture(
                width,
                height,
                tuple(
                    (30000 + x % 10000, 40000, 50000, WORD_MAXIMUM - y % 1000)
                    for y in range(height)
                    for x in range(width)
                ),
                color=6,
                depth=16,
            ).encoded()
        )
        mask = root / "mask.png"
        mask.write_bytes(
            FIXTURES.Fixture(
                width,
                height,
                tuple((255 if x % 17 == 0 else 0,) for _ in range(height) for x in range(width)),
            ).encoded()
        )
        topologies = {
            "representation": [],
            "nlm": ["--denoise", "nlm"],
            "tvl1": ["--denoise", "tvl1"],
            "fft": ["--deblur", "wiener", "--psf", "gaussian"],
            "clahe": ["--contrast", "clahe"],
            "combined": [
                *("--illumination", "morph", "--denoise", "tvl1"),
                *("--deblur", "wiener", "--psf", "gaussian"),
                *("--contrast", "clahe", "--sharpen", "unsharp"),
            ],
        }
        original = hashlib.sha256(source.read_bytes()).hexdigest()
        for name, options in topologies.items():
            result = observe(
                executable, source, root / name, [*options, "--protect-mask", str(mask)]
            )
            cases.append(
                {
                    "name": name,
                    "width": width,
                    "height": height,
                    "source_sha256": original,
                    "options": options,
                    **result,
                }
            )
        if hashlib.sha256(source.read_bytes()).hexdigest() != original:
            message = "Measurement changed its source"
            raise ValueError(message)
    report = {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "build": build,
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "sampling_interval_seconds": SAMPLE_INTERVAL,
        "limits": (
            "Direct-child RSS/CPU; threads/disk sampled, not group maxima or quotas; "
            "32 MiB/1024 scratch entries per fixed 1024x768 case; "
            "1 MiB response, no stderr, 600 seconds per command; native timing unavailable"
        ),
        "cases": cases,
    }
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
