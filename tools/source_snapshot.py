# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Read one committed first-party tree as immutable blobs, never ambient worktree contents."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
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
    # A regular input file avoids simultaneous blocking writes to Git's two batch pipes.
    with tempfile.TemporaryFile() as requests:
        requests.write(requested.encode())
        requests.seek(0)
        result = subprocess.run(
            [git, "-c", f"safe.directory={root.resolve()}", "cat-file", "--batch"],
            stdin=requests,
            capture_output=True,
            cwd=root,
            env=command_environment(),
            check=True,
            timeout=60,
        )
    data = result.stdout
    files = {}
    offset = 0
    for name, identity in records:
        end = data.index(b"\n", offset)
        found, kind, size = data[offset:end].decode("ascii").split()
        offset = end + 1
        count = int(size)
        if (
            found != identity
            or kind != "blob"
            or count < 0
            or len(data) - offset < count + 1
            or data[offset + count : offset + count + 1] != b"\n"
        ):
            msg = "Committed source blob response is incomplete or mismatched"
            raise ValueError(msg)
        files[name] = data[offset : offset + count]
        offset += count + 1
    if offset != len(data) or not files:
        msg = "Committed source snapshot is empty or contains unexpected data"
        raise ValueError(msg)
    return files
