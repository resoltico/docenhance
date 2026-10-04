#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Create a deterministic first-party source archive; never include downloaded dependencies."""

from __future__ import annotations

import argparse
import hashlib
import os
import tempfile
from pathlib import Path

from archive_payload import write_payload
from project_version import version_from_text
from source_snapshot import snapshot

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = frozenset(
    {
        ".git",
        ".cache",
        ".venv",
        "out",
        "dist",
        "__pycache__",
        ".pytest_cache",
        ".ruff_cache",
        ".mypy_cache",
    }
)
EXCLUDED_NAMES = frozenset(
    {
        ".DS_Store",
        "CMakeUserPresets.json",
        "SOURCE_MANIFEST.sha256",
        "Thumbs.db",
        "compile_commands.json",
        "desktop.ini",
    }
)
MAX_EPOCH = 4_294_967_295
EXECUTABLE_MODE = 0o755
REGULAR_MODE = 0o644


class ArchiveError(ValueError):
    """The source tree or destination cannot produce a trustworthy archive."""


def source_files(root: Path) -> dict[str, bytes]:
    """Select committed source blobs, excluding only declared build and machine artifacts."""
    return {
        name: data
        for name, data in snapshot(root).items()
        if not any(part in EXCLUDED for part in Path(name).parts)
        and Path(name).name not in EXCLUDED_NAMES
        and not name.endswith(".pyc")
    }


def file_mode(data: bytes) -> int:
    """A file with a shebang is executable; everything else is a regular file."""
    return EXECUTABLE_MODE if data.startswith(b"#!") else REGULAR_MODE


def write_tar(target: Path, entries: dict[str, bytes], manifest: str, epoch: int) -> None:
    """Write a reproducible PAX tar.gz with normalized ownership, modes and times."""
    files = {name: (data, file_mode(data)) for name, data in entries.items()}
    files["SOURCE_MANIFEST.sha256"] = (manifest.encode(), REGULAR_MODE)
    write_payload(target, files, "docenhance", epoch)


def publish(temp_path: Path, target: Path) -> None:
    """Move the new archive into place; an existing archive must be byte-identical."""
    if not target.exists():
        temp_path.rename(target)
        return
    if (
        hashlib.sha256(target.read_bytes()).digest()
        != hashlib.sha256(temp_path.read_bytes()).digest()
    ):
        msg = f"Existing source archive differs: {target}; select a new directory"
        raise FileExistsError(msg)
    temp_path.unlink()


def make_archive(destination: Path, epoch: int = 0) -> Path:
    """Build the archive and its .sha256 file in destination and return the archive path."""
    entries = source_files(ROOT)
    version = version_from_text(entries["CMakeLists.txt"].decode("utf-8"))
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / f"docenhance-{version}-source.tar.gz"
    inside_repository = target.resolve().is_relative_to(ROOT.resolve())
    if inside_repository and not destination.resolve().is_relative_to((ROOT / "dist").resolve()):
        msg = "Inside the repository, source archives must go under dist/"
        raise ArchiveError(msg)
    manifest = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(entries.items())
    )
    with tempfile.NamedTemporaryFile(prefix=".docenhance-", dir=destination, delete=False) as temp:
        temp_path = Path(temp.name)
    try:
        write_tar(temp_path, entries, manifest, epoch)
        # Do not silently replace an existing artifact with different bytes.
        publish(temp_path, target)
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        checksum = target.with_suffix(target.suffix + ".sha256")
        checksum.write_text(f"{digest}  {target.name}\n", encoding="utf-8")
        return target
    finally:
        temp_path.unlink(missing_ok=True)


def main() -> int:
    """Create the archive in --out-dir using SOURCE_DATE_EPOCH for timestamps."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    if epoch < 0 or epoch > MAX_EPOCH:
        parser.error("SOURCE_DATE_EPOCH must fit an unsigned 32-bit timestamp")
    print(make_archive(args.out_dir, epoch))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
