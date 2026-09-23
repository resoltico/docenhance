#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Exercise admitted path identity using the real CLI, codec and no-replace publication."""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[2]
ARGUMENT_ERROR = 2
OUTPUT_ERROR = 5
EXPECTED_ARGUMENT_COUNT = 2
SCHEMA = ROOT / "schemas/command-response.schema.json"


def chunk(kind: bytes, data: bytes) -> bytes:
    """A checked PNG chunk for the deterministic grayscale fixture."""
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def fixture() -> bytes:
    """A two-by-two 8-bit grayscale PNG, independent of the production encoder."""
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"\0\x00\x80\0\xff\x40"))
        + chunk(b"IEND", b"")
    )


def invoke(binary: Path, source: bytes, target: bytes) -> subprocess.CompletedProcess[bytes]:
    """Use native argv bytes on POSIX and strict UTF-8 conversion on Windows."""
    argv = [
        os.fsencode(binary),
        b"process",
        source,
        b"--out-dir",
        target,
        b"--binarize",
        b"fixed",
        b"--json",
    ]
    command = [value.decode("utf-8") for value in argv] if os.name == "nt" else argv
    return subprocess.run(command, capture_output=True, check=False, timeout=20)


def verify_response(result: subprocess.CompletedProcess[bytes], expected: int) -> dict[str, object]:
    """Validate each exact response with the repository's full schema."""
    if result.returncode != expected or result.stderr:
        msg = f"Unexpected response: {result.returncode}; {result.stdout!r}; {result.stderr!r}"
        raise AssertionError(msg)
    value: dict[str, object] = json.loads(result.stdout)
    Draft202012Validator(json.loads(SCHEMA.read_text(encoding="utf-8"))).validate(value)
    if value["exit_code"] != expected:
        msg = "Response and process exit codes disagree"
        raise AssertionError(msg)
    return value


def valid_paths(binary: Path, root: Path, source: Path) -> int:
    """Unicode spelling remains exact; POSIX backslashes remain filename characters."""
    names = ["Rīga-文書-📄", "decomposed-e\u0301", 'quoted-"-line\n']
    if os.name == "nt":
        # Quotes and newlines are not portable Windows filename components.
        names = names[:2]
    else:
        names += [r"literal\backslash", "nested"]
        (root / "nested").mkdir()
        names[-1] = r"nested/leaf\name"
    for name in names:
        target = root / name
        result = invoke(binary, str(source).encode("utf-8"), str(target).encode("utf-8"))
        response = verify_response(result, 0)
        expected = target / "result.png"
        if response["output"] != str(expected) or not expected.is_file():
            msg = (
                f"Published and reported paths differ: {response['output']!r} != {str(expected)!r}"
            )
            raise AssertionError(msg)
    return len(names)


def malformed_paths(binary: Path, root: Path, source: Path) -> int:
    """Non-UTF-8 POSIX paths are rejected before output staging or publication."""
    if os.name == "nt":
        return 0  # Raw non-UTF-8 byte filenames are a POSIX-only boundary.
    target_bytes = os.fsencode(root) + b"/invalid-\xff"
    before = set(root.iterdir())
    response = verify_response(invoke(binary, os.fsencode(source), target_bytes), ARGUMENT_ERROR)
    if response["publication"] != "not_started" or set(root.iterdir()) != before:
        msg = "Malformed output path was not rejected before effects"
        raise AssertionError(msg)
    invalid_source = os.fsencode(root) + b"/invalid-\x80.png"
    before = set(root.iterdir())
    response = verify_response(
        invoke(binary, invalid_source, os.fsencode(root / "never-created")),
        ARGUMENT_ERROR,
    )
    if response["publication"] != "not_started" or set(root.iterdir()) != before:
        msg = "Malformed input path was not rejected before effects"
        raise AssertionError(msg)
    return 2


def refused_delivery(binary: Path, root: Path, source: Path) -> int:
    """A real full output device must not turn completed processing into a safe retry."""
    sink = Path("/dev/full")
    if os.name != "posix" or not sink.is_char_device():
        return 0  # This OS integration fixture is unavailable on Windows and most macOS hosts.
    target = root / "committed-without-response"
    with sink.open("wb") as output:
        result = subprocess.run(
            [
                str(binary),
                "process",
                str(source),
                "--out-dir",
                str(target),
                "--binarize",
                "fixed",
                "--json",
            ],
            stdout=output,
            stderr=subprocess.PIPE,
            check=False,
            timeout=20,
        )
    if result.returncode != OUTPUT_ERROR or result.stderr or not (target / "result.png").is_file():
        msg = "Response delivery refusal lost the committed result or caused a misleading response"
        raise AssertionError(msg)
    return 1


def main() -> int:
    """Run against a specified built executable; never construct a substitute processor."""
    if len(sys.argv) != EXPECTED_ARGUMENT_COUNT:
        msg = "Usage: test_paths.py DOCENHANCE_EXECUTABLE"
        raise SystemExit(msg)
    binary = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="docenhance-paths-") as directory:
        root = Path(directory)
        source = root / "input.png"
        source.write_bytes(fixture())
        count = (
            valid_paths(binary, root, source)
            + malformed_paths(binary, root, source)
            + refused_delivery(binary, root, source)
        )
        if source.read_bytes() != fixture():
            msg = "Processing changed its input"
            raise AssertionError(msg)
    print(f"Path identity/admission and delivery: {count} cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
