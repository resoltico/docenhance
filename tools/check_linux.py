#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
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
from pathlib import Path

from cache_lock import exclusive_cache
from parallel import MAX_JOBS

ROOT = Path(__file__).resolve().parents[1]
IMAGE_PATTERN = re.compile(r"[a-z0-9./_-]+@sha256:[0-9a-f]{64}")
IMAGE_FILES = (
    "tools/linux-gate.Dockerfile",
    "deps/tools.json",
    "tools/install_build_tools.py",
    "tools/install_llvm.py",
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


def run_arguments(image: str, platform: str, root: Path = ROOT) -> list[str]:
    """Keep Linux outputs/caches separate from the host and process the current worktree."""
    cache = cache_identity(image, platform, root)
    command = [
        docker_executable(),
        "run",
        "--rm",
        "--init",
        "--platform",
        platform,
        "--workdir",
        "/source",
    ]
    command += ["--mount", f"type=bind,source={root},target=/source"]
    for directory in ("out", ".cache", "dist"):
        suffix = directory.removeprefix(".")
        command += [
            "--mount",
            f"type=volume,source={cache}-{suffix},target=/source/{directory}",
        ]
    host_cache = root / ".cache/deps"
    if host_cache.is_dir():
        command += ["--mount", f"type=bind,source={host_cache},target=/host-deps,readonly"]
    jobs = os.environ.get("DE_BUILD_JOBS", "2")
    if not re.fullmatch(r"[1-9][0-9]*", jobs) or not 1 <= int(jobs) <= MAX_JOBS:
        msg = f"DE_BUILD_JOBS must be a canonical integer in [1,{MAX_JOBS}]"
        raise ValueError(msg)
    command += ["--env", f"DE_BUILD_JOBS={jobs}", image, "sh", "tools/linux_gate.sh"]
    return command


def invoke(arguments: list[str], log: Path) -> bool:
    """Retain every diagnostic and fail on the actual subprocess status."""
    with log.open("a", encoding="utf-8") as stream:
        stream.write("\n" + " ".join(arguments) + "\n")
        stream.flush()
        result = subprocess.run(arguments, cwd=ROOT, stdout=stream, stderr=stream, check=False)
    return result.returncode == 0


def prepare_image(base: str, image: str, platform: str, log: Path) -> bool:
    """Use a content-addressed image cache; image preparation is explicit and separate."""
    found = subprocess.run(
        [docker_executable(), "image", "inspect", image],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if found.returncode == 0:
        return True
    with tempfile.TemporaryDirectory(prefix="docenhance-linux-image-") as directory:
        context = Path(directory)
        for name in IMAGE_FILES:
            target = context / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        return invoke(
            [
                docker_executable(),
                "build",
                "--platform",
                platform,
                "--build-arg",
                f"BASE_IMAGE={base}",
                "--tag",
                image,
                "--file",
                str(context / "tools/linux-gate.Dockerfile"),
                str(context),
            ],
            log,
        )


def native_platform() -> str:
    """Select the Linux daemon's native architecture explicitly, not an ambient emulation hint."""
    if os.environ.get("DOCKER_DEFAULT_PLATFORM"):
        msg = "DOCKER_DEFAULT_PLATFORM is unsupported for the native Linux gate"
        raise ValueError(msg)
    result = subprocess.run(
        [docker_executable(), "info", "--format", "{{.OSType}}/{{.Architecture}}"],
        capture_output=True,
        text=True,
        check=True,
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
    logs = ROOT / ".cache/linux-gate"
    logs.mkdir(parents=True, exist_ok=True)
    lock = logs / "active"
    log = logs / "latest.log"
    try:
        with exclusive_cache(lock):
            directory = Path(tempfile.mkdtemp(prefix="run-", dir=logs))
            log = directory / "diagnostics.log"
            log.write_text("", encoding="utf-8")
            print(f"Linux Docker gate: full build/test diagnostics: {log}", flush=True)
            platform = native_platform()
            base, image = image_identity(platform)
            if not prepare_image(base, image, platform, log) or not invoke(
                run_arguments(image, platform), log
            ):
                print(f"FAIL: Linux Docker gate; complete diagnostics: {log}", file=sys.stderr)
                return 1
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: Linux Docker gate: {error}; diagnostics: {log}", file=sys.stderr)
        return 1
    finally:
        if log != logs / "latest.log" and log.is_file():
            shutil.copy2(log, logs / "latest.log")
    print(f"PASS: Linux native release workflow and local checks; evidence: {log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
