#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Fresh-process synthetic D01/I01->D01 runtime, charged preparation and RSS observations."""

from __future__ import annotations

import argparse
import importlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from types import ModuleType

ROOT = Path(__file__).resolve().parents[1]


def process_observation() -> ModuleType:
    """Developer-only POSIX measurement; runtime processing has no Python dependency."""
    if os.name != "posix":
        msg = "POSIX child RSS observation is unavailable on Windows"
        raise RuntimeError(msg)
    return importlib.import_module("os")


def chunk(kind: bytes, data: bytes) -> bytes:
    """Encode independently framed PNG chunks."""
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def fixture(width: int, height: int) -> bytes:
    """Bounded synthetic 8-bit grayscale shading/noise; no native encoder or semantic claim."""
    compressor = zlib.compressobj()
    compressed = bytearray()
    for y in range(height):
        row = bytes(180 + (40 * x // width) + ((x * 37 + y * 13) % 9) - 4 for x in range(width))
        compressed.extend(compressor.compress(b"\0" + row))
    compressed.extend(compressor.flush())
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", bytes(compressed))
        + chunk(b"IEND", b"")
    )


def run_one(executable: Path, source: Path, output: Path, *, illumination: bool) -> dict[str, Any]:
    """Wait/reap one child individually, without importing a parent's RSS or cumulative maximum."""
    process_api = process_observation()
    options = ["--denoise", "nlm", "--json"]
    if illumination:
        options += ["--illumination", "surface", "--background-cell", "64"]
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        started = time.monotonic()
        child = subprocess.Popen(
            [str(executable), "process", str(source), "--out-dir", str(output), *options],
            stdout=stdout,
            stderr=stderr,
        )
        try:
            _, status, usage = process_api.wait4(child.pid, 0)
        except BaseException:
            child.kill()
            child.wait()
            raise
        elapsed = time.monotonic() - started
        child.returncode = process_api.waitstatus_to_exitcode(status)
        stdout.seek(0)
        stderr.seek(0)
        response: dict[str, Any] = json.load(stdout)
        errors = stderr.read().decode("utf-8")
    if child.returncode or errors:
        msg = f"Measured processing failed: {response}; {errors}"
        raise RuntimeError(msg)
    rss = usage.ru_maxrss if sys.platform == "darwin" else usage.ru_maxrss * 1024
    return {
        "elapsed_seconds": elapsed,
        "process_peak_rss_bytes": rss,
        "illumination_requested": illumination,
        "denoising": response["denoising"],
        "illumination_status": response["illumination"]["status"],
    }


def main() -> int:
    """Retain actual build identity and settings with every measured operation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--native-observer", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    build = json.loads(subprocess.check_output([str(executable), "version", "--json"]))
    native = json.loads(subprocess.check_output([str(args.native_observer.resolve())]))
    results: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for width, height in ((2000, 1500), (4000, 3000), (6000, 4000)):
            source = root / f"synthetic-{width}x{height}.png"
            source.write_bytes(fixture(width, height))
            for illumination in (False, True):
                measured = run_one(
                    executable, source, root / f"result-{len(results)}", illumination=illumination
                )
                results.append({"width": width, "height": height, **measured})
                print(
                    f"Measured {width}x{height}, I01={illumination}: "
                    f"{measured['elapsed_seconds']:.2f}s",
                    flush=True,
                )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps({"build": build, "native": native, "runs": results}, indent=2) + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
