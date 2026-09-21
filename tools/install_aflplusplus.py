#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Explicit online build of the pinned AFL++ release for scheduled fuzzing campaigns.

The release tag and the exact commit it must resolve to come from deps/tools.json; a tag that
has moved is refused. AFL++ is built against the pinned LLVM (install it first with
tools/install_llvm.py --fuzzing). Its directory is printed and, under GitHub Actions, prepended to
PATH, so afl-clang-fast++ and afl-fuzz are found by the fuzz-afl preset and tools/run_fuzzers.py.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from dep_verify import run

ROOT = Path(__file__).resolve().parents[1]


class InstallError(RuntimeError):
    """The pinned AFL++ could not be fetched, verified or built."""


def fetch(pin: dict[str, str], target: Path) -> None:
    """Fetch exactly the pinned tag into target and require the pinned commit."""
    shutil.rmtree(target, ignore_errors=True)
    run("git", "-c", "init.defaultBranch=main", "init", str(target))
    run("git", "remote", "add", "origin", pin["repository"], cwd=target)
    run(
        "git",
        "fetch",
        "--depth=1",
        "--no-recurse-submodules",
        "origin",
        f"{pin['ref']}:{pin['ref']}",
        cwd=target,
    )
    resolved = run("git", "rev-parse", f"{pin['ref']}^{{commit}}", cwd=target)
    if resolved != pin["object"]:
        msg = f"AFL++ {pin['ref']} resolves to {resolved}, not the pinned {pin['object']}"
        raise InstallError(msg)
    run("git", "checkout", "--detach", pin["object"], cwd=target)


def main() -> int:
    """Fetch, verify and build AFL++."""
    tools = json.loads((ROOT / "deps/tools.json").read_text(encoding="utf-8"))
    pin = tools["fuzzing"]["afl_plus_plus"]
    major = tools["fuzzing"]["llvm_major"]
    target = Path(os.environ.get("RUNNER_TEMP", ROOT / "out")) / "aflplusplus"
    make = shutil.which("make")
    llvm_config = shutil.which(f"llvm-config-{major}") or shutil.which("llvm-config")
    if make is None or llvm_config is None:
        msg = f"make and llvm-config {major} are required (tools/install_llvm.py --fuzzing)"
        raise InstallError(msg)
    fetch(pin, target)
    jobs = str(os.cpu_count() or 2)
    build = [make, "-j", jobs, "source-only", f"LLVM_CONFIG={llvm_config}", "NO_NYX=1"]
    subprocess.run(build, cwd=target, check=True)
    print(f"AFL++ {pin['version']} directory: {target}")
    if github_path := os.environ.get("GITHUB_PATH"):
        with Path(github_path).open("a", encoding="utf-8") as stream:
            stream.write(f"{target}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
