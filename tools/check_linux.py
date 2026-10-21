#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Run the real Linux native gates in a pinned, cached Docker development environment."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

from cache_lock import exclusive_cache
from docker_execution import LABEL, remaining, rpc_timeout, run_container
from linux_gate_logs import (
    admit_diagnostics,
    publish_latest,
    retain_command,
    retain_preflight_failure,
)
from parallel import MAX_JOBS
from workflow_config import workflow

ROOT = Path(__file__).resolve().parents[1]
IMAGE_RECORD_FIELDS = 2
IMAGE_PATTERN = re.compile(r"[a-z0-9./_-]+@sha256:[0-9a-f]{64}")
IMAGE_FILES = (
    "tools/linux-gate.Dockerfile",
    "deps/tools.json",
    "tools/install_build_tools.py",
    "tools/install_llvm.py",
    "tools/llvm_cache.py",
    "tools/dep_acquire.py",
    "tools/dep_verify.py",
    "tools/cache_lock.py",
)


def docker_executable() -> str:
    """Require an actual Docker executable; an unavailable gate is a failure."""
    executable = shutil.which("docker")
    if executable is None:
        msg = "Docker is required for the local Linux gate; start/install Docker and retry"
        raise ValueError(msg)
    return executable


def image_identity(platform: str, root: Path = ROOT) -> tuple[str, str]:
    """Bind the development image to its exact base and repository-owned tool installation."""
    pins = json.loads((root / "deps/tools.json").read_text(encoding="utf-8"))
    base = pins["linux_container"]["image"]
    if not isinstance(base, str) or IMAGE_PATTERN.fullmatch(base) is None:
        msg = "Linux gate base image must be a repository-qualified SHA-256 digest"
        raise ValueError(msg)
    digest = hashlib.sha256(platform.encode())
    for name in IMAGE_FILES:
        digest.update(name.encode())
        digest.update((root / name).read_bytes())
    return base, f"docenhance-linux-gate:{digest.hexdigest()[:20]}"


def cache_identity(image: str, platform: str, root: Path = ROOT) -> str:
    """Isolate native build products by checkout and immutable tool/source configuration."""
    digest = hashlib.sha256((str(root.resolve()) + image + platform).encode())
    for name in ("deps/tools.json", "deps/lock.json", "deps/features.json"):
        digest.update((root / name).read_bytes())
    recipes = (
        path
        for path in (root / "cmake").rglob("*")
        if path.suffix == ".cmake" or path.name == "CMakeLists.txt"
    )
    for path in sorted(recipes):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(path.read_bytes())
    return f"docenhance-linux-{digest.hexdigest()[:20]}"


def source_cache_identity(root: Path = ROOT) -> str:
    """Verified source bytes need no compiler, architecture or private-build recipe copy."""
    digest = hashlib.sha256(str(root.resolve()).encode())
    for name in ("deps/lock.json", "tools/dep_acquire.py", "tools/dep_verify.py"):
        digest.update(name.encode())
        digest.update((root / name).read_bytes())
    return f"docenhance-sources-{digest.hexdigest()[:20]}"


def run_arguments(image: str, platform: str, name: str, token: str, root: Path = ROOT) -> list[str]:
    """Keep Linux outputs/caches separate from the host and process the current worktree."""
    cache = cache_identity(image, platform, root)
    command = [
        docker_executable(),
        "run",
        "--init",
        "--name",
        name,
        "--label",
        f"{LABEL}={token}",
        "--platform",
        platform,
        "--workdir",
        "/source",
    ]
    command += ["--mount", f"type=bind,source={root},target=/source,readonly"]
    for directory in ("out", ".cache", "dist"):
        suffix = directory.removeprefix(".")
        command += [
            "--mount",
            f"type=volume,source={cache}-{suffix},target=/source/{directory}",
        ]
    command += [
        "--mount",
        f"type=volume,source={source_cache_identity(root)},target=/source/.cache/deps",
    ]
    jobs = os.environ.get("DE_BUILD_JOBS", "2")
    if not re.fullmatch(r"[1-9][0-9]*", jobs) or not 1 <= int(jobs) <= MAX_JOBS:
        msg = f"DE_BUILD_JOBS must be a canonical integer in [1,{MAX_JOBS}]"
        raise ValueError(msg)
    command += host_cache_mounts(root)
    command += ["--network", "none", "--pull", "never"]
    for setting in (
        f"DE_BUILD_JOBS={jobs}",
        "PYTHONDONTWRITEBYTECODE=1",
        "RUFF_CACHE_DIR=/source/.cache/ruff",
        "MYPY_CACHE_DIR=/source/.cache/mypy",
        f"TMPDIR=/tmp/docenhance-linux-tmp/{token}",
    ):
        command += ["--env", setting]
    command += [image, "sh", "tools/linux_gate.sh"]
    return command


def host_cache_mounts(root: Path) -> list[str]:
    """Read only the prepared host cache and original locked archives; never acquire here."""
    cache = root / ".cache/deps"
    archives = cache / "archives"
    if cache.is_symlink() or archives.is_symlink() or not archives.is_dir():
        msg = "Linux gate requires the prepared host cache and original locked archives"
        raise ValueError(msg)
    return [
        "--mount",
        f"type=bind,source={cache},target=/host-deps,readonly",
        "--mount",
        f"type=bind,source={archives},target=/source/.cache/deps/archives,readonly",
    ]


def cached_image(image: str, platform: str, deadline: float) -> str:
    """Bind a prepared image's immutable ID without pulling or starting a daemon build."""
    result = subprocess.run(
        [
            docker_executable(),
            "image",
            "inspect",
            "--format",
            "{{.Id}} {{.Os}}/{{.Architecture}}",
            image,
        ],
        capture_output=True,
        text=True,
        check=False,
        timeout=rpc_timeout(deadline),
    )
    fields = result.stdout.split()
    if result.returncode or len(fields) != IMAGE_RECORD_FIELDS:
        msg = f"Required cached Linux image {image} is absent; prepare the locked image explicitly"
        raise ValueError(msg)
    if re.fullmatch(r"sha256:[0-9a-f]{64}", fields[0]) is None or fields[1] != platform:
        msg = "Cached Linux image identity or native platform differs"
        raise ValueError(msg)
    return fields[0]


def gate_seconds() -> int:
    """Use the existing reviewed native CI ceiling, not a second global timeout authority."""
    minutes = workflow(ROOT / ".github/workflows/ci.yml")["jobs"]["native"]["timeout-minutes"]
    if not isinstance(minutes, str) or re.fullmatch(r"[1-9][0-9]*", minutes) is None:
        msg = "Native CI must declare a positive integer timeout-minutes"
        raise ValueError(msg)
    return int(minutes) * 60


def native_platform(deadline: float) -> str:
    """Select the Linux daemon's native architecture explicitly, not an ambient emulation hint."""
    if os.environ.get("DOCKER_DEFAULT_PLATFORM"):
        msg = "DOCKER_DEFAULT_PLATFORM is unsupported for the native Linux gate"
        raise ValueError(msg)
    result = subprocess.run(
        [docker_executable(), "info", "--format", "{{.OSType}}/{{.Architecture}}"],
        capture_output=True,
        text=True,
        check=True,
        timeout=rpc_timeout(deadline),
    ).stdout.strip()
    names = {
        "linux/x86_64": "linux/amd64",
        "linux/amd64": "linux/amd64",
        "linux/aarch64": "linux/arm64",
        "linux/arm64": "linux/arm64",
    }
    if result not in names:
        msg = f"Unsupported Docker daemon platform: {result}"
        raise ValueError(msg)
    return names[result]


def main() -> int:
    """One writer per checkout, isolated state and retained diagnostics for every gate run."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    seconds = gate_seconds()
    deadline = time.monotonic() + seconds
    logs = ROOT / ".cache/linux-gate"
    logs.mkdir(parents=True, exist_ok=True)
    lock = logs / "active"
    log = logs / "latest.log"
    primary: BaseException | None = None
    try:
        with exclusive_cache(lock):
            admit_diagnostics(logs)
            directory = Path(tempfile.mkdtemp(prefix="run-", dir=logs))
            log = directory / "diagnostics.log"
            print(f"Linux Docker gate: full build/test diagnostics: {log}", flush=True)
            platform = native_platform(deadline)
            _base, image = image_identity(platform)
            image_id = cached_image(image, platform, deadline)
            token = uuid.uuid4().hex
            name = f"docenhance-gate-{token}"
            arguments = run_arguments(image_id, platform, name, token)
            metadata = (
                json.dumps(
                    {
                        "argv": arguments,
                        "container": name,
                        "label": token,
                        "image_tag": image,
                        "image_id": image_id,
                        "total_seconds": seconds,
                    },
                    indent=2,
                )
                + "\n"
            ).encode("utf-8")
            retain_command(directory / "command.json", metadata)
            if not run_container(arguments, log, name, token, deadline):
                primary = RuntimeError("Linux Docker gate returned a failed status")
                print(f"FAIL: Linux Docker gate; complete diagnostics: {log}", file=sys.stderr)
                return 1
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        primary = error
        retain_preflight_failure(log, error)
        print(f"FAIL: Linux Docker gate: {error}; diagnostics: {log}", file=sys.stderr)
        return 1
    finally:
        if log != logs / "latest.log":
            publish_latest(
                log, logs / "latest.log", primary if primary is not None else sys.exception()
            )
    remaining(deadline)
    print(f"PASS: Linux native release workflow and local checks; evidence: {log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
