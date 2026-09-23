# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Owned fuzz-run workspaces, byte-preserving corpus staging and bounded engine execution."""

from __future__ import annotations

import contextlib
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from fuzz_manifest import ENGINE_GRACE, MAX_SECONDS, ROOT, FuzzError, Target, positive, settings

PER_INPUT_SECONDS = 5
MEMORY_LIMIT_MIB = 2048
EXECUTIONS = re.compile(r"^stat::number_of_executed_units:\s*(\d+)\s*$", re.MULTILINE)


@dataclass(frozen=True)
class Run:
    """One explicitly selected engine, binary and input contract."""

    target: Target
    binary: Path
    engine: str
    seconds: int
    root: Path = ROOT


def write_json(path: Path, value: object) -> None:
    """Write a complete report inside the exclusively owned run directory."""
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def new_directory(root: Path, prefix: str) -> Path:
    """Claim a fresh directory; never adopt or recursively delete old run evidence."""
    root.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix, dir=root))


def stage_corpus(run: Run, directory: Path) -> Path:
    """Content-address bytes while retaining every seed/regression origin in an index."""
    corpus = directory / "corpus"
    corpus.mkdir()
    origins = []
    for kind in ("corpus", "regressions"):
        source = run.root / "fuzz" / kind / run.target.name
        if source.is_symlink() or not source.is_dir():
            msg = f"Missing regular corpus directory: {source}"
            raise FuzzError(msg)
        for path in sorted(source.iterdir()):
            if path.is_symlink() or not path.is_file():
                msg = f"Corpus entry is not a regular file: {path}"
                raise FuzzError(msg)
            if path.stat().st_size > run.target.max_len:
                msg = f"Corpus entry exceeds {run.target.max_len} bytes: {path}"
                raise FuzzError(msg)
            data = path.read_bytes()
            if len(data) > run.target.max_len:
                msg = f"Corpus entry grew beyond its bound: {path}"
                raise FuzzError(msg)
            digest = hashlib.sha256(data).hexdigest()
            (corpus / digest).write_bytes(data)
            origins.append(
                {
                    "origin": path.relative_to(run.root).as_posix(),
                    "sha256": digest,
                    "bytes": len(data),
                }
            )
    if not origins:
        msg = "A fuzz run needs at least one seed"
        raise FuzzError(msg)
    write_json(directory / "corpus-index.json", origins)
    return corpus


def engine_command(run: Run, directory: Path, corpus: Path) -> list[str]:
    """Build a closed command line; user-controlled extra engine flags are not supported."""
    dictionary = run.root / "fuzz/dict" / f"{run.target.name}.dict"
    if run.engine == "libfuzzer":
        artifacts = directory / "artifacts"
        artifacts.mkdir()
        return [
            str(run.binary),
            f"-max_total_time={run.seconds}",
            f"-max_len={run.target.max_len}",
            f"-artifact_prefix={artifacts}/",
            f"-timeout={PER_INPUT_SECONDS}",
            f"-rss_limit_mb={MEMORY_LIMIT_MIB}",
            f"-malloc_limit_mb={MEMORY_LIMIT_MIB}",
            "-use_value_profile=1",
            "-reduce_inputs=1",
            "-print_final_stats=1",
            "-error_exitcode=77",
            "-timeout_exitcode=78",
            *(["-len_control=0"] if run.target.full_length else []),
            *([f"-dict={dictionary}"] if dictionary.is_file() else []),
            str(corpus),
        ]
    afl = shutil.which("afl-fuzz")
    if run.engine != "afl" or afl is None:
        msg = "The requested fuzz engine is unavailable"
        raise FuzzError(msg)
    return [
        afl,
        "-i",
        str(corpus),
        "-o",
        str(directory / "afl"),
        "-V",
        str(run.seconds),
        "-t",
        str(PER_INPUT_SECONDS * 1000),
        "-m",
        "none",
        "-G",
        str(run.target.max_len),
        *(["-x", str(dictionary)] if dictionary.is_file() else []),
        "--",
        str(run.binary),
    ]


def environment(run: Run) -> dict[str, str]:
    """Enforce sanitizer failure behavior without exporting the inherited environment."""
    result = {**os.environ, **settings(run.root)["sanitizer_options"]}
    if run.engine == "afl":
        result.update(AFL_NO_UI="1", AFL_SKIP_CPUFREQ="1")
        result["ASAN_OPTIONS"] += ":symbolize=0"
    return result


def stop_process(process: subprocess.Popen[bytes], *, graceful: bool) -> None:
    """Terminate only this run's process group, then reap its direct child."""
    if graceful and os.name == "posix":
        with contextlib.suppress(ProcessLookupError):
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
        else:
            return
    if os.name == "posix":
        with contextlib.suppress(ProcessLookupError):
            os.killpg(process.pid, signal.SIGKILL)
    else:
        process.kill()
    process.wait()


def bounded_process(
    command: list[str], env: dict[str, str], log: Path, timeout: int, *, graceful: bool = False
) -> int:
    """Run without a shell and preserve logs, including timeout/interrupt diagnostics."""
    with (
        log.open("wb") as stream,
        subprocess.Popen(
            command,
            stdout=stream,
            stderr=subprocess.STDOUT,
            env=env,
            start_new_session=os.name == "posix",
        ) as process,
    ):
        try:
            return process.wait(timeout=timeout)
        finally:
            if process.poll() is None:
                stop_process(process, graceful=graceful)


def engine_evidence(run: Run, directory: Path) -> tuple[int, list[str]]:
    """Require positive native execution statistics and enumerate all recorded findings."""
    if run.engine == "libfuzzer":
        text = (directory / "engine.log").read_text(encoding="utf-8", errors="replace")
        counts = EXECUTIONS.findall(text)
        executions = int(counts[-1]) if counts else 0
        findings = [path for path in (directory / "artifacts").iterdir() if path.is_file()]
    else:
        output = directory / "afl/default"
        values = dict(
            line.split(":", 1)
            for line in (output / "fuzzer_stats").read_text().splitlines()
            if ":" in line
        )
        values = {key.strip(): value.strip() for key, value in values.items()}
        executions = int(values.get("execs_done", "0"))
        findings = [
            path
            for kind in ("crashes", "hangs")
            for path in (output / kind).glob("id*")
            if path.is_file()
        ]
        if int(values.get("saved_crashes", "0")) or int(values.get("saved_hangs", "0")):
            findings.append(output / "fuzzer_stats")
    return executions, sorted({path.relative_to(directory).as_posix() for path in findings})


def completed(
    exit_code: int, executions: int, findings: list[str], elapsed: float, seconds: int
) -> bool:
    """An early successful exit or seed-only run is not a completed time-budgeted campaign."""
    return exit_code == 0 and executions > 0 and not findings and elapsed >= seconds


def execute(run: Run, work: Path) -> Path:
    """Always retain a result record once a workspace is claimed; only complete runs pass."""
    positive(run.seconds, MAX_SECONDS, "seconds")
    directory = new_directory(work, f"{run.target.name}-")
    started = time.monotonic()
    report: dict[str, Any] = {
        "target": run.target.name,
        "engine": run.engine,
        "seconds": run.seconds,
        "passed": False,
        "exit_code": None,
        "executions": 0,
        "findings": [],
        "manifest_sha256": hashlib.sha256(
            (run.root / "fuzz/targets.json").read_bytes()
        ).hexdigest(),
    }
    write_json(directory / "result.json", report)
    try:
        report["binary_sha256"] = hashlib.sha256(run.binary.read_bytes()).hexdigest()
        corpus = stage_corpus(run, directory)
        command = engine_command(run, directory, corpus)
        report["command"] = command
        write_json(directory / "result.json", report)
        engine_started = time.monotonic()
        report["exit_code"] = bounded_process(
            command, environment(run), directory / "engine.log", run.seconds + ENGINE_GRACE
        )
        report["engine_elapsed_seconds"] = time.monotonic() - engine_started
        executions, findings = engine_evidence(run, directory)
        report.update(executions=executions, findings=findings)
        report["passed"] = completed(
            report["exit_code"], executions, findings, report["engine_elapsed_seconds"], run.seconds
        )
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        report["error"] = str(exc)
    finally:
        report["elapsed_seconds"] = time.monotonic() - started
        write_json(directory / "result.json", report)
    return directory
