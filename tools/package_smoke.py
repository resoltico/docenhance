#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Extract a native package into a fresh directory and test the shipped executable."""

from __future__ import annotations

import argparse
import json
import platform
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from deps import ROOT, safe_extract


def run_json(exe: Path, *args: str) -> dict[str, Any]:
    """Run the packaged executable and parse its JSON stdout."""
    result = subprocess.run(
        [str(exe), *args], capture_output=True, text=True, encoding="utf-8", check=True
    )
    data: dict[str, Any] = json.loads(result.stdout)
    return data


def macos_minimum_errors(exe: Path) -> list[str]:
    """On macOS, the executable must declare exactly the pinned minimum OS version."""
    if platform.system() != "Darwin":
        return []
    tools = json.loads((ROOT / "deps/tools.json").read_text(encoding="utf-8"))
    expected = tools["macos"]["deployment_target"]
    otool = shutil.which("otool")
    if otool is None:
        return ["otool is required to read the executable's minimum macOS version"]
    commands = subprocess.run(
        [otool, "-l", str(exe)], capture_output=True, text=True, check=True
    ).stdout
    found = re.findall(r"^\s*minos\s+(\S+)$", commands, flags=re.MULTILINE)
    if found != [expected]:
        return [f"Executable minimum macOS is {found}, expected [{expected!r}]"]
    return []


def smoke(root: Path) -> list[str]:
    """Return every failed expectation for an extracted package."""
    candidates = [*root.rglob("docenhance.exe"), *root.rglob("bin/docenhance")]
    if len(candidates) != 1:
        return ["Package must contain exactly one runtime executable"]
    failures = macos_minimum_errors(candidates[0])
    version = run_json(candidates[0], "version", "--json")
    if version["development_stage"] != "foundation" or version["exit_code"] != 0:
        failures.append(f"Unexpected version report: {version}")
    if not list(root.rglob("sbom.spdx.json")) or not list(root.rglob("THIRD_PARTY_NOTICES.md")):
        failures.append("Package lacks its SBOM or third-party notices")
    if run_json(candidates[0], "methods", "--json")["methods"] != []:
        failures.append("Package advertises unimplemented methods")
    return failures


def main() -> int:
    """Extract the archive into a temporary directory and smoke-test it."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="docenhance-package-") as temp:
        root = Path(temp)
        safe_extract(args.archive, root)
        failures = smoke(root)
    print("\n".join(failures) if failures else "PASS: relocated native package smoke test")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
