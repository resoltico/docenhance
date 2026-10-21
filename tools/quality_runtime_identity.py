# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual CPython/JSON observations; configured locations need reviewed OS correspondence."""

from __future__ import annotations

import _json
import hashlib
import json
import json.decoder
import json.encoder
import json.scanner
import marshal
import os
import sys
import sysconfig
from pathlib import Path
from types import FunctionType, ModuleType
from typing import Any

import quality_json as evidence_json
from quality_acquisition import fingerprint
from quality_json import (
    CANONICAL_OPTIONS,
    MAX_JSON_DEPTH,
    canonical,
    read_json,
    unique_object,
    validate_depth,
)
from quality_provenance_contract import exact
from quality_runtime_json import _native_route

windows_api: ModuleType | None
try:
    import _winapi
except ImportError:
    windows_api = None
else:
    windows_api = _winapi

MODULE_ROLES = {
    "json",
    "encoder",
    "decoder",
    "scanner",
    "native_json",
    "canonical",
    "sysconfig",
    "build_data",
}
CODE_ROLES = {
    "canonical",
    "read_json",
    "validate_depth",
    "unique_object",
    "json_dumps",
    "json_loads",
    "encoder_init",
    "encoder_encode",
    "encoder_iterencode",
    "decoder_init",
    "decoder_decode",
    "decoder_raw_decode",
    "read_json_body",
}
CONFIG_KEYS = {
    "Py_ENABLE_SHARED",
    "PYTHONFRAMEWORK",
    "PYTHONFRAMEWORKPREFIX",
    "PYTHONFRAMEWORKINSTALLDIR",
    "LDLIBRARY",
    "INSTSONAME",
    "LIBDIR",
    "SOABI",
    "ABIFLAGS",
    "Py_DEBUG",
    "Py_GIL_DISABLED",
}
SOURCE_ROLES = {"_json.c", "floatobject.c", "pystrtod.c", "dtoa.c"}
OBSERVED_KEYS = {
    "implementation",
    "version",
    "platform",
    "abi",
    "executable",
    "core_configuration",
    "core",
    "modules",
    "code",
    "float",
    "canonical_options",
    "startup",
    "json_limits",
}
FLOAT = {"radix": 2, "mant_dig": 53, "max_exp": 1024, "min_exp": -1021}


def _image(path: Path) -> dict[str, Any]:
    """Fingerprint the actual resolved implementation file selected by its owner."""
    resolved = path.resolve(strict=True)
    return {"path": str(resolved), **fingerprint(resolved)}


def _configuration() -> tuple[dict[str, Any], ModuleType]:
    """Retain actual build configuration and its loaded module without alternate selectors."""
    selectors = ("_PYTHON_SYSCONFIGDATA_NAME", "_PYTHON_SYSCONFIGDATA_PATH")
    if any(key in os.environ for key in selectors):
        message = "Alternate evaluator build-data selectors are unreviewed"
        raise ValueError(message)
    values = sysconfig.get_config_vars()
    configuration = {key: values.get(key) for key in CONFIG_KEYS}
    getter = vars(sysconfig).get("_get_sysconfigdata_name")
    name = "_sysconfig" if os.name == "nt" else getter() if callable(getter) else None
    if not isinstance(name, str):
        message = "Selected evaluator build-data name is unavailable"
        raise TypeError(message)
    owner = sys.modules.get(name)
    if not isinstance(owner, ModuleType):
        message = "Selected evaluator build-data owner is unavailable"
        raise TypeError(message)
    return configuration, owner


def _core(configuration: dict[str, Any]) -> dict[str, Any]:
    """Resolve the configured actual core image under its recorded platform linkage."""
    if os.name == "nt":
        locate = vars(windows_api).get("GetModuleFileName") if windows_api is not None else None
        if not callable(locate):
            message = "Selected Windows image metadata API is unavailable"
            raise ValueError(message)
        handle = vars(sys).get("dllhandle")
        if type(handle) is not int or handle <= 0:
            message = "Selected Windows interpreter DLL identity is unavailable"
            raise ValueError(message)
        return {"linkage": "dll", "image": _image(Path(locate(handle)))}
    framework = configuration["PYTHONFRAMEWORK"]
    shared = configuration["Py_ENABLE_SHARED"]
    if framework or shared == 1:
        directory = configuration["PYTHONFRAMEWORKPREFIX"] if framework else configuration["LIBDIR"]
        name = configuration["INSTSONAME"]
        if not isinstance(directory, str) or not directory or not isinstance(name, str) or not name:
            message = "Selected configured interpreter image is unavailable"
            raise ValueError(message)
        if not Path(directory).is_absolute():
            message = "Configured interpreter image requires an absolute directory"
            raise ValueError(message)
        return {
            "linkage": "framework" if framework else "shared_library",
            "image": _image(Path(directory) / name),
        }
    if shared != 0:
        message = "Selected interpreter linkage metadata is unsupported"
        raise ValueError(message)
    return {"linkage": "executable", "image": _image(Path(sys.executable))}


def _code() -> dict[str, str]:
    """Hash actual selected Python code objects without invoking those implementations."""
    functions = {
        "canonical": canonical,
        "read_json": read_json,
        "validate_depth": validate_depth,
        "unique_object": unique_object,
        "read_json_body": vars(evidence_json).get("_read_json"),
        "json_dumps": json.dumps,
        "json_loads": json.loads,
        "encoder_init": json.JSONEncoder.__init__,
        "encoder_encode": json.JSONEncoder.encode,
        "encoder_iterencode": json.JSONEncoder.iterencode,
        "decoder_init": json.JSONDecoder.__init__,
        "decoder_decode": json.JSONDecoder.decode,
        "decoder_raw_decode": json.JSONDecoder.raw_decode,
    }
    result: dict[str, str] = {}
    for name, function in functions.items():
        if not isinstance(function, FunctionType):
            message = "Selected JSON implementation code identity is unavailable"
            raise TypeError(message)
        result[name] = hashlib.sha256(marshal.dumps(function.__code__)).hexdigest()
    return result


def _modules(
    body: dict[str, Any], core: dict[str, Any], executable: dict[str, Any], build_data: ModuleType
) -> dict[str, Any]:
    """Bind actual module origins to their reviewed standalone or embedded file images."""
    owners = {
        "json": json,
        "encoder": json.encoder,
        "decoder": json.decoder,
        "scanner": json.scanner,
        "native_json": _json,
        "canonical": sys.modules.get(canonical.__module__),
        "sysconfig": sysconfig,
        "build_data": build_data,
    }
    result: dict[str, Any] = {}
    embedded: set[str] = set()
    for name, owner in owners.items():
        if not isinstance(owner, ModuleType):
            message = "Selected interpreter module owner is unavailable"
            raise TypeError(message)
        spec = owner.__spec__
        origin = spec.origin if spec is not None else None
        if origin in ("built-in", "frozen"):
            embedded.add(name)
            carrier = body["embedded"].get(name)
            if carrier not in ("core", "executable"):
                message = "Embedded module lacks its reviewed actual image carrier"
                raise ValueError(message)
            image = core["image"] if carrier == "core" else executable
        else:
            location = vars(owner).get("__file__")
            if not isinstance(origin, str) or not isinstance(location, str) or not location:
                message = "Selected interpreter module location is unavailable"
                raise ValueError(message)
            origin, image = "file", _image(Path(location))
        result[name] = {"origin": origin, "image": image}
    exact(body["embedded"], embedded)
    return result


def runtime_identity(body: dict[str, Any]) -> dict[str, Any]:
    """Observe selected facts; independent qualification still owns loader correspondence."""
    if sys.implementation.name != "cpython":
        message = "Selected evaluator is not the reviewed CPython implementation"
        raise ValueError(message)
    if sys.flags.dont_write_bytecode != 1 or sys.dont_write_bytecode is not True:
        message = "Selected evaluator requires bytecode suppression from interpreter startup"
        raise ValueError(message)
    _native_route()
    configuration, build_data = _configuration()
    if not isinstance(configuration["SOABI"], str) or not configuration["SOABI"]:
        message = "Selected interpreter ABI metadata is unavailable"
        raise ValueError(message)
    core, executable = _core(configuration), _image(Path(sys.executable))
    floats = {key: getattr(sys.float_info, key) for key in FLOAT}
    if floats != FLOAT:
        message = "Selected evaluator is outside the reviewed binary64 domain"
        raise ValueError(message)
    standalone = vars(evidence_json).get("MAX_JSON_DEPTH")
    enclosure = vars(evidence_json).get("COMPLETED_ENCLOSURE_DEPTH")
    if (
        type(standalone) is not int
        or standalone != MAX_JSON_DEPTH
        or type(enclosure) is not int
        or not 0 <= enclosure < 2**63
    ):
        message = "Selected JSON owner depth metadata is unavailable"
        raise ValueError(message)
    return {
        "implementation": sys.implementation.name,
        "version": sys.version,
        "platform": sys.platform,
        "abi": vars(sys).get("abiflags"),
        "executable": executable,
        "core_configuration": configuration,
        "core": core,
        "modules": _modules(body, core, executable, build_data),
        "code": _code(),
        "float": floats,
        "canonical_options": {
            **CANONICAL_OPTIONS,
            "separators": list(CANONICAL_OPTIONS["separators"]),
        },
        "startup": {"dont_write_bytecode": sys.flags.dont_write_bytecode},
        "json_limits": {"standalone": standalone, "completed_enclosure": enclosure},
    }
