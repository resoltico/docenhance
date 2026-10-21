# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Method wire Fields retain closed parameters, observations and current version identities."""

from __future__ import annotations

import copy
import json
from typing import Any

from tools_path import ROOT

from jsonschema import Draft202012Validator
from method_schema_fixture import MethodSchemaCase, native_schema

import method_metadata
from parameter_contract import merged_options


class MethodWireMetadataTests(MethodSchemaCase):
    """Native wire templates and generated method discovery keep distinct owners."""

    def test_otsu_fields_are_closed_and_shared_with_records(self) -> None:
        """Schema checks primitives/ranges; native readers additionally bind fallback and counts."""
        run = native_schema("run-record.schema.json")
        response = self.response_branch("process", "record")
        field = run["properties"]["execution"]["properties"]["binarization"]
        response_execution = response["properties"]["record"]["properties"]["execution"]
        self.assertEqual(
            field,
            response_execution["properties"]["binarization"],
        )
        schema = self.alternative(field, "method", "B01")
        validator = Draft202012Validator(schema)
        valid = {
            "method": "B01",
            "disposition": "applied",
            "reason": "none",
            "black_samples": 0,
            "white_samples": 8,
            "threshold_bin": 2047,
            "single_bin_fallback": True,
        }
        validator.validate(valid)
        for threshold in (0, 4094):
            validator.validate(
                valid
                | {
                    "threshold_bin": threshold,
                    "single_bin_fallback": False,
                    "black_samples": 3,
                    "white_samples": 5,
                }
            )
        change: dict[str, Any]
        for change in (
            {"threshold_bin": -1},
            {"threshold_bin": 4095},
            {"threshold_bin": True},
            {"single_bin_fallback": "true"},
            {"unknown": 0},
            {"method": "B02"},
        ):
            self.assertFalse(validator.is_valid(valid | change))
        missing = copy.deepcopy(valid)
        del missing["threshold_bin"]
        self.assertFalse(validator.is_valid(missing))
        # Exact fallback2047 and one-polarity/count agreement are native-only relationships.
        # binary_fact_cases, record.cpp and binarization.cpp retain healthy/refusal controls.

    def test_sharpening_selection_requires_closed_parameters_and_identity(self) -> None:
        """S01 cannot borrow fields, omit settings or advertise another version/selector."""
        schema = self.request_schema("sharpening")
        request = self.selected_request(
            "sharpening", "S01", {"sigma": 0.8, "amount": 0.5, "threshold": 1}
        )
        validator = Draft202012Validator(schema)
        validator.validate(request)
        validator.validate(None)
        change: dict[str, Any]
        for change in (
            {"sigma": 0.29},
            {"amount": 2.1},
            {"threshold": 20.1},
            {"blend": 1},
            {"sigma": True},
        ):
            self.assertFalse(
                validator.is_valid(request | {"parameters": request["parameters"] | change})
            )
        for change in (
            {"method": None},
            {"method": "C03"},
            {"version": request["version"] + 1},
            {"version": True},
            {"selector": "clahe"},
            {"parameters": None},
        ):
            self.assertFalse(validator.is_valid(request | change))

    def test_binary_selections_bind_current_implemented_identities(self) -> None:
        """B01/B02/B03 retain exact current versions, with no top-level legacy identity."""
        validator = Draft202012Validator(self.request_schema("binarization"))
        for method, parameters in (
            ("B01", {}),
            ("B02", {"window": 31, "k": 0.2, "r": 0.5}),
            ("B03", {"threshold": 0.5}),
        ):
            request = self.selected_request("binarization", method, parameters)
            validator.validate(request)
            for change in (
                {"method": "B00"},
                {"method": "I01"},
                {"version": request["version"] + 1},
                {"version": True},
            ):
                self.assertFalse(validator.is_valid(request | change))
            missing = copy.deepcopy(request)
            del missing["version"]
            self.assertFalse(validator.is_valid(missing))

    def test_warning_schemas_are_response_only_closed_and_unique(self) -> None:
        """Derived S01 warnings belong to the root response, never completed persistent Facts."""
        run = native_schema("run-record.schema.json")
        self.assertNotIn("warnings", run["properties"])
        facts = run["properties"]["execution"]["properties"]["sharpening"]["anyOf"][0]
        report = {
            "method": "S01",
            "disposition": "no_change",
            "reason": "no_eligible_samples",
            "counts": {"eligible": 0, "protected": 1, "evaluated": 0, "changed": 0},
            "context_samples": 0,
            "corrected_samples": 0,
            "pre_clamp": None,
            "clipped_low_samples": 0,
            "clipped_high_samples": 0,
        }
        validator = Draft202012Validator(facts)
        validator.validate(report)
        for warnings in ([], ["W_SHARPENING"], ["W_SHARPENING", "W_SHARPENING"]):
            self.assertFalse(validator.is_valid(report | {"warnings": warnings}))
        self.assertFalse(validator.is_valid(report | {"method": "C03"}))
        root = self.response_branch("process", "record")
        response_execution = root["properties"]["record"]["properties"]["execution"]
        self.assertEqual(
            response_execution["properties"]["sharpening"],
            run["properties"]["execution"]["properties"]["sharpening"],
        )
        warning_schema = Draft202012Validator(root["properties"]["warnings"])
        warning_schema.validate(["W_SHARPENING"])
        warning_schema.validate([])
        for invalid in (["W_SHARPENING", "W_SHARPENING"], ["W_UNKNOWN"], "W_SHARPENING", [True]):
            self.assertFalse(warning_schema.is_valid(invalid))
        # Amount-based one/none projection is executable behavior, retained by warning_cases and
        # actual-rendering response_fields controls, rather than a stored warning constraint.

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
        cli = json.loads((ROOT / "spec/cli-contract.json").read_text())
        matrix = method_metadata.support_matrix(cli["formats"])
        self.assertEqual([item["format"] for item in matrix], ["png", "jpeg", "tiff"])
        for item in matrix:
            self.assertEqual(item["output_modes"], ["preserve", "gray", "bw"])

    def test_denoising_version_changes_typed_discovery_not_wire_artifacts(self) -> None:
        """The descriptor version changes once; native schemas follow compiled descriptors."""
        cli = json.loads((ROOT / "spec/cli-contract.json").read_text())
        methods = json.loads((ROOT / "spec/method-contract.json").read_text())
        options = merged_options(cli, methods)
        before = method_metadata.metadata_outputs(
            ROOT, methods["families"], options, cli["formats"]
        )
        changed = copy.deepcopy(methods)
        nlm = next(
            method
            for family in changed["families"]
            for method in family["methods"]
            if method["id"] == "D01"
        )
        nlm["version"] += 1
        after = method_metadata.metadata_outputs(
            ROOT, changed["families"], merged_options(cli, changed), cli["formats"]
        )
        catalog = ROOT / "include/docenhance/methods/method_catalog.hpp"
        self.assertNotEqual(before[catalog], after[catalog])
        self.assertIn(f".method_version = {nlm['version']}U", after[catalog])
        schema = self.request_schema("denoising")
        request = self.selected_request(
            "denoising", "D01", {"h": 3, "patch": 7, "search": 21, "blend": 0.5}
        )
        validator = Draft202012Validator(schema)
        validator.validate(request)
        validator.validate(None)
        change: dict[str, Any]
        for change in (
            {"method": None},
            {"parameters": None},
            {"version": request["version"] + 1},
            {"version": True},
            {"selector": "off"},
        ):
            self.assertFalse(validator.is_valid(request | change))
        self.assertFalse(
            validator.is_valid(request | {"parameters": request["parameters"] | {"search": True}})
        )
        self.assertNotIn(ROOT / "schemas/run-record.schema.json", after)

    def test_clahe_request_is_closed_and_bound_to_its_method(self) -> None:
        """C03 cannot borrow gamma parameters or accept grid values outside typed domains."""
        schema = self.request_schema("contrast")
        request = self.selected_request(
            "contrast", "C03", {"grid_columns": 8, "grid_rows": 8, "clip": 2, "blend": 1}
        )
        validator = Draft202012Validator(schema)
        validator.validate(request)
        change: dict[str, Any]
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
            self.assertFalse(
                validator.is_valid(request | {"parameters": request["parameters"] | change})
            )
        self.assertFalse(validator.is_valid(request | {"parameters": {"gamma": 1, "blend": 1}}))
        self.assertFalse(validator.is_valid(request | {"method": "C02"}))

    def test_restoration_request_keeps_closed_path_free_psf_alternatives(self) -> None:
        """Zero-blend R01 still requires a valid PSF; kernel bytes are a confined record asset."""
        schema = self.request_schema("restoration")
        base = self.selected_request(
            "restoration",
            "R01",
            {"psf": {"kind": "gaussian", "sigma": 1}, "k": 0.01, "blend": 0},
        )
        validator = Draft202012Validator(schema)
        validator.validate(base)
        for psf in ({"kind": "motion", "length": 5, "angle": -37}, {"kind": "kernel"}):
            validator.validate(base | {"parameters": base["parameters"] | {"psf": psf}})
        change: dict[str, Any]
        for change in (
            {"psf": {"kind": "gaussian"}},
            {"psf": {"kind": "gaussian", "sigma": 0.29}},
            {"psf": {"kind": "motion", "length": 5, "angle": 0, "sigma": 1}},
            {"psf": {"kind": "unknown"}},
            {"psf": {"kind": "kernel", "path": "raw.png"}},
            {"psf": {"kind": "kernel", "path": ""}},
            {"psf": {"kind": "kernel", "path": "a\0b"}},
            {"k": 0},
            {"k": 1.01},
            {"blend": -0.1},
            {"blind": True},
        ):
            self.assertFalse(validator.is_valid(base | {"parameters": base["parameters"] | change}))
        for change in ({"method": None}, {"version": base["version"] + 1}, {"version": True}):
            self.assertFalse(validator.is_valid(base | change))
        # Required Kernel asset identity/dimensions/moments at zero blend are native RunRecord
        # relations; record.cpp and actual repaired-digest verify_artifacts preserve those controls.
