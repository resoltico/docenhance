#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Check the architecture rules of spec/architecture.json.

C++ has no established equivalent of ArchUnit, so the rules are built from the clang tooling this
project already pins. Without `--build` this checks what the sources and the documentation say;
with one it also checks the real include graph, the real abstract syntax tree, the links the build
declares and whether every public header stands on its own. The `architecture` test inside a build
passes `--build`, and tools/check_all.py runs the source rules on their own.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from architecture import ArchitectureError, load_manifest, source_violations
from architecture_build import build_violations


def main() -> int:
    """Check the architecture rules against the sources and, when given one, against a build."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=None)
    args = parser.parse_args()
    try:
        errors = source_violations()
        if args.build is not None:
            errors += build_violations(load_manifest(), args.build.resolve())
    except (OSError, ArchitectureError) as exc:
        print(f"Architecture rules could not be checked: {exc}", file=sys.stderr)
        return 1
    if errors:
        print("\n".join(errors))
        return 1
    scope = "sources" if args.build is None else "sources and the real build"
    print(f"PASS: layer, package, call and link rules over the {scope}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
