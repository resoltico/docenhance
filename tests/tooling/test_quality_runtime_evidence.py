# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Selected runtime path/startup boundaries, without inventing native producer qualification."""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from types import FunctionType, SimpleNamespace
from typing import Any
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_json import CANONICAL_OPTIONS, canonical, read_json
from quality_native_contract import compilation_values
from quality_runtime_evidence import _image_shape, admit_runtime, runtime_shape
from quality_runtime_identity import _core
from quality_runtime_json import _native_route

prepare_tool_imports()


class QualityRuntimeEvidenceTests(unittest.TestCase):
    """Preserve selected interpreter and serialization premises."""

    def test_default_encoder_change_refuses_before_body_comparison_without_changing_code(
        self,
    ) -> None:
        """Default encoder change refuses before body comparison without changing code."""

        class OtherEncoder(json.JSONEncoder):
            pass

        _native_route()
        function = json.dumps
        if not isinstance(function, FunctionType):
            self.fail("The selected dumps owner is not a Python function")
        before = function.__code__
        for key, value in (("cls", OtherEncoder), ("indent", "\t")):
            initial = function.__kwdefaults__
            if initial is None:
                self.fail("The selected dumps owner has no keyword defaults")
            defaults = dict(initial)
            defaults[key] = value
            with self.subTest(key=key), patch.object(json.dumps, "__kwdefaults__", defaults):
                with patch("quality_runtime_evidence.runtime_shape") as shape:
                    with self.assertRaisesRegex(ValueError, "encoder default"):
                        admit_runtime({}, Path())
                    shape.assert_not_called()
                with self.assertRaisesRegex(ValueError, "encoder default"):
                    canonical({})
                self.assertIs(function.__code__, before)
        _native_route()

    def test_parser_defaults_refuse_each_single_callback_or_relaxed_strictness(self) -> None:
        """Ordinary defaults pass; each single changed parser default refuses before body checks."""
        _native_route()
        hooks = ("object_hook", "parse_float", "parse_int", "parse_constant", "object_pairs_hook")
        for function, names in ((json.loads, ("cls", *hooks)), (json.JSONDecoder.__init__, hooks)):
            if not isinstance(function, FunctionType) or function.__kwdefaults__ is None:
                self.fail("The ordinary parser owner has no concrete keyword defaults")
            initial = function.__kwdefaults__
            for name in names:
                changed = initial | {name: object()}
                with (
                    self.subTest(owner=function.__qualname__, name=name),
                    patch.object(function, "__kwdefaults__", changed),
                ):
                    with self.assertRaisesRegex(ValueError, "parser defaults"):
                        _native_route()
                    with self.assertRaisesRegex(ValueError, "parser defaults"):
                        read_json(b"{}")
                    with self.assertRaisesRegex(ValueError, "parser defaults"):
                        compilation_values(b"[]")
        decoder = json.JSONDecoder.__init__
        if not isinstance(decoder, FunctionType) or decoder.__kwdefaults__ is None:
            self.fail("The ordinary decoder owner has no concrete keyword defaults")
        with (
            patch.object(decoder, "__kwdefaults__", decoder.__kwdefaults__ | {"strict": False}),
            self.assertRaisesRegex(ValueError, "strict default"),
        ):
            _native_route()
        _native_route()
        self.assertEqual(read_json(b"{}"), {})
        self.assertEqual(compilation_values(b"[]"), [])

    def test_canonical_global_alias_and_primitive_options_are_actual_route_premises(self) -> None:
        """Canonical global alias and primitive options are actual route premises."""
        for key, alias in (
            ("json", SimpleNamespace()),
            ("CANONICAL_OPTIONS", dict(CANONICAL_OPTIONS)),
        ):
            with (
                self.subTest(key=key),
                patch.dict(canonical.__globals__, {key: alias}),
                self.assertRaisesRegex(ValueError, "serializer aliases"),
            ):
                _native_route()
        for key, option in (
            ("allow_nan", True),
            ("ensure_ascii", 0),
            ("sort_keys", 1),
            ("separators", [",", ":"]),
        ):
            with self.subTest(key=key), patch.dict(CANONICAL_OPTIONS, {key: option}):
                with self.assertRaisesRegex(ValueError, "serializer aliases"):
                    _native_route()
                with self.assertRaisesRegex(ValueError, "serializer aliases"):
                    canonical({})
        _native_route()
        self.assertEqual(canonical({"ordinary": 1}), b'{"ordinary":1}')

    def test_offline_image_paths_follow_recorded_platform(self) -> None:
        """Offline image paths follow recorded platform."""
        identity = {"sha256": "a" * 64, "bytes": 1}
        _image_shape(identity | {"path": "/selected/python"}, "linux")
        _image_shape(identity | {"path": "C:\\selected\\python.exe"}, "win32")
        for platform, path in (
            ("linux", "relative/python"),
            ("win32", "C:python.exe"),
            ("win32", "/selected/python"),
            ("linux", "C:\\selected\\python.exe"),
        ):
            with self.subTest(platform=platform, path=path), self.assertRaises(ValueError):
                _image_shape(identity | {"path": path}, platform)

    def test_configured_core_cannot_resolve_a_relative_directory_against_evaluator_cwd(
        self,
    ) -> None:
        """Configured core cannot resolve a relative directory against evaluator cwd."""
        configuration = {
            "PYTHONFRAMEWORK": "",
            "Py_ENABLE_SHARED": 1,
            "LIBDIR": "unreviewed-relative",
            "INSTSONAME": "libpython.so",
        }
        with (
            patch("quality_runtime_identity.os", SimpleNamespace(name="posix")),
            patch("quality_runtime_identity._image") as image,
        ):
            with self.assertRaisesRegex(ValueError, "absolute directory"):
                _core(configuration)
            image.assert_not_called()

    def test_pending_or_old_inline_runtime_metadata_cannot_claim_selected_implementation(
        self,
    ) -> None:
        """Pending or old inline runtime metadata cannot claim selected implementation."""
        body: dict[str, Any]
        for body in ({}, {"canonical_options": {}}, {"kind": "docenhance.quality.interpreter"}):
            with self.subTest(body=body), self.assertRaises(ValueError):
                runtime_shape(body)
