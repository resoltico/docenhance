#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Explicit dependency acquisition. Normal configure/build runs verify, never fetch.

Git entries pin the *tag object*, not merely a mutable tag name. Archive entries
pin the bytes. Receipts hash every source file and are checked before each build.
No command updates the committed lock automatically.
"""

from __future__ import annotations

import argparse
import sys
import tarfile
from pathlib import Path

from dep_acquire import fetch
from dep_verify import (
    ROOT,
    CommandError,
    Dependency,
    DependencyError,
    digest_file,
    inventory,
    load_lock,
    receipt_for,
    run,
    safe_extract,
    verify,
)

# The public verification interface used by the other tools and the tooling tests.
__all__ = [
    "ROOT",
    "CommandError",
    "Dependency",
    "DependencyError",
    "digest_file",
    "fetch",
    "inventory",
    "load_lock",
    "receipt_for",
    "run",
    "safe_extract",
    "verify",
]


def main() -> int:
    """Fetch or verify the locked dependencies."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["fetch", "verify"])
    parser.add_argument("--cache", type=Path, default=ROOT / ".cache" / "deps")
    parser.add_argument("--lock", type=Path, default=ROOT / "deps" / "lock.json")
    parser.add_argument("--dependency", help="One exact dependency name; default is all")
    args = parser.parse_args()
    try:
        lock = load_lock(args.lock)
        selected = [
            d for d in lock["dependencies"] if not args.dependency or d["name"] == args.dependency
        ]
        if not selected:
            msg = f"Unknown dependency: {args.dependency}"
            raise DependencyError(msg)
        for dep in selected:
            if args.command == "fetch":
                fetch(dep, args.cache.resolve())
            else:
                verify(dep, args.cache.resolve())
    except (OSError, ValueError, RuntimeError, tarfile.TarError) as exc:
        print(f"Dependency acquisition/verification failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
