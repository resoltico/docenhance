# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Method identities have one authoring source and require an actual compiled implementation."""

from __future__ import annotations

import copy
import json
import unittest
from typing import Any

from tools_path import ROOT

from jsonschema import Draft202012Validator

import method_metadata


class MethodMetadataTests(unittest.TestCase):
    """Reject identity/selector drift and schema payloads with invented method versions."""

    @staticmethod
    def entry() -> dict[str, Any]:
        """Return a minimal implemented method for independent generator-negative cases."""
        return {
            "id": "B02",
            "selector": "sauvola",
            "family": "binarization",
            "method_version": 1,
            "status": "implemented",
            "arguments": ["--binarize"],
        }

    def test_bad_identity_and_option_declarations_are_rejected(self) -> None:
        """An authoring flag or typo alone cannot silently extend the generated capabilities."""
        for change in (
            {"id": ""},
            {"id": 2},
            {"id": "bad"},
            {"family": "unsupported"},
            {"method_version": True},
            {"method_version": 0},
            {"method_version": 1.5},
            {"status": "available"},
            {"selector": ""},
            {"selector": "a-b"},
            {"arguments": ["--absent"]},
            {"arguments": ["--output-mode", "bw", "--binarize", "--binarize"]},
            {"arguments": "--binarize"},
        ):
            with self.subTest(change=change), self.assertRaises(ValueError):
                method_metadata.implemented([self.entry() | change], [{"name": "--binarize"}])

    def test_duplicate_ids_and_selectors_are_rejected(self) -> None:
        """Neither an ID nor an implemented selector can refer to two methods."""
        entry = self.entry()
        for second in (entry, entry | {"id": "B03"}):
            with self.subTest(second=second), self.assertRaises(ValueError):
                method_metadata.implemented([entry, second], [{"name": "--binarize"}])
        with self.assertRaises(ValueError):
            method_metadata.implemented([entry | {"status": "not-implemented"}], [])

    def test_only_implemented_entries_generate_descriptors(self) -> None:
        """Planned method arguments need not be CLI capabilities and never enter the catalog."""
        active, planned = self.entry(), self.entry() | {"id": "B01", "status": "not-implemented"}
        planned["arguments"] = ["--future-option"]
        result = method_metadata.implemented([active, planned], [{"name": "--binarize"}])
        self.assertEqual(result, [active])
        header = method_metadata.render_catalog(result)
        self.assertIn('"B02"', header)
        self.assertNotIn('"B01"', header)

    def test_schema_binds_id_and_version_and_rejects_duplicate_capabilities(self) -> None:
        """The wire schema rejects a success attributed to a different or unknown implementation."""
        schema = json.loads((ROOT / "schemas/command-response.schema.json").read_text())
        validator = Draft202012Validator(schema)
        response = {
            "schema_version": 1,
            "command": "process",
            "version": "0.3.0",
            "exit_code": 0,
            "method": "B02",
            "method_version": 1,
            "output": "result/result.png",
            "publication": "completed",
        }
        for identity in ("B02", "B03"):
            validator.validate(response | {"method": identity})
        for change in (
            {"method": "B01"},
            {"method": "I01"},
            {"method_version": 2},
            {"method_version": True},
        ):
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(response | change))
        missing = copy.deepcopy(response)
        del missing["method_version"]
        self.assertFalse(validator.is_valid(missing))
        capabilities: dict[str, Any] = {
            "schema_version": 1,
            "command": "methods",
            "version": "0.3.0",
            "exit_code": 0,
            "methods": [{"id": "B02", "method_version": 1}],
            "supported_formats": ["png"],
        }
        validator.validate(capabilities)
        capabilities["methods"] *= 2
        self.assertFalse(validator.is_valid(capabilities))
