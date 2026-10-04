#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Measure real JPEG decoder allocations and fresh-process peak RSS separately."""

from __future__ import annotations

import argparse
import importlib
import importlib.util
import json
import platform
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from types import ModuleType

RESOURCE: ModuleType | None = None
try:
    RESOURCE = importlib.import_module("resource")
except ImportError:
    RESOURCE = None

ROOT = Path(__file__).resolve().parents[1]
CASES = ((2000, 1500), (4000, 3000), (6000, 4000))


def encoded_fixture(width: int, height: int, *, progressive: bool) -> bytes:
    """Use the independent coefficient encoder, outside the measured process."""
    spec = importlib.util.spec_from_file_location(
        "jpeg_resource_fixture", ROOT / "tests/fixtures/jpeg/generate.py"
    )
    if spec is None or spec.loader is None:
        msg = "JPEG fixture generator is unavailable"
        raise ValueError(msg)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    data: bytes = module.JpegFixture(
        width, height, color="ycbcr", sampling=(2, 2), progressive=progressive
    ).encoded()
    return data


def worker(command: list[str]) -> int:
    """One measured child per fresh worker makes ru_maxrss a per-case observation."""
    if RESOURCE is None:
        msg = "POSIX resource observations are unavailable"
        raise ValueError(msg)

    started = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    usage = RESOURCE.getrusage(RESOURCE.RUSAGE_CHILDREN)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return result.returncode
    rss = usage.ru_maxrss
    if platform.system() != "Darwin":
        rss *= 1024
    print(
        json.dumps(
            {
                "wall_seconds": time.monotonic() - started,
                "peak_rss_bytes": rss,
                "observations": json.loads(result.stdout),
            }
        )
    )
    return 0


def measure(command: list[str]) -> dict[str, Any]:
    """A separate worker observes the real program without cumulative child RSS."""
    result = subprocess.run(
        [sys.executable, str(Path(__file__).resolve()), "--worker", *command],
        capture_output=True,
        text=True,
        check=True,
    )
    data: dict[str, Any] = json.loads(result.stdout)
    return data


def main() -> int:
    """Generate explicit 3/12/24 MP cases and retain actual observations, never estimates."""
    if len(sys.argv) > 1 and sys.argv[1] == "--worker":
        return worker(sys.argv[2:])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()
    if platform.system() not in {"Darwin", "Linux"}:
        parser.error("Peak-RSS collection is implemented only on macOS/Linux")
    results = []
    with tempfile.TemporaryDirectory(prefix="docenhance-jpeg-measure-") as directory:
        root = Path(directory)
        for width, height in CASES:
            for progressive in (False, True):
                name = f"{width}x{height}-{'progressive' if progressive else 'baseline'}"
                source = root / (name + ".jpg")
                source.write_bytes(encoded_fixture(width, height, progressive=progressive))
                decoded = measure([str(arguments.probe.resolve()), str(source)])
                pipeline = measure(
                    [
                        str(arguments.executable.resolve()),
                        "process",
                        str(source),
                        "--out-dir",
                        str(root / name),
                        "--illumination",
                        "surface",
                        "--background-target",
                        "0.8",
                        "--json",
                    ]
                )
                results.append(
                    {
                        "case": name,
                        "width": width,
                        "height": height,
                        "encoded_bytes": source.stat().st_size,
                        "decoder": decoded,
                        "pipeline_with_i01": pipeline,
                    }
                )
                print(f"Measured {name}", flush=True)
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    arguments.out.write_text(
        json.dumps(
            {"platform": platform.platform(), "machine": platform.machine(), "cases": results},
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
