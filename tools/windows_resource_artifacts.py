# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Finite identities and lossless maps for the two actual Windows resource executables."""

from __future__ import annotations

import gzip
import hashlib
import io
import json
import os
import re
from typing import TYPE_CHECKING, Any, BinaryIO, override

if TYPE_CHECKING:
    from collections.abc import Buffer
    from pathlib import Path

from native_journal import identify_journal, journal_identity

MAP_BYTES = 16 * 1024 * 1024
GZIP_BYTES = 2 * 1024 * 1024
TARGETS = ("de_fft_resources", "de_restoration_resources")


def identity(path: Path) -> dict[str, Any]:
    """Hash one regular immutable file, never retain proprietary object/archive bytes."""
    before = identify_journal(path)
    if before is None:
        message = f"Missing identity input: {path}"
        raise ValueError(message)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        opened = os.fstat(stream.fileno())
        if journal_identity(opened) != before:
            message = f"Identity changed while opening: {path}"
            raise ValueError(message)
        while chunk := stream.read(65536):
            digest.update(chunk)
        after = os.fstat(stream.fileno())
        if journal_identity(after) != before or after.st_ctime_ns != opened.st_ctime_ns:
            message = f"Identity changed while hashing: {path}"
            raise ValueError(message)
    if identify_journal(path) != before:
        message = f"Identity pathname changed: {path}"
        raise ValueError(message)
    return {"path": str(path), "bytes": before.size, "sha256": digest.hexdigest()}


class MapWriter(io.RawIOBase):
    """Bound every compressed write, including headers and trailer."""

    def __init__(self, stream: BinaryIO) -> None:
        """Own the map artifact writer bound without altering source bytes."""
        super().__init__()
        self.stream, self.count = stream, 0

    @override
    def write(self, data: Buffer) -> int:
        view = memoryview(data)
        if view.nbytes > GZIP_BYTES - self.count:
            message = "Windows map compressed writer bound exceeded"
            raise ValueError(message)
        count = self.stream.write(view)
        if count != view.nbytes:
            message = "Windows map short artifact write"
            raise OSError(message)
        self.count += count
        return count


def archive_map(path: Path, directory: Path) -> tuple[bytes, dict[str, Any]]:
    """Preserve a bounded stable actual map before inspecting its provider lines."""
    epoch = identify_journal(path)
    if epoch is None or not 0 < epoch.size <= MAP_BYTES:
        message = "Windows map raw admission bound exceeded"
        raise ValueError(message)
    before = identity(path)
    with path.open("rb") as original:
        opened = os.fstat(original.fileno())
        if journal_identity(opened) != epoch:
            message = "Windows map opened another source epoch"
            raise ValueError(message)
        data = original.read(MAP_BYTES + 1)
        after = os.fstat(original.fileno())
        if journal_identity(after) != epoch or after.st_ctime_ns != opened.st_ctime_ns:
            message = "Windows map source epoch changed during read"
            raise ValueError(message)
    if len(data) != before["bytes"] or identity(path) != before:
        message = "Windows map changed during admission"
        raise ValueError(message)
    destination = directory / (path.name + ".gz")
    partial = destination.with_suffix(destination.suffix + ".partial")
    with (
        partial.open("xb") as compressed,
        gzip.GzipFile(
            filename="", mode="wb", fileobj=MapWriter(compressed), mtime=0, compresslevel=1
        ) as zipped,
    ):
        zipped.write(data)
    with gzip.open(partial, "rb") as roundtrip:
        if roundtrip.read(MAP_BYTES + 1) != data:
            message = "Windows map roundtrip mismatch"
            raise ValueError(message)
    with destination.open("xb") as final:
        final.write(partial.read_bytes())
    partial.unlink()
    return data, {"source": before, "gzip": identity(destination)}


def map_routes(text: str, *, debug: bool) -> dict[str, str]:
    """Require actual public-symbol rows to name the direct hook and static original provider."""
    provider = "libucrtd" if debug else "libucrt"
    symbols = {
        "malloc": "allocation_interpose",
        "free": "allocation_interpose",
        "_malloc_dbg" if debug else "_malloc_base": provider,
        "_free_dbg" if debug else "_free_base": provider,
    }
    routes = {}
    for symbol, owner in symbols.items():
        rows = [
            line
            for line in text.splitlines()
            if re.match(r"\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+" + re.escape(symbol) + r"\s", line)
            and (
                re.search(r"\b" + owner + r"(?:\.lib)?:", line, re.IGNORECASE)
                if owner.startswith("libucrt")
                else owner.casefold() in line.casefold()
            )
        ]
        if len(rows) != 1:
            message = f"Actual map must identify exactly one {symbol}/{owner} row"
            raise ValueError(message)
        routes[symbol] = rows[0]
    return routes


def write_json(path: Path, value: object) -> None:
    """Publish one small exclusive metadata result."""
    data = json.dumps(value, indent=2).encode() + b"\n"
    if len(data) > 4 * 1024 * 1024:
        message = "Windows resource proof metadata exceeds4MiB"
        raise ValueError(message)
    with path.open("xb") as stream:
        stream.write(data)
