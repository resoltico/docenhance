#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Real-codec regressions: stored samples, malformed inputs and exclusive publication."""

from __future__ import annotations

import concurrent.futures
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

from test_cli import call_json, chunk, expect, read_gray_png, write_gray_png

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png(depth: int, color: int, samples: bytes, metadata: bytes = b"") -> bytes:
    """Create a two-pixel grayscale or deliberately unsupported input."""
    header = struct.pack(">IIBBBBB", 2, 1, depth, color, 0, 0, 0)
    return (
        SIGNATURE
        + chunk(b"IHDR", header)
        + metadata
        + chunk(b"IDAT", zlib.compress(b"\0" + samples))
        + chunk(b"IEND", b"")
    )


def command(source: Path, output: Path) -> list[str]:
    """Exercise the production composition root, never a mock codec."""
    return [
        "process",
        str(source),
        "--out-dir",
        str(output),
        "--output-mode",
        "bw",
        "--binarize",
        "fixed",
        "--json",
    ]


def accepted_samples(exe: Path, root: Path) -> None:
    """Low bit depths expand predictably; gAMA must not change stored threshold samples."""
    for depth, samples in ((1, b"\x40"), (2, b"\x30"), (4, b"\x0f"), (8, b"\x00\xff")):
        source, output = root / f"depth-{depth}.png", root / f"depth-{depth}"
        source.write_bytes(png(depth, 0, samples))
        call_json(exe, command(source, output))
        expect(read_gray_png(output / "result.png") == b"\0\xff", "stored sample expansion")
    source, output = root / "gamma.png", root / "gamma"
    source.write_bytes(png(8, 0, b"\x7f\x80", chunk(b"gAMA", struct.pack(">I", 100000))))
    call_json(exe, command(source, output))
    expect(read_gray_png(output / "result.png") == b"\0\xff", "gamma did not change samples")
    source, output = root / "interlaced.png", root / "interlaced"
    source.write_bytes(
        SIGNATURE
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 0, 0, 0, 1))
        + chunk(b"IDAT", zlib.compress(b"\0\x7f\0\x80"))
        + chunk(b"IEND", b"")
    )
    call_json(exe, command(source, output))
    expect(read_gray_png(output / "result.png") == b"\0\xff", "Adam7 pass combination")
    source, output = root / "dokuments-ā-文.png", root / "rezultāts-文"
    write_gray_png(source, b"\0\xff")
    response = call_json(exe, command(source, output))
    expect(response["output"] == str(output / "result.png"), "Unicode output path round trip")


def rejected_inputs(exe: Path, root: Path) -> None:
    """CRC, truncation, color, depth and transparency failures never create an output."""
    valid = png(8, 0, b"\0\xff")
    bad_crc = bytearray(valid)
    bad_crc[29] ^= 1
    inputs = [
        b"not png",
        valid[:20],
        valid[:-5],
        bytes(bad_crc),
        png(16, 0, b"\0\0\xff\xff"),
        png(8, 2, b"\0\0\0\xff\xff\xff"),
        png(8, 0, b"\0\xff", chunk(b"tRNS", b"\0\0")),
    ]
    for number, data in enumerate(inputs):
        source, output = root / f"bad-{number}.png", root / f"bad-{number}"
        source.write_bytes(data)
        response = call_json(exe, command(source, output), 3)
        expect(response["publication"] == "not_started", "bad input did not begin publication")
        expect(not output.exists(), "invalid input created no output")
        expect(source.read_bytes() == data, "input bytes were preserved")


def exclusive_outputs(exe: Path, root: Path) -> None:
    """Existing destinations, staging occupants and competing publishers retain their ownership."""
    source = root / "publication.png"
    write_gray_png(source, b"\0\xff")
    for name, is_directory in (("existing-dir", True), ("existing-file", False)):
        output = root / name
        if is_directory:
            output.mkdir()
        else:
            output.write_bytes(b"foreign")
        call_json(exe, command(source, output), 5)
        expect(
            output.is_dir() if is_directory else output.read_bytes() == b"foreign",
            "existing destination was preserved",
        )
    if os.name != "nt":  # Windows symlink creation can require a privilege absent on runners.
        output = root / "dangling-output"
        output.symlink_to("absent-target")
        call_json(exe, command(source, output), 5)
        expect(
            output.is_symlink() and output.readlink() == Path("absent-target"),
            "a dangling destination symlink was not followed or replaced",
        )
    output = root / "with-staging"
    occupied = root / "with-staging.staging-0"
    occupied.mkdir()
    (occupied / "owner").write_bytes(b"foreign")
    call_json(exe, command(source, output))
    expect((occupied / "owner").read_bytes() == b"foreign", "foreign staging directory preserved")
    target = root / "contended"

    # The private native-rename unit test covers the exact race at the commit operation.
    def publish(_: int) -> bool:
        result = subprocess.run(
            [str(exe), *command(source, target)], capture_output=True, timeout=10, check=False
        )
        expect(result.returncode in (0, 5), "publisher failed without an expected refusal")
        return result.returncode == 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        expect(sum(pool.map(publish, range(2))) == 1, "exactly one publisher owns the destination")
    expect(read_gray_png(target / "result.png") == b"\0\xff", "winner published complete output")
    expect(not list(root.glob("contended.staging-*")), "losing publisher cleaned its own stage")


def main() -> int:
    """Run codec and publication checks against the built command-line executable."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        accepted_samples(exe, root)
        rejected_inputs(exe, root)
        exclusive_outputs(exe, root)
    print("PASS: PNG and publication regressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
