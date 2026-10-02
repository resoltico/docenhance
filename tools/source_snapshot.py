# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Read one committed first-party tree as immutable blobs, never ambient worktree contents."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path, PurePosixPath

from dep_verify import command_environment, run


def snapshot(root: Path) -> dict[str, bytes]:
    """Require an exact repository root and snapshot every regular blob of its resolved HEAD."""
    if Path(run("git", "rev-parse", "--show-toplevel", cwd=root)).resolve() != root.resolve():
        msg = "Source archive root must be the Git repository root"
        raise ValueError(msg)
    commit = run("git", "rev-parse", "--verify", "HEAD^{commit}", cwd=root)
    tree = run("git", "ls-tree", "-r", "-z", "--full-tree", commit, cwd=root)
    records = []
    for record in tree.split("\0"):
        if not record:
            continue
        facts, name = record.split("\t", 1)
        mode, kind, identity = facts.split()
        path = PurePosixPath(name)
        if (
            mode not in {"100644", "100755"}
            or kind != "blob"
            or path.is_absolute()
            or any(part in {"", ".", ".."} for part in path.parts)
        ):
            msg = f"Source archive requires confined regular committed files: {name}"
            raise ValueError(msg)
        records.append((name, identity))
    requested = "".join(identity + "\n" for _, identity in records)
    git = shutil.which("git")
    if git is None:
        msg = "Git is required for committed source packaging"
        raise ValueError(msg)
    result = subprocess.run(
        [git, "-c", f"safe.directory={root.resolve()}", "cat-file", "--batch"],
        input=requested.encode(),
        capture_output=True,
        cwd=root,
        env=command_environment(),
        check=True,
        timeout=60,
    )
    data = result.stdout
    files = {}
    for name, identity in records:
        header, data = data.split(b"\n", 1)
        found, kind, size = header.decode("ascii").split()
        count = int(size)
        if (
            found != identity
            or kind != "blob"
            or count < 0
            or len(data) < count + 1
            or data[count : count + 1] != b"\n"
        ):
            msg = "Committed source blob response is incomplete or mismatched"
            raise ValueError(msg)
        files[name] = data[:count]
        data = data[count + 1 :]
    if data or not files:
        msg = "Committed source snapshot is empty or contains unexpected data"
        raise ValueError(msg)
    return files
