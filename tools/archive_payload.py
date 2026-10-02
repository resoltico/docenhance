# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Write archive payload bytes and explicit portable metadata, never host extended attributes."""

from __future__ import annotations

import gzip
import io
import tarfile
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def write_payload(
    target: Path, files: dict[str, tuple[bytes, int]], prefix: str, epoch: int
) -> None:
    """Serialize a sorted closed file table with explicit modes, ownership and timestamps."""
    with (
        target.open("wb") as raw,
        gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=epoch, compresslevel=9) as zipped,
        tarfile.open(fileobj=zipped, mode="w", format=tarfile.PAX_FORMAT) as archive,
    ):
        for name, (data, mode) in sorted(files.items()):
            info = tarfile.TarInfo(prefix + "/" + name)
            info.size = len(data)
            info.mode = mode
            info.mtime = epoch
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            archive.addfile(info, io.BytesIO(data))
