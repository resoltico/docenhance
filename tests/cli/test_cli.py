#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Black-box contract tests. Run only against a real built application."""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from functools import cache
from pathlib import Path
from typing import Any

import cli_defaults
from continuous_fixtures import decode_output
from contract_reference import option_projection, reviewed_methods, reviewed_options
from jsonschema import Draft202012Validator

SCHEMA = Path(__file__).resolve().parents[2] / "schemas/command-response.schema.json"


def response_version() -> int:
    """Every actual native response alternative carries the same positive wire version."""
    branches = json.loads(SCHEMA.read_bytes())["oneOf"]
    versions = [branch["properties"]["schema_version"]["const"] for branch in branches]
    if not versions or any(type(value) is not int or value <= 0 for value in versions):
        message = "Native response branches lack positive integral schema versions"
        raise ValueError(message)
    if any(value != versions[0] for value in versions):
        message = "Native response alternatives disagree about their schema version"
        raise ValueError(message)
    version: object = versions[0]
    if type(version) is not int:
        message = "Native response version is not an integer"
        raise TypeError(message)
    return version


WIRE_VERSION = response_version()
RECORD_VERSION = json.loads(SCHEMA.with_name("run-record.schema.json").read_bytes())["properties"][
    "record"
]["properties"]["version"]["const"]
COMMAND_TIMEOUT_SECONDS = 30
SHA256_HEX_LENGTH = 64
BINARY_METHOD_VERSION = 2
EXIT_INVOCATION = 2
EXIT_PROCESSING = 4
HELP_COMMANDS = ("", "process", "verify", "methods", "version")
REJECTED_INVOCATIONS = (
    ["plan"],
    ["inspect"],
    ["presets"],
    ["--version", "--json"],
    ["version", "--wat"],
    ["--help", "--help"],
    ["methods", "--json", "--json"],
    ["process"],
    ["process", "file.png"],
    ["process", "x.png", "--out-dir", "y", "--out-dir", "z"],
    ["process", "x.png", "--out-dir", "y", "--output-mode", "bw", "--binarize", "unknown"],
    ["--json", "version"],
)


class ContractError(AssertionError):
    """The executable violated its CLI contract."""


def expect(condition: object, message: str) -> None:
    """Fail with message unless condition holds (unlike assert, never stripped by -O)."""
    if not condition:
        raise ContractError(message)


def call(exe: Path, args: list[str], code: int = 0, *, cwd: Path | None = None) -> str:
    """Run the executable, require the exit code, and return stdout."""
    result = subprocess.run(
        [str(exe), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=COMMAND_TIMEOUT_SECONDS,
        check=False,
        cwd=cwd,
    )
    expect(
        result.returncode == code,
        f"{args}: expected {code}, got {result.returncode}: {result.stdout} {result.stderr}",
    )
    return result.stdout


@cache
def response_validator() -> Draft202012Validator:
    """Admit this process's schema once; every response still receives fresh validation."""
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def call_json(
    exe: Path, args: list[str], code: int = 0, *, cwd: Path | None = None
) -> dict[str, Any]:
    """Run the executable, parse its single JSON object and validate it against the schema."""
    data: dict[str, Any] = json.loads(call(exe, args, code, cwd=cwd))
    response_validator().validate(data)
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
    """Read strict physical grayPNG1 as logical0/1; no display-byte expansion."""
    image = decode_output(path.read_bytes())
    expect(
        image.depth == 1 and image.color == 0 and image.height == 1,
        "single-row physical binary output",
    )
    return bytes(p[0] for p in image.pixels)


def discovery_cases(exe: Path) -> None:
    """Version, method and help discovery report capabilities honestly."""
    version = call_json(exe, ["version", "--json"])
    expect(version["methods"] == reviewed_methods(), "implemented metadata matches authority")
    expect(
        version["supported_formats"] == ["png", "jpeg", "tiff"],
        "reviewed PNG/JPEG/TIFF admission advertised",
    )
    expect(
        version["input_support"]
        == [
            {"format": name, "output_modes": ["preserve", "gray", "bw"]}
            for name in ("png", "jpeg", "tiff")
        ],
        "common fixed pipeline input/mode discovery",
    )
    expect(len(version["dependency_lock_sha256"]) == SHA256_HEX_LENGTH, "lock digest")
    expect(
        call_json(exe, ["methods", "--json"])["methods"] == version["methods"],
        "methods list matches implementation",
    )
    for command in HELP_COMMANDS:
        text = call(exe, [*([command] if command else []), "--help"])
        for method in version["methods"]:
            expect(method["id"] in text, "help includes each implemented method")
        expect("G01" not in text, "help keeps common geometry outside the method catalog")
        expect("png: preserve, gray, bw" in text, "help reports PNG modes")
        expect("jpeg: preserve, gray, bw" in text, "help reports common JPEG modes")
        expect("tiff: preserve, gray, bw" in text, "help reports common TIFF modes")
        response = call_json(exe, [*([command] if command else []), "--help", "--json"])
        expect(response["exit_code"] == 0, f"{command} help exit code")
        expect(isinstance(response["options"], list), f"{command} help options")
    expect(
        "Commands: process, verify, methods, version" in call(exe, ["--help"]),
        "root help lists every current command",
    )
    options = call_json(exe, ["process", "--help", "--json"])["options"]
    expected = [
        option_projection(option) for option in reviewed_options() if "process" in option["scope"]
    ]
    expect(options == expected, "complete reviewed process option descriptors")


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
    call(exe, ["methods", "G01"], EXIT_PROCESSING)


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
        selection = response["record"]["request"]["binarization"]
        expect(
            selection["method"] == "B03" and selection["version"] == BINARY_METHOD_VERSION,
            "reported current method",
        )
        expect(response["publication"] == "completed", "published result")
        target = output / "result.png"
        expect(response["output"] == str(target), "reported output path")
        expect(read_gray_png(target) == bytes([0, 0, 1, 1]), "threshold output")


def literal_filename_cases(exe: Path) -> None:
    """Open literal leading-dash operands relative to the child's actual working directory."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        samples = bytes([0, 64, 192, 255])
        for index, name in enumerate(("-", "-h", "-input.png", "--input.png")):
            source = root / name
            write_gray_png(source, samples)
            original = source.read_bytes()
            output = f"bundle-{index}"
            response = call_json(
                exe,
                [
                    "process",
                    "--out-dir",
                    output,
                    "--output-mode",
                    "bw",
                    "--binarize",
                    "fixed",
                    "--json",
                    *(["--"] if name.startswith("--") else []),
                    name,
                ],
                cwd=root,
            )
            expect(response["publication"] == "completed", f"literal {name} is processed")
            expect(
                read_gray_png(root / output / "result.png") == bytes([0, 0, 1, 1]),
                "literal input produces the expected pixels",
            )
            expect(source.read_bytes() == original, "literal source remains unchanged")
            call_json(exe, ["verify", output, "--json"], cwd=root)


def main() -> int:
    """Run every contract case against the executable named on the command line."""
    exe = Path(sys.argv[1]).resolve()
    discovery_cases(exe)
    for args in REJECTED_INVOCATIONS:
        call(exe, args, EXIT_INVOCATION)
    unavailable_cases(exe)
    processing_case(exe)
    literal_filename_cases(exe)
    cli_defaults.default_cases(exe)
    cli_defaults.execution_cases(exe)
    print("PASS: real-executable CLI contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
