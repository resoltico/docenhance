#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Check (or with --fix, apply) clang-format on every first-party C/C++ file.

Formatting output differs between clang-format releases, so the major version must match the
pin in deps/tools.json. Files are discovered exactly as the quality gates discover them.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from check_gates import code_files

ROOT = Path(__file__).resolve().parents[1]
CANDIDATES = ("clang-format-23", "clang-format")
HOMEBREW_HINTS = (Path("/opt/homebrew/opt/llvm/bin"), Path("/usr/local/opt/llvm/bin"))


def pinned_major() -> str:
    """Return the pinned clang-format major version."""
    tools = json.loads((ROOT / "deps/tools.json").read_text(encoding="utf-8"))
    return str(tools["clang_format"]["version"]).split(".", 1)[0]


def find_clang_format(major: str) -> tuple[str | None, str]:
    """Return the first clang-format whose major version matches, and what was found."""
    seen = []
    search = [shutil.which(name) for name in CANDIDATES]
    search += [str(hint / "clang-format") for hint in HOMEBREW_HINTS]
    for candidate in dict.fromkeys(c for c in search if c and Path(c).is_file()):
        result = subprocess.run(
            [candidate, "--version"], capture_output=True, text=True, check=False
        )
        match = re.search(r"version (\d+)\.", result.stdout)
        found = match.group(1) if match else "unknown"
        seen.append(f"{candidate} ({found})")
        if found == major:
            return candidate, ""
    return None, ", ".join(seen) or "none"


def main() -> int:
    """Run clang-format over the first-party sources."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true", help="Rewrite files in place")
    args = parser.parse_args()
    major = pinned_major()
    executable, seen = find_clang_format(major)
    if executable is None:
        print(f"clang-format {major} is required (found: {seen}); see docs/build.md")
        return 1
    files = [str(path) for path, _, kind in code_files(ROOT) if kind == "cxx"]
    mode = ["-i"] if args.fix else ["--dry-run", "--Werror"]
    result = subprocess.run([executable, *mode, "--style=file", *files], check=False)
    if result.returncode == 0:
        print(f"PASS: clang-format {major} over {len(files)} files")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
