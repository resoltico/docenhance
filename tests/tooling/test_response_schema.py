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


def response_branch(command: str, member: str) -> dict[str, Any]:
    """Select the native-emitted closed shape; this is schema testing, not CLI evidence."""
    return next(
        branch
        for branch in SCHEMA["oneOf"]
        if branch["properties"]["command"]["const"] == command and member in branch["properties"]
    )


def header(command: str, exit_code: int) -> dict[str, Any]:
    """Use the emitted compiled version while challenging envelope semantics independently."""
    branch = SCHEMA["oneOf"][0]["properties"]
    return {
        "schema_version": 15,
        "command": command,
        "version": branch["version"]["const"],
        "exit_code": exit_code,
    }


def methods_response() -> dict[str, Any]:
    """Use the actual B03 singleton metadata as a positive schema mutation fixture."""
    fields = response_branch("methods", "methods")["properties"]
    methods = next(
        value for value in fields["methods"]["enum"] if len(value) == 1 and value[0]["id"] == "B03"
    )
    return header("methods", 0) | {
        "methods": copy.deepcopy(methods),
        "supported_formats": copy.deepcopy(fields["supported_formats"]["const"]),
        "input_support": copy.deepcopy(fields["input_support"]["const"]),
    }


def failure_context() -> dict[str, Any]:
    """Synthetic unstarted fields; no runtime acquisition or measurement is claimed."""
    return {"failed_stage": None, "completed": {}, "diagnostics": None, "warnings": []}


class ResponseSchemaTests(unittest.TestCase):
    """Payload shape, array items, integer semantics and exit-code correspondence are enforced."""

    def test_schema_is_valid(self) -> None:
        """The schema itself and its positive fixture are accepted by Draft 2020-12."""
        Draft202012Validator.check_schema(SCHEMA)
        Draft202012Validator(SCHEMA).validate(methods_response())

    def test_current_unstarted_shape_and_obsolete_real_fixture(self) -> None:
        """Current synthetic absence is closed; retained old real bytes stay obsolete."""
        old = json.loads((ROOT / "tests/fixtures/response/unstarted-continuous.json").read_bytes())
        response = (
            header("process", 3)
            | failure_context()
            | {
                "publication": "not_started",
                "error": {"code": "E_INPUT", "message": "Refused input"},
            }
        )
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        self.assertFalse(validator.is_valid(old))
        for family in ("illumination", "denoising", "restoration", "contrast", "sharpening"):
            for field, value in (
                ("progress", "completed"),
                ("eligible_samples", 1),
                ("complete", True),
            ):
                altered = copy.deepcopy(response)
                altered[family] = {field: value}
                with self.subTest(family=family, field=field):
                    self.assertFalse(validator.is_valid(altered))
        altered = copy.deepcopy(response)
        altered["completed"]["illumination"] = {"gain_capped_fraction": 0}
        self.assertFalse(validator.is_valid(altered))

    def test_bad_envelopes_are_rejected(self) -> None:
        """A bare envelope, stale command, mixed payload and boolean integer all fail."""
        base = methods_response()
        mutations: list[dict[str, Any]] = [
            {"command": "plan"},
            {"schema_version": True},
            {"schema_version": 1},
            {"schema_version": 2},
            {"schema_version": 3},
            {"schema_version": 4},
            {"schema_version": 5},
            {"schema_version": 6},
            {"schema_version": 7},
            {"schema_version": 8},
            {"schema_version": 9},
            {"schema_version": 10},
            {"schema_version": 11},
            {"schema_version": 12},
            {"schema_version": 13},
            {"schema_version": 14},
            {"version": "garbage"},
            {"version": base["version"] + "\n"},
            {"exit_code": True},
            {"methods": [base["methods"][0] | {"method_version": True}]},
            {"methods": [base["methods"][0] | {"invented": 1}]},
            {"methods": ["B03"]},
            {"supported_formats": ["jpeg"]},
            {"output": "foreign-payload"},
            {"error": {"code": "E_INPUT", "message": "bad"}},
        ]
        validator = Draft202012Validator(SCHEMA)
        change: dict[str, Any]
        for change in mutations:
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(base | change))
        for name in ("methods", "supported_formats"):
            missing = copy.deepcopy(base)
            del missing[name]
            self.assertFalse(validator.is_valid(missing))

    def test_uncertainty_cause_does_not_replace_actual_publication_fact(self) -> None:
        """The typed cause retains exit7; processing publication independently records effects."""
        response = (
            header("process", 7)
            | failure_context()
            | {
                "publication": "unknown",
                "error": {"code": "E_PUBLICATION_UNKNOWN", "message": "Inspect output"},
            }
        )
        validator = Draft202012Validator(SCHEMA)
        for publication in ("not_started", "not_published", "completed", "unknown"):
            validator.validate(response | {"publication": publication})
        self.assertFalse(validator.is_valid(response | {"exit_code": 5}))
        self.assertFalse(validator.is_valid(response | {"command": "verify"}))

    def test_non_cancelled_process_causes_admit_independent_publication(self) -> None:
        """A machine response must not infer actual effects from a general error category."""
        causes = (
            ("E_ARGUMENT", 2),
            ("E_INPUT", 3),
            ("E_RESOURCE", 4),
            ("E_OUTPUT", 5),
            ("E_OUTPUT_VERIFY", 5),
            ("E_PUBLICATION_UNKNOWN", 7),
            ("E_NOT_IMPLEMENTED", 4),
            ("E_METHOD_INAPPLICABLE", 4),
            ("E_NUMERICAL", 4),
            ("E_INVARIANT", 8),
        )
        validator = Draft202012Validator(SCHEMA)
        for code, exit_code in causes:
            response = (
                header("process", exit_code)
                | failure_context()
                | {"error": {"code": code, "message": "Synthetic cause, independent effect"}}
            )
            for publication in ("not_started", "not_published", "completed", "unknown"):
                with self.subTest(code=code, publication=publication):
                    validator.validate(response | {"publication": publication})
                    self.assertFalse(
                        validator.is_valid(response | {"publication": publication, "exit_code": 0})
                    )
        unknown = (
            header("process", 8)
            | failure_context()
            | {"publication": "unknown", "error": {"code": "E_INVENTED", "message": "Unknown"}}
        )
        self.assertFalse(validator.is_valid(unknown))

    def test_integrity_or_resource_failure_retains_the_actual_publication(self) -> None:
        """Confirmed completion and uncertainty remain representable after a later failure."""
        validator = Draft202012Validator(SCHEMA)
        for code, exit_code in (("E_OUTPUT_VERIFY", 5), ("E_RESOURCE", 4)):
            response = (
                header("process", exit_code)
                | failure_context()
                | {"publication": "completed", "error": {"code": code, "message": "Retain effects"}}
            )
            validator.validate(response)
            validator.validate(response | {"publication": "unknown"})
            self.assertFalse(validator.is_valid(response | {"command": "verify"}))

    def test_help_options_are_typed(self) -> None:
        """Every option item has the complete closed descriptor shape."""
        response = {
            "schema_version": 15,
            "command": "root",
            "version": header("root", 0)["version"],
            "exit_code": 0,
            "usage": response_branch("root", "usage")["properties"]["usage"]["const"],
            "options": copy.deepcopy(
                response_branch("root", "usage")["properties"]["options"]["const"]
            ),
        }
        if "error" in response:
            response.update(failure_context())
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        self.assertFalse(validator.is_valid(response | {"options": [{"name": "--help"}]}))

    def test_cancellation_cannot_claim_completion_or_uncertainty(self) -> None:
        """Only cancellation has the shared publication restriction; readonly work is empty."""
        response = (
            header("process", 130)
            | failure_context()
            | {
                "publication": "not_started",
                "error": {"code": "E_CANCELLED", "message": "Cancelled"},
            }
        )
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        validator.validate(response | {"publication": "not_published"})
        for command in ("verify", "root", "methods", "version"):
            validator.validate(response | {"command": command})
            self.assertFalse(
                validator.is_valid(response | {"command": command, "publication": "not_published"})
            )
        change: dict[str, Any]
        for change in (
            {"publication": "completed"},
            {"publication": "unknown"},
            {"exit_code": 0},
            {"exit_code": 7},
        ):
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(response | change))

    def test_readonly_and_discovery_failures_cannot_claim_publication(self) -> None:
        """Only processing failures can describe staging, commit or uncertainty."""
        response: dict[str, Any] = {
            "schema_version": 15,
            "command": "verify",
            "version": header("root", 0)["version"],
            "exit_code": 3,
            "publication": "not_started",
            "error": {"code": "E_INPUT", "message": "Refused input"},
        }
        response.update(failure_context())
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
            "schema_version": 15,
            "command": "verify",
            "version": header("root", 0)["version"],
            "exit_code": 0,
            "directory": "bundle",
            "run": "a" * 32,
            "recorded": "2026-10-02T00:00:00Z",
            "establishes": "artifacts_agree_with_record",
            "confirmed": [
                {"path": "result.png", "sha256": "b" * 64, "bytes": 1},
                {"path": "run.json", "sha256": "c" * 64, "bytes": 1},
            ],
        }
        if "error" in response:
            response.update(failure_context())
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        changes: list[dict[str, Any]] = [
            {"directory": ""},
            {"recorded": "yesterday"},
            {"confirmed": []},
            {"confirmed": response["confirmed"][:1]},
            {"confirmed": response["confirmed"] * 17},
        ]
        change: dict[str, Any]
        for change in changes:
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(response | change))
        for path in (
            "",
            "/foreign",
            "../foreign",
            "a/../foreign",
            "a//b",
            "a\n//b",
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

    def test_failed_stage_order_is_distinct_from_general_error_publication(self) -> None:
        """A real typed earlier fact cannot appear at or after the reported failed family."""
        gamma = {
            "method": "C02",
            "disposition": "no_change",
            "reason": "identity_gamma",
            "counts": {"eligible": 1, "protected": 0, "evaluated": 0, "changed": 0},
        }
        response = (
            header("process", 4)
            | failure_context()
            | {
                "publication": "not_started",
                "failed_stage": "sharpening",
                "completed": {"contrast": gamma},
                "error": {"code": "E_RESOURCE", "message": "Synthetic later stage refusal"},
            }
        )
        validator = Draft202012Validator(SCHEMA)
        validator.validate(response)
        change: dict[str, Any]
        for change in (
            {"failed_stage": "contrast"},
            {"failed_stage": "illumination"},
            {"publication": "completed"},
        ):
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(response | change))
        readonly = (
            header("verify", 4)
            | failure_context()
            | {"publication": "not_started", "error": response["error"]}
        )
        validator.validate(readonly)
        for change in (
            {"completed": {"contrast": gamma}},
            {"failed_stage": "sharpening"},
            {"warnings": ["W_DEPTH_REDUCED"]},
            {"diagnostics": {"threads": 1, "memory_limit_mib": 1024, "charged_peak_bytes": 1}},
        ):
            with self.subTest(readonly_change=change):
                self.assertFalse(validator.is_valid(readonly | change))
