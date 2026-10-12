#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Require actual native OS/CPU execution on the reviewed CI runner, before building."""

from __future__ import annotations

import argparse
import json
import platform
from pathlib import Path

from ci_contract import NATIVE_RUNNERS


def runtime_errors(name: str, system: str, machine: str, version: str) -> list[str]:
    """Reject newer macOS as substitute evidence for a minimum-runtime lane."""
    if name not in NATIVE_RUNNERS:
        return ["Unknown native execution lane"]
    expected_system = {"linux": "Linux", "macos": "Darwin", "windows": "Windows"}[
        name.split("-", maxsplit=1)[0]
    ]
    architectures = {"arm64", "aarch64"} if name.endswith("arm64") else {"x86_64", "amd64"}
    errors = []
    if system != expected_system or machine.lower() not in architectures:
        errors.append(f"Wrong native OS/CPU: {system}/{machine} for {name}")
    if system == "Darwin":
        runner = NATIVE_RUNNERS[name]
        expected_major = runner.split("-")[1]
        floor = json.loads((Path(__file__).resolve().parents[1] / "deps/tools.json").read_bytes())[
            "macos"
        ]["deployment_target"]
        if version.split(".", maxsplit=1)[0] != expected_major:
            errors.append(f"Actual macOS {version} differs from lane {runner}")
        if int(expected_major) < int(floor.split(".")[0]):
            errors.append("Runner predates the declared deployment floor")
    return errors


def main() -> int:
    """Print observed host identity; package smoke separately executes relocated bytes."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=tuple(NATIVE_RUNNERS))
    args = parser.parse_args()
    system, machine = platform.system(), platform.machine()
    version = platform.mac_ver()[0] if system == "Darwin" else platform.release()
    errors = runtime_errors(args.platform, system, machine, version)
    print(
        json.dumps(
            {
                "lane": args.platform,
                "system": system,
                "machine": machine,
                "version": version,
                "errors": errors,
            }
        )
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
