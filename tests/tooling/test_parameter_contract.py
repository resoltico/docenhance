# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Challenge authored parameter admission before it can generate executable declarations."""

from __future__ import annotations

import copy
import unittest
from fractions import Fraction

from parameter_fixtures import contracts, numeric

from parameter_contract import merged_options
from parameter_values import domain_text, exact_number, validate_value


class ParameterContractTests(unittest.TestCase):
    """Reject declaration errors at the single generated authority boundary."""

    def test_exact_numbers(self) -> None:
        """Rational floors and decimal exponent defaults retain their authored meaning."""
        self.assertEqual(exact_number("1/255"), Fraction(1, 255))
        self.assertEqual(exact_number("1e-5"), Fraction(1, 100000))
        self.assertEqual(exact_number("-180"), -180)
        self.assertEqual(exact_number("1e100000000/1e100000000"), 1)
        self.assertEqual(exact_number("0e100000000"), 0)
        for text in (
            "nan",
            "inf",
            "+1",
            " 1",
            "1/0",
            "1/2/3",
            "0x1",
            "1e309",
            "1e-324",
            "1e100000000",
            "1e-100000000",
        ):
            with self.subTest(text=text), self.assertRaises(ValueError):
                exact_number(text)

    def test_numeric_kind_and_domain_refusals(self) -> None:
        """Reject fractional unsigned bounds, impossible defaults and malformed kinds."""
        cases = (
            {"min": "2", "max": "1"},
            {"default": "2"},
            {"default": "NaN"},
            {"kind": "integer"},
            {"kind": "integer", "min": "-1"},
            {"kind": "integer", "min": "0", "max": "4294967296"},
            {"kind": "integer", "min": "0", "default": "0.5"},
            {"kind": "odd_integer", "min": "2", "max": "2", "default": "2"},
            {"kind": "odd_integer", "min": "1", "max": "5", "default": "2"},
            {"kind": "future_recipe"},
            {"default": 0.5},
            {"required": True},
        )
        for updates in cases:
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                validate_value(numeric(**updates))

    def test_optional_automatic_values(self) -> None:
        """Only the declared union accepts its symbolic automatic spelling."""
        validate_value(numeric(kind="auto_or_integer", min="8", max="512", default="auto"))
        validate_value(numeric(kind="source_or_decimal", default="source"))
        for updates in (
            {"default": "auto"},
            {"default": "source"},
            {"kind": "auto_or_integer", "min": "8", "max": "512", "default": "source"},
        ):
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                validate_value(numeric(**updates))

    def test_grid_both_axes(self) -> None:
        """Both contextual dimensions obey the integer domain."""
        validate_value(numeric(kind="grid", min="2", max="32", default="8x8"))
        for default in ("8", "8X8", "1x8", "8x33", "8x0.5", "8x8x8"):
            with self.subTest(default=default), self.assertRaises(ValueError):
                validate_value(numeric(kind="grid", min="2", max="32", default=default))

    def test_merged_ownership_and_required_presence(self) -> None:
        """Shared fields and required resource paths keep actual applicability."""
        cli, methods = contracts()
        options = merged_options(cli, methods)
        self.assertEqual(
            [o["name"] for o in options], ["--json", "--deblur", "--psf", "--psf-file"]
        )
        self.assertEqual(options[1]["values"], ["off", "wiener"])
        self.assertEqual(options[-1]["when"], {"--psf": "kernel"})
        self.assertNotIn("default", options[-1])
        self.assertEqual(options[-1]["methods"], ["R01"])
        self.assertEqual(options[-1]["applies_to"], ["preserve", "gray"])
        self.assertEqual(domain_text(numeric()), "0.5; finite [1/255,1]")

    def test_name_collisions_cross_authorities(self) -> None:
        """A request option cannot shadow a method selector or parameter."""
        for name in ("--deblur", "--psf", "--psf-file"):
            cli, methods = contracts()
            cli["options"][0]["name"] = name
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "collision"):
                merged_options(cli, methods)

    def test_when_references_and_values(self) -> None:
        """Conditions must select a reachable choice of the same owning method."""
        conditions: tuple[object, ...] = (
            {"--missing": "kernel"},
            {"--psf": "absent"},
            {"--psf-file": "kernel"},
            {"--json": "kernel"},
            [],
        )
        for when in conditions:
            cli, methods = contracts()
            methods["families"][0]["methods"][0]["parameters"][-1]["when"] = when
            with self.subTest(when=when), self.assertRaises(ValueError):
                merged_options(cli, methods)

    def test_duplicate_method_identity(self) -> None:
        """A second family cannot alias an existing executable method identity."""
        cli, methods = contracts()
        other = copy.deepcopy(methods["families"][0])
        other.update(
            family="other",
            order=2,
            selector={"option": "--other", "metavar": "M", "default": "off"},
        )
        methods["families"].append(other)
        with self.assertRaisesRegex(ValueError, "unique IDs"):
            merged_options(cli, methods)

    def test_duplicate_parameter_field(self) -> None:
        """Distinct options cannot generate the same member in one parameter type."""
        cli, methods = contracts()
        parameters = methods["families"][0]["methods"][0]["parameters"]
        parameters[-1]["field"] = parameters[0]["field"]
        with self.assertRaisesRegex(ValueError, "parameter field"):
            merged_options(cli, methods)

    def test_choice_and_flag_refusals(self) -> None:
        """Type-specific declarations cannot be silently ignored by generation."""
        for changes in ({"values": ["a", "a"]}, {"default": "absent"}, {"min": "0"}):
            option = {
                "option": "--choice",
                "kind": "choice",
                "metavar": "C",
                "values": ["a"],
                **changes,
            }
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                validate_value(option)
        for flag_option in (
            {"option": "--flag", "kind": "flag", "metavar": "X"},
            {"option": "--flag", "kind": "flag", "metavar": "", "default": "true"},
        ):
            with self.subTest(option=flag_option), self.assertRaises(ValueError):
                validate_value(flag_option)


if __name__ == "__main__":
    unittest.main()
