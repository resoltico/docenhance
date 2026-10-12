#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Fresh-process measured RSS/CPU/wall/disk/thread observations for explicit stage topologies."""

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import os
import platform
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/cli"))
FIXTURES = importlib.import_module("continuous_fixtures")
SAMPLE_INTERVAL = 0.05
CASE_TIMEOUT = 600
CAPTURE_LIMIT = 1024 * 1024
WORD_MAXIMUM = 65535


def threads(pid: int) -> int:
    """Count live threads at one sampling instant, not an unsampled hard upper bound."""
    if platform.system() == "Darwin":
        result = subprocess.run(
            ["/bin/ps", "-M", "-p", str(pid), "-o", "pid="],
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
        return len(result.stdout.splitlines())
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
    """Observe owned staged/output regular file sizes, excluding the separate source fixture."""
    sizes = []
    for path in root.rglob("*"):
        try:
            if path.is_file():
                sizes.append(path.stat().st_size)
        except FileNotFoundError:
            continue
    return sum(sizes)


def observe(executable: Path, source: Path, root: Path, options: list[str]) -> dict[str, Any]:
    """Reap one real child with wait4; CPU and RSS belong to this process, not earlier cases."""
    root.mkdir()
    api = importlib.import_module("os")
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        started = time.monotonic()
        child = subprocess.Popen(
            [
                str(executable),
                "process",
                str(source),
                "--out-dir",
                str(root / "result"),
                *options,
                "--json",
            ],
            stdout=stdout,
            stderr=stderr,
        )
        sampled_threads, sampled_disk = 0, 0
        try:
            while True:
                sampled_threads = max(sampled_threads, threads(child.pid))
                sampled_disk = max(sampled_disk, disk_bytes(root))
                pid, status, usage = api.wait4(child.pid, os.WNOHANG)
                if pid:
                    child.returncode = api.waitstatus_to_exitcode(status)
                    break
                if time.monotonic() - started > CASE_TIMEOUT:
                    message = "Measured case exceeded its declared wall-work envelope"
                    raise TimeoutError(message)
                time.sleep(SAMPLE_INTERVAL)
        finally:
            if child.returncode is None:
                child.kill()
                child.wait()
        wall = time.monotonic() - started
        stdout.seek(0)
        stderr.seek(0)
        payload = stdout.read(CAPTURE_LIMIT + 1)
        if len(payload) > CAPTURE_LIMIT or stderr.read(CAPTURE_LIMIT + 1):
            message = "Measured command response was overlarge or failed delivery"
            raise ValueError(message)
        response = json.loads(payload)
    if (
        child.returncode != 0
        or response["exit_code"] != 0
        or response["publication"] != "completed"
    ):
        message = "Resource measurements require successful completed processing"
        raise ValueError(message)
    return {
        "wall_seconds": wall,
        "user_cpu_seconds": usage.ru_utime,
        "system_cpu_seconds": usage.ru_stime,
        "peak_rss_bytes": usage.ru_maxrss * (1 if platform.system() == "Darwin" else 1024),
        "sampled_threads_peak": sampled_threads,
        "sampled_stage_and_output_bytes_peak": max(sampled_disk, disk_bytes(root)),
        "native_interval_seconds": None,
        "response": response,
    }


def main() -> int:
    """Named repeatable fixtures cover alpha, masks and each retained photometric stage."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Per-process wait4 observations require macOS or Linux")
    executable = args.executable.resolve()
    build = json.loads(subprocess.check_output([str(executable), "version", "--json"]))
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
                "--illumination",
                "morph",
                "--denoise",
                "tvl1",
                "--deblur",
                "wiener",
                "--psf",
                "gaussian",
                "--contrast",
                "clahe",
                "--sharpen",
                "unsharp",
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
    args.out.write_text(
        json.dumps(
            {
                "platform": platform.platform(),
                "machine": platform.machine(),
                "build": build,
                "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                "sampling_interval_seconds": SAMPLE_INTERVAL,
                "limits": "RSS is observed, threads/disk sampled; native call timing unavailable",
                "cases": cases,
            },
            indent=2,
        )
        + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
