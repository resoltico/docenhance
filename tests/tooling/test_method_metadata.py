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
                method_metadata.implemented(
                    [self.entry() | change], [{"name": "--binarize", "methods": "B02"}]
                )

    def test_duplicate_ids_and_selectors_are_rejected(self) -> None:
        """Neither an ID nor an implemented selector can refer to two methods."""
        entry = self.entry()
        for second in (entry, entry | {"id": "B03"}):
            with self.subTest(second=second), self.assertRaises(ValueError):
                method_metadata.implemented(
                    [entry, second], [{"name": "--binarize", "methods": "B02"}]
                )
        with self.assertRaises(ValueError):
            method_metadata.implemented([entry | {"status": "not-implemented"}], [])

    def test_only_implemented_entries_generate_descriptors(self) -> None:
        """Planned method arguments need not be CLI capabilities and never enter the catalog."""
        active, planned = self.entry(), self.entry() | {"id": "B01", "status": "not-implemented"}
        planned["arguments"] = ["--future-option"]
        result = method_metadata.implemented(
            [active, planned], [{"name": "--binarize", "methods": "B02"}]
        )
        self.assertEqual(result, [active])
        header = method_metadata.render_catalog(result)
        self.assertIn('"B02"', header)
        self.assertNotIn('"B01"', header)

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
            "schema_version": 11,
            "command": "process",
            "version": "0.3.0",
            "exit_code": 0,
            "method": "B02",
            "binarization": None,
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
            "schema_version": 11,
            "command": "methods",
            "version": "0.3.0",
            "exit_code": 0,
            "methods": [{"id": "B02", "method_version": 1}],
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
                "complete": True,
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

    def test_option_attribution_is_bidirectional(self) -> None:
        """Unknown, duplicated, missing and contradictory method attributions fail."""
        for attribution in ("", "B03", "B02,B02", "B02,B99"):
            with self.subTest(attribution=attribution), self.assertRaises(ValueError):
                method_metadata.implemented(
                    [self.entry()], [{"name": "--binarize", "methods": attribution}]
                )
        with self.assertRaises(ValueError):
            method_metadata.implemented(
                [self.entry()],
                [{"name": "--binarize", "methods": "B02"}, {"name": "--extra", "methods": "B02"}],
            )

    def test_format_support_requires_complete_unique_admission(self) -> None:
        """No absent, duplicate or loosely typed format policy can generate capabilities."""
        for support in (
            [],
            [{"format": "png"}],
            [{"format": "png", "binary": 1}],
            [{"format": "png", "binary": True}] * 2,
        ):
            with self.subTest(support=support), self.assertRaises(ValueError):
                method_metadata.support_matrix(support)

    def test_denoising_schema_follows_reviewed_identity_and_selection(self) -> None:
        """Changing the reviewed D01 version updates both schemas; off cannot carry settings."""
        methods = json.loads((ROOT / "spec/method-contract.json").read_text())["methods"]
        contract = json.loads((ROOT / "spec/cli-contract.json").read_text())
        for entry in methods:
            if entry["id"] == "D01":
                entry["method_version"] = 2
        outputs = method_metadata.metadata_outputs(
            ROOT, methods, contract["options"], contract["input_support"]
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
