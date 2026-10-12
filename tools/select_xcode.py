#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Select the declared installed Xcode for actual minimum-macOS CI execution."""

from __future__ import annotations

import json
import os
import platform
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def developer_directory(applications: Path, version: str) -> Path:
    """Refuse missing bundles rather than selecting a moving/default toolchain."""
    selected = applications / f"Xcode_{version}.app" / "Contents/Developer"
    if not (selected / "usr/bin/xcodebuild").is_file():
        message = f"Required Xcode {version} is missing: {selected}"
        raise ValueError(message)
    return selected


def confirm_version(observed: str, expected: str) -> None:
    """An existing bundle name alone cannot prove the selected tool version."""
    if not observed.startswith(f"Xcode {expected}\nBuild version "):
        message = "Selected Xcode version differs from deps/tools.json"
        raise ValueError(message)


def main() -> int:
    """Export only the verified developer directory for subsequent CI/build steps."""
    if platform.system() != "Darwin":
        message = "Xcode selection requires macOS"
        raise ValueError(message)
    version = json.loads((ROOT / "deps/tools.json").read_bytes())["macos"]["minimum_runner_xcode"]
    selected = developer_directory(Path("/Applications"), version)
    environment = dict(os.environ, DEVELOPER_DIR=str(selected))
    observed = subprocess.check_output(
        [str(selected / "usr/bin/xcodebuild"), "-version"], env=environment, text=True
    )
    confirm_version(observed, version)
    print(observed.strip())
    print(f"DEVELOPER_DIR={selected}")
    if destination := os.environ.get("GITHUB_ENV"):
        with Path(destination).open("a", encoding="utf-8") as stream:
            stream.write(f"DEVELOPER_DIR={selected}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
