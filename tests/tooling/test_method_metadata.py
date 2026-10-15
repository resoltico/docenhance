# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Method identities have one authoring source and require an actual compiled implementation."""

from __future__ import annotations

import copy
import json
import unittest
from typing import Any

from tools_path import ROOT

from jsonschema import Draft202012Validator
from parameter_fixtures import contracts

import generate_spec
import method_metadata
from parameter_contract import merged_options


class MethodMetadataTests(unittest.TestCase):
    """Reject identity/selector drift and schema payloads with invented method versions."""

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
        for change in changes:
            cli, methods = contracts()
            methods["families"][0]["methods"][0].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                merged_options(cli, methods)

    def test_discovery_metadata_is_closed_without_enriching_record_identity(self) -> None:
        """A current method exposes its domains, while v14 records retain id/version only."""
        generated = generate_spec.outputs()
        response = json.loads(generated[ROOT / "schemas/command-response.schema.json"])
        record = json.loads(generated[ROOT / "schemas/run-record.schema.json"])
        identity = {"id": "R01", "method_version": 1}
        for schema in (response, record):
            validator = Draft202012Validator(
                {"$defs": schema["$defs"], "$ref": "#/$defs/restoration_method"}
            )
            validator.validate(identity)
            self.assertFalse(validator.is_valid(identity | {"name": "invented"}))
        validator = Draft202012Validator(response["$defs"]["catalog_entry"])
        entry = next(
            item["const"]
            for item in response["$defs"]["catalog_entry"]["oneOf"]
            if item["const"]["id"] == "I01"
        )
        validator.validate(entry)
        self.assertEqual(entry["selectors"], ["surface", "auto"])
        self.assertFalse(validator.is_valid(identity))
        self.assertFalse(validator.is_valid(entry | {"parameters": []}))
        self.assertFalse(validator.is_valid(entry | {"method_version": 2}))
        self.assertFalse(validator.is_valid(entry | {"invented": True}))
        self.assertNotIn(ROOT / "docs/roadmap.md", generated)

    def test_otsu_observations_are_closed_and_shared_with_records(self) -> None:
        """Both schemas derive one bounded fit shape and reject impossible fallback facts."""
        response = json.loads((ROOT / "schemas/command-response.schema.json").read_text())
        record = json.loads((ROOT / "schemas/run-record.schema.json").read_text())
        self.assertEqual(response["$defs"]["otsu_observation"], record["$defs"]["otsu_observation"])
        validator = Draft202012Validator(response["$defs"]["otsu_observation"])
        for threshold, fallback in ((0, False), (4094, False), (2047, True)):
            validator.validate({"threshold_bin": threshold, "single_bin_fallback": fallback})
        valid = {"threshold_bin": 2047, "single_bin_fallback": True}
        for change in (
            {"threshold_bin": -1},
            {"threshold_bin": 4095},
            {"threshold_bin": True},
            {"threshold_bin": 2046},
            {"single_bin_fallback": "true"},
            {"unknown": 0},
        ):
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(valid | change))
        self.assertFalse(validator.is_valid({"threshold_bin": 2047}))

    def test_schema_binds_id_and_version_and_rejects_duplicate_capabilities(self) -> None:
        """The wire schema rejects a success attributed to a different or unknown implementation."""
        schema = json.loads((ROOT / "schemas/command-response.schema.json").read_text())
        validator = Draft202012Validator(schema)
        response = {
            "schema_version": 15,
            "command": "process",
            "version": "0.3.0",
            "exit_code": 0,
            "method": "B02",
            "binarization": None,
            "rotation_degrees": 0,
            "method_version": 1,
            "output": "result/result.png",
            "publication": "completed",
            "source_decoding": None,
            "record": {
                "run": "0" * 32,
                "path": "run.json",
                "sha256": "0" * 64,
                "bytes": 512,
            },
        }
        for identity in ("B02", "B03"):
            validator.validate(response | {"method": identity})
        validator.validate(
            response
            | {
                "method": "B01",
                "binarization": {"threshold_bin": 2047, "single_bin_fallback": True},
            }
        )
        for change in (
            {"method": "B01"},
            {"binarization": {"threshold_bin": 2047, "single_bin_fallback": True}},
            {"method": "B00"},
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
            "schema_version": 15,
            "command": "methods",
            "version": "0.3.0",
            "exit_code": 0,
            "methods": [
                next(
                    item["const"]
                    for item in schema["$defs"]["catalog_entry"]["oneOf"]
                    if item["const"]["id"] == "B02"
                )
            ],
            "supported_formats": ["png", "jpeg", "tiff"],
            "input_support": [
                {"format": "png", "output_modes": ["preserve", "gray", "bw"]},
                {"format": "jpeg", "output_modes": ["preserve", "gray"]},
                {"format": "tiff", "output_modes": ["preserve", "gray"]},
            ],
        }
        validator.validate(capabilities)
        capabilities["methods"] *= 2
        self.assertFalse(validator.is_valid(capabilities))

    def test_sharpening_request_schema_requires_closed_parameters(self) -> None:
        """S01 requests cannot borrow another method's fields or use invalid parameter domains."""
        schema = json.loads((ROOT / "schemas/command-response.schema.json").read_text())
        validator = Draft202012Validator(
            {"$defs": schema["$defs"], "$ref": "#/$defs/sharpening_request"}
        )
        request: dict[str, Any] = {
            "method": {"id": "S01", "method_version": 1},
            "parameters": {"sigma": 0.8, "amount": 0.5, "threshold": 1},
        }
        validator.validate(request)
        for change in ({"sigma": 0.29}, {"amount": 2.1}, {"threshold": 20.1}, {"blend": 1}):
            altered = copy.deepcopy(request)
            altered["parameters"].update(change)
            with self.subTest(change=change):
                self.assertFalse(validator.is_valid(altered))
        self.assertFalse(validator.is_valid({"method": None, "parameters": request["parameters"]}))

    def test_sharpening_report_schema_requires_amount_based_warning(self) -> None:
        """Applied and bypassed positive requests warn; zero amount cannot carry that warning."""
        for filename in ("command-response.schema.json", "run-record.schema.json"):
            schema = json.loads((ROOT / "schemas" / filename).read_text())
            validator = Draft202012Validator(
                {"$defs": schema["$defs"], "$ref": "#/$defs/sharpening"}
            )
            report: dict[str, Any] = {
                "method": {"id": "S01", "method_version": 1},
                "parameters": {"sigma": 0.8, "amount": 0.5, "threshold": 1},
                "status": "no_change",
                "reason": "no_eligible_samples",
                "progress": "completed",
                "representation": "float64",
                "pre_clamp": None,
                "clipped_fraction": None,
                "warnings": ["W_SHARPENING"],
                "eligible_samples": 0,
                "protected_samples": 1,
                "context_samples": 0,
                "evaluated_samples": 0,
                "corrected_samples": 0,
                "changed_samples": 0,
                "clipped_low_samples": 0,
                "clipped_high_samples": 0,
                "preparation_charge_peak": 0,
            }
            with self.subTest(filename=filename):
                validator.validate(report)
                for warnings in ([], ["W_SHARPENING", "W_SHARPENING"], ["W_CONTRAST"]):
                    self.assertFalse(validator.is_valid(report | {"warnings": warnings}))
                zero = copy.deepcopy(report)
                zero["parameters"]["amount"] = 0
                zero["reason"] = "zero_amount"
                zero["warnings"] = []
                validator.validate(zero)
                self.assertFalse(validator.is_valid(zero | {"warnings": ["W_SHARPENING"]}))
                for method in (
                    {"id": "C03", "method_version": 1},
                    {"id": "S01", "method_version": 2},
                ):
                    self.assertFalse(validator.is_valid(report | {"method": method}))

    def test_format_support_requires_complete_unique_admission(self) -> None:
        """No absent, duplicate or loosely typed format policy can generate capabilities."""
        for support in (
            [],
            [{"format": "png"}],
            [{"format": "png", "modes": ["preserve", "invented"]}],
            [{"format": "png", "modes": ["preserve", "gray"]}] * 2,
        ):
            with self.subTest(support=support), self.assertRaises(ValueError):
                method_metadata.support_matrix(support)

    def test_denoising_schema_follows_reviewed_identity_and_selection(self) -> None:
        """Changing the reviewed D01 version updates both schemas; off cannot carry settings."""
        families = json.loads((ROOT / "spec/method-contract.json").read_text())["families"]
        contract = json.loads((ROOT / "spec/cli-contract.json").read_text())
        for family in families:
            for entry in family["methods"]:
                if entry["id"] == "D01":
                    entry["version"] = 2
        outputs = method_metadata.metadata_outputs(
            ROOT,
            families,
            merged_options(contract, {"schema_version": 3, "families": families}),
            contract["formats"],
        )
        request: dict[str, Any] = {
            "method": {"id": "D01", "method_version": 2},
            "parameters": {"h": 3, "patch": 7, "search": 21, "blend": 0.5},
        }
        for filename in ("command-response.schema.json", "run-record.schema.json"):
            schema = json.loads(outputs[ROOT / "schemas" / filename])
            validator = Draft202012Validator(
                {"$defs": schema["$defs"], "$ref": "#/$defs/denoising_request"}
            )
            validator.validate(request)
            validator.validate({"method": None, "parameters": None})
            for change in (
                {"method": None},
                {"parameters": None},
                {"method": {"id": "D01", "method_version": 1}},
            ):
                with self.subTest(filename=filename, change=change):
                    self.assertFalse(validator.is_valid(request | change))

    def test_clahe_request_is_closed_and_bound_to_its_method(self) -> None:
        """C03 cannot borrow gamma parameters or accept grid values outside the typed contract."""
        schema = json.loads((ROOT / "schemas/command-response.schema.json").read_text())
        validator = Draft202012Validator(
            {"$ref": "#/$defs/contrast_request", "$defs": schema["$defs"]}
        )
        request: dict[str, Any] = {
            "method": {"id": "C03", "method_version": 1},
            "parameters": {"grid_columns": 8, "grid_rows": 8, "clip": 2, "blend": 1},
        }
        validator.validate(request)
        for change in (
            {"grid_columns": True},
            {"grid_rows": 1},
            {"grid_columns": 33},
            {"grid_rows": 2.5},
            {"clip": 0.9},
            {"clip": 8.1},
            {"blend": -1},
            {"gamma": 1},
        ):
            with self.subTest(change=change):
                self.assertFalse(
                    validator.is_valid(request | {"parameters": request["parameters"] | change})
                )
        self.assertFalse(validator.is_valid(request | {"parameters": {"gamma": 1, "blend": 1}}))
        self.assertFalse(
            validator.is_valid(request | {"method": {"id": "C02", "method_version": 1}})
        )

    def test_restoration_request_preserves_closed_psf_alternatives(self) -> None:
        """R01 fields are complete, kind-specific, and mandatory even at zero blend."""
        schema = json.loads((ROOT / "schemas/command-response.schema.json").read_text())
        validator = Draft202012Validator(
            {"$ref": "#/$defs/restoration_request", "$defs": schema["$defs"]}
        )
        base: dict[str, Any] = {
            "method": {"id": "R01", "method_version": 1},
            "parameters": {"psf": {"kind": "gaussian", "sigma": 1}, "k": 0.01, "blend": 0},
        }
        validator.validate(base)
        for psf in (
            {"kind": "motion", "length": 5, "angle": -37},
            {"kind": "kernel", "path": "raw.png"},
        ):
            validator.validate(base | {"parameters": base["parameters"] | {"psf": psf}})
        for change in (
            {"psf": {"kind": "gaussian"}},
            {"psf": {"kind": "gaussian", "sigma": 0.29}},
            {"psf": {"kind": "motion", "length": 5, "angle": 0, "sigma": 1}},
            {"psf": {"kind": "kernel", "path": ""}},
            {"psf": {"kind": "kernel", "path": "a\0b"}},
            {"k": 0},
            {"k": 1.01},
            {"blend": -0.1},
            {"blind": True},
        ):
            with self.subTest(change=change):
                self.assertFalse(
                    validator.is_valid(base | {"parameters": base["parameters"] | change})
                )
        self.assertFalse(validator.is_valid(base | {"method": None}))
        self.assertFalse(validator.is_valid(base | {"method": {"id": "R01", "method_version": 2}}))
        record = json.loads((ROOT / "schemas/run-record.schema.json").read_text())
        self.assertEqual(record["$defs"]["restoration_psf"], schema["$defs"]["restoration_psf"])
