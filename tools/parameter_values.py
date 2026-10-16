# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Validate and merge the request and method option authorities before generation."""

from __future__ import annotations

import math
import re
from fractions import Fraction
from typing import Any, NoReturn

MAX_NUMBER_LENGTH = 128
# Coefficients contain at most MAX_NUMBER_LENGTH digits each. Beyond this exponent difference,
# no nonzero coefficient ratio can be a normal binary64 value. Check before forming a power.
MAX_DECIMAL_POWER = 1024
RATIONAL_PARTS = 2
UINT32_MAX = (1 << 32) - 1
CLI_SCHEMA = 13
METHOD_SCHEMA = 3
KINDS = frozenset(
    {
        "flag",
        "path",
        "choice",
        "decimal",
        "integer",
        "odd_integer",
        "auto_or_integer",
        "source_or_decimal",
        "grid",
    }
)
NUMERIC = KINDS - {"flag", "path", "choice"}
INTEGER = frozenset({"integer", "odd_integer", "auto_or_integer", "grid"})
DECIMAL = re.compile(r"-?[0-9]+(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?\Z")
OPTION = re.compile(r"--[a-z][a-z0-9]*(?:-[a-z0-9]+)*\Z")
IDENTIFIER = re.compile(r"[a-z][a-z0-9_]*\Z")
CHOICE_IDENTIFIER = re.compile(r"[A-Za-z0-9]+(?:[_-][A-Za-z0-9]+)*\Z")
CPP_KEYWORDS = frozenset(
    [
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "auto",
        "bitand",
        "bitor",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "class",
        "compl",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "const_cast",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
        "xor_eq",
    ]
)


def invalid(message: str) -> NoReturn:
    """Refuse an invalid authoring declaration without silently repairing it."""
    raise ValueError(message)


def exact_number(text: object) -> Fraction:
    """Read an exact authored decimal or rational, refusing non-finite binary domains."""
    if not isinstance(text, str) or not text or len(text) > MAX_NUMBER_LENGTH:
        invalid("Bounds and numeric defaults require bounded exact strings")
    parts = text.split("/")
    if len(parts) > RATIONAL_PARTS or any(DECIMAL.fullmatch(part) is None for part in parts):
        invalid(f"Invalid exact numeric declaration: {text!r}")
    try:
        numerator, exponent = decimal_parts(parts[0])
        denominator, divisor_exponent = (
            decimal_parts(parts[1]) if len(parts) == RATIONAL_PARTS else (1, 0)
        )
        value = Fraction(numerator, denominator)
        power = exponent - divisor_exponent
        if value and abs(power) > MAX_DECIMAL_POWER:
            invalid(f"Numeric declaration cannot be a normal finite double: {text!r}")
        if value:
            value *= Fraction(10) ** power
        binary = float(value)
    except (ValueError, ZeroDivisionError, OverflowError) as error:
        message = f"Invalid exact numeric declaration: {text!r}"
        raise ValueError(message) from error
    if not math.isfinite(binary) or (value and abs(binary) < float.fromhex("0x1p-1022")):
        invalid(f"Numeric declaration is not a normal finite double: {text!r}")
    return value


def decimal_parts(text: str) -> tuple[int, int]:
    """Separate a bounded coefficient from its exponent without allocating exponent-sized data."""
    mantissa, _, exponent = text.lower().partition("e")
    whole, _, fraction = mantissa.partition(".")
    return int(whole + fraction), (int(exponent) if exponent else 0) - len(fraction)


def validate_value(option: dict[str, Any]) -> None:
    """Check kind, bounds, default, required presence and conditional declarations."""
    kind = option.get("kind")
    if kind not in KINDS:
        invalid(f"Unknown parameter kind: {kind!r}")
    name = option.get("name", option.get("option"))
    if not isinstance(name, str) or OPTION.fullmatch(name) is None:
        invalid("Options require a long canonical spelling")
    if not isinstance(option.get("metavar"), str) or bool(option["metavar"]) == (kind == "flag"):
        invalid(f"{name}: flag/value metavar disagrees with kind")
    if type(option.get("required", False)) is not bool:
        invalid(f"{name}: required must be Boolean")
    if option.get("required") and "default" in option:
        invalid(f"{name}: required raw presence cannot have a default")
    validate_domain(option)
    validate_default(option)


def validate_domain(option: dict[str, Any]) -> None:
    """Require a nonempty kind-specific domain."""
    kind = option["kind"]
    name = option.get("name", option.get("option"))
    if kind in NUMERIC:
        low, high = exact_number(option.get("min")), exact_number(option.get("max"))
        if low > high:
            invalid(f"{name}: inverted domain")
        if kind in INTEGER and (
            low.denominator != 1 or high.denominator != 1 or low < 0 or high > UINT32_MAX
        ):
            invalid(f"{name}: integer bounds must fit uint32")
        if kind == "odd_integer" and not any(n % 2 for n in (int(low), int(high))) and low == high:
            invalid(f"{name}: odd domain is empty")
    elif "min" in option or "max" in option:
        invalid(f"{name}: nonnumeric kind cannot carry bounds")
    if kind == "choice":
        values = option.get("values")
        if (
            not isinstance(values, list)
            or not values
            or any(not isinstance(value, str) or not value for value in values)
            or len(values) != len(set(values))
        ):
            invalid(f"{name}: choices must be distinct nonempty strings")
        if any(CHOICE_IDENTIFIER.fullmatch(value) is None for value in values):
            invalid(f"{name}: unsupported C++ choice spelling")
    elif "values" in option:
        invalid(f"{name}: only a choice declares values")


def validate_default(option: dict[str, Any]) -> None:
    """Check symbolic and numeric defaults against their actual kind."""
    kind = option["kind"]
    name = option.get("name", option.get("option"))
    if "default" not in option:
        return
    default = option["default"]
    if not isinstance(default, str):
        invalid(f"{name}: defaults are exact strings")
    if (kind == "auto_or_integer" and default == "auto") or (
        kind == "source_or_decimal" and default == "source"
    ):
        return
    if kind == "choice":
        if default not in option["values"]:
            invalid(f"{name}: default outside choices")
    elif kind in NUMERIC:
        validate_numeric_default(option)
    elif kind == "path" and (not default or "\0" in default):
        invalid(f"{name}: invalid path default")
    elif kind == "flag":
        invalid(f"{name}: flags record raw presence, without defaults")


def domain_text(option: dict[str, Any]) -> str:
    """Derive presentation from the same declarations used by typed validators."""
    kind = option["kind"]
    if kind == "choice":
        domain = " | ".join(option["values"])
    elif kind in NUMERIC:
        domain = f"[{option['min']},{option['max']}]"
        prefixes = {
            "odd_integer": "odd integer ",
            "integer": "integer ",
            "grid": "CxR, each integer ",
            "auto_or_integer": "auto | integer ",
            "source_or_decimal": "source | finite ",
            "decimal": "finite ",
        }
        domain = prefixes[kind] + domain
    else:
        domain = "well-formed UTF-8 path" if kind == "path" else "flag"
    if "default" in option:
        return f"{option['default']}; {domain}"
    return ("required; " if option.get("required") else "optional; ") + domain


def validate_numeric_default(option: dict[str, Any]) -> None:
    """Both grid axes and scalar defaults satisfy the exact primitive domain."""
    kind = option["kind"]
    name = option.get("name", option.get("option"))
    default = option["default"]
    components = default.split("x") if kind == "grid" else [default]
    if kind == "grid" and len(components) != RATIONAL_PARTS:
        invalid(f"{name}: grid default must be CxR")
    low, high = exact_number(option["min"]), exact_number(option["max"])
    for component in components:
        value = exact_number(component)
        if not low <= value <= high:
            invalid(f"{name}: default outside bounds")
        if kind in INTEGER and value.denominator != 1:
            invalid(f"{name}: fractional integer default")
        if kind == "odd_integer" and int(value) % 2 != 1:
            invalid(f"{name}: even default")
