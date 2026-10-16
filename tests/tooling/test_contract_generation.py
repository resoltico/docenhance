# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Authored fields, derived relations and independently owned envelope versions stay closed."""

from __future__ import annotations

import copy
import json
import shutil
import tempfile
import unittest
from pathlib import Path
from typing import override
from unittest.mock import patch

from tools_path import ROOT

import generate_spec


class ContractGenerationTests(unittest.TestCase):
    """Exercise actual generator owners using private reviewed contract copies."""

    @override
    def setUp(self) -> None:
        """Each control keeps checked-in schemas and generated consumers untouched."""
        temporary = tempfile.TemporaryDirectory(prefix="docenhance-contract-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        shutil.copytree(ROOT / "spec", self.root / "spec")
        shutil.copy2(ROOT / ".clang-format", self.root / ".clang-format")
        self.contract = json.loads((self.root / "spec/cli-contract.json").read_bytes())
        self.methods = json.loads((self.root / "spec/method-contract.json").read_bytes())
        owner = patch.object(generate_spec, "ROOT", self.root)
        owner.start()
        self.addCleanup(owner.stop)

    def write(self, name: str, value: dict[str, object]) -> None:
        """Write only the private authoring contract under test."""
        (self.root / "spec" / name).write_text(json.dumps(value))

    def test_source_options_reject_obsolete_extra_missing_and_nonstring_fields(self) -> None:
        """Derived attribution never licenses stale or ignored source declarations."""
        for change in ({"methods": "B01"}, {"invented": "ignored"}, {"summary": 1}):
            altered = copy.deepcopy(self.contract)
            altered["options"][0].update(change)
            self.write("cli-contract.json", altered)
            with self.subTest(change=change), self.assertRaises(ValueError):
                generate_spec.outputs()
        altered = copy.deepcopy(self.contract)
        del altered["options"][0]["kind"]
        self.write("cli-contract.json", altered)
        with self.assertRaises(ValueError):
            generate_spec.outputs()

    def test_bindings_scopes_and_option_identities_remain_checked(self) -> None:
        """One attribution authority does not remove typed CLI structure validation."""
        for field, value in (("binding", "bad-binding"), ("scope", ["unknown"])):
            altered = copy.deepcopy(self.contract)
            altered["options"][0][field] = value
            self.write("cli-contract.json", altered)
            with self.subTest(field=field), self.assertRaises(ValueError):
                generate_spec.outputs()
        for field in ("name",):
            altered = copy.deepcopy(self.contract)
            altered["options"][1][field] = altered["options"][0][field]
            self.write("cli-contract.json", altered)
            with self.subTest(field=field), self.assertRaises(ValueError):
                generate_spec.outputs()

    def test_argument_edit_updates_derived_consumers_without_authoring_cli_relations(self) -> None:
        """A single method edit reaches descriptors/docs while syntax stays authored once."""
        before = generate_spec.outputs()
        for family in self.methods["families"]:
            for entry in family["methods"]:
                if entry["id"] == "D01":
                    entry["parameters"] = [
                        p for p in entry["parameters"] if p["option"] != "--nlm-h"
                    ]
        self.write("method-contract.json", self.methods)
        after = generate_spec.outputs()
        for relative in ("include/docenhance/app/options.hpp", "docs/cli-reference.md"):
            self.assertNotEqual(before[self.root / relative], after[self.root / relative])
        self.assertNotEqual(
            before[self.root / "fuzz/dict/cli.dict"], after[self.root / "fuzz/dict/cli.dict"]
        )
        self.assertEqual(
            json.loads((self.root / "spec/cli-contract.json").read_bytes()), self.contract
        )
        self.assertNotIn("methods", self.contract["options"][0])

    def test_envelope_versions_are_generated_independently_and_require_integers(self) -> None:
        """Response and record schema authorities can change independently."""
        record = json.loads((self.root / "spec/run-record.schema.json").read_bytes())
        response = json.loads((self.root / "spec/command-response.schema.json").read_bytes())
        record["properties"]["record"]["properties"]["version"]["const"] = 19
        response["properties"]["schema_version"]["const"] = 20
        self.write("run-record.schema.json", record)
        self.write("command-response.schema.json", response)
        outputs = generate_spec.outputs()
        self.assertIn(
            "record_version = 19U",
            outputs[self.root / "include/docenhance/record/record_version.hpp"],
        )
        self.assertIn(
            "response_schema_version = 20U",
            outputs[self.root / "include/docenhance/app/options.hpp"],
        )
        for invalid in (True, 0, -1, 1.5, "14"):
            for name in ("run-record.schema.json", "command-response.schema.json"):
                changed_record, changed_response = copy.deepcopy(record), copy.deepcopy(response)
                if name == "run-record.schema.json":
                    changed_record["properties"]["record"]["properties"]["version"]["const"] = (
                        invalid
                    )
                else:
                    changed_response["properties"]["schema_version"]["const"] = invalid
                self.write("run-record.schema.json", changed_record)
                self.write("command-response.schema.json", changed_response)
                with (
                    self.subTest(name=name, invalid=invalid),
                    self.assertRaises(ValueError),
                ):
                    generate_spec.outputs()

    def test_unchanged_generation_preserves_modification_times(self) -> None:
        """Generating already-current bytes never churns reviewed artifacts."""
        with patch("sys.argv", ["generate_spec.py"]):
            self.assertEqual(generate_spec.main(), 0)
            before = {path: path.stat().st_mtime_ns for path in generate_spec.outputs()}
            self.assertEqual(generate_spec.main(), 0)
            after = {path: path.stat().st_mtime_ns for path in generate_spec.outputs()}
        self.assertEqual(before, after)
