#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Black-box contract tests. Run only against a real built application."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

SCHEMA = Path(__file__).resolve().parents[2] / "schemas/command-response.schema.json"
SHA256_HEX_LENGTH = 64
PROCESS_OPTION_COUNT = 68
EXIT_INVOCATION = 2
EXIT_PROCESSING = 4
HELP_COMMANDS = ("", "process", "plan", "inspect", "presets", "methods", "version")
REJECTED_INVOCATIONS = (
    ["--version", "--json"],
    ["version", "--wat"],
    ["--help", "--help"],
    ["methods", "--json", "--json"],
    ["process"],
    ["process", "file.jpg"],
    ["process", "x", "--out-dir", "y", "--out-dir", "z"],
    ["process", "x", "--out-dir", "y", "--recur"],
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


def validate(value: Any, schema: dict[str, Any], where: str) -> None:  # noqa: ANN401
    """Check a value against the JSON Schema subset the command contract uses."""
    if "const" in schema:
        expect(value == schema["const"], f"{where}: expected {schema['const']!r}, got {value!r}")
    if "enum" in schema:
        expect(value in schema["enum"], f"{where}: {value!r} is not one of {schema['enum']}")
    types = {"object": dict, "array": list, "string": str, "integer": int}
    if "type" in schema:
        expect(isinstance(value, types[schema["type"]]), f"{where}: expected {schema['type']}")
    if "pattern" in schema:
        expect(re.fullmatch(schema["pattern"], value) is not None, f"{where}: {value!r}")
    if isinstance(value, dict):
        for name in schema.get("required", []):
            expect(name in value, f"{where}: missing required field {name}")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            undeclared = sorted(set(value) - set(properties))
            expect(not undeclared, f"{where}: undeclared fields {undeclared}")
        for name, item in value.items():
            if name in properties:
                validate(item, properties[name], f"{where}.{name}")


def call_json(exe: Path, args: list[str], code: int = 0) -> dict[str, Any]:
    """Run the executable, parse its single JSON object and validate it against the schema."""
    data: dict[str, Any] = json.loads(call(exe, args, code))
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    validate(data, schema, f"{args} response")
    return data


def discovery_cases(exe: Path) -> None:
    """Version, method and help discovery report capabilities honestly."""
    version = call_json(exe, ["version", "--json"])
    expect(version["methods"] == [], "no methods advertised")
    expect(version["supported_formats"] == [], "no formats advertised")
    expect(len(version["dependency_lock_sha256"]) == SHA256_HEX_LENGTH, "lock digest")
    expect(call_json(exe, ["methods", "--json"])["methods"] == [], "methods list is empty")
    for command in HELP_COMMANDS:
        response = call_json(exe, [*([command] if command else []), "--help", "--json"])
        expect(response["exit_code"] == 0, f"{command} help exit code")
        expect(isinstance(response["options"], list), f"{command} help options")
    options = call_json(exe, ["process", "--help", "--json"])["options"]
    expect(len(options) == PROCESS_OPTION_COUNT, "process option count")


def unavailable_cases(exe: Path) -> None:
    """Processing commands fail before touching inputs or outputs."""
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        source = root / "untrusted.jpg"
        source.write_bytes(b"not an image")
        output = root / "result"
        args = ["process", str(source), "--out-dir", str(output), "--json"]
        response = call_json(exe, args, EXIT_PROCESSING)
        expect(response["error"]["code"] == "E_NOT_IMPLEMENTED", "process is unavailable")
        expect(response["publication"] == "not_started", "nothing published")
        expect(not output.exists(), "no output directory created")
        expect(source.read_bytes() == b"not an image", "input untouched")
        for command in ("plan", "inspect"):
            args = [command, str(root / "missing.jpg"), "--json"]
            response = call_json(exe, args, EXIT_PROCESSING)
            expect(response["error"]["code"] == "E_NOT_IMPLEMENTED", f"{command} unavailable")
    if os.name == "posix":
        # Arguments are bytes on POSIX and need not be UTF-8; JSON output must stay valid.
        args = ["version", os.fsdecode(b"\x80"), "--json"]
        response = call_json(exe, args, EXIT_INVOCATION)
        expect(response["error"]["code"] == "E_ARGUMENT", "non-UTF-8 argument is an argument error")
    call(exe, ["presets"], EXIT_PROCESSING)
    call(exe, ["methods", "I01"], EXIT_PROCESSING)


def main() -> int:
    """Run every contract case against the executable named on the command line."""
    exe = Path(sys.argv[1]).resolve()
    discovery_cases(exe)
    for args in REJECTED_INVOCATIONS:
        call(exe, args, EXIT_INVOCATION)
    unavailable_cases(exe)
    print("PASS: real-executable CLI contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
