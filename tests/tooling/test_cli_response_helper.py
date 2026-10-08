# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Schema reuse preserves fresh validation and rejects invalid initialization."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import override
from unittest.mock import patch

from tools_path import ROOT

from jsonschema import Draft202012Validator, SchemaError, ValidationError

sys.path.insert(0, str(ROOT / "tests/cli"))
import test_cli

SCHEMA = {
    "type": "object",
    "properties": {"exit_code": {"type": "integer"}},
    "required": ["exit_code"],
    "additionalProperties": False,
}


class CliResponseHelperTests(unittest.TestCase):
    """Only the schema validator is reused; observed response bytes are never cached."""

    @override
    def setUp(self) -> None:
        """Each control begins with a fresh process-like validator state."""
        test_cli.response_validator.cache_clear()

    @override
    def tearDown(self) -> None:
        """Fixture validators cannot affect another test's schema admission."""
        test_cli.response_validator.cache_clear()

    def test_later_payloads_are_validated_against_the_same_admitted_schema(self) -> None:
        """A valid first response cannot excuse malformed later bytes from the same invocation."""
        with tempfile.TemporaryDirectory() as temporary:
            schema = Path(temporary) / "response.schema.json"
            schema.write_text(json.dumps(SCHEMA), encoding="utf-8")
            with (
                patch.object(test_cli, "SCHEMA", schema),
                patch.object(
                    test_cli, "call", side_effect=['{"exit_code":0}', "{}", '{"exit_code":0}']
                ) as command,
                patch.object(
                    Draft202012Validator,
                    "check_schema",
                    wraps=Draft202012Validator.check_schema,
                ) as checked,
            ):
                self.assertEqual(test_cli.call_json(Path("executable"), []), {"exit_code": 0})
                with self.assertRaises(ValidationError):
                    test_cli.call_json(Path("executable"), [])
                self.assertEqual(test_cli.call_json(Path("executable"), []), {"exit_code": 0})
                checked.assert_called_once()
                self.assertEqual(command.call_count, 3)

    def test_malformed_schema_is_refused_and_failed_initialization_is_not_cached(self) -> None:
        """A schema with invalid type syntax must fail before payload validation can succeed."""
        with tempfile.TemporaryDirectory() as temporary:
            schema = Path(temporary) / "response.schema.json"
            schema.write_text('{"type":"not-a-json-schema-type"}', encoding="utf-8")
            with (
                patch.object(test_cli, "SCHEMA", schema),
                patch.object(test_cli, "call", return_value='{"exit_code":0}'),
            ):
                with self.assertRaises(SchemaError):
                    test_cli.call_json(Path("executable"), [])
                schema.write_text(json.dumps(SCHEMA), encoding="utf-8")
                self.assertEqual(test_cli.call_json(Path("executable"), []), {"exit_code": 0})

    def test_response_exit_code_still_agrees_with_the_expected_process_status(self) -> None:
        """Schema-valid bytes with a different exit code still fail the process envelope check."""
        with tempfile.TemporaryDirectory() as temporary:
            schema = Path(temporary) / "response.schema.json"
            schema.write_text(json.dumps(SCHEMA), encoding="utf-8")
            with (
                patch.object(test_cli, "SCHEMA", schema),
                patch.object(test_cli, "call", return_value='{"exit_code":1}'),
                self.assertRaises(test_cli.ContractError),
            ):
                test_cli.call_json(Path("executable"), [])
