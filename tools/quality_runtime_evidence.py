# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed interpreter evidence admission with one retained implementation identity owner."""

from __future__ import annotations

import copy
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any

from quality_acquisition import fingerprint, identified_reference
from quality_json import CANONICAL_OPTIONS, MAX_JSON_DEPTH, canonical
from quality_provenance_contract import exact, identity_shape, reference_shape, sha256_text
from quality_runtime_identity import (
    CODE_ROLES,
    CONFIG_KEYS,
    FLOAT,
    MODULE_ROLES,
    OBSERVED_KEYS,
    SOURCE_ROLES,
    runtime_identity,
)
from quality_runtime_json import _native_route


def _image_shape(value: dict[str, Any], platform: str) -> None:
    """Validate a recorded finite file identity under its own platform path domain."""
    exact(value, {"path", "sha256", "bytes"})
    identity_shape({key: value[key] for key in ("sha256", "bytes")})
    path = value["path"]
    if not isinstance(path, str) or not path or "\0" in path:
        message = "Runtime image needs an actual absolute implementation location"
        raise ValueError(message)
    absolute = (
        PureWindowsPath(path).is_absolute()
        if platform == "win32"
        else PurePosixPath(path).is_absolute()
    )
    if not absolute:
        message = "Runtime image path differs from its recorded platform"
        raise ValueError(message)
    path.encode("utf-8")


def _scalar_shape(observed: dict[str, Any]) -> None:
    """Validate actual interpreter scalar, startup, depth and configured-core domains."""
    exact(observed, OBSERVED_KEYS)
    if observed["implementation"] != "cpython":
        message = "Interpreter evidence identifies another implementation"
        raise ValueError(message)
    for key in ("version", "platform"):
        if not isinstance(observed[key], str) or not observed[key]:
            message = "Interpreter identity text is unavailable"
            raise ValueError(message)
    if observed["abi"] is not None and not isinstance(observed["abi"], str):
        message = "Interpreter ABI metadata is malformed"
        raise ValueError(message)
    exact(observed["json_limits"], {"standalone", "completed_enclosure"})
    if (
        type(observed["json_limits"]["standalone"]) is not int
        or observed["json_limits"]["standalone"] != MAX_JSON_DEPTH
        or type(observed["json_limits"]["completed_enclosure"]) is not int
        or not 0 <= observed["json_limits"]["completed_enclosure"] < 2**63
    ):
        message = "Interpreter JSON owner limits are unavailable"
        raise ValueError(message)
    exact(observed["startup"], {"dont_write_bytecode"})
    startup = observed["startup"]["dont_write_bytecode"]
    if type(startup) is not int or startup != 1:
        message = "Interpreter evidence lacks actual startup bytecode suppression"
        raise ValueError(message)
    exact(observed["core_configuration"], CONFIG_KEYS)
    configuration = observed["core_configuration"].values()
    if any(value is not None and not isinstance(value, (str, int)) for value in configuration):
        message = "Interpreter configuration contains unsupported metadata"
        raise ValueError(message)
    exact(observed["core"], {"linkage", "image"})
    if observed["core"]["linkage"] not in ("executable", "shared_library", "framework", "dll"):
        message = "Interpreter core linkage is unsupported"
        raise ValueError(message)
    _image_shape(observed["executable"], observed["platform"])
    _image_shape(observed["core"]["image"], observed["platform"])


def _module_shape(body: dict[str, Any], observed: dict[str, Any]) -> None:
    """Bind each recorded module origin and its exact embedded carrier domain."""
    exact(observed["modules"], MODULE_ROLES)
    for module in observed["modules"].values():
        exact(module, {"origin", "image"})
        if module["origin"] not in ("file", "built-in", "frozen"):
            message = "Interpreter module origin is unsupported"
            raise ValueError(message)
        _image_shape(module["image"], observed["platform"])
    embedded = {
        name
        for name, module in observed["modules"].items()
        if module["origin"] in ("built-in", "frozen")
    }
    exact(body["embedded"], embedded)
    for name, carrier in body["embedded"].items():
        if carrier not in ("core", "executable"):
            message = "Embedded module carrier is unsupported"
            raise ValueError(message)
        image = observed["core"]["image"] if carrier == "core" else observed["executable"]
        if observed["modules"][name]["image"] != image:
            message = "Embedded module carrier disagrees with its recorded image"
            raise ValueError(message)


def runtime_shape(body: dict[str, Any]) -> None:
    """Closed runtime facts retain the independent source/build/OS qualification requirement."""
    exact(body, {"kind", "observed", "embedded", "imports", "source_basis"})
    if body["kind"] != "docenhance.quality.interpreter" or not isinstance(body["embedded"], dict):
        message = "Unsupported interpreter evidence body"
        raise ValueError(message)
    observed = body["observed"]
    _scalar_shape(observed)
    _module_shape(body, observed)
    exact(observed["code"], CODE_ROLES)
    for digest in observed["code"].values():
        sha256_text(digest)
    options = {**CANONICAL_OPTIONS, "separators": list(CANONICAL_OPTIONS["separators"])}
    if canonical(observed["float"]) != canonical(FLOAT) or canonical(
        observed["canonical_options"]
    ) != canonical(options):
        message = "Interpreter serialization/float evidence differs from its selected domain"
        raise ValueError(message)
    exact(body["source_basis"], SOURCE_ROLES)
    for source in body["source_basis"].values():
        exact(source, {"reference", "image"})
        reference_shape(source["reference"])
        _image_shape(source["image"], observed["platform"])
    _imports_shape(body["imports"], observed["platform"])


def _imports_shape(value: dict[str, Any], platform: str) -> None:
    exact(value, {"interpreter", "native_json", "core", "startup_files"})
    for name in ("interpreter", "native_json", "core"):
        exact(value[name], {"image", "dependencies"})
        _image_shape(value[name]["image"], platform)
        if not isinstance(value[name]["dependencies"], list):
            message = "Runtime import dependencies need an actual finite file inventory"
            raise TypeError(message)
    if not isinstance(value["startup_files"], list) or not value["startup_files"]:
        message = "Runtime startup evidence is unavailable"
        raise ValueError(message)
    images = [
        image
        for name in ("interpreter", "native_json", "core")
        for image in value[name]["dependencies"]
    ] + value["startup_files"]
    for image in images:
        _image_shape(image, platform)


def _remember_import(paths: dict[str, dict[str, Any]], image: dict[str, Any]) -> None:
    """One actual implementation path cannot describe conflicting dependency bytes."""
    previous = paths.get(image["path"])
    if previous is not None and previous != image:
        message = "Runtime import evidence contradicts one physical path"
        raise ValueError(message)
    paths[image["path"]] = image


def _imports(body: dict[str, Any], observed: dict[str, Any]) -> tuple[tuple[Path, bytes], ...]:
    """Retain actual dependency bytes after closed import relations and duplicate checks."""
    expected = {
        "interpreter": observed["executable"],
        "core": observed["core"]["image"],
        "native_json": observed["modules"]["native_json"]["image"],
    }
    paths: dict[str, dict[str, Any]] = {}
    for name, image in expected.items():
        node = body["imports"][name]
        if canonical(node["image"]) != canonical(image):
            message = "Reviewed runtime import root differs from the actual selected image"
            raise ValueError(message)
        seen: set[str] = set()
        for dependency in node["dependencies"]:
            if dependency["path"] in seen:
                message = "Runtime import evidence repeats a dependency"
                raise ValueError(message)
            seen.add(dependency["path"])
            _remember_import(paths, dependency)
    core = expected["core"]
    interpreter_dependencies = body["imports"]["interpreter"]["dependencies"]
    if core != expected["interpreter"] and core not in interpreter_dependencies:
        message = "Configured POSIX/core image lacks its reviewed interpreter import relation"
        raise ValueError(message)
    for image in body["imports"]["startup_files"]:
        _remember_import(paths, image)
    retained: list[tuple[Path, bytes]] = []
    for path, image in paths.items():
        actual = fingerprint(Path(path))
        identity = {key: image[key] for key in ("sha256", "bytes")}
        if actual != identity:
            message = "Reviewed runtime dependency/startup bytes changed"
            raise ValueError(message)
        retained.append((Path(path), canonical(identity)))
    return tuple(retained)


@dataclass(frozen=True)
class RuntimeAdmission:
    """Retain one selected body and actual identities; no JSON-reference reacquisition on launch."""

    _body: dict[str, Any]
    _expected: bytes
    _dependencies: tuple[tuple[Path, bytes], ...]

    def before_launch(self) -> None:
        """Recheck actual implementation files/code with the original admitted receipt."""
        if canonical(runtime_identity(self._body)) != self._expected:
            message = "Selected evaluator implementation changed after admission"
            raise ValueError(message)
        for path, expected in self._dependencies:
            if canonical(fingerprint(path)) != expected:
                message = "Selected runtime dependency changed after admission"
                raise ValueError(message)


def admit_runtime(body: dict[str, Any], root: Path) -> RuntimeAdmission:
    """Match actual selected facts and reviewed correspondence; do not mint qualified evidence."""
    _native_route()
    runtime_shape(body)
    owned = copy.deepcopy(body)
    observed = runtime_identity(owned)
    if canonical(observed) != canonical(owned["observed"]):
        message = "Interpreter evidence differs from the actual selected implementation"
        raise ValueError(message)
    dependencies = _imports(owned, observed)
    for name, source in owned["source_basis"].items():
        image = (
            observed["modules"]["native_json"]["image"]
            if name == "_json.c"
            else observed["core"]["image"]
        )
        if source["image"] != image:
            message = "Reviewed source evidence identifies another compiled implementation"
            raise ValueError(message)
        identified_reference(root, source["reference"])
    return RuntimeAdmission(owned, canonical(observed), dependencies)


def nested_references(body: dict[str, Any]) -> list[dict[str, str]]:
    """Return only the already acquired body's raw source references, without reads or queries."""
    runtime_shape(body)
    return [dict(body["source_basis"][name]["reference"]) for name in sorted(SOURCE_ROLES)]


def physical_references(body: dict[str, Any]) -> list[dict[str, str]]:
    """Inventory actual absolute implementation files separately from corpus-owned references."""
    runtime_shape(body)
    observed, imports = body["observed"], body["imports"]
    images = [
        observed["executable"],
        observed["core"]["image"],
        *(module["image"] for module in observed["modules"].values()),
        *imports["startup_files"],
    ]
    for name in ("interpreter", "native_json", "core"):
        images.extend([imports[name]["image"], *imports[name]["dependencies"]])
    return [{key: image[key] for key in ("path", "sha256")} for image in images]
