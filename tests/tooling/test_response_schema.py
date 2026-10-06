# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reject malformed machine responses rather than testing a home-made schema subset."""

from __future__ import annotations

import copy
import json
import unittest
from typing import Any

from tools_path import ROOT

from jsonschema import Draft202012Validator

SCHEMA = json.loads((ROOT / "schemas/command-response.schema.json").read_text(encoding="utf-8"))


def methods_response() -> dict[str, Any]:
    """Return one complete, valid response for mutation tests."""
    return {
        "schema_version": 4,
        "command": "methods",
        "version": "0.2.0",
        "exit_code": 0,
        "methods": [{"id": "B03", "method_version": 1}],
        "supported_formats": ["png", "jpeg", "tiff"],
        "input_support": [
            {"format": "png", "output_modes": ["preserve", "gray", "bw"]},
            {"format": "jpeg", "output_modes": ["preserve", "gray"]},
            {"format": "tiff", "output_modes": ["preserve", "gray"]},
        ],
    }


class ResponseSchemaTests(unittest.TestCase):
    """Payload shape, array items, integer semantics and exit-code correspondence are enforced."""

    def test_schema_is_valid(self) -> None:
        """The schema itself and its positive fixture are accepted by Draft 2020-12."""
        Draft202012Validator.check_schema(SCHEMA)
        Draft202012Validator(SCHEMA).validate(methods_response())

    def test_bad_envelopes_are_rejected(self) -> None:
        """A bare envelope, stale command, mixed payload and boolean integer all fail."""
        base = methods_response()
        mutations: list[dict[str, Any]] = [
            {"command": "plan"},
            {"schema_version": True},
            {"schema_version": 1},
            {"schema_version": 2},
            {"schema_version": 3},
            {"schema_version": 5},
            {"exit_code": True},
            {"methods": [{"id": "B03", "method_version": True}]},
            {"methods": [{"id": "B03", "method_version": 1, "invented": 1}]},
            {"methods": ["B03"]},
            {"supported_formats": ["jpeg"]},
            {"output": "foreign-payload"},
            {"error": {"code": "E_INPUT", "message": "bad"}},
        ]
        validator = Draft202012Validator(SCHEMA)
        for change in mutations:
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(base | change))
        for name in ("methods", "supported_formats"):
            missing = copy.deepcopy(base)
            del missing[name]
            self.assertFalse(validator.is_valid(missing))

    def test_publication_uncertainty_is_distinct(self) -> None:
        """Unknown publication has one error code and may not claim a safe retry."""
        response = {
            "schema_version": 4,
            "command": "process",
            "version": "0.2.0",
            "exit_code": 7,
            "publication": "unknown",
            "error": {"code": "E_PUBLICATION_UNKNOWN", "message": "Inspect output"},
        }
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        self.assertFalse(validator.is_valid(response | {"publication": "not_started"}))
        self.assertFalse(validator.is_valid(response | {"exit_code": 5}))
        self.assertFalse(validator.is_valid(response | {"publication": "completed"}))

    def test_unimplemented_capability_never_starts_publication(self) -> None:
        """An unavailable processing branch has exit four and no execution effects."""
        response = {
            "schema_version": 4,
            "command": "process",
            "version": "0.6.0",
            "exit_code": 4,
            "publication": "not_started",
            "error": {"code": "E_NOT_IMPLEMENTED", "message": "Unsupported branch"},
        }
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        for publication in ("not_published", "completed", "unknown"):
            with self.subTest(publication=publication):
                self.assertFalse(validator.is_valid(response | {"publication": publication}))
        self.assertFalse(validator.is_valid(response | {"exit_code": 3}))

    def test_integrity_failure_can_retain_known_publication(self) -> None:
        """A failed postcommit inspection cannot erase an observed successful rename."""
        response = {
            "schema_version": 4,
            "command": "process",
            "version": "0.3.0",
            "exit_code": 5,
            "publication": "completed",
            "error": {"code": "E_OUTPUT_VERIFY", "message": "Inspect the published bundle"},
        }
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        self.assertFalse(validator.is_valid(response | {"publication": "unknown"}))
        self.assertFalse(validator.is_valid(response | {"command": "verify"}))

    def test_help_options_are_typed(self) -> None:
        """Every option item has the complete closed descriptor shape."""
        response = {
            "schema_version": 4,
            "command": "root",
            "version": "0.2.0",
            "exit_code": 0,
            "usage": "docenhance COMMAND",
            "options": [],
        }
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        self.assertFalse(validator.is_valid(response | {"options": [{"name": "--help"}]}))

    def test_cancellation_cannot_claim_completion_or_uncertainty(self) -> None:
        """Cancellation has its own status; ambiguous publication is a different outcome."""
        response = {
            "schema_version": 4,
            "command": "process",
            "version": "0.3.0",
            "exit_code": 130,
            "publication": "not_started",
            "error": {"code": "E_CANCELLED", "message": "Cancelled"},
        }
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        validator.validate(response | {"publication": "not_published"})
        validator.validate(response | {"command": "verify"})
        self.assertFalse(
            validator.is_valid(response | {"command": "verify", "publication": "not_published"})
        )
        for change in (
            {"publication": "completed"},
            {"publication": "unknown"},
            {"command": "methods"},
            {"exit_code": 0},
            {"exit_code": 7},
        ):
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(response | change))

    def test_readonly_and_discovery_failures_cannot_claim_publication(self) -> None:
        """Only processing failures can describe staging, commit or uncertainty."""
        response: dict[str, Any] = {
            "schema_version": 4,
            "command": "verify",
            "version": "0.5.0",
            "exit_code": 3,
            "publication": "not_started",
            "error": {"code": "E_INPUT", "message": "Refused input"},
        }
        validator = Draft202012Validator(SCHEMA)
        for command in ("verify", "root", "methods", "version"):
            validator.validate(response | {"command": command})
            for publication in ("not_published", "completed", "unknown"):
                with self.subTest(command=command, publication=publication):
                    self.assertFalse(
                        validator.is_valid(
                            response | {"command": command, "publication": publication}
                        )
                    )

    def test_verification_success_requires_bounded_confined_identity(self) -> None:
        """Empty/unbounded inventories, zero identities and escaping paths are refused."""
        response: dict[str, Any] = {
            "schema_version": 4,
            "command": "verify",
            "version": "0.5.0",
            "exit_code": 0,
            "directory": "bundle",
            "run": "a" * 32,
            "recorded": "2026-10-02T00:00:00Z",
            "establishes": "artifacts_agree_with_record",
            "confirmed": [{"path": "result.png", "sha256": "b" * 64, "bytes": 1}],
        }
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        changes: list[dict[str, Any]] = [
            {"directory": ""},
            {"recorded": "yesterday"},
            {"confirmed": []},
            {"confirmed": response["confirmed"] * 17},
        ]
        for change in changes:
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(response | change))
        for path in (
            "",
            "/foreign",
            "../foreign",
            "a/../foreign",
            "a//b",
            "a/",
            "a\\b",
            "a:b",
            "a\0b",
        ):
            altered = copy.deepcopy(response)
            altered["confirmed"][0]["path"] = path
            with self.subTest(path=path):
                self.assertFalse(validator.is_valid(altered))
        altered = copy.deepcopy(response)
        altered["confirmed"][0]["bytes"] = 0
        self.assertFalse(validator.is_valid(altered))
