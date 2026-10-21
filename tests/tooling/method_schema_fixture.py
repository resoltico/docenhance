# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual native schema selection helpers shared by discovery and wire ownership tests."""

from __future__ import annotations

import json
import unittest
from typing import Any

from tools_path import ROOT

from jsonschema import Draft202012Validator


def native_schema(name: str) -> dict[str, Any]:
    """Read shipped emitter artifacts; CTest schemas-current separately binds their native bytes."""
    schema: dict[str, Any] = json.loads((ROOT / "schemas" / name).read_text())
    Draft202012Validator.check_schema(schema)
    return schema


class MethodSchemaCase(unittest.TestCase):
    """Select the actual emitted method Fields alternatives for independent receiver controls."""

    def alternative(self, schema: dict[str, Any], key: str, value: str) -> dict[str, Any]:
        """Select exactly one actual inline Fields alternative, without a parallel wire catalog."""
        matches: list[dict[str, Any]] = []
        if schema.get("properties", {}).get(key, {}).get("const") == value:
            matches.append(schema)
        for keyword in ("oneOf", "anyOf"):
            for branch in schema.get(keyword, []):
                if branch.get("properties", {}).get(key, {}).get("const") == value:
                    matches.append(branch)
                elif "oneOf" in branch or "anyOf" in branch:
                    matches.append(self.alternative(branch, key, value))
        self.assertEqual(len(matches), 1, (key, value))
        return matches[0]

    def response_branch(self, command: str, field: str) -> dict[str, Any]:
        """A concrete payload key distinguishes success, help and error for one command."""
        branches: list[dict[str, Any]] = native_schema("command-response.schema.json")["oneOf"]
        selected = [
            branch
            for branch in branches
            if branch["properties"]["command"]["const"] == command and field in branch["properties"]
        ]
        self.assertEqual(len(selected), 1, (command, field))
        return selected[0]

    def request_schema(self, family: str) -> dict[str, Any]:
        """Persistent request Fields are nested unchanged in the successful response."""
        run = native_schema("run-record.schema.json")
        response = self.response_branch("process", "record")
        actual: dict[str, Any] = run["properties"]["request"]["properties"][family]
        self.assertEqual(
            actual,
            response["properties"]["record"]["properties"]["request"]["properties"][family],
        )
        return actual

    def selected_request(
        self, family: str, method: str, parameters: dict[str, Any]
    ) -> dict[str, Any]:
        """Use actual emitted identity/selector literals with independent numeric test values."""
        schema = self.alternative(self.request_schema(family), "method", method)
        properties = schema["properties"]
        reviewed = json.loads((ROOT / "spec/method-contract.json").read_text())
        authored = next(
            entry
            for owner in reviewed["families"]
            for entry in owner["methods"]
            if entry["id"] == method
        )
        self.assertEqual(properties["version"]["const"], authored["version"])
        return {
            "method": method,
            "version": properties["version"]["const"],
            "selector": properties["selector"]["enum"][0],
            "parameters": parameters,
        }
