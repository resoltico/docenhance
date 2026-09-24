#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Black-box contract tests. Run only against a real built application."""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

SCHEMA = Path(__file__).resolve().parents[2] / "schemas/command-response.schema.json"
SHA256_HEX_LENGTH = 64
PROCESS_OPTION_COUNT = 12
EXIT_INVOCATION = 2
EXIT_PROCESSING = 4
PNG_FILTER_NONE = 0
PNG_FILTER_SUB = 1
PNG_FILTER_UP = 2
PNG_FILTER_AVERAGE = 3
PNG_FILTER_PAETH = 4
BYTE_MODULUS = 256
HALF = 2
HELP_COMMANDS = ("", "process", "methods", "version")
REJECTED_INVOCATIONS = (
    ["--version", "--json"],
    ["version", "--wat"],
    ["--help", "--help"],
    ["methods", "--json", "--json"],
    ["process"],
    ["process", "file.png"],
    ["process", "x.png", "--out-dir", "y", "--out-dir", "z"],
    ["process", "x.png", "--out-dir", "y", "--output-mode", "bw", "--binarize", "otsu"],
    ["--json", "version"],
)


class ContractError(AssertionError):
    """The executable violated its CLI contract."""


def expect(condition: object, message: str) -> None:
    """Fail with message unless condition holds (unlike assert, never stripped by -O)."""
    if not condition:
        raise ContractError(message)


def call(exe: Path, args: list[str], code: int = 0) -> str:
    """Run the executable, require the exit code, and return stdout."""
    result = subprocess.run(
        [str(exe), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=10,
        check=False,
    )
    expect(
        result.returncode == code,
        f"{args}: expected {code}, got {result.returncode}: {result.stdout} {result.stderr}",
    )
    return result.stdout


def call_json(exe: Path, args: list[str], code: int = 0) -> dict[str, Any]:
    """Run the executable, parse its single JSON object and validate it against the schema."""
    data: dict[str, Any] = json.loads(call(exe, args, code))
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(data)
    expect(data["exit_code"] == code, "envelope and process exit codes must agree")
    return data


def chunk(kind: bytes, payload: bytes) -> bytes:
    """Return one PNG chunk with its checked CRC."""
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload))
    )


def write_gray_png(path: Path, samples: bytes) -> None:
    """Write one filter-free 8-bit grayscale PNG for the real-binary test."""
    signature = b"\x89PNG\r\n\x1a\n"
    header = struct.pack(">IIBBBBB", len(samples), 1, 8, 0, 0, 0, 0)
    path.write_bytes(
        signature
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(b"\0" + samples))
        + chunk(b"IEND", b"")
    )


def read_gray_png(path: Path) -> bytes:
    """Read the single-row grayscale fixture output using PNG's lossless row filters."""
    data = path.read_bytes()
    parts = []
    offset = 8
    while offset < len(data):
        size = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + size]
        if kind == b"IDAT":
            parts.append(payload)
        offset += size + 12
    decoded = zlib.decompress(b"".join(parts))
    filter_kind = decoded[0]
    samples = bytearray(decoded[1:])
    for index, value in enumerate(samples):
        left = samples[index - 1] if index else 0
        if filter_kind == PNG_FILTER_NONE:
            continue
        if filter_kind in {PNG_FILTER_SUB, PNG_FILTER_PAETH}:
            samples[index] = (value + left) % BYTE_MODULUS
        elif filter_kind == PNG_FILTER_UP:
            samples[index] = value
        elif filter_kind == PNG_FILTER_AVERAGE:
            samples[index] = (value + (left // HALF)) % BYTE_MODULUS
        else:
            message = f"unexpected PNG filter {filter_kind}"
            raise ContractError(message)
    return bytes(samples)


def discovery_cases(exe: Path) -> None:
    """Version, method and help discovery report capabilities honestly."""
    version = call_json(exe, ["version", "--json"])
    expect(
        version["methods"]
        == [{"id": "B02", "method_version": 1}, {"id": "B03", "method_version": 1}],
        "implemented methods advertised",
    )
    expect(version["supported_formats"] == ["png"], "PNG advertised")
    expect(len(version["dependency_lock_sha256"]) == SHA256_HEX_LENGTH, "lock digest")
    expect(
        call_json(exe, ["methods", "--json"])["methods"]
        == [{"id": "B02", "method_version": 1}, {"id": "B03", "method_version": 1}],
        "methods list matches implementation",
    )
    for command in HELP_COMMANDS:
        response = call_json(exe, [*([command] if command else []), "--help", "--json"])
        expect(response["exit_code"] == 0, f"{command} help exit code")
        expect(isinstance(response["options"], list), f"{command} help options")
    options = call_json(exe, ["process", "--help", "--json"])["options"]
    expect(len(options) == PROCESS_OPTION_COUNT, "process option count")


def unavailable_cases(exe: Path) -> None:
    """Invalid inputs and unsupported method names fail without publication."""
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        source = root / "untrusted.png"
        source.write_bytes(b"not a PNG")
        output = root / "result"
        args = [
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
        response = call_json(exe, args, 3)
        expect(response["error"]["code"] == "E_INPUT", "invalid PNG is an input failure")
        expect(response["publication"] == "not_started", "nothing published")
        expect(not output.exists(), "no output directory created")
        expect(source.read_bytes() == b"not a PNG", "input untouched")
    if os.name == "posix":
        # Arguments are bytes on POSIX and need not be UTF-8; JSON output must stay valid.
        args = ["version", os.fsdecode(b"\x80"), "--json"]
        response = call_json(exe, args, EXIT_INVOCATION)
        expect(response["error"]["code"] == "E_ARGUMENT", "non-UTF-8 argument is an argument error")
    call(exe, ["methods", "I01"], EXIT_PROCESSING)


def processing_case(exe: Path) -> None:
    """The real binary thresholds a grayscale PNG and atomically publishes its result."""
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        source = root / "input.png"
        output = root / "result"
        write_gray_png(source, bytes([0, 127, 128, 255]))
        response = call_json(
            exe,
            [
                "process",
                str(source),
                "--out-dir",
                str(output),
                "--output-mode",
                "bw",
                "--binarize",
                "fixed",
                "--fixed-threshold",
                "0.5",
                "--json",
            ],
        )
        expect(response["method"] == "B03", "reported method")
        expect(response["publication"] == "completed", "published result")
        target = output / "result.png"
        expect(response["output"] == str(target), "reported output path")
        expect(read_gray_png(target) == bytes([0, 0, 255, 255]), "threshold output")


def main() -> int:
    """Run every contract case against the executable named on the command line."""
    exe = Path(sys.argv[1]).resolve()
    discovery_cases(exe)
    for args in REJECTED_INVOCATIONS:
        call(exe, args, EXIT_INVOCATION)
    unavailable_cases(exe)
    processing_case(exe)
    print("PASS: real-executable CLI contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
