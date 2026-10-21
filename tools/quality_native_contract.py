# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Primitive native metadata, path and ordinary configuration contracts."""

from __future__ import annotations

import json
from pathlib import PurePosixPath, PureWindowsPath
from typing import Any

from command_arguments import command_arguments
from quality_json import _parser_defaults, canonical, unique_object, validate_depth
from quality_provenance_contract import exact, identity_shape, reference_shape

DISABLED = {"", "0", "OFF", "FALSE", "NO", "N", "IGNORE", "NOTFOUND"}


def compilation_values(data: bytes) -> list[object]:
    """Read the actual array-root compilation database under the existing strict JSON policy."""
    _parser_defaults()
    try:
        value = json.loads(data.decode("utf-8", "strict"), object_pairs_hook=unique_object)
        validate_depth(value)
        canonical(value)
    except RecursionError as error:
        message = "Native compilation database exceeds supported JSON depth"
        raise ValueError(message) from error
    if not isinstance(value, list):
        message = "Native compilation database must be an array"
        raise TypeError(message)
    return value


def instrumented(text: str, system: str) -> bool:
    """Recognize unadmitted instrumentation and code-transformer command routes."""
    args = command_arguments(text, windows=system == "Windows")
    prefixes = (
        "-fsanitize",
        "-fprofile",
        "-fcs-profile",
        "-ftest-coverage",
        "-fcoverage",
        "-finstrument-functions",
        "-fplugin",
        "-fpass-plugin",
        "-load",
        "-plugin",
        "--plugin",
        "-wl,-plugin",
        "-wl,--plugin",
        "/fsanitize",
        "/genprofile",
        "/fastgenprofile",
        "/useprofile",
        "/ltcg:pg",
    )
    return any(
        arg.lower().startswith(prefixes) or arg.lower() in {"--coverage", "-pg", "/gh", "/profile"}
        for arg in args
    )


def need(condition: object, message: str) -> None:
    """Refuse a concrete native evidence relationship without rewriting facts."""
    if not condition:
        raise ValueError(message)


def native_path(value: object, system: str) -> PurePosixPath | PureWindowsPath:
    """Interpret actual path text using its recorded producer platform."""
    if not isinstance(value, str) or not value or "\0" in value:
        message = "Native path is unavailable"
        raise ValueError(message)
    value.encode("utf-8", "strict")
    return PureWindowsPath(value) if system == "Windows" else PurePosixPath(value)


def physical(value: object, system: str) -> None:
    """Require an absolute existing physical-file identity shape."""
    value = exact(value, {"path", "sha256", "bytes"})
    identity_shape({key: value[key] for key in ("sha256", "bytes")})
    need(
        native_path(value["path"], system).is_absolute(),
        "Native implementation path is not absolute",
    )


def object_snapshot(value: dict[str, Any]) -> dict[str, Any]:
    """Read the object domain of a closed embedded JSON snapshot."""
    exact(value, {"reference", "value"})
    reference_shape(value["reference"])
    actual = value["value"]
    if not isinstance(actual, dict):
        message = "Native JSON snapshot must be an object"
        raise TypeError(message)
    return actual


def text_snapshot(value: dict[str, Any]) -> str:
    """Read the exact text domain of a closed metadata snapshot."""
    exact(value, {"reference", "value"})
    reference_shape(value["reference"])
    actual = value["value"]
    if not isinstance(actual, str):
        message = "Native text snapshot is unavailable"
        raise TypeError(message)
    return actual


def compile_snapshot(value: dict[str, Any]) -> list[dict[str, Any]]:
    """Read the actual array of compilation records without an object-root fiction."""
    exact(value, {"reference", "value"})
    reference_shape(value["reference"])
    actual = value["value"]
    if not isinstance(actual, list) or not all(isinstance(item, dict) for item in actual):
        message = "Actual native compilation is absent"
        raise ValueError(message)
    return actual


def configuration_values(actual: object) -> None:
    """Require the actual ordinary native build's closed scalar configuration domains."""
    if (
        not isinstance(actual, dict)
        or not actual
        or not all(isinstance(item, str) for item in actual.values())
    ):
        message = "Native configuration is unavailable"
        raise ValueError(message)
    need(
        actual.get("CMAKE_SYSTEM_NAME") in {"Darwin", "Linux", "Windows"},
        "Unsupported native platform",
    )
    for key in (
        "DE_ENABLE_ASAN",
        "DE_ENABLE_UBSAN",
        "DE_ENABLE_TSAN",
        "DE_ENABLE_FUZZING",
        "DE_FUZZ_ONLY",
    ):
        need(
            key in actual and actual[key].upper() in DISABLED,
            "Native instrumentation route is unreviewed",
        )
    need(actual.get("DE_CXX_STANDARD") == "23", "Native language configuration differs")
    need(
        actual.get("DE_TOOLCHAIN") in {"analysis", "platform"}, "Native toolchain scope is unknown"
    )
    for key, value in actual.items():
        if key.startswith(("CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS", "CMAKE_EXE_LINKER_FLAGS")):
            need(
                not instrumented(value, actual["CMAKE_SYSTEM_NAME"]),
                "Instrumented native flags are unsupported",
            )
    if actual["CMAKE_SYSTEM_NAME"] == "Windows":
        need(
            actual.get("CMAKE_CXX_COMPILER_ID") == "MSVC"
            and actual.get("CMAKE_MSVC_RUNTIME_LIBRARY") in {"MultiThreaded", "MultiThreadedDebug"},
            "Native Windows compiler/static CRT differs",
        )
