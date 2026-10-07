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
from pathlib import Path
from typing import Any

from continuous_fixtures import Fixture
from jsonschema import Draft202012Validator

SCHEMA = Path(__file__).resolve().parents[2] / "schemas/command-response.schema.json"
COMMAND_TIMEOUT_SECONDS = 30
SHA256_HEX_LENGTH = 64
EXIT_INVOCATION = 2
EXIT_PROCESSING = 4
PNG_FILTER_NONE = 0
PNG_FILTER_SUB = 1
PNG_FILTER_UP = 2
PNG_FILTER_AVERAGE = 3
PNG_FILTER_PAETH = 4
BYTE_MODULUS = 256
HALF = 2
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
        timeout=COMMAND_TIMEOUT_SECONDS,
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
        == [
            {"id": "I01", "method_version": 1},
            {"id": "I02", "method_version": 1},
            {"id": "D01", "method_version": 1},
            {"id": "D02", "method_version": 1},
            {"id": "C01", "method_version": 1},
            {"id": "C02", "method_version": 1},
            {"id": "C03", "method_version": 1},
            {"id": "B02", "method_version": 1},
            {"id": "B03", "method_version": 1},
        ],
        "implemented methods advertised",
    )
    expect(
        version["supported_formats"] == ["png", "jpeg", "tiff"],
        "reviewed PNG/JPEG/TIFF admission advertised",
    )
    expect(len(version["dependency_lock_sha256"]) == SHA256_HEX_LENGTH, "lock digest")
    expect(
        call_json(exe, ["methods", "--json"])["methods"]
        == [
            {"id": "I01", "method_version": 1},
            {"id": "I02", "method_version": 1},
            {"id": "D01", "method_version": 1},
            {"id": "D02", "method_version": 1},
            {"id": "C01", "method_version": 1},
            {"id": "C02", "method_version": 1},
            {"id": "C03", "method_version": 1},
            {"id": "B02", "method_version": 1},
            {"id": "B03", "method_version": 1},
        ],
        "methods list matches implementation",
    )
    for command in HELP_COMMANDS:
        text = call(exe, [*([command] if command else []), "--help"])
        for method in version["methods"]:
            expect(method["id"] in text, "help includes each implemented method")
        expect("R01" not in text, "help excludes planned methods")
        expect("png: preserve, gray, bw" in text, "help reports PNG modes")
        expect("jpeg: preserve, gray\n" in text, "help reports scoped JPEG modes")
        response = call_json(exe, [*([command] if command else []), "--help", "--json"])
        expect(response["exit_code"] == 0, f"{command} help exit code")
        expect(isinstance(response["options"], list), f"{command} help options")
    expect(
        "Commands: process, verify, methods, version" in call(exe, ["--help"]),
        "root help lists every current command",
    )
    options = call_json(exe, ["process", "--help", "--json"])["options"]
    contract = json.loads((SCHEMA.parents[1] / "spec/cli-contract.json").read_text())
    expected = [
        {key: option[key] for key in ("name", "metavar", "description", "domain", "methods")}
        for option in contract["options"]
        if option["scope"] in {"P", "All commands"}
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
    call(exe, ["methods", "R01"], EXIT_PROCESSING)


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


def default_cases(exe: Path) -> None:
    """Compare actual admitted records with the defaults advertised in the reviewed contract."""
    contract = json.loads((SCHEMA.parents[1] / "spec/cli-contract.json").read_text())
    defaults = {item["name"]: item["domain"].split(";", 1)[0] for item in contract["options"]}
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "source.png"
        side = 32
        source.write_bytes(Fixture(side, side, ((200,),) * (side * side)).encoded())
        for option in contract["options"]:
            if option["scope"] == "P" and option["metavar"]:
                output = root / "refused"
                arguments = ["process", str(source)]
                if option["name"] != "--out-dir":
                    arguments.extend(["--out-dir", str(output)])
                response = call_json(
                    exe,
                    [*arguments, option["name"], "", "--json"],
                    EXIT_INVOCATION,
                )
                expect(response["publication"] == "not_started", "empty value refuses execution")
                expect(not output.exists(), "empty value publishes nothing")
        records = []
        for index, options in enumerate(
            (
                [],
                ["--output-mode", "bw"],
                ["--output-mode", "bw", "--binarize", "fixed"],
                ["--illumination", "surface"],
                ["--denoise", "nlm"],
            )
        ):
            out = root / str(index)
            call_json(exe, ["process", str(source), "--out-dir", str(out), *options, "--json"])
            call_json(exe, ["verify", str(out), "--json"])
            records.append(json.loads((out / "run.json").read_text()))
        groups = (
            (
                records[0]["request"]["operation"]["parameters"],
                {
                    "output_mode": "--output-mode",
                    "depth": "--bit-depth",
                    "alpha": "--alpha",
                    "profile": "--profile-policy",
                },
            ),
            (
                records[1]["request"]["operation"]["parameters"],
                {"window": "--sauvola-window", "k": "--sauvola-k", "r": "--sauvola-r"},
            ),
            (records[2]["request"]["operation"]["parameters"], {"threshold": "--fixed-threshold"}),
            (
                records[3]["execution"]["illumination"]["requested"],
                {
                    "strength": "--background-strength",
                    "max_gain": "--background-max-gain",
                    "target": "--background-target",
                    "cell": "--background-cell",
                    "quantile": "--background-quantile",
                    "smooth": "--background-smooth",
                },
            ),
            (
                records[4]["request"]["denoising"]["parameters"],
                {
                    "h": "--nlm-h",
                    "patch": "--nlm-patch",
                    "search": "--nlm-search",
                    "blend": "--denoise-blend",
                },
            ),
        )
        for actual, names in groups:
            for key, option in names.items():
                value = defaults[option]
                expected = float(value) if isinstance(actual[key], (int, float)) else value
                if option == "--bit-depth":
                    expected = "automatic" if value == "auto" else value
                expect(
                    actual[key] == expected, f"{option}: executed default matches reviewed default"
                )
        expect(records[1]["request"]["operation"]["method"]["id"] == "B02", "default bw is B02")
        expect(defaults["--binarize"] == "sauvola for bw", "reviewed default binary selector")
        for stage, option in (("illumination", "--illumination"), ("denoising", "--denoise")):
            expect(defaults[option] == "off", f"{stage} defaults to off")
            expect(
                records[0]["execution"][stage]["status"] == "disabled", f"{stage} remains disabled"
            )
        expect(not records[0]["request"]["protection_supplied"], "default has no mask")


def main() -> int:
    """Run every contract case against the executable named on the command line."""
    exe = Path(sys.argv[1]).resolve()
    discovery_cases(exe)
    for args in REJECTED_INVOCATIONS:
        call(exe, args, EXIT_INVOCATION)
    unavailable_cases(exe)
    processing_case(exe)
    default_cases(exe)
    print("PASS: real-executable CLI contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
