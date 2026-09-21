#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Run fuzz targets with strict engine and sanitizer settings; any finding fails the run.

libFuzzer (the default) or AFL++ fuzzes a target for a time budget, starting from a scratch copy
of its committed seed corpus plus its recorded regressions, with its dictionary. Crashes, sanitizer
reports, violated harness properties, timeouts and out-of-memory conditions are findings: each
reproducer is kept under the work directory and the command exits non-zero. With --merge, only
inputs adding new coverage are copied back into the committed seed corpus.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
FUZZ = ROOT / "fuzz"
PER_INPUT_TIMEOUT_SECONDS = 5
MEMORY_LIMIT_MB = 2048
LIBFUZZER_FLAGS = (
    f"-timeout={PER_INPUT_TIMEOUT_SECONDS}",
    f"-rss_limit_mb={MEMORY_LIMIT_MB}",
    f"-malloc_limit_mb={MEMORY_LIMIT_MB}",
    "-use_value_profile=1",
    "-reduce_inputs=1",
    "-print_final_stats=1",
    "-error_exitcode=77",
    "-timeout_exitcode=78",
)


class FuzzError(RuntimeError):
    """The fuzz run could not be set up."""


def settings() -> dict[str, Any]:
    """Return fuzz/targets.json."""
    data: dict[str, Any] = json.loads((FUZZ / "targets.json").read_text(encoding="utf-8"))
    return data


def sanitizer_environment() -> dict[str, str]:
    """The strict sanitizer runtime options, on top of the current environment."""
    return {**os.environ, **settings()["sanitizer_options"]}


def afl_environment() -> dict[str, str]:
    """AFL++ refuses to start unless the sanitizer options disable symbolization."""
    environment = {**sanitizer_environment(), "AFL_NO_UI": "1", "AFL_SKIP_CPUFREQ": "1"}
    environment["ASAN_OPTIONS"] += ":symbolize=0"
    return environment


def seed_corpus(target: str, destination: Path) -> Path:
    """Copy the committed seeds and regressions of target into a fresh directory."""
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True)
    for source in (FUZZ / "corpus" / target, FUZZ / "regressions" / target):
        for path in sorted(source.iterdir()):
            if path.is_file():
                shutil.copyfile(path, destination / f"{source.name}-{path.name}")
    return destination


def dictionary(target: str) -> list[Path]:
    """The target's dictionary, when it has one."""
    path = FUZZ / "dict" / f"{target}.dict"
    return [path] if path.is_file() else []


def run_libfuzzer(binary: Path, target: str, seconds: int, work: Path) -> list[Path]:
    """Fuzz with libFuzzer and return the reproducers it wrote."""
    corpus = seed_corpus(target, work / "corpus")
    artifacts = work / "artifacts"
    shutil.rmtree(artifacts, ignore_errors=True)
    artifacts.mkdir(parents=True)
    target_settings = settings()["targets"][target]
    max_len = target_settings["max_len"]
    command = [
        str(binary),
        f"-max_total_time={seconds}",
        f"-max_len={max_len}",
        f"-artifact_prefix={artifacts}/",
        *[f"-dict={path}" for path in dictionary(target)],
        *LIBFUZZER_FLAGS,
        *target_settings.get("libfuzzer_options", []),
        str(corpus),
    ]
    result = subprocess.run(command, env=sanitizer_environment(), check=False)
    findings = sorted(artifacts.iterdir())
    if result.returncode != 0 and not findings:
        msg = f"libFuzzer exited with {result.returncode} without writing a reproducer"
        raise FuzzError(msg)
    return findings


def run_afl(binary: Path, target: str, seconds: int, work: Path) -> list[Path]:
    """Fuzz with AFL++ and return its crashes and hangs."""
    afl_fuzz = shutil.which("afl-fuzz")
    if afl_fuzz is None:
        msg = "afl-fuzz not found; install the pinned AFL++ (docs/build.md)"
        raise FuzzError(msg)
    seeds = seed_corpus(target, work / "seeds")
    output = work / "afl"
    shutil.rmtree(output, ignore_errors=True)
    command = [
        afl_fuzz,
        "-i",
        str(seeds),
        "-o",
        str(output),
        "-V",
        str(seconds),
        "-t",
        str(PER_INPUT_TIMEOUT_SECONDS * 1000),
        "-m",
        "none",
        *[arg for path in dictionary(target) for arg in ("-x", str(path))],
        "--",
        str(binary),
    ]
    subprocess.run(command, env=afl_environment(), check=True)
    return sorted(
        path
        for kind in ("crashes", "hangs")
        for path in (output / "default" / kind).glob("id*")
        if path.is_file()
    )


def merge(binary: Path, target: str, work: Path) -> None:
    """Add inputs with new coverage from the last run's corpus to the committed seeds."""
    committed = FUZZ / "corpus" / target
    command = [str(binary), "-merge=1", str(committed), str(work / "corpus")]
    subprocess.run(command, env=sanitizer_environment(), check=True)


def main() -> int:
    """Fuzz one target and report findings."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=sorted(settings()["targets"]))
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True, help="Scratch directory")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--engine", choices=["libfuzzer", "afl"], default="libfuzzer")
    parser.add_argument("--merge", action="store_true", help="Merge new coverage into fuzz/corpus")
    args = parser.parse_args()
    work = args.work.resolve() / args.target
    try:
        run = run_afl if args.engine == "afl" else run_libfuzzer
        findings = run(args.binary.resolve(), args.target, args.seconds, work)
        if args.merge and args.engine == "libfuzzer" and not findings:
            merge(args.binary.resolve(), args.target, work)
    except (OSError, subprocess.CalledProcessError, FuzzError) as exc:
        print(f"Fuzzing {args.target} failed: {exc}", file=sys.stderr)
        return 1
    if findings:
        print(f"FINDINGS in {args.target}: fix the defect, then add each reproducer to", end=" ")
        print(f"fuzz/regressions/{args.target}/:")
        print("\n".join(f"  {path}" for path in findings))
        return 1
    print(f"PASS: {args.target} fuzzed for {args.seconds}s ({args.engine}) without findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
