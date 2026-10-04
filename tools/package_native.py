#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Package CPack's owned installed stage as regular payload bytes without filesystem metadata."""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

from archive_payload import write_payload

EXECUTABLE_MODE = 0o755
REGULAR_MODE = 0o644


def package(stage: Path, target: Path) -> None:
    """Accept one confined CPack stage/archive pair and atomically finish its byte-only payload."""
    if (
        stage.is_symlink()
        or not stage.is_dir()
        or target.parent.resolve() != stage.parent.resolve()
        or target.name != stage.name + ".tar.gz"
    ):
        msg = "Native packaging requires its owned CPack stage and sibling archive"
        raise ValueError(msg)
    files = {}
    for path in stage.rglob("*"):
        if path.is_symlink() or (not path.is_file() and not path.is_dir()):
            msg = f"Package stage is not a regular file/directory: {path}"
            raise ValueError(msg)
        if path.is_file():
            name = path.relative_to(stage).as_posix()
            mode = (
                EXECUTABLE_MODE
                if name in {"bin/docenhance", "bin/docenhance.exe"}
                else REGULAR_MODE
            )
            files[name] = (path.read_bytes(), mode)
    if not files:
        msg = "A native package must contain installed payload files"
        raise ValueError(msg)
    with tempfile.NamedTemporaryFile(
        prefix=".payload-", dir=stage.parent, delete=False
    ) as temporary:
        prepared = Path(temporary.name)
    try:
        write_payload(prepared, files, stage.name, 0)
        prepared.replace(target)
    finally:
        prepared.unlink(missing_ok=True)


def main() -> int:
    """Run only against the actual staged install selected by CPack External."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    args = parser.parse_args()
    package(args.stage, args.archive)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
