#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Audit configured dependency feature values and OpenCV's actual module closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FALSE = {"0", "OFF", "FALSE", "NO", "N", "IGNORE", "NOTFOUND", ""}
# Independent reference for the reviewed corrected 3.12.0 single header. This proves identity;
# native allocation-refusal tests separately prove cleanup behavior.
JSON_BOUNDED_HEADER_SHA256 = "f504f6fa07b84e3e264a1f7757f15da1502bb539285c90e908c1cabab47b572e"


def read_cache(path: Path) -> dict[str, str]:
    """Parse KEY:TYPE=VALUE entries of a CMakeCache.txt."""
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(("#", "//")) or ":" not in line or "=" not in line:
            continue
        left, value = line.split("=", 1)
        values[left.split(":", 1)[0]] = value
    return values


def json_header_failures(binary: Path) -> list[str]:
    """Compare installed JSON bytes with the actual corrected dependency build input."""
    owned = binary / "owned-source/single_include/nlohmann/json.hpp"
    installed = binary.parents[1] / "prefix" / "include" / "nlohmann" / "json.hpp"
    if not owned.is_file() or not installed.is_file():
        return ["JSON: missing reviewed or installed bounded-destruction header"]
    if owned.read_bytes() != installed.read_bytes():
        return ["JSON: installed header differs from the reviewed private copy"]
    actual = hashlib.sha256(installed.read_bytes()).hexdigest()
    if actual != JSON_BOUNDED_HEADER_SHA256:
        return [f"JSON: header differs from reviewed output digest: {actual}"]
    return []


def feature_failures(
    name: str, settings: dict[str, bool | str], cache: dict[str, str], binary: Path
) -> list[str]:
    """Compare one dependency's configured cache with its declared feature settings."""
    failures = []
    for key, expected in settings.items():
        if key not in cache:
            failures.append(f"{name}: missing declared feature {key}")
            continue
        actual = cache[key]
        if isinstance(expected, bool):
            actual_bool = actual.upper() not in FALSE and not actual.endswith("-NOTFOUND")
            if actual_bool != expected:
                failures.append(f"{name}: unexpected {key}={actual}")
        else:
            resolved = expected.replace("<BINARY>", binary.as_posix())
            if actual.replace("\\", "/") != resolved:
                failures.append(f"{name}: unexpected {key}={actual}; expected {resolved}")
    if name == "opencv":
        modules = set(cache.get("OPENCV_MODULES_BUILD", "").split(";"))
        expected_modules = {"opencv_" + part for part in str(settings["BUILD_LIST"]).split(",")}
        if modules != expected_modules:
            failures.append(f"OpenCV module closure differs: {sorted(modules)}")
    if name == "json":
        failures.extend(json_header_failures(binary))
    if cache.get("BUILD_SHARED_LIBS", "").upper() not in FALSE:
        failures.append(f"{name}: unexpected shared-library build")
    return failures


def audit(build: Path) -> list[str]:
    """Audit every configured dependency in a superbuild tree."""
    policy = json.loads((ROOT / "deps/features.json").read_text(encoding="utf-8"))
    features = policy["dependencies"]
    binding = json.loads((build / "build-identity.json").read_text(encoding="utf-8"))
    if binding["CMAKE_SYSTEM_NAME"] in {"Darwin", "Linux"}:
        for name, settings in policy["unix_dependencies"].items():
            features[name].update(settings)
    selected = json.loads((build / "dependency-plan.json").read_text(encoding="utf-8"))
    if not isinstance(selected, list) or not selected or len(set(selected)) != len(selected):
        return ["Invalid or empty configured dependency plan"]
    failures = []
    for name in selected:
        if name not in features:
            failures.append(f"Unknown configured dependency: {name}")
            continue
        binary = build / "deps" / name
        path = binary / "CMakeCache.txt"
        if not path.exists():
            failures.append(f"Missing configured dependency cache: {name}")
            continue
        cache = read_cache(path)
        failures.extend(feature_failures(name, features[name], cache, binary))
        for key in (
            "ZLIB_LIBRARY",
            "ZLIB_LIBRARY_RELEASE",
            "ZLIB_LIBRARY_DEBUG",
            "ZLIB_INCLUDE_DIR",
            "JPEG_LIBRARY",
            "JPEG_LIBRARY_RELEASE",
            "JPEG_LIBRARY_DEBUG",
            "JPEG_INCLUDE_DIR",
        ):
            value = cache.get(key, "")
            if (
                value
                and not value.endswith("-NOTFOUND")
                and not Path(value).resolve().is_relative_to((build / "prefix").resolve())
            ):
                failures.append(f"{name}: foreign dependency provider {key}={value}")
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    errors = audit(args.build)
    print("\n".join(errors) if errors else "PASS: dependency feature and module-closure audit")
    raise SystemExit(bool(errors))
