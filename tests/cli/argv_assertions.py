# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent grammar predictions checked against real executable responses."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

from argument_grammar import analyze, reviewed_syntax
from test_cli import expect, response_validator

ARGUMENT_EXIT = 2
REPOSITORY = Path(__file__).resolve().parents[2]
GRAMMAR_OPTIONS, OPERAND_LIMITS = reviewed_syntax(
    json.loads((REPOSITORY / "spec/cli-contract.json").read_bytes()),
    json.loads((REPOSITORY / "spec/method-contract.json").read_bytes()),
)


def invoke(exe: Path, args: list[str], root: Path, code: int) -> subprocess.CompletedProcess[str]:
    """Observe both streams and process status without inferring effects from missing responses."""
    roles = analyze(args, GRAMMAR_OPTIONS, OPERAND_LIMITS)
    result = subprocess.run(
        [str(exe), *args],
        cwd=root,
        text=True,
        encoding="utf-8",
        capture_output=True,
        timeout=30,
        check=False,
    )
    expect(
        result.returncode == code, f"{args}: {result.returncode}: {result.stdout} {result.stderr}"
    )
    expect(result.stdout.startswith("{") == roles.json, f"{args}: oracle presentation roles")
    if roles.json:
        response = json.loads(result.stdout)
        response_validator().validate(response)
        expect(not result.stderr and response["exit_code"] == code, "single JSON delivery")
        if not roles.accepted:
            expect(response["error"]["code"] == "E_ARGUMENT", "syntax error vocabulary")
            expect(response["publication"] == "not_started", "syntax refusal before effects")
    if not roles.accepted:
        expect(result.returncode == ARGUMENT_EXIT, f"{args}: syntax refusal must exit 2")
        if not roles.json:
            expect("E_ARGUMENT:" in result.stderr, "human syntax error vocabulary")
    return result


def root_cases(exe: Path, root: Path) -> None:
    """Root grammar and missing operands retain explicit executable admission expectations."""
    before = set(root.iterdir())
    for arguments, code in (
        ([], 0),
        (["--help"], 0),
        (["--json"], 0),
        (["--help", "--json"], 0),
        (["--version"], 0),
        (["--version", "--json"], 2),
        (["--json", "--version"], 2),
        (["--version", "--help"], 2),
        (["--help", "--version"], 2),
        (["process", "--json"], 2),
        (["verify", "--json"], 2),
    ):
        invoke(exe, arguments, root, code)
    expect(set(root.iterdir()) == before, "root and missing-operand cases stay read-only")
