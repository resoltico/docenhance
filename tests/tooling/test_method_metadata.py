# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Typed discovery generation and shipped native Fields schemas have separate owners."""

from __future__ import annotations

import copy
import json
from typing import Any

from tools_path import ROOT

from jsonschema import Draft202012Validator
from method_schema_fixture import MethodSchemaCase
from parameter_fixtures import contracts

import generate_spec
import method_metadata
from parameter_contract import merged_options


class MethodMetadataTests(MethodSchemaCase):
    """Discovery binds actual compiled capabilities without authoring persistent wire schemas."""

    def test_authored_method_taxonomy_and_bad_identities_are_refused(self) -> None:
        """No authoring flag can advertise a planned or malformed executable identity."""
        changes: tuple[dict[str, Any], ...] = (
            {"id": ""},
            {"id": "bad"},
            {"version": True},
            {"version": 0},
            {"classification": "planned"},
            {"status": "implemented"},
            {"selectors": {}},
            {"selectors": {"bad-selector": {}}},
            {"selectors": {"wiener": {"mode": "planned"}}},
        )
        change: dict[str, Any]
        for change in changes:
            cli, methods = contracts()
            methods["families"][0]["methods"][0].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                merged_options(cli, methods)

    def test_discovery_generation_has_no_persistent_wire_authority(self) -> None:
        """Typed descriptors retain discovery domains; native Fields own persistent identities."""
        cli = json.loads((ROOT / "spec/cli-contract.json").read_text())
        methods = json.loads((ROOT / "spec/method-contract.json").read_text())
        outputs = method_metadata.metadata_outputs(
            ROOT, methods["families"], merged_options(cli, methods), cli["formats"]
        )
        self.assertEqual(
            set(outputs),
            {
                ROOT / "include/docenhance/methods/method_catalog.hpp",
                ROOT / "include/docenhance/methods/method_parameter_metadata.hpp",
            },
        )
        generated = generate_spec.outputs()
        for name in ("command-response.schema.json", "run-record.schema.json"):
            self.assertNotIn(ROOT / "schemas" / name, generated)
            self.assertNotIn(ROOT / "schemas" / name, outputs)
        self.assertNotIn(ROOT / "docs/roadmap.md", generated)
        request = self.selected_request(
            "restoration",
            "R01",
            {"psf": {"kind": "gaussian", "sigma": 1}, "k": 0.01, "blend": 0},
        )
        validator = Draft202012Validator(self.request_schema("restoration"))
        validator.validate(request)
        change: dict[str, Any]
        for change in ({"name": "invented"}, {"method_version": 1}, {"invented": True}):
            self.assertFalse(validator.is_valid(request | change))

    def test_compiled_capabilities_bind_complete_unique_exact_entries(self) -> None:
        """The emitted catalog accepts its actual full list or one compiled selection only."""
        schema = self.response_branch("methods", "methods")
        properties = schema["properties"]
        actual_methods = max(properties["methods"]["enum"], key=len)
        response = {
            key: properties[key]["const"]
            for key in (
                "command",
                "schema_version",
                "version",
                "exit_code",
                "supported_formats",
                "input_support",
            )
        }
        response["methods"] = copy.deepcopy(actual_methods)
        validator = Draft202012Validator(schema)
        validator.validate(response)
        entry = next(item for item in actual_methods if item["id"] == "I01")
        self.assertEqual(entry["selectors"], ["surface", "auto"])
        validator.validate(response | {"methods": [entry]})
        change: dict[str, Any]
        for change in (
            {"id": "I99"},
            {"method_version": entry["method_version"] + 1},
            {"method_version": True},
            {"parameters": []},
            {"invented": True},
        ):
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(response | {"methods": [entry | change]}))
        self.assertFalse(validator.is_valid(response | {"methods": [entry, entry]}))
        missing = copy.deepcopy(entry)
        del missing["method_version"]
        self.assertFalse(validator.is_valid(response | {"methods": [missing]}))
        for field, value in (("version", "garbage"), ("schema_version", True), ("extra", 0)):
            self.assertFalse(validator.is_valid(response | {field: value}))
