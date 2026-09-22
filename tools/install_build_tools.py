#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Explicit online install of pinned developer tools into the active Python environment.

Installs the CMake/Ninja wheels, or with --lint only Ruff, mypy, clang-format and pre-commit, so
that an existing build tree keeps the CMake it was configured with. Every version comes from
deps/tools.json. Use a virtual environment locally. This script is never invoked by
application runtime, configure, build, or test. It is an opt-in developer convenience.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST_TOOLS = ("jsonschema", "types_jsonschema")
BUILD_TOOLS = ("cmake", "ninja", *TEST_TOOLS)
LINT_TOOLS = ("ruff", "mypy", "clang_format", "pre_commit", *TEST_TOOLS)


def requirements(names: tuple[str, ...]) -> list[str]:
    """Return exact pip requirements for the named deps/tools.json entries."""
    tools = json.loads((ROOT / "deps/tools.json").read_text(encoding="utf-8"))
    return [f"{tools[name].get('package', name)}=={tools[name]['version']}" for name in names]


def main() -> int:
    """Install the pinned wheels with pip."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lint", action="store_true", help="Install the pinned linters instead")
    args = parser.parse_args()
    names = LINT_TOOLS if args.lint else BUILD_TOOLS
    command = [sys.executable, "-m", "pip", "install", "--disable-pip-version-check"]
    subprocess.run([*command, "--only-binary=:all:", *requirements(names)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
